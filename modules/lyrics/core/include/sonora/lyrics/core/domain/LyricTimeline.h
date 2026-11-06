// 时间轴：把"当前毫秒 → 第几行"收在一处。
//
// 二分查找不是为了性能好看。滚动歌词每秒问 30 次，几百行线性扫也跑得动；
// 但如果哪天有人把 tick 调成 1ms（或者拿它给逐字高亮当基座），
// 线性扫描就会变成一次自找的掉帧。O(log n) 且没有额外分配，成本是零。
#pragma once

#include <cstdint>
#include <span>

namespace sonora::lyrics::core {

class LyricTimeline {
public:
    // 还没唱到第一行时返回 -1（界面用它表示"没有高亮行"）。
    static constexpr std::int32_t kBeforeFirst = -1;

    // times 必须**单调不减**（Lyrics::create 保证）。
    // nowMs 落在第 i 行与第 i+1 行之间 → 返回 i。
    [[nodiscard]] static std::int32_t indexAt(std::span<const std::int64_t> times,
                                              std::int64_t nowMs) noexcept;
};

}  // namespace sonora::lyrics::core
