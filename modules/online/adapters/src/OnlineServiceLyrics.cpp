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

    // —— 在线歌词的按需拉取与缓存 ——

    kernel::Result<contracts::OnlineLyricDto> OnlineService::handle(
        const contracts::FetchOnlineLyrics &query)
    {
        // 先排空：调用方是 1 秒一次的定时重试，回包可能刚好在这一拍之前到。
        drainLyrics();

        const std::string &songId = query.songId;
        if (songId.empty())
        {
            return kernel::Result<contracts::OnlineLyricDto>::ok(contracts::OnlineLyricDto{});
        }

        if (const auto it = lyrics_.find(songId); it != lyrics_.end())
        {
            return kernel::Result<contracts::OnlineLyricDto>::ok(it->second.dto);
        }

        // 退避闸门挡住时**不报错**，就当"还没到"：歌词是背景服务，
        // 把它变成一条红色错误只会打扰正在听歌的人。
        if (failures_.blocked(nowMs()))
        {
            return kernel::Result<contracts::OnlineLyricDto>::ok(contracts::OnlineLyricDto{});
        }

        // 内存上限：超了就把最早那首丢掉（它不是 LRU，但歌词的重取成本极低）。
        if (lyrics_.size() >= kLyricCacheSize && !lyricOrder_.empty())
        {
            lyrics_.erase(lyricOrder_.front());
            lyricOrder_.erase(lyricOrder_.begin());
        }

        LyricEntry entry;
        entry.loading = true;
        lyrics_.emplace(songId, entry);
        lyricOrder_.push_back(songId);

        provider_.startLyric(songId, [this, songId](core::codec::LyricResult result) {
            // worker 线程：只入队（与其它回包同一个约定）。
            const std::scoped_lock lock{lyricMutex_};
            lyricPending_.push_back(PendingLyric{songId, std::move(result)});
        });
        return kernel::Result<contracts::OnlineLyricDto>::ok(contracts::OnlineLyricDto{});
    }

    void OnlineService::drainLyrics()
    {
        std::vector<PendingLyric> incoming;
        {
            const std::scoped_lock lock{lyricMutex_};
            incoming.swap(lyricPending_);
        }
        for (auto &item : incoming)
        {
            const auto it = lyrics_.find(item.songId);
            if (it == lyrics_.end())
            {
                // 已经被内存上限淘汰掉了：结果没处放，也不算失败。
                continue;
            }
            contracts::OnlineLyricDto dto;
            dto.ready = true;
            if (item.result.ok)
            {
                dto.lrc = std::move(item.result.lrc);
                dto.translated = std::move(item.result.translated);
                dto.instrumental = item.result.instrumental;
                noteSuccess();
            }
            else
            {
                // 歌词取不到不影响听歌，所以只留痕、不进全局 lastError。
                noteFailure(item.result.error, "歌词");
                dto.error = withHint(item.result.error);
                dto.instrumental = true;
            }
            it->second.dto = std::move(dto);
            it->second.loading = false;
        }
    }


} // namespace sonora::online::adapters
