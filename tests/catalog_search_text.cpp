// SearchText：入库体与查询表达式的形状。
//
// 这两个函数必须配套：入库把汉字拆成单字 token，查询把汉字写成短语，
// 两者形状不一致就搜不到（而且是"静默搜不到"，不会报错）。
// 所以这里断言的是**字面形状**，不是"能搜到"——形状错了，
// 集成测试里那条"中文子串能搜到"才会跟着挂。
#include "doctest_support.h"

#include "sonora/catalog/adapters/SearchText.h"

using sonora::catalog::adapters::isIdeograph;
using sonora::catalog::adapters::matchQuery;
using sonora::catalog::adapters::searchBody;

TEST_CASE("汉字被逐字拆开，ASCII 转小写")
{
    // 三段拼接、每字带空格——这是 FTS5 能按短语匹配的前提。
    CHECK(searchBody("夜航西飞", "某某", "Live") == "夜 航 西 飞 某 某 live");
    CHECK(searchBody("Hotel California", "Eagles", "") == "hotel california eagles");
    CHECK(searchBody("", "", "") == "");
    CHECK(searchBody("", "周杰伦", "") == "周 杰 伦");
}

TEST_CASE("假名与谚文同样逐字拆开")
{
    CHECK(searchBody("夜に駆ける", "", "") == "夜 に 駆 け る");
    CHECK(searchBody("봄날", "", "") == "봄 날");
    CHECK(isIdeograph(U'夜'));
    CHECK(isIdeograph(U'に'));
    CHECK(isIdeograph(U'봄'));
    CHECK_FALSE(isIdeograph(U'a'));
    CHECK_FALSE(isIdeograph(U'1'));
    CHECK_FALSE(isIdeograph(U'-'));
}

TEST_CASE("汉字查询变成短语")
{
    CHECK(matchQuery("夜航") == "\"夜 航\"");
    CHECK(matchQuery("夜航西飞") == "\"夜 航 西 飞\"");
    // 单个汉字也成立：比原计划的"3 个字以上"更宽松，这是逐字索引的附带好处。
    CHECK(matchQuery("夜") == "\"夜\"");
}

TEST_CASE("ASCII 查询变成带引号的前缀词")
{
    CHECK(matchQuery("abc") == "\"abc\"*");
    CHECK(matchQuery("ABC") == "\"abc\"*");
    // 引号是分隔符（跟 FTS5 的 tokenizer 一致）：两段 AND，不会变成语法错误。
    CHECK(matchQuery("a\"b") == "\"a\"* \"b\"*");
    // % 不是通配符，也不是语法；它只是把一段切开。
    CHECK(matchQuery("100%") == "\"100\"*");
}

TEST_CASE("纯标点输入等于没输入")
{
    // FTS5 的 tokenizer 把标点全当分隔符，`"%"*` 一个 token 都切不出来，
    // 直接喂给 MATCH 是语法错误。这类输入统一退化成空串（调用方不筛）。
    CHECK(matchQuery("%") == "");
    CHECK(matchQuery(" - ") == "");
    CHECK(matchQuery("...") == "");
}

TEST_CASE("中英混排切成两段，空格等于 AND")
{
    CHECK(matchQuery("夜航 abc") == "\"夜 航\" \"abc\"*");
    CHECK(matchQuery("夜航abc") == "\"夜 航\" \"abc\"*");
    CHECK(matchQuery("abc夜航") == "\"abc\"* \"夜 航\"");
}

TEST_CASE("空白输入返回空串（调用方据此不筛）")
{
    CHECK(matchQuery("") == "");
    CHECK(matchQuery("   ") == "");
    CHECK(matchQuery("\t\u3000 ") == "");
}

TEST_CASE("多段查询用空格连接")
{
    CHECK(matchQuery("周杰伦 稻香") == "\"周 杰 伦\" \"稻 香\"");
}

TEST_CASE("坏字节不会让转换崩掉")
{
    // 一个孤立的 UTF-8 续字节：应被当作坏字符吃掉，后续内容仍然处理。
    const std::string broken = "夜\xA0\xA0航";
    const std::string body = searchBody(broken, "", "");
    CHECK(body.find("夜") != std::string::npos);
    CHECK(body.find("航") != std::string::npos);
    CHECK_FALSE(matchQuery(broken).empty());
}
