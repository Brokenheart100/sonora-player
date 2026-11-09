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

    // —— 歌单/榜单/专辑/歌手/新歌/推荐 的分页与卡片列表 ——

    kernel::Result<void> OnlineService::handle(const c::LoadCollections &command)
    {
        const auto kind = command.kind;
        if (kind != c::SourceKind::Playlist && kind != c::SourceKind::Chart)
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "这一类没有卡片列表"};
        }

        // 卡片列表没有 token：失败信息直接落在页对象上，界面看到的是"重试"而不是"空"。
        auto &page = collections_[static_cast<int>(kind)];
        const std::int64_t now = nowMs();
        if (failures_.blocked(now))
        {
            const std::int64_t waitMs = failures_.nextRetryAtMs - now;
            c::OnlineError error{kernel::ErrorCode::Unavailable,
                                 "连续失败，约 " + std::to_string((waitMs + 999) / 1000) + " 秒后自动重试", true, 0};
            diag_.recordError(error, now, whereOf(kind));
            page.loaded = false;
            page.items.clear();
            page.error = error;
            ++page.revision;
            return {};
        }
        if (!bucket_.take(now))
        {
            c::OnlineError error{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            diag_.recordError(error, now, whereOf(kind));
            page.loaded = false;
            page.items.clear();
            page.error = error;
            ++page.revision;
            return {};
        }

        const auto limit = std::clamp<std::int32_t>(command.limit, 1, core::provider::kMaxCollectionLimit);
        auto onList = [this, kind](core::codec::CollectionListResult result) {
            std::lock_guard<std::mutex> guard(pageMutex_);
            collectionPending_.push_back(PendingCollection{kind, std::move(result)});
        };
        if (kind == c::SourceKind::Chart)
        {
            provider_.startToplist(std::move(onList));
        }
        else
        {
            provider_.startPersonalizedPlaylists(limit, std::move(onList));
        }
        return {};
    }

    kernel::Result<c::CollectionPageDto> OnlineService::handle(const c::FetchCollections &query)
    {
        // 卡片墙的轮询就是它自己的驱动源：网络回包先落在 collectionPending_，
        // 没有搜索任务时没人替我们调 drain()，所以这里顺手排干。
        drainPages();

        const auto it = collections_.find(static_cast<int>(query.kind));
        if (it == collections_.end())
        {
            return kernel::Result<c::CollectionPageDto>::ok(c::CollectionPageDto{});
        }
        return kernel::Result<c::CollectionPageDto>::ok(it->second);
    }

    kernel::Result<c::CollectionDetailDto> OnlineService::handle(const c::FetchCollectionDetail &query) const
    {
        const auto it = details_.find(pageKey(query.kind, query.collectionId));
        if (it == details_.end())
        {
            // 还没取到头部（或取失败）：交一个 ok=false 的空对象，界面据此显示占位标题。
            c::CollectionDetailDto empty;
            empty.id = query.collectionId;
            empty.kind = query.kind;
            return kernel::Result<c::CollectionDetailDto>::ok(std::move(empty));
        }
        return kernel::Result<c::CollectionDetailDto>::ok(it->second);
    }

    void OnlineService::startPlaylistBatch(std::uint64_t token, c::SourceKind kind, const std::string &collectionId,
                                           const std::vector<std::string> &trackIds, std::int32_t offset,
                                           std::int32_t limit)
    {
        if (trackIds.empty())
        {
            jobs_.setPhase(token, c::JobPhase::Empty);
            return;
        }
        // 第二批起也要过一次令牌桶：一次翻页最多 500 首，不该绕过限流。
        const std::int64_t now = nowMs();
        if (!bucket_.take(now))
        {
            c::OnlineError error{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            diag_.recordError(error, now, whereOf(kind));
            jobs_.fail(token, error);
            return;
        }

        const std::size_t total = trackIds.size();
        const std::size_t begin = std::min<std::size_t>(static_cast<std::size_t>(std::max(offset, 0)), total);
        const std::size_t end = std::min<std::size_t>(begin + static_cast<std::size_t>(std::max(limit, 1)), total);
        std::vector<std::string> ids(trackIds.begin() + static_cast<std::ptrdiff_t>(begin),
                                     trackIds.begin() + static_cast<std::ptrdiff_t>(end));
        if (ids.empty())
        {
            jobs_.setPhase(token, c::JobPhase::Empty);
            return;
        }

        const std::string key = pageKey(kind, collectionId);
        provider_.startSongDetails(ids, [this, token, kind, key, offset, limit](core::codec::DecodeResult result) {
            std::lock_guard<std::mutex> guard(pageMutex_);
            pagePending_.push_back(PendingPage{token, kind, key, offset, limit, std::move(result)});
        });
    }

    void OnlineService::applyPlaylistDetail(const PendingPlaylistDetail &item)
    {
        if (!item.result.ok)
        {
            noteFailure(item.result.error, whereOf(item.kind));
            jobs_.fail(item.token, withHint(item.result.error));
            return;
        }

        const std::string key = pageKey(item.kind, item.collectionId);
        PlaylistPlan plan;
        plan.detail = item.result.detail;
        plan.trackIds = item.result.trackIds;
        details_[key] = plan.detail;

        const auto command = requests_.find(item.token);
        const std::int32_t offset = command != requests_.end() ? command->second.offset : 0;
        const std::int32_t limit = command != requests_.end() ? command->second.limit : 100;
        playlists_[key] = std::move(plan);
        startPlaylistBatch(item.token, item.kind, item.collectionId, playlists_[key].trackIds, offset, limit);
    }

    void OnlineService::applyPage(PendingPage &item)
    {
        if (!item.result.ok)
        {
            noteFailure(item.result.error, whereOf(item.kind));
            jobs_.fail(item.token, withHint(item.result.error));
            return;
        }

        if (item.kind == c::SourceKind::Playlist || item.kind == c::SourceKind::Chart)
        {
            const auto plan = playlists_.find(item.key);
            const std::size_t total = plan != playlists_.end() ? plan->second.trackIds.size() : item.result.rows.size();
            if (item.offset == 0 && item.result.rows.empty())
            {
                jobs_.setPhase(item.token, c::JobPhase::Empty);
                return;
            }
            const std::size_t count = item.result.rows.size();
            const bool hasMore = static_cast<std::size_t>(item.offset) + count < total;
            jobs_.appendRows(item.token, std::move(item.result.rows), hasMore);
            noteSuccess();
            return;
        }

        // 专辑/歌手/新歌：第一页的回包就是全量，后面几页都在内存里切。
        auto &full = fullPages_[item.key];
        if (item.result.detail.ok)
        {
            details_[item.key] = item.result.detail;
        }
        if (item.offset == 0 || full.empty())
        {
            full = item.result.rows;
        }
        const std::size_t begin =
            std::min<std::size_t>(static_cast<std::size_t>(std::max(item.offset, 0)), full.size());
        const std::size_t end =
            std::min<std::size_t>(begin + static_cast<std::size_t>(std::max(item.limit, 1)), full.size());
        std::vector<c::OnlineTrackDto> slice(full.begin() + static_cast<std::ptrdiff_t>(begin),
                                             full.begin() + static_cast<std::ptrdiff_t>(end));
        if (begin == 0 && slice.empty())
        {
            jobs_.setPhase(item.token, c::JobPhase::Empty);
            return;
        }
        const bool hasMore = end < full.size();
        jobs_.appendRows(item.token, std::move(slice), hasMore);
        noteSuccess();
    }

    void OnlineService::applyCollectionList(const PendingCollection &item)
    {
        auto &page = collections_[static_cast<int>(item.kind)];
        if (!item.result.ok)
        {
            noteFailure(item.result.error, whereOf(item.kind));
            page.loaded = false;
            page.items.clear();
            page.error = withHint(item.result.error);
            ++page.revision;
            return;
        }
        page.items = item.result.items;
        page.hasMore = item.result.hasMore;
        page.loaded = true;
        page.error = {};
        ++page.revision;
        noteSuccess();
    }

    void OnlineService::drainPages()
    {
        std::vector<PendingCollection> collections;
        std::vector<PendingPlaylistDetail> detailPages;
        std::vector<PendingPage> pages;
        {
            std::lock_guard<std::mutex> guard(pageMutex_);
            collections.swap(collectionPending_);
            detailPages.swap(playlistDetailPending_);
            pages.swap(pagePending_);
        }

        for (const auto &item : collections)
        {
            applyCollectionList(item);
        }
        for (const auto &item : detailPages)
        {
            applyPlaylistDetail(item);
        }
        for (auto &item : pages)
        {
            applyPage(item);
        }
        // 登录侧的两条回包通道：与页面结果同一个排水口。
        drainFm();
        drainFmUrls();
        drainMyPlaylists();
        drainFavorite();
    }


} // namespace sonora::online::adapters
