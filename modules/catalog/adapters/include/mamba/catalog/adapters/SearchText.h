// 搜索文本的两段转换：入库时怎么存、查询时怎么问。
//
// 为什么需要这个文件：SQLite 的 FTS5 只有按"词"切分的分词器。`unicode61` 把
// 一串连续汉字当成**一个词**，于是 `MATCH '夜航'` 匹配不到 `夜航西飞`——只有
// `MATCH '夜航*'`（前缀）能中。中文用户想的是子串，这里就要补上这一层。
//
// 做法（不写自定义分词器，纯 SQL 能解决）：
//   入库：把每个 CJK 字符用空格隔开 → `夜 航 西 飞`，这样"每个字"都是一个 token；
//   查询：把连续汉字写成**短语** → `"夜 航 西"`，FTS5 的短语要求 token 相邻且有序，
//         等价于在原文里找连续子串。
// 副作用是好消息：一个汉字也能搜（`"夜"`），比原计划的"3 个字以上"更宽松。
//
// 非 CJK 部分保持原样（只转小写），查询时用带引号的前缀 `"abc"*`，
// 这样 `abc` 能命中 `abcdef`，而用户输入里的 FTS5 语法字符（`"` `*` `:` `-`）
// 都被引号吃掉，不会变成语法错误。
#pragma once

#include <string>
#include <string_view>

namespace mamba::catalog::adapters {

// 一个字符是否属于"需要逐字切分"的表意文字。覆盖汉字（含扩展 A 与兼容区）、
// 假名、谚文——它们都是"用户会按子串搜"的文字。
[[nodiscard]] bool isIdeograph(char32_t codepoint) noexcept;

// 入库体：`title` / `artist` / `album` 拼起来，CJK 逐字空格分隔，ASCII 转小写。
// 三段之间用一个空格连接；空段跳过。
[[nodiscard]] std::string searchBody(std::string_view title,
                                     std::string_view artist,
                                     std::string_view album);

// 查询表达式：连续汉字 → 短语（`"夜 航"`），其余连续非空白 → 前缀词（`"abc"*`）。
// 多个条件之间是空格，FTS5 里等于 AND。输入为空或全是空白时返回空串，
// 调用方据此走"不筛"的路径。
[[nodiscard]] std::string matchQuery(std::string_view text);

}  // namespace mamba::catalog::adapters
