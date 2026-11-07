// Cookie 头的解析与重建。
//
// 用户从浏览器里复制出来的东西五花八门：可能是完整的 `MUSIC_U=...; __csrf=...`，
// 也可能带 `Cookie:` 前缀、带换行、只有 MUSIC_U。这一层只做一件事——把里面
// 我们真正需要的两个键抠出来，再拼成一个干净的请求头。
//
// 为什么放在 core（零 Qt、零网络）：它是纯字符串变换，测试里不需要任何依赖，
// 也就能覆盖"带前缀""有多余 cookie""只有 __csrf""空串"这些真实形态。
#pragma once

#include <string>
#include <string_view>

namespace sonora::online::core::auth
{

    struct CookieParts
    {
        std::string musicU;
        std::string csrf;

        // 网易云的身份只认 MUSIC_U；__csrf 只用于写操作。
        [[nodiscard]] bool usable() const noexcept { return !musicU.empty(); }
    };

    // 接受三种输入：`MUSIC_U=..; __csrf=..`、`Cookie: MUSIC_U=..`、以及裸的
    // `MUSIC_U=..`。键名大小写不敏感（浏览器里见过 `music_u`）。
    [[nodiscard]] CookieParts parseCookieHeader(std::string_view header);

    // 只输出两个键，顺序固定，方便日志与断言。
    [[nodiscard]] std::string buildCookieHeader(const CookieParts &parts);

} // namespace sonora::online::core::auth
