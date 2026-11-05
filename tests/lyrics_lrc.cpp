// LRC 解析 / 歌词聚合 / 时间轴的单测。
//
// 断言的是**解析结果的字面形状**（几行、每行的毫秒与文本），不是"能不能显示"。
// 这样以后改 UI 不会让这些用例跟着抖；而一旦解析语义变了，它们会立刻红。
#include "doctest_support.h"

#include <cstdint>
#include <string>
#include <vector>

#include "mamba/lyrics/core/domain/LrcParser.h"
#include "mamba/lyrics/core/domain/LyricTimeline.h"
#include "mamba/lyrics/core/domain/Lyrics.h"

namespace {

using mamba::lyrics::core::LrcParser;
using mamba::lyrics::core::LyricEntry;
using mamba::lyrics::core::LyricTimeline;
using mamba::lyrics::core::Lyrics;

}  // namespace

TEST_CASE("LRC：三行标准格式，时间与文本都对得上")
{
    const auto parsed = LrcParser::parse("[00:12.00]第一句\n[00:15.50]第二句\n[01:02.03]第三句\n");

    REQUIRE(parsed.entries.size() == 3);
    CHECK(parsed.entries[0].timeMs == 12000);
    CHECK(parsed.entries[0].text == "第一句");
    CHECK(parsed.entries[1].timeMs == 15500);
    CHECK(parsed.entries[2].timeMs == 62030);
    CHECK(parsed.tagsSeen == 3);
    CHECK(parsed.linesDropped == 0);
}

TEST_CASE("LRC：一行多个时间标签 → 各自成行")
{
    const auto parsed = LrcParser::parse("[00:10.00][00:20.00][00:30.00]副歌\n");

    REQUIRE(parsed.entries.size() == 3);
    CHECK(parsed.entries[0].timeMs == 10000);
    CHECK(parsed.entries[1].timeMs == 20000);
    CHECK(parsed.entries[2].timeMs == 30000);
    for (const auto &entry : parsed.entries) {
        CHECK(entry.text == "副歌");
    }
}

TEST_CASE("LRC：[offset:] 整篇平移，负值可以把第一行推到负时间")
{
    const auto late = LrcParser::parse("[offset:+1500]\n[00:10.00]甲\n");
    CHECK(late.offsetMs == 1500);
    REQUIRE(late.entries.size() == 1);
    CHECK(late.entries[0].timeMs == 11500);

    const auto early = LrcParser::parse("[offset:-500]\n[00:10.00]甲\n");
    CHECK(early.offsetMs == -500);
    REQUIRE(early.entries.size() == 1);
    CHECK(early.entries[0].timeMs == 9500);

    // 平移过头：解析器照实算（-10000），丢掉负行是 Lyrics::create 的职责。
    const auto overshoot = LrcParser::parse("[offset:-20000]\n[00:10.00]甲\n");
    REQUIRE(overshoot.entries.size() == 1);
    CHECK(overshoot.entries[0].timeMs == -10000);
}

TEST_CASE("LRC：重复时间戳 = 翻译，合并进上一行而不是变成两行")
{
    const auto parsed = LrcParser::parse(
        "[00:10.00]I love you\n[00:10.00]我爱你\n[00:12.00]next line\n");

    REQUIRE(parsed.entries.size() == 2);
    CHECK(parsed.entries[0].text == "I love you");
    CHECK(parsed.entries[0].translation == "我爱你");
    CHECK(parsed.entries[1].text == "next line");
    CHECK(parsed.entries[1].translation.empty());
}

TEST_CASE("LRC：增强格式的逐字时间被拆出来，正文去掉尖括号")
{
    const auto parsed = LrcParser::parse("[00:10.00]<00:10.00>逐<00:10.35>字\n");

    REQUIRE(parsed.entries.size() == 1);
    const auto &entry = parsed.entries[0];
    CHECK(entry.text == "逐字");
    REQUIRE(entry.words.size() == 2);
    CHECK(entry.words[0].timeMs == 10000);
    CHECK(entry.words[0].text == "逐");
    CHECK(entry.words[1].timeMs == 10350);
    CHECK(entry.words[1].text == "字");
}

