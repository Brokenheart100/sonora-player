#include "sonora/lyrics/core/domain/LyricTimeline.h"

#include <algorithm>

namespace sonora::lyrics::core {

std::int32_t LyricTimeline::indexAt(std::span<const std::int64_t> times,
                                    std::int64_t nowMs) noexcept
{
    if (times.empty() || nowMs < times.front()) {
        return kBeforeFirst;
    }
    // upper_bound 找第一个 > nowMs 的位置，它前面那个就是当前行。
    // 同一时间戳有多行（空行 + 正文）时，upper_bound 让我们落到**最后**一行，
    // 也就是"同一时刻先说最后一句"——比停在第一行更符合直觉。
    const auto it = std::upper_bound(times.begin(), times.end(), nowMs);
    return static_cast<std::int32_t>(it - times.begin()) - 1;
}

}  // namespace sonora::lyrics::core
