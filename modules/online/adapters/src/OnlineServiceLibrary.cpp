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

    // —— 我的歌单、收藏、每日推荐 ——

    kernel::Result<std::vector<c::PlaylistDto>> OnlineService::handle(const c::GetMyPlaylists &)
    {
        // 与 GetOnlineJob 同一个道理：界面 50ms 轮询这个查询，顺手把回包搬进缓存。
        drainFm();
        drainFmUrls();
        drainMyPlaylists();
        return kernel::Result<std::vector<c::PlaylistDto>>::ok(myPlaylists_);
    }

    kernel::Result<void> OnlineService::handle(const c::LoadMyPlaylists &)
    {
        if (!status_.signedIn || userId_.empty())
        {
            // 游客：不发请求、不取令牌、不记错误。界面上"我的"页显示"登录后可见"。
            myPlaylists_.clear();
            myPlaylistsError_ = {};
            myPlaylistsLoaded_ = true;
            ++myPlaylistsRevision_;
            return {};
        }
        if (myPlaylistsLoading_)
        {
            return {};
        }

        const std::int64_t now = nowMs();
        if (failures_.blocked(now))
        {
            const std::int64_t waitMs = failures_.nextRetryAtMs - now;
            myPlaylistsError_ = c::OnlineError{kernel::ErrorCode::Unavailable,
                                               "连续失败，约 " + std::to_string((waitMs + 999) / 1000) +
                                                   " 秒后自动重试",
                                               true, 0};
            ++myPlaylistsRevision_;
            return {};
        }
        if (!bucket_.take(now))
        {
            myPlaylistsError_ = c::OnlineError{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            ++myPlaylistsRevision_;
            return {};
        }

        myPlaylistsLoading_ = true;
        provider_.startUserPlaylists(userId_, [this](core::codec::PlaylistsResult result) {
            const std::scoped_lock lock{pageMutex_};
            userPlaylistsPending_.push_back({std::move(result)});
        });
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::ToggleFavorite &command)
    {
        if (!status_.signedIn)
        {
            return kernel::Error{kernel::ErrorCode::Unauthenticated, "登录后才能收藏"};
        }
        if (command.songId.empty() || command.playlistId.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌曲或歌单"};
        }

        diag_.recordFavoriteWrite();
        // `csrf` 只作为表单字段进请求；它不缓存、不进日志、不进任何 DTO。
        const std::string csrf = csrfToken();
        provider_.startToggleFavorite(
            command.playlistId, command.songId, command.add, csrf, [this](core::codec::FavoriteResult result) {
                const std::scoped_lock lock{pageMutex_};
                favoritePending_.push_back({std::move(result), true});
            });
        return {};
    }

    std::string OnlineService::csrfToken() const
    {
        // cookie 形如 `MUSIC_U=…; __csrf=abc123; ...`，取出 `__csrf` 的值。
        const std::string key = "__csrf=";
        const std::size_t at = sessionCookie_.find(key);
        if (at == std::string::npos)
        {
            return {};
        }
        const std::size_t begin = at + key.size();
        const std::size_t end = sessionCookie_.find(';', begin);
        return sessionCookie_.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }


    void OnlineService::drainMyPlaylists()
    {
        std::vector<PendingUserPlaylists> incoming;
        {
            const std::scoped_lock lock{pageMutex_};
            incoming.swap(userPlaylistsPending_);
        }
        for (auto &item : incoming)
        {
            myPlaylistsLoading_ = false;
            if (item.result.ok)
            {
                myPlaylists_ = std::move(item.result.items);
                myPlaylistsError_ = {};
                myPlaylistsLoaded_ = true;
                noteSuccess();
            }
            else
            {
                noteFailure(item.result.error, "我的歌单");
                myPlaylistsError_ = withHint(item.result.error);
            }
            ++myPlaylistsRevision_;
        }
    }

    void OnlineService::drainFavorite()
    {
        std::vector<PendingFavorite> incoming;
        {
            const std::scoped_lock lock{pageMutex_};
            incoming.swap(favoritePending_);
        }
        for (auto &item : incoming)
        {
            if (item.result.ok)
            {
                noteSuccess();
                continue;
            }
            // 写操作的失败**单独计数**，并且只留最后一条错误给界面回滚用。
            diag_.recordFavoriteFailure();
            status_.lastError = withHint(item.result.error);
        }
    }


} // namespace sonora::online::adapters