TEST_CASE("LRC：垃圾行被丢弃，不报错也不吞掉正文")
{
    // 网易云导出的文件头上真有一行 JSON，后面跟着 [ti:]/[ar:] 之类。
    const std::string text =
        "{\"t\":0,\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"Clear Sky\"}]}\n"
        "[ti:Call of Silence]\n"
        "[ar:Clear Sky]\n"
        "[by:someone]\n"
        "[length:03:21]\n"
        "[00:30.056]Don't you think about me enough\n";

    const auto parsed = LrcParser::parse(text);
    REQUIRE(parsed.entries.size() == 1);
    CHECK(parsed.entries[0].timeMs == 30056);
    CHECK(parsed.entries[0].text == "Don't you think about me enough");
    CHECK(parsed.linesSeen == 6);
    CHECK(parsed.linesDropped == 5);
}

TEST_CASE("LRC：带 BOM 与 CRLF 的文件照样能解")
{
    const auto parsed = LrcParser::parse("\xEF\xBB\xBF[00:01.00]甲\r\n[00:02.00]乙\r\n");

    REQUIRE(parsed.entries.size() == 2);
    CHECK(parsed.entries[0].text == "甲");
    CHECK(parsed.entries[1].text == "乙");
}

TEST_CASE("Lyrics::create：乱序输入被排序、负时间被丢掉、空行保留")
{
    std::vector<LyricEntry> entries;
    entries.push_back(LyricEntry{15000, "乙", {}, {}});
    entries.push_back(LyricEntry{-5, "平移过头的行", {}, {}});
    entries.push_back(LyricEntry{5000, "甲", {}, {}});
    entries.push_back(LyricEntry{10000, "", {}, {}});  // 停顿时留的空行

    const Lyrics lyrics = Lyrics::create(std::move(entries));
    REQUIRE(lyrics.size() == 3);
    CHECK(lyrics.entries()[0].timeMs == 5000);
    CHECK(lyrics.entries()[1].timeMs == 10000);
    CHECK(lyrics.entries()[1].text.empty());
    CHECK(lyrics.entries()[2].timeMs == 15000);
    CHECK(lyrics.lastTimeMs() == 15000);
    CHECK_FALSE(lyrics.hasWords());
}

TEST_CASE("LyricTimeline：边界值、重复时间戳、空时间轴")
{
    const std::vector<std::int64_t> times{1000, 2000, 2000, 3000};

    CHECK(LyricTimeline::indexAt(times, 999) == -1);
    CHECK(LyricTimeline::indexAt(times, 1000) == 0);
    CHECK(LyricTimeline::indexAt(times, 1500) == 0);
    // 同一毫秒有两行时落到**后**一行：滚动不会在半路停住。
    CHECK(LyricTimeline::indexAt(times, 2000) == 2);
    CHECK(LyricTimeline::indexAt(times, 2999) == 2);
    CHECK(LyricTimeline::indexAt(times, 3000) == 3);
    CHECK(LyricTimeline::indexAt(times, 999999) == 3);

    const std::vector<std::int64_t> empty;
    CHECK(LyricTimeline::indexAt(empty, 0) == -1);
}

TEST_CASE("parseTimeTag：合法写法与非法写法")
{
    using mamba::lyrics::core::LrcParser;

    REQUIRE(LrcParser::parseTimeTag("00:12.00").has_value());
    CHECK(*LrcParser::parseTimeTag("00:12.00") == 12000);
    CHECK(*LrcParser::parseTimeTag("00:12.5") == 12500);      // 一位小数
    CHECK(*LrcParser::parseTimeTag("00:12.5555") == 12555);   // 超过三位直接截断
    CHECK(*LrcParser::parseTimeTag("00:12") == 12000);        // 没有小数
    CHECK(*LrcParser::parseTimeTag("00:01:05") == 1050);      // 冒号分隔的小数（少见但存在）
    CHECK(*LrcParser::parseTimeTag("123:45.678") == 123 * 60000 + 45 * 1000 + 678);

    CHECK_FALSE(LrcParser::parseTimeTag("abc").has_value());
    CHECK_FALSE(LrcParser::parseTimeTag("").has_value());
    CHECK_FALSE(LrcParser::parseTimeTag("00:123").has_value());        // 秒超两位
    CHECK_FALSE(LrcParser::parseTimeTag("12:34.56.78").has_value());   // 尾巴多余
    CHECK_FALSE(LrcParser::parseTimeTag("12.34").has_value());         // 没有冒号
}

TEST_CASE("LRC 空文本：整篇只有标签时得到零行（由调用方判成纯音乐）")
{
    const auto parsed = LrcParser::parse("[ti:标题]\n[ar:歌手]\n");
    CHECK(parsed.entries.empty());
    CHECK(parsed.linesDropped == 2);

    const Lyrics lyrics = Lyrics::create({});
    CHECK(lyrics.empty());
    CHECK(lyrics.lineIndexAt(0) == -1);
}
