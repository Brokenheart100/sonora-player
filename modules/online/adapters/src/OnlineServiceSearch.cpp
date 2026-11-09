#include "sonora/online/adapters/OnlineService.h"
#include "OnlineServiceShared.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "sonora/online/core/app/Hint.h"
#include "sonora/online/core/auth/CookieHeader.h"
#include "sonora/online/core/auth/QrCode.h"
#include "sonora/online/core/provider/NeteaseUrls.h"
#include "sonora/platform/storage/SqliteStatement.h"

namespace sonora::online::adapters
{
    using namespace detail;

    namespace c = online::contracts;

    // —— 在线页面的入口与搜索通路（检索、缓存命中、分页请求的分发） ——

    // ---------------------------------------------------------------- 命令

    kernel::Result<void> OnlineService::handle(const c::RequestOnlinePage &command)
    {
        return startPage(command);
    }

    kernel::Result<void> OnlineService::startPage(const c::RequestOnlinePage &command)
    {
        if (command.kind != c::SourceKind::Search)
        {
            // 非搜索类先补 token、登记任务，再按资源类型走各自的取数路径。
            std::uint64_t token = command.token == 0 ? jobs_.currentToken() + 1 : command.token;
            auto request = command;
            request.token = token;
            // 只有"新开一页"才重置任务：翻页用的是同一个 token，再 begin 一次会把
            // 前一页的行清空（界面表现为"加载更多以后还是 100 首"）。
            if (jobs_.state(token).phase == c::JobPhase::Idle)
            {
                jobs_.begin(token, request.kind, request.keywords);
            }
            requests_[token] = request;
            if (!gate(token, request.kind, nowMs()))
            {
                return {};
            }

            const std::string key = pageKey(request.kind, request.collectionId);
            switch (request.kind)
            {
            case c::SourceKind::Playlist:
            case c::SourceKind::Chart:
            {
                if (request.collectionId.empty())
                {
                    jobs_.fail(token, c::OnlineError{kernel::ErrorCode::InvalidArgument, "没有指定歌单", false, 0});
                    return {};
                }
                // 第二页起 trackIds 已经在内存里，直接取下一批歌曲详情。
                if (const auto plan = playlists_.find(key); plan != playlists_.end())
                {
                    startPlaylistBatch(token, request.kind, request.collectionId, plan->second.trackIds, request.offset,
                                       request.limit);
                    return {};
                }
                provider_.startPlaylistDetail(
                    request.collectionId,
                    [this, token, kind = request.kind, id = request.collectionId](
                        core::codec::PlaylistDetailResult result) {
                        std::lock_guard<std::mutex> guard(pageMutex_);
                        playlistDetailPending_.push_back(
                            PendingPlaylistDetail{token, id, kind, std::move(result)});
                    });
                return {};
            }
            case c::SourceKind::Album:
            case c::SourceKind::Artist:
            case c::SourceKind::NewSongs:
            case c::SourceKind::DailyRecommend:
            {
                // 这三类一次取全（都 ≤ 100 行），分页在内存里切片；重进同一页不再发请求。
                if (const auto full = fullPages_.find(key); full != fullPages_.end())
                {
                    const auto &rows = full->second;
                    const std::size_t begin = std::min<std::size_t>(static_cast<std::size_t>(std::max(request.offset, 0)),
                                                                    rows.size());
                    const std::size_t end =
                        std::min<std::size_t>(begin + static_cast<std::size_t>(std::max(request.limit, 1)), rows.size());
                    std::vector<c::OnlineTrackDto> slice(rows.begin() + static_cast<std::ptrdiff_t>(begin),
                                                         rows.begin() + static_cast<std::ptrdiff_t>(end));
                    if (begin == 0 && slice.empty())
                    {
                        jobs_.setPhase(token, c::JobPhase::Empty);
                        return {};
                    }
                    const bool hasMore = end < rows.size();
                    jobs_.appendRows(token, std::move(slice), hasMore);
                    return {};
                }

                auto onPage = [this, token, kind = request.kind, key, offset = request.offset,
                              limit = request.limit](core::codec::DecodeResult result) {
                    std::lock_guard<std::mutex> guard(pageMutex_);
                    pagePending_.push_back(PendingPage{token, kind, key, offset, limit, std::move(result)});
                };
                switch (request.kind)
                {
                case c::SourceKind::Album:
                    provider_.startAlbum(request.collectionId, onPage);
                    return {};
                case c::SourceKind::Artist:
                    provider_.startArtist(request.collectionId, onPage);
                    return {};
                case c::SourceKind::DailyRecommend:
                    provider_.startDailySongs(onPage);
                    return {};
                default:
                    provider_.startNewSongs(request.limit, onPage);
                    return {};
                }
            }
            default:
                jobs_.fail(token, notWired("这一路资源还没接：只做了搜索/歌单/榜单/专辑/歌手/新歌"));
                return {};
            }
        }
        if (command.keywords.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "搜索关键词为空"};
        }

        // token 正常由界面递增；传 0 时补一个，免得所有请求挤在同一条记录上。
        std::uint64_t token = command.token;
        if (token == 0)
        {
            token = jobs_.currentToken() + 1;
        }

        auto request = command;
        request.token = token;
        request.limit = std::clamp<std::int32_t>(command.limit, 1, core::provider::kMaxSearchLimit);

        jobs_.begin(token, c::SourceKind::Search, request.keywords);
        requests_[token] = request;

        // 缓存先查：改一个字再改回来是最常见的输入模式，命中一次就省一次请求。
        // 命中时不在这里解码：命令 handler 的预算是 5ms，30 行的 JSON 解码 +
        // 落 ViewModel 会把预算吃掉，所以只把原文塞进待处理队列，交给 50ms 轮询
        // 的那次 drain() 解码（它本来就要读队列）。
        if (const auto cached = store_.loadSearch(kProvider, request.keywords, request.offset))
        {
            diag_.recordCacheHit();
            cachedTokens_.insert(token);
            Pending pending;
            pending.token = token;
            pending.fromCache = true;
            pending.keywords = request.keywords;
            pending.offset = request.offset;
            pending.rawForDecode = *cached;
            {
                std::lock_guard<std::mutex> guard(pendingMutex_);
                pending_.push_back(std::move(pending));
            }
            return {};
        }

        // 两道闸门挡在真正发请求之前。顺序有讲究：先退避再限流——退避是"连续失败
        // 之后别急着再来"，限流是"连打太快"；被退避挡住时不该再去消耗令牌。
        const std::int64_t now = nowMs();
        if (failures_.blocked(now))
        {
            const std::int64_t waitMs = failures_.nextRetryAtMs - now;
            c::OnlineError error{kernel::ErrorCode::Unavailable,
                                 "连续失败，约 " + std::to_string((waitMs + 999) / 1000) + " 秒后自动重试", true, 0};
            // 只进降级计数，不动连续失败计数：被闸门挡下不是又失败了一次。
            diag_.recordError(error, now, "搜索");
            jobs_.fail(token, error);
            return {};
        }
        if (!bucket_.take(now))
        {
            c::OnlineError error{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            diag_.recordError(error, now, "搜索");
            jobs_.fail(token, error);
            return {};
        }

        submit(request, false);
        return {};
    }


} // namespace sonora::online::adapters
