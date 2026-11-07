// 登录相关的纯函数测试：cookie 解析 + 账号响应解析。
//
// 这两块都不碰网络、不碰 Windows API，所以能直接进 sonora_tests —— 而
// "cookie 里到底抠什么"这类规则正是最容易被改坏、又最难在界面上发现的。
#include <doctest/doctest.h>

#include <string>

#include "sonora/online/core/auth/CookieHeader.h"
#include "sonora/online/core/codec/NeteaseJson.h"

using namespace sonora::online::core;

namespace
{
    bool same(const std::string &left, const std::string &right)
    {
        return left == right;
    }
} // namespace

TEST_CASE("cookie：浏览器里整段复制也能识别")
{
    const auto parts = auth::parseCookieHeader(
        "Cookie: NMTID=xyz; MUSIC_U=0010abcDEF; __csrf=c5d3f1; os=pc; appver=2.0.3");
    CHECK(parts.usable());
    CHECK(same(parts.musicU, "0010abcDEF"));
    CHECK(same(parts.csrf, "c5d3f1"));
}

TEST_CASE("cookie：只有 MUSIC_U 也算可用（__csrf 只用于写操作）")
{
    const auto parts = auth::parseCookieHeader("MUSIC_U=abc123");
    CHECK(parts.usable());
    CHECK(parts.csrf.empty());
    CHECK(same(auth::buildCookieHeader(parts), "MUSIC_U=abc123"));
}

TEST_CASE("cookie：键名大小写不敏感")
{
    const auto parts = auth::parseCookieHeader("music_u=abc; __CSRF=zz");
    CHECK(parts.usable());
    CHECK(same(parts.musicU, "abc"));
    CHECK(same(parts.csrf, "zz"));
}

TEST_CASE("cookie：没有 MUSIC_U 时不可用（别把半段 cookie 拿去登录）")
{
    CHECK_FALSE(auth::parseCookieHeader("__csrf=only").usable());
    CHECK_FALSE(auth::parseCookieHeader("").usable());
    CHECK_FALSE(auth::parseCookieHeader("MUSIC_U=").usable());
}

TEST_CASE("cookie：重建只留两个键，且顺序固定")
{
    auth::CookieParts parts;
    parts.musicU = "u";
    parts.csrf = "c";
    CHECK(same(auth::buildCookieHeader(parts), "MUSIC_U=u; __csrf=c"));
}

TEST_CASE("账号：登录态的响应里能读出昵称")
{
    const auto result = codec::decodeAccount(R"({
        "code": 200,
        "account": {"id": 3295327513, "userName": "1a2b3c"},
        "profile": {"nickname": "七里香", "vipType": 11}
    })");
    CHECK(result.ok);
    CHECK(result.loggedIn);
    CHECK(same(result.account.nickname, "七里香"));
    CHECK(same(result.account.userId, "3295327513"));
    CHECK(result.account.vipType == 11);
}

TEST_CASE("账号：account 为 null 是「没登录」，不是错误")
{
    const auto result = codec::decodeAccount(R"({"code": 200, "account": null, "profile": null})");
    CHECK(result.ok);
    CHECK_FALSE(result.loggedIn);
    CHECK(result.error.code == sonora::kernel::ErrorCode::None);
}

TEST_CASE("账号：301 视为需要登录（cookie 失效）")
{
    const auto result = codec::decodeAccount(R"({"code": 301, "msg": "需要登录"})");
    CHECK_FALSE(result.ok);
    CHECK(result.error.code == sonora::kernel::ErrorCode::Unauthenticated);
    CHECK_FALSE(result.error.retryable);
}

TEST_CASE("账号：profile 缺昵称时退回 userName")
{
    const auto result = codec::decodeAccount(
        R"({"code": 200, "account": {"id": 7, "userName": "fallback"}, "profile": {}})");
    CHECK(result.loggedIn);
    CHECK(same(result.account.nickname, "fallback"));
}

TEST_CASE("账号：非 JSON 报 Upstream 且可重试")
{
    const auto result = codec::decodeAccount("<html>502 Bad Gateway</html>");
    CHECK_FALSE(result.ok);
    CHECK(result.error.code == sonora::kernel::ErrorCode::Upstream);
    CHECK(result.error.retryable);
}
