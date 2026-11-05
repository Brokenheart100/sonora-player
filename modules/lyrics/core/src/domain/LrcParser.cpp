#include "mamba/lyrics/core/domain/LrcParser.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <vector>

namespace mamba::lyrics::core {
namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

[[nodiscard]] bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t';
}

[[nodiscard]] std::string_view trimLeft(std::string_view text) noexcept
{
    std::size_t begin = 0;
    while (begin < text.size() && isSpace(text[begin])) {
        ++begin;
    }
    return text.substr(begin);
}

[[nodiscard]] std::string_view trimRight(std::string_view text) noexcept
{
    std::size_t end = text.size();
    while (end > 0 && (isSpace(text[end - 1]) || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(0, end);
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    return trimRight(trimLeft(text));
}

// 读一串十进制数字，越界就失败。分钟、秒、毫秒都走它。
[[nodiscard]] std::optional<int> readDigits(std::string_view text, std::size_t &pos,
                                            int minDigits, int maxDigits) noexcept
{
    int value = 0;
    int digits = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && digits < maxDigits) {
        value = value * 10 + (text[pos] - '0');
        ++pos;
        ++digits;
    }
    if (digits < minDigits) {
        return std::nullopt;
    }
    return value;
}

// 小数部分统一折算成毫秒：".5"→500、".50"→500、".500"→500、".5555"→555。
[[nodiscard]] int fractionToMillis(std::string_view digits) noexcept
{
    int millis = 0;
    int scale = 100;
    for (std::size_t i = 0; i < digits.size() && i < 3; ++i) {
        millis += (digits[i] - '0') * scale;
        scale /= 10;
    }
    return millis;
}

}  // namespace

std::optional<std::int64_t> LrcParser::parseTimeTag(std::string_view body) noexcept
{
    body = trim(body);
    if (body.empty()) {
        return std::nullopt;
    }

    std::size_t pos = 0;
    const auto minutes = readDigits(body, pos, 1, 4);  // 允许 [123:45.67] 这种超长分钟
    if (!minutes || pos >= body.size() || body[pos] != ':') {
        return std::nullopt;
    }
    ++pos;

    const auto seconds = readDigits(body, pos, 1, 2);
    if (!seconds) {
        return std::nullopt;
    }

    int millis = 0;
    // 小数两种写法：`ss.xx`（标准）与 `ss:xx`（少数写入器）
    if (pos < body.size() && (body[pos] == '.' || body[pos] == ':')) {
        ++pos;
        const std::size_t begin = pos;
        while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9') {
            ++pos;
        }
        if (pos == begin) {
            return std::nullopt;
        }
        millis = fractionToMillis(body.substr(begin, pos - begin));
    }
    if (pos != body.size()) {
        return std::nullopt;  // 时间标签后面还挂着东西 → 这不是时间标签
    }

    return static_cast<std::int64_t>(*minutes) * 60000
           + static_cast<std::int64_t>(*seconds) * 1000
           + static_cast<std::int64_t>(millis);
}

namespace {

// `[offset:+500]` / `[offset:-200]`；不是就返回 nullopt。
[[nodiscard]] std::optional<std::int64_t> parseOffsetTag(std::string_view body) noexcept
{
    constexpr std::string_view kPrefix = "offset:";
    body = trim(body);
    if (body.size() <= kPrefix.size()
        || body.substr(0, kPrefix.size()) != kPrefix) {
        return std::nullopt;
    }
    std::string_view value = trim(body.substr(kPrefix.size()));
    bool negative = false;
    if (!value.empty() && (value.front() == '+' || value.front() == '-')) {
        negative = value.front() == '-';
        value.remove_prefix(1);
    }
    std::int64_t amount = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), amount);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return negative ? -amount : amount;
}

