// 文本小工具：只做「跟语言无关的字节级」处理，不碰编码转换。
//
// 为什么在 Kernel：曲库导入（adapters 读标签）与领域层（core 建聚合）都要用同一套
// 空白规则，各写一份迟早分叉——分叉的症状是"标签里带全角空格的标题被存成非空"，
// 列表里出现一行看起来是空白但删不掉的记录。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sonora::kernel {

namespace detail {

inline bool isAsciiBlank(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// 全角空格 U+3000 的 UTF-8 是 E3 80 80。中文标题里很常见（从网页复制来的），
// 只 trim ASCII 会把 "　夜航西飞" 当成非空标题存进去。
inline bool isFullWidthSpaceAt(std::string_view text, std::size_t index) noexcept
{
    return index + 2 < text.size() && static_cast<unsigned char>(text[index]) == 0xE3
           && static_cast<unsigned char>(text[index + 1]) == 0x80
           && static_cast<unsigned char>(text[index + 2]) == 0x80;
}

} // namespace detail

// 去掉两端的 ASCII 空白与全角空格，返回**视图**（不复制）。
[[nodiscard]] inline std::string_view trimSpace(std::string_view text) noexcept
{
    using detail::isAsciiBlank;
    using detail::isFullWidthSpaceAt;

    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end) {
        if (isAsciiBlank(text[begin])) {
            ++begin;
        } else if (isFullWidthSpaceAt(text, begin)) {
            begin += 3;
        } else {
            break;
        }
    }

    while (end > begin) {
        const std::size_t last = end - 1;
        if (isAsciiBlank(text[last])) {
            end = last;
            continue;
        }
        if (end >= begin + 3 && isFullWidthSpaceAt(text, end - 3)) {
            end -= 3;
            continue;
        }
        break;
    }

    return text.substr(begin, end - begin);
}

// 需要独立字符串时用这个（不要在热路径上随手调：它一定会分配）。
[[nodiscard]] inline std::string trimSpaceCopy(std::string_view text)
{
    return std::string{trimSpace(text)};
}

} // namespace sonora::kernel
