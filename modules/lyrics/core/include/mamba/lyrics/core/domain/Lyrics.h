// 歌词本身（聚合）。
//
// 它是一个**只读产物**：解析一次，之后只回答问题。没有 setter、没有状态机。
// 之所以还叫聚合，是因为它守一条不变量：**时间戳单调不减**。
// 外面拿到的每一行都可以放心地拿去做二分，不必自己再验一遍。
#pragma once

#include <cstdint>
#include <vector>

#include "mamba/lyrics/core/domain/LyricEntry.h"

namespace mamba::lyrics::core {

class Lyrics {
public:
    // 唯一入口。**不返回 Result**：这里没有任何会失败的事——
    // 非法的行被丢掉（负时间/空行），剩下的排好序，最坏情况是零行。
    // "零行"不是错误，是"这首歌没有歌词"，由调用方决定怎么说。
    [[nodiscard]] static Lyrics create(std::vector<LyricEntry> entries);

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<LyricEntry> &entries() const noexcept { return entries_; }

    // 当前该高亮第几行；还没到第一行返回 -1。
    [[nodiscard]] std::int32_t lineIndexAt(std::int64_t nowMs) const noexcept;

    // 有任何一行带逐字时间吗（决定要不要走卡拉 OK 渲染）。
    [[nodiscard]] bool hasWords() const noexcept;

    // 最后一行的起始时间，给"歌词到底有多长"这种偏实体用。
    [[nodiscard]] std::int64_t lastTimeMs() const noexcept;

private:
    Lyrics(std::vector<LyricEntry> entries, std::vector<std::int64_t> times);

    std::vector<LyricEntry> entries_;
    // 冗余一份时间：二分时不必跳 40 字节的结构体，缓存友好是白拿的。
    std::vector<std::int64_t> times_;
};

}  // namespace mamba::lyrics::core
