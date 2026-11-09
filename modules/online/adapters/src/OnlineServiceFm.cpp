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

    // —— 私人 FM 批次与播放地址回填 ——

    void OnlineService::setFmListener(FmListener listener)
    {
        fmListener_ = std::move(listener);
    }

    kernel::Result<void> OnlineService::handle(const contracts::StartPersonalFm &command)
    {
        if (fmActive_)
        {
            // 已经在取：拖住第二个请求，别让"队列喊饿"变成一串并行请求。
            return {};
        }

        const std::int32_t count = std::clamp(command.count, 1, kFmMaxBatch);
        const std::int64_t now = nowMs();
        if (failures_.blocked(now) || !bucket_.take(now))
        {
            // FM 是背景服务：被闸门挡住不报错、不弹窗，只留一个计数，
            // 下一首播完还会再来要。
            diag_.recordFmFailure();
            return {};
        }

        fmActive_ = true;
        fmExpected_ = count;
        fmSettled_ = 0;
        fmSongs_.clear();
        for (std::int32_t i = 0; i < count; ++i)
        {
            provider_.startRadio([this](core::codec::DecodeResult result) {
                const std::scoped_lock lock{fmMutex_};
                radioPending_.push_back({std::move(result)});
            });
        }
        return {};
    }

    kernel::Result<void> OnlineService::handle(const contracts::DislikeFmSong &command)
    {
        if (command.songId.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌曲"};
        }
        fmDisliked_.push_back(command.songId);
        if (fmDisliked_.size() > kFmSeenLimit)
        {
            fmDisliked_.erase(fmDisliked_.begin());
        }
        return {};
    }

    void OnlineService::drainFm()
    {
        std::vector<PendingRadio> incoming;
        {
            const std::scoped_lock lock{fmMutex_};
            incoming.swap(radioPending_);
        }
        if (incoming.empty())
        {
            return;
        }

        for (PendingRadio &item : incoming)
        {
            if (!item.result.ok)
            {
                // 单独计数：FM 的失败不该把搜索那条退避闸门按下去。
                diag_.recordFmFailure();
            }
            else
            {
                for (contracts::OnlineTrackDto &dto : item.result.rows)
                {
                    if (dto.songId.empty())
                    {
                        continue;
                    }
                    const bool disliked =
                        std::find(fmDisliked_.begin(), fmDisliked_.end(), dto.songId) != fmDisliked_.end();
                    const bool seen = std::find(fmSeen_.begin(), fmSeen_.end(), dto.songId) != fmSeen_.end();
                    const bool pending = std::any_of(fmSongs_.begin(), fmSongs_.end(),
                                                     [&dto](const contracts::OnlineTrackDto &other) {
                                                         return other.songId == dto.songId;
                                                     });
                    if (disliked || seen || pending)
                    {
                        continue;
                    }
                    fmSongs_.push_back(std::move(dto));
                }
            }

            ++fmSettled_;
            if (fmSettled_ < fmExpected_)
            {
                continue;
            }

            // 一批齐了：解析直链。没解析出来的直接丢（FM 不弹"无版权"）。
            fmExpected_ = 0;
            fmSettled_ = 0;
            if (fmSongs_.empty())
            {
                fmActive_ = false;
                return;
            }

            std::vector<std::string> ids;
            ids.reserve(fmSongs_.size());
            for (const contracts::OnlineTrackDto &dto : fmSongs_)
            {
                ids.push_back(dto.songId);
            }
            provider_.startResolveUrls(ids, [this](core::codec::UrlResult result) {
                const std::scoped_lock lock{fmMutex_};
                fmUrlPending_.push_back({std::move(result)});
            });
            return;
        }
    }

    void OnlineService::drainFmUrls()
    {
        std::vector<PendingFmUrls> incoming;
        {
            const std::scoped_lock lock{fmMutex_};
            incoming.swap(fmUrlPending_);
        }
        if (incoming.empty())
        {
            return;
        }

        PendingFmUrls item = std::move(incoming.back());
        fmActive_ = false;
        if (!item.result.ok)
        {
            diag_.recordFmFailure();
            fmSongs_.clear();
            return;
        }

        std::vector<contracts::OnlineTrackDto> ready;
        ready.reserve(fmSongs_.size());
        for (contracts::OnlineTrackDto &dto : fmSongs_)
        {
            const auto found = std::find_if(item.result.entries.begin(), item.result.entries.end(),
                                            [&dto](const core::codec::UrlEntry &entry) {
                                                return entry.songId == dto.songId;
                                            });
            if (found == item.result.entries.end() || found->url.empty())
            {
                continue;
            }
            dto.playUrl = found->url;
            dto.playable = true;
            fmSeen_.push_back(dto.songId);
            if (fmSeen_.size() > kFmSeenLimit)
            {
                fmSeen_.erase(fmSeen_.begin());
            }
            ready.push_back(std::move(dto));
        }
        fmSongs_.clear();

        if (ready.empty() || !fmListener_)
        {
            return;
        }
        fmListener_(ready);
    }


} // namespace sonora::online::adapters
