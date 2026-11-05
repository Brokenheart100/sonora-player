#include "mamba/catalog/adapters/SearchText.h"

#include <cstddef>
#include <cstdint>

namespace mamba::catalog::adapters {
namespace {

// 最小可用的 UTF-8 解码：合法序列返回码点，非法字节返回 0 并把 index 前移 1。
// 不做完整校验——文件名里混进坏字节能显示成方块，但不该让导入失败。
char32_t decodeUtf8(std::string_view text, std::size_t &index) noexcept
{
    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
        ++index;
        return lead;
    }

    std::size_t extra = 0;
    char32_t value = 0;
    if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        value = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        value = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        value = lead & 0x07;
    } else {
        ++index;
        return 0;
    }

    if (index + extra >= text.size()) {
        // 截断的序列：当作一个坏字节吃掉，避免越界。
        ++index;
        return 0;
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const auto next = static_cast<unsigned char>(text[index + i]);
        if ((next & 0xC0) != 0x80) {
            ++index;
            return 0;
        }
        value = (value << 6) | (next & 0x3F);
    }
    index += extra + 1;
    return value;
}

void appendCodepoint(std::string &out, char32_t codepoint)
{
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

char toLowerAscii(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

void appendNormalized(std::string &out, std::string_view text)
{
    std::size_t index = 0;
    while (index < text.size()) {
        const char32_t codepoint = decodeUtf8(text, index);
        if (isIdeograph(codepoint)) {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
            appendCodepoint(out, codepoint);
            out.push_back(' ');
            continue;
        }
        if (codepoint < 0x80) {
            out.push_back(toLowerAscii(static_cast<char>(codepoint)));
        } else {
            appendCodepoint(out, codepoint);
        }
    }
}

}  // namespace

bool isIdeograph(char32_t codepoint) noexcept
{
    // 汉字：基本区、扩展 A、兼容区、扩展 B 的常见部分（BMP 之外的罕见字按普通字符处理，
    // 它们不会出现在歌曲标题里，代价是不支持那部分的子串搜索）。
    const bool han = (codepoint >= 0x3400 && codepoint <= 0x4DBF)
                     || (codepoint >= 0x4E00 && codepoint <= 0x9FFF)
                     || (codepoint >= 0xF900 && codepoint <= 0xFAFF);
    // 假名（平假名 + 片假名 + 半角片假名）
    const bool kana = (codepoint >= 0x3040 && codepoint <= 0x30FF) || (codepoint >= 0xFF66 && codepoint <= 0xFF9F);
    // 谚文音节
    const bool hangul = (codepoint >= 0xAC00 && codepoint <= 0xD7AF);
    return han || kana || hangul;
}

std::string searchBody(std::string_view title, std::string_view artist, std::string_view album)
{
    std::string body;
    body.reserve(title.size() + artist.size() + album.size() + 8);

    const std::string_view parts[3] = {title, artist, album};
    for (const std::string_view part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!body.empty() && body.back() != ' ') {
            body.push_back(' ');
        }
        appendNormalized(body, part);
    }
    while (!body.empty() && body.back() == ' ') {
        body.pop_back();
    }
    return body;
}

std::string matchQuery(std::string_view text)
{
    std::string query;
    query.reserve(text.size() + 8);

    std::size_t index = 0;
    std::string run;   // 当前连续段的内容（已规范化）
    bool runIsIdeograph = false;
    bool runHasContent = false;
    bool runHasToken = false;  // 段里至少有一个能被 FTS5 切出 token 的字符

    const auto flush = [&]() {
        // runHasToken 为假意味着这一整段都是标点。FTS5 的 tokenizer 会把标点
        // 全当分隔符，于是 `"%"*` 这种查询一个 token 都切不出来、直接是语法错误。
        // 与其让用户收到一个 SQL 报错，不如把这种输入当成"什么都没输入"。
        if (!runHasContent || !runHasToken) {
            run.clear();
            runHasContent = false;
            runHasToken = false;
            return;
        }
        if (!query.empty()) {
            query.push_back(' ');
        }
        if (runIsIdeograph) {
            // 短语：`"夜 航"`。run 已经是"字 字 字"的形状。
            query.push_back('"');
            query.append(run);
            query.push_back('"');
        } else {
            // 前缀词：`"abc"*`。引号防止用户输入变成 FTS5 语法。
            query.push_back('"');
            query.append(run);
            query.append("\"*");
        }
        run.clear();
        runHasContent = false;
        runHasToken = false;
    };

    while (index < text.size()) {
        const char32_t codepoint = decodeUtf8(text, index);
        if (codepoint == U' ' || codepoint == U'\t' || codepoint == U'\u3000') {
            flush();
            continue;
        }
        const bool ideograph = isIdeograph(codepoint);
        if (runHasContent && ideograph != runIsIdeograph) {
            flush();
        }
        if (ideograph) {
            if (runHasContent) {
                run.push_back(' ');
            }
            appendCodepoint(run, codepoint);
            runIsIdeograph = true;
            runHasToken = true;
        } else {
            const bool ascii = codepoint < 0x80;
            const bool asciiWord = ascii && ((codepoint >= U'0' && codepoint <= U'9')
                                             || (codepoint >= U'a' && codepoint <= U'z')
                                             || (codepoint >= U'A' && codepoint <= U'Z'));
            if (ascii && !asciiWord) {
                // ASCII 标点（含引号）：FTS5 的 tokenizer 在这里切词，我们照做。
                // 这样 "100%" 会被拆成 100 + 分隔符，搜出来的仍是"以 100 开头"。
                runIsIdeograph = false;
                flush();
                continue;
            }
            if (ascii) {
                run.push_back(toLowerAscii(static_cast<char>(codepoint)));
            } else {
                appendCodepoint(run, codepoint);
            }
            runIsIdeograph = false;
            runHasToken = true;
        }
        runHasContent = true;
    }
    flush();
    return query;
}

}  // namespace mamba::catalog::adapters