// 把 `<00:12.00>逐<00:12.35>字` 拆成逐字时间；没有尖括号就原样返回 false。
[[nodiscard]] bool splitWords(std::string_view text, std::vector<LyricWord> &words,
                              std::string &plain)
{
    bool found = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        if (text[pos] != '<') {
            ++pos;
            continue;
        }
        const std::size_t close = text.find('>', pos + 1);
        if (close == std::string_view::npos) {
            break;
        }
        const auto time = LrcParser::parseTimeTag(text.substr(pos + 1, close - pos - 1));
        if (!time) {
            ++pos;  // 尖括号里不是时间 → 当成普通字符，别把歌词吃掉
            continue;
        }
        const std::size_t next = text.find('<', close + 1);
        const std::string_view chunk =
            text.substr(close + 1, (next == std::string_view::npos ? text.size() : next) - (close + 1));
        words.push_back(LyricWord{*time, std::string(chunk)});
        plain.append(chunk);
        found = true;
        pos = (next == std::string_view::npos) ? text.size() : next;
    }
    return found;
}

}  // namespace

ParsedLyrics LrcParser::parse(std::string_view text)
{
    ParsedLyrics parsed;

    if (text.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
        text.remove_prefix(kUtf8Bom.size());  // 记事本存的 LRC 常带 BOM
    }

    // 先按文件顺序攒原始行，最后再合并译文——合并要看"上一条"，而一行可能有
    // 多个时间标签，边解析边合并会把同一行自己的两个标签当成译文。
    std::vector<LyricEntry> raw;

    std::size_t lineStart = 0;
    while (lineStart <= text.size()) {
        const std::size_t lineEnd = text.find('\n', lineStart);
        std::string_view line =
            text.substr(lineStart, (lineEnd == std::string_view::npos ? text.size() : lineEnd) - lineStart);
        line = trimRight(line);
        lineStart = (lineEnd == std::string_view::npos) ? text.size() + 1 : lineEnd + 1;

        if (line.empty()) {
            continue;
        }
        ++parsed.linesSeen;

        std::vector<std::int64_t> stamps;
        std::size_t pos = 0;
        // 行首连续的时间标签 / offset 标签；遇到第一个不是标签的就停。
        while (pos < line.size() && line[pos] == '[') {
            const std::size_t close = line.find(']', pos + 1);
            if (close == std::string_view::npos) {
                break;
            }
            const std::string_view body = line.substr(pos + 1, close - pos - 1);
            if (const auto stamp = parseTimeTag(body)) {
                stamps.push_back(*stamp);
                ++parsed.tagsSeen;
            } else if (const auto offset = parseOffsetTag(body)) {
                parsed.offsetMs = *offset;
            }
            // 其余（[ti:] [ar:] [al:] [by:] [length:] [kana:]）静默忽略。
            pos = close + 1;
        }

        if (stamps.empty()) {
            ++parsed.linesDropped;
            continue;
        }

        std::string_view content = line.substr(pos);
        if (!content.empty() && content.front() == ' ') {
            content.remove_prefix(1);  // `[00:12.00] 你在哪里` 这种多打一个空格
        }

        LyricEntry entry;
        std::string plain;  // splitWords 往里拼"去掉时间标签的正文"，初值必须为空
        if (splitWords(content, entry.words, plain)) {
            entry.text = std::move(plain);
        } else {
            entry.words.clear();  // 半截解析结果不要流出去
            entry.text = std::string(content);
        }

        for (const std::int64_t stamp : stamps) {
            entry.timeMs = stamp;
            raw.push_back(entry);
        }
    }

    // offset 整篇平移（可能为负，负到行首之前的时间由 Lyrics::create 丢掉）。
    if (parsed.offsetMs != 0) {
        for (LyricEntry &entry : raw) {
            entry.timeMs += parsed.offsetMs;
        }
    }

    // 相邻同时间戳 → 后者是前者的译文。已有译文就不再覆盖（三语歌词只留前两句）。
    parsed.entries.reserve(raw.size());
    for (LyricEntry &entry : raw) {
        if (!parsed.entries.empty()) {
            LyricEntry &prev = parsed.entries.back();
            if (prev.timeMs == entry.timeMs && prev.translation.empty()
                && !entry.text.empty()) {
                prev.translation = std::move(entry.text);
                // 译文行若带逐字时间也不合并——那是另一套时间轴，混进来会错。
                continue;
            }
        }
        parsed.entries.push_back(std::move(entry));
    }

    return parsed;
}

}  // namespace mamba::lyrics::core
