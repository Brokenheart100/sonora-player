// 播放队列聚合（M3 最小版：只有顺序模式）。
//
// 为什么不叫 Playlist：Playlist 是用户自己的歌单（Playlists 模块，持久化）；
// PlayQueue 是"当前这一次播放会话里的顺序"，不落库，进程关了就没。
//
// M3 只做顺序推进。M6 才加随机/单曲循环/列表循环——那时这里会长出一个
// 预生成的排列 + 游标（Fisher–Yates，见 §7.2），而不是"每次抽一首"，
// 因为"上一首"必须能精确回到刚才放过的那首。
#pragma once

#include <cstdint>
#include <vector>

#include <mamba/playback/contracts/Commands.h>

namespace mamba::playback::core {

class PlayQueue {
public:
    /// 换一整个队列并从 startIndex 开始。
    /// startIndex 会被夹取；空列表则退化为"没有当前项"。
    void setEntries(std::vector<contracts::QueueEntry> entries, std::int32_t startIndex);

    void clear();

    [[nodiscard]] const std::vector<contracts::QueueEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] const contracts::QueueEntry* current() const noexcept;
    [[nodiscard]] std::int32_t index() const noexcept { return index_; }
    [[nodiscard]] std::int32_t size() const noexcept {
        return static_cast<std::int32_t>(entries_.size());
    }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    /// 往后挪一首。已经在末尾则返回 false（M3 不循环）。
    [[nodiscard]] bool advance() noexcept;

    /// 往前挪一首。已经在开头则返回 false。
    [[nodiscard]] bool retreat() noexcept;

private:
    std::vector<contracts::QueueEntry> entries_;
    std::int32_t index_ = -1;
};

}  // namespace mamba::playback::core
