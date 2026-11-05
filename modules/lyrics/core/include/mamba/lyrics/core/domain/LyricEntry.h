// 歌词里的一行——领域层自己的形态。
//
// 它和 contracts::LyricLine 长得很像。为什么不直接共用一个？
// 因为契约一变，域内所有代码都得跟着变；而域里将来可能加"这行属于第几段"
// 这类只对领域有意义的东西，那时契约不该跟着抖。
// 目前两者差别是零，这是**刻意的**：宁可多一行转换，也不要让适配器决定领域形状。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mamba::lyrics::core {

struct LyricWord {
    std::int64_t timeMs = 0;
    std::string text;
};

struct LyricEntry {
    std::int64_t timeMs = 0;
    std::string text;
    // 同一时间点的译文（LRC 里表现为重复的时间戳）。
    std::string translation;
    // 增强 LRC 的逐字时间；普通 LRC 为空。
    std::vector<LyricWord> words;
};

}  // namespace mamba::lyrics::core
