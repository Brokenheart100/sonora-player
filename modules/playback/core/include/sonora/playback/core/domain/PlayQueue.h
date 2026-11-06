// 播放队列聚合：条目 + 一种播放模式 + 一条播放顺序。
//
// 为什么不叫 Playlist：Playlist 是用户自己的歌单（Playlists 模块，持久化）；
// PlayQueue 是"当前这一次播放会话里的顺序"。
//
// 关键设计：**顺序是预生成的排列，不是"每次抽一首"**。
//   - Sequential / RepeatAll / RepeatOne：排列就是原顺序，游标即条目下标
//   - Shuffle：Fisher–Yates 排列 + 游标，「上一首」= 游标减一 = 刚才真放的那首
// 这样"上一首能精确回溯"是结构保证的，不靠历史栈去猜。
#pragma once

#include <cstdint>
#include <vector>

#include <sonora/playback/contracts/Commands.h>
#include <sonora/playback/contracts/PlaybackMode.h>

namespace sonora::playback::core {

/// 为什么要区分手动和自动：
/// 单曲循环下，"播完了"该重放本曲，而用户点"下一首"该前进——
/// 同一个 advance() 对这两种触发必须给出不同答案。
enum class AdvanceReason : std::uint8_t {
    Manual,
    AutoFinished,
};

class PlayQueue {
public:
    /// 换一整个队列并从 startIndex 开始。
    /// startIndex 会被夹取；空列表则退化为"没有当前项"。
    void setEntries(std::vector<contracts::QueueEntry> entries, std::int32_t startIndex);

    void clear();

    /// 换模式。切到随机会重新洗一次排列（并且保持当前这首在最前面）；
    /// 切回顺序/循环则回到原始顺序、游标落在当前这首上。
    void setMode(contracts::PlaybackMode mode, std::uint64_t seed = 0);

    [[nodiscard]] contracts::PlaybackMode mode() const noexcept { return mode_; }

    [[nodiscard]] const std::vector<contracts::QueueEntry>& entries() const noexcept {
        return entries_;
    }

    /// 播放顺序：元素是 entries_ 的下标。界面画"接下来放什么"用它。
    [[nodiscard]] const std::vector<std::int32_t>& order() const noexcept { return order_; }

    /// 排列用的随机种子。持久化时和 order 一起存，下次启动才能接着放。
    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

    [[nodiscard]] const contracts::QueueEntry* current() const noexcept;

    /// 当前曲目在 entries_ 里的下标（不是播放序里的位置）。
    [[nodiscard]] std::int32_t index() const noexcept;

    [[nodiscard]] std::int32_t size() const noexcept {
        return static_cast<std::int32_t>(entries_.size());
    }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    /// 往后挪一首。返回 false 表示"到头了且不循环"。
    /// RepeatOne + AutoFinished 会返回 true 但**不动游标**（即重放本曲）。
    [[nodiscard]] bool advance(AdvanceReason reason = AdvanceReason::Manual);

    /// 往前挪一首。返回 false 表示已经在开头且不循环。
    [[nodiscard]] bool retreat();

    /// 在队列面板里点了某一项（entries_ 的下标）。找不到返回 false。
    [[nodiscard]] bool jumpTo(std::int32_t entryIndex);

private:
    void rebuildOrder(std::int32_t keepEntry, std::uint64_t seed);

    std::vector<contracts::QueueEntry> entries_;
    std::vector<std::int32_t> order_;
    std::int32_t cursor_ = -1; // 在 order_ 里的位置
    contracts::PlaybackMode mode_ = contracts::PlaybackMode::Sequential;
    std::uint64_t seed_ = 0;
};

} // namespace sonora::playback::core
