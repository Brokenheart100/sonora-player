// LRC 解析器。
//
// 要处理的东西（都是真实文件里出现过的）：
//   * `[mm:ss.xx]`、`[mm:ss:xx]`、`[mm:ss]` 三种时间写法（第三种没有小数）
//   * 一行多个时间标签：`[00:12.00][01:20.00]同一句`
//   * `[offset:+500]` / `[offset:-200]`：整篇平移
//   * 双语歌词：**重复的时间戳**表示"这是上一句的译文"，合并进上一行
//   * 增强 LRC：`[00:12.00]<00:12.00>逐<00:12.35>字`
//   * 垃圾行：网易云导出的文件头上压着一行 JSON（`{"t":0,"c":[...]}`），
//     以及各种 `[ti:]` `[ar:]` `[length:]` `[by:]` —— 一律丢弃，不报错
//
// 设计取向：**解析器不抛错、不返回 Result**。歌词是"有就用、没有就空着"的
// 附属信息，为它让播放失败是本末倒置。能读的行读出来，读不懂的行计个数。
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "mamba/lyrics/core/domain/LyricEntry.h"

namespace mamba::lyrics::core {

struct ParsedLyrics {
    // 仍是**文件顺序**（可能不单调），由 Lyrics::create 负责排序与去负。
    std::vector<LyricEntry> entries;
    // 已应用的 `[offset:]`（毫秒，可为负），只留作诊断。
    std::int64_t offsetMs = 0;
    std::size_t linesSeen = 0;
    // 没有时间标签、被丢掉的行数（JSON 头、纯空白）。
    std::size_t linesDropped = 0;
    // 时间标签个数（含一行多个的情况）。
    std::size_t tagsSeen = 0;
};

class LrcParser {
public:
    [[nodiscard]] static ParsedLyrics parse(std::string_view text);

    // `12:34.56` / `12:34:56` / `12:34` → 毫秒；不是时间就返回 nullopt。
    // 公开出来是为了让单测能一条条钉住边界（负数、超长分钟、四位小数）。
    [[nodiscard]] static std::optional<std::int64_t> parseTimeTag(std::string_view body) noexcept;
};

}  // namespace mamba::lyrics::core
