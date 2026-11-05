// 适配器内部共用的小工具（**不在公开头里**：这些是实现细节，不该出现在 include/ 下）。
//
// 三条纪律，每条都踩过一次：
//   1. `fs::path` 从 `std::string` 构造时按当前 ANSI 代码页解释，不是 UTF-8。
//      中文文件名必须经 u8Path()；反向拼路径用 u8String()。
//   2. 歌词文件在 Windows 上可能是 UTF-8 BOM / UTF-16LE BOM / 无 BOM 的 UTF-8。
//      没有 BOM 又解不出 UTF-8 的（GBK），宁可报"编码不支持"，也别把乱码画上屏幕。
//   3. 转换函数自己写，不引 iconv / Windows API：UTF-16 → UTF-8 只有二十来行，
//      换来的好处是这个文件在任何平台上行为一致，也不给 boundaries.py 添新规矩。
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace mamba::lyrics::adapters::detail {

inline std::filesystem::path u8Path(const std::string &utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string u8String(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// 只做结构校验（续字节、不超长、不落在代理区之外），不做规范化。
inline bool isValidUtf8(std::string_view text) noexcept
{
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        std::uint32_t code = 0;
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            code = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            code = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            code = lead & 0x07u;
        } else {
            return false;
        }
        if (i + extra >= text.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto byte = static_cast<unsigned char>(text[i + k]);
            if ((byte & 0xC0) != 0x80) {
                return false;
            }
            code = (code << 6) | (byte & 0x3Fu);
        }
        if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800)
            || (extra == 3 && code < 0x10000) || code > 0x10FFFF
            || (code >= 0xD800 && code <= 0xDFFF)) {
            return false;  // 超长编码 / 代理区，都是非法 UTF-8
        }
        i += extra + 1;
    }
    return true;
}

inline void appendUtf8(std::string &out, std::uint32_t code)
{
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// UTF-16（带不带 BOM 都行，BOM 由调用方剥掉）→ UTF-8。孤立的代理项写成 U+FFFD。
inline std::string utf16ToUtf8(std::string_view bytes, bool bigEndian)
{
    std::string out;
    out.reserve(bytes.size());
    const auto unitAt = [&bytes, bigEndian](std::size_t index) -> std::uint32_t {
        const auto first = static_cast<unsigned char>(bytes[index]);
        const auto second = static_cast<unsigned char>(bytes[index + 1]);
        return bigEndian ? (static_cast<std::uint32_t>(first) << 8) | second
                         : (static_cast<std::uint32_t>(second) << 8) | first;
    };
    std::size_t i = 0;
    while (i + 1 < bytes.size()) {
        std::uint32_t code = unitAt(i);
        i += 2;
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < bytes.size()) {
            const std::uint32_t low = unitAt(i);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                i += 2;
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            } else {
                code = 0xFFFD;
            }
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            code = 0xFFFD;
        }
        appendUtf8(out, code);
    }
    return out;
}

}  // namespace mamba::lyrics::adapters::detail
