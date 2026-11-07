// 扫码登录的纯函数测试：二维码点阵 + unikey/轮询响应解析。
//
// 二维码本身没人会去肉眼比对，但"点阵是 N×N、四周留白、同一串永远一样"这三条
// 一旦破了，界面上只会看到一块黑白噪声，扫码扫不出来也不报错——所以钉在这里。
#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include "sonora/online/core/auth/QrCode.h"
#include "sonora/online/core/codec/NeteaseJson.h"

using namespace sonora::online::core;

TEST_CASE("encodeQrRows：方形点阵 + 四周一格白边")
{
    const auto rows = auth::encodeQrRows("https://music.163.com/login?codekey=abc");

    REQUIRE(!rows.empty());
    const auto side = rows.size();
    for (const auto &row : rows)
    {
        CHECK(row.size() == side);
    }

    // 白边：第一行/第一列/最后一行/最后一列全是 '0'。
    CHECK(rows.front().find('1') == std::string::npos);
    CHECK(rows.back().find('1') == std::string::npos);
    for (const auto &row : rows)
    {
        CHECK(row.front() == '0');
        CHECK(row.back() == '0');
    }

    // 中间一定有点：全 '0' 说明编码器没跑起来。
    bool anyDark = false;
    for (std::size_t i = 1; i + 1 < side && !anyDark; ++i)
    {
        anyDark = rows[i].find('1', 1) != std::string::npos;
    }
    CHECK(anyDark);
}

TEST_CASE("encodeQrRows：同样的输入永远给同样的点阵")
{
    const std::string payload = "https://music.163.com/login?codekey=stable";
    CHECK(auth::encodeQrRows(payload) == auth::encodeQrRows(payload));

    // 换一个 key 必须换一张码（否则用户点了"刷新二维码"扫到的还是旧码）。
    CHECK(auth::encodeQrRows(payload) != auth::encodeQrRows(payload + "x"));
}

TEST_CASE("encodeQrRows：超长输入要么给码，要么给空，不许抛")
{
    const std::string huge(4000, 'a');
    const auto rows = auth::encodeQrRows(huge);
    CHECK((rows.empty() || (!rows.empty() && rows.front().size() == rows.size())));
}

TEST_CASE("decodeQrKey：正常拿 unikey")
{
    const auto result = codec::decodeQrKey(R"({"code":200,"unikey":"252c43ab-1111"})");
    CHECK(result.ok);
    CHECK(result.unikey == "252c43ab-1111");
}

TEST_CASE("decodeQrKey：缺 unikey 视为失败")
{
    CHECK_FALSE(codec::decodeQrKey(R"({"code":200})").ok);
    CHECK_FALSE(codec::decodeQrKey("not json").ok);
}

TEST_CASE("decodeQrPoll：801 是进度不是错误")
{
    const auto result = codec::decodeQrPoll(R"({"code":801,"message":"等待扫码"})", {});
    CHECK(result.ok);
    CHECK(result.code == 801);
    CHECK(result.cookie.empty());
}

TEST_CASE("decodeQrPoll：803 从 Set-Cookie 抽出 MUSIC_U 与 __csrf")
{
    const std::vector<std::pair<std::string, std::string>> headers = {
        {"set-cookie", "MUSIC_U=abc123; Path=/; HttpOnly"},
        {"set-cookie", "__csrf=deadbeef; Path=/"},
        {"content-type", "application/json"},
    };

    const auto result = codec::decodeQrPoll(R"({"code":803,"message":"授权成功"})", headers);
    CHECK(result.ok);
    CHECK(result.code == 803);
    CHECK(result.cookie == "MUSIC_U=abc123; __csrf=deadbeef");
}

TEST_CASE("decodeQrPoll：803 但没有 MUSIC_U 视为失败")
{
    const std::vector<std::pair<std::string, std::string>> headers = {{"set-cookie", "__csrf=only; Path=/"}};
    CHECK_FALSE(codec::decodeQrPoll(R"({"code":803})", headers).ok);
}
