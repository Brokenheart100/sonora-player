#include "sonora/lyrics/core/domain/Lyrics.h"

#include <algorithm>
#include <utility>

#include "sonora/lyrics/core/domain/LyricTimeline.h"

namespace sonora::lyrics::core {

Lyrics Lyrics::create(std::vector<LyricEntry> entries)
{
    // 丢掉负时间的行（`[offset:-99999]` 打过头会造出这种东西）。
    // 空文本的行**保留**：LRC 里它是"这里的停顿"，丢了会让滚动节奏变紧。
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [](const LyricEntry &entry) { return entry.timeMs < 0; }),
                  entries.end());

    // stable_sort 而不是 sort：同一时间戳的多行（正文 + 译文没合上的情况）
    // 保持文件里的先后顺序，视觉上才不会跳。
    std::stable_sort(entries.begin(), entries.end(),
                     [](const LyricEntry &lhs, const LyricEntry &rhs) {
                         return lhs.timeMs < rhs.timeMs;
                     });

    std::vector<std::int64_t> times;
    times.reserve(entries.size());
    for (const LyricEntry &entry : entries) {
        times.push_back(entry.timeMs);
    }
    return Lyrics{std::move(entries), std::move(times)};
}

Lyrics::Lyrics(std::vector<LyricEntry> entries, std::vector<std::int64_t> times)
    : entries_(std::move(entries)), times_(std::move(times))
{
}

std::int32_t Lyrics::lineIndexAt(std::int64_t nowMs) const noexcept
{
    return LyricTimeline::indexAt(times_, nowMs);
}

bool Lyrics::hasWords() const noexcept
{
    return std::any_of(entries_.begin(), entries_.end(),
                       [](const LyricEntry &entry) { return !entry.words.empty(); });
}

std::int64_t Lyrics::lastTimeMs() const noexcept
{
    return times_.empty() ? 0 : times_.back();
}

}  // namespace sonora::lyrics::core
