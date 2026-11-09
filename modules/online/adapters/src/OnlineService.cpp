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

    // —— 装配、取消/重试、核心查询、内部辅助（退避/令牌/提交/应用）、排水、会话恢复 ——

    OnlineService::OnlineService(c::IHttpTransport &transport, c::IExecutor &executor,
                                 platform::storage::SqliteConnection &db, c::ISecretStore &secrets,
                                 const kernel::IClock &clock)
        : provider_(transport, executor), store_(db, clock), clock_(clock), secrets_(secrets), db_(db)
    {
        status_.provider = kProvider;
        status_.enabled = true;
        status_.signedIn = false;
        status_.accountPhase = c::AccountPhase::Guest;
        bucket_.prime(nowMs());
    }

    kernel::Result<void> OnlineService::handle(const c::CancelOnlineJob &command)
    {
        if (const auto it = inflight_.find(command.token); it != inflight_.end())
        {
            provider_.cancel(it->second);
            inflight_.erase(it);
        }
        jobs_.setPhase(command.token, c::JobPhase::Idle);
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::RetryOnlineJob &command)
    {
        const auto it = requests_.find(command.token);
        if (it == requests_.end())
        {
            return kernel::Error{kernel::ErrorCode::NotFound, "没有这个任务"};
        }
        jobs_.setPhase(command.token, c::JobPhase::Loading);
        if (it->second.kind == c::SourceKind::Search)
        {
            submit(it->second, /*bypassCache=*/true);
        }
        else
        {
            // 非搜索类的重试就是把同一条命令再走一遍（中间状态若还在，会跳过一次网络）。
            startPage(it->second);
        }
        return {};
    }

    // ---------------------------------------------------------------- 查询

    kernel::Result<c::OnlineJobState> OnlineService::handle(const c::GetOnlineJob &query)
    {
        drain();
        drainAuth();
        auto state = jobs_.state(query.token);
        state.fromCache = cachedTokens_.count(query.token) > 0;
        return kernel::Result<c::OnlineJobState>::ok(state);
    }

    kernel::Result<c::OnlinePageDto> OnlineService::handle(const c::FetchOnlineRows &query)
    {
        return kernel::Result<c::OnlinePageDto>::ok(jobs_.page(query.token, query.offset, query.limit));
    }

    kernel::Result<c::OnlinePlaybackDto> OnlineService::handle(const c::GetOnlinePlayback &)
    {
        drainPlayback();
        return kernel::Result<c::OnlinePlaybackDto>::ok(playback_);
    }

    void OnlineService::drainPlayback()
    {
        std::vector<PendingPlayback> incoming;
        {
            std::lock_guard<std::mutex> lock(playbackMutex_);
            incoming.swap(playbackPending_);
        }
        if (incoming.empty())
        {
            return;
        }

        // 一次只认最后一份：用户可能连点了好几首，前面的结果已经没有意义。
        PendingPlayback item = std::move(incoming.back());
        ++playbackRevision_;

        if (!item.result.ok)
        {
            noteFailure(item.result.error, "取地址");
            // URL 过期（20 分钟 TTL）在矩阵里单独一行：它是唯一"重解析一次就好"的失败。
            if (item.result.error.code == kernel::ErrorCode::NotFound)
            {
                diag_.recordUrlExpired();
            }
            c::OnlinePlaybackDto next;
            next.revision = playbackRevision_;
            next.error = withHint(item.result.error);
            next.startIndex = item.startIndex;
            playback_ = std::move(next);
            return;
        }

        c::OnlinePlaybackDto next;
        next.revision = playbackRevision_;
        next.startIndex = item.startIndex;
        next.entries.reserve(item.result.entries.size());
        for (const auto &entry : item.result.entries)
        {
            c::OnlinePlaybackEntry out;
            out.songId = entry.songId;
            out.url = entry.url;
            out.bitrateKbps = entry.bitrate;
            next.entries.push_back(std::move(out));
        }
        noteSuccess();
        playback_ = std::move(next);
    }

    kernel::Result<c::SourceStatusDto> OnlineService::handle(const c::GetSourceStatus &)
    {
        // 这个查询是界面每一拍都会问的那一条（登录态），把它当成心跳：
        // 私人 FM 的批次不走 JobRegistry，只靠这条心跳才不会被饿死。
        drain();
        drainAuth();
        return kernel::Result<c::SourceStatusDto>::ok(status_);
    }

    // ---------------------------------------------------------------- 内部

    std::int64_t OnlineService::nowMs() const noexcept
    {
        return nowMillis(clock_);
    }

    void OnlineService::noteFailure(const c::OnlineError &error, std::string where)
    {
        const std::int64_t now = nowMs();
        diag_.recordError(error, now, std::move(where));

        // 取消不算失败：记录里已经跳过，这里也不能让退避闸门关起来——
        // 否则用户连打三个字又删掉，就会触发一次"连续失败 3 次"。
        if (error.code == kernel::ErrorCode::Internal && error.message == "已取消")
        {
            return;
        }
        failures_.recordFailure(now, backoff_);
    }

    c::OnlineError OnlineService::withHint(c::OnlineError error) const
    {
        return core::app::withInterfaceHint(std::move(error), failures_.consecutive);
    }

    void OnlineService::submit(const c::RequestOnlinePage &command, bool bypassCache)
    {
        core::provider::SearchParams params;
        params.keywords = command.keywords;
        params.offset = command.offset;
        params.limit = command.limit;

        const std::uint64_t token = command.token;
        const std::string keywords = command.keywords;
        const std::int32_t offset = command.offset;

        const std::uint64_t requestId = provider_.startSearch(
            params, [this, token, keywords, offset](core::codec::DecodeResult result) {
                // 这里是 worker 线程：只许塞队列，不许碰 jobs_/store_/status_。
                std::lock_guard<std::mutex> guard(pendingMutex_);
                Pending pending;
                pending.token = token;
                pending.result = std::move(result);
                pending.keywords = keywords;
                pending.offset = offset;
                pending_.push_back(std::move(pending));
            });

        inflight_[token] = requestId;
        (void)bypassCache;
    }

    void OnlineService::apply(std::uint64_t token, core::codec::DecodeResult result, const std::string &keywords,
                              std::int32_t offset)
    {
        if (!result.ok)
        {
            noteFailure(result.error, "搜索");
            jobs_.fail(token, withHint(result.error));
            return;
        }

        if (result.rows.empty() && offset == 0)
        {
            jobs_.setPhase(token, c::JobPhase::Empty);
            return;
        }

        jobs_.appendRows(token, std::move(result.rows), result.hasMore);

        if (!result.raw.empty())
        {
            store_.saveSearch(kProvider, keywords, offset, result.raw);
            store_.trimSearch();
        }
        // 能解码出结果就算业务成功：空结果也是上游在正常说话，不该继续退避。
        noteSuccess();
    }

    std::string OnlineService::pageKey(c::SourceKind kind, const std::string &collectionId)
    {
        return std::string{c::toName(kind)} + ":" + collectionId;
    }

    std::string OnlineService::whereOf(c::SourceKind kind)
    {
        switch (kind)
        {
        case c::SourceKind::Search:
            return "搜索";
        case c::SourceKind::Playlist:
            return "歌单";
        case c::SourceKind::Chart:
            return "榜单";
        case c::SourceKind::Album:
            return "专辑";
        case c::SourceKind::Artist:
            return "歌手";
        case c::SourceKind::NewSongs:
            return "新歌";
        case c::SourceKind::DailyRecommend:
            return "每日推荐";
        default:
            return "在线";
        }
    }

    bool OnlineService::gate(std::uint64_t token, c::SourceKind kind, std::int64_t now)
    {
        const std::string where = whereOf(kind);
        if (failures_.blocked(now))
        {
            const std::int64_t waitMs = failures_.nextRetryAtMs - now;
            c::OnlineError error{kernel::ErrorCode::Unavailable,
                                 "连续失败，约 " + std::to_string((waitMs + 999) / 1000) + " 秒后自动重试", true, 0};
            // 只进降级计数，不动连续失败计数：被闸门挡下不是又失败了一次。
            diag_.recordError(error, now, where);
            jobs_.fail(token, error);
            return false;
        }
        if (!bucket_.take(now))
        {
            c::OnlineError error{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            diag_.recordError(error, now, where);
            jobs_.fail(token, error);
            return false;
        }
        return true;
    }

    void OnlineService::drain()
    {
        std::vector<Pending> ready;
        {
            std::lock_guard<std::mutex> guard(pendingMutex_);
            ready.swap(pending_);
        }
        for (auto &item : ready)
        {
            if (const auto it = inflight_.find(item.token); it != inflight_.end())
            {
                inflight_.erase(it);
            }
            if (!item.fromCache)
            {
                // 真正的网络回包：这一页不再是"上次结果"。
                cachedTokens_.erase(item.token);
            }
            if (!item.rawForDecode.empty())
            {
                item.result = core::codec::decodeSearch(item.rawForDecode);
                item.result.raw = std::move(item.rawForDecode);
            }
            apply(item.token, std::move(item.result), item.keywords, item.offset);
        }
        // 非搜索类的回包走各自的队列；同一个 50ms 轮询里一起排空。
        drainPages();
        drainLyrics();
        drainFm();
        drainFmUrls();
    }

    void OnlineService::restoreSession()
    {
        auto stored = secrets_.load(kProvider);
        if (!stored)
        {
            // 从没登录过（或密文解不开）：安静地当游客，别在界面上吓人。
            return;
        }

        // 先拿昵称把界面填上，再异步问网易云"这还认吗"——网络慢时胶囊不是空白。
        status_.nickname = loadStoredNickname();
        const auto parts = core::auth::parseCookieHeader(stored.value());
        if (parts.musicU.empty())
        {
            return;
        }

        std::string cookie = core::auth::buildCookieHeader(parts);
        pendingCookie_ = cookie;
        authRequestId_ = provider_.startValidateAccount(
            cookie, [this, cookie = std::move(cookie)](core::codec::AccountResult result) {
                std::lock_guard<std::mutex> lock(authMutex_);
                authPending_.push_back(PendingAuth{cookie, std::move(result)});
            });
    }


    kernel::Result<void> OnlineService::handle(const c::ResolveAndPlayOnline &command)
    {
        if (command.songIds.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有可播放的曲目"};
        }

        // 单批上限 kMaxSongIds：一次取 200 首的地址是 R0 实测能跑通的形状，
        // 超过的部分先不取（R3 只做"点一首放整页"，队列超长时后面的进队列再补）。
        std::vector<std::string> ids;
        const std::size_t count =
            std::min<std::size_t>(command.songIds.size(), static_cast<std::size_t>(core::provider::kMaxSongIds));
        ids.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            ids.push_back(command.songIds[index]);
        }

        const std::int32_t startIndex = command.startIndex;
        provider_.startResolveUrls(ids, [this, startIndex](core::codec::UrlResult result) {
            // worker 线程：只入队，不碰状态表（那唯一的 SQLite 连接在主线程）。
            std::lock_guard<std::mutex> lock(playbackMutex_);
            playbackPending_.push_back(PendingPlayback{std::move(result), startIndex});
        });
        return {};
    }


} // namespace sonora::online::adapters
