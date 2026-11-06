// 歌词的对外形态。
//
// 一条设计约束贯穿本文件：**界面拿到的必须是"已经能画"的东西**。
// 没有"原始 LRC 文本"这种东西流出去——文本在模块内部被解析成时间轴，
// 出了模块就只有 {时间, 文本, 翻译, 逐字} 四件事。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sonora::lyrics::contracts {

// 逐字时间（增强 LRC 的 `<mm:ss.xx>`）。普通 LRC 没有逐字信息，words 为空。
// 现在没有界面消费它，但解析器已经能解——等 M8 的卡拉 OK 效果要用，
// 那时再加一个 role 就行，不必回到解析层动刀。
struct LyricWord {
    std::int64_t timeMs = 0;
    std::string text;
};

// 一行歌词。
struct LyricLine {
    std::int64_t timeMs = 0;
    std::string text;
    // 同一时间点的第二行（双语歌词的译文）。LRC 用"重复的时间戳"表达翻译，
    // 解析时已经合并进这一行，界面不需要自己配对。
    std::string translation;
    std::vector<LyricWord> words;
};

enum class LyricsStatus : std::uint8_t {
    Found,         // 有歌词
    Instrumental,  // 纯音乐（或者文件里一条有效时间标签都没有）
    NotFound,      // 压根没有歌词来源
};

// 名字里带 Name 而不是 ToString：kernel 里的转名函数统一叫 toName，
// 原因见 modules/kernel/include/sonora/kernel/Error.h 顶部的注释
// （doctest 的无限定 toString 会通过 ADL 抢走候选集）。
[[nodiscard]] constexpr std::string_view toName(LyricsStatus status) noexcept
{
    switch (status) {
    case LyricsStatus::Found:
        return "found";
    case LyricsStatus::Instrumental:
        return "instrumental";
    case LyricsStatus::NotFound:
        return "not-found";
    }
    return "unknown";
}

// 一次查询的全部结果。
struct LyricsView {
    LyricsStatus status = LyricsStatus::NotFound;
    // 人读的来源说明："同名 .lrc" / "内嵌标签" / 空。出错时放错误原因。
    std::string source;
    std::vector<LyricLine> lines;
};

}  // namespace sonora::lyrics::contracts
