// 网易云的 URL / 参数 / 请求头构造，只此一处。
//
// 为什么要把拼 URL 单独抽出来：这是**唯一**知道"某家服务把关键词放在哪个
// query 键上"的地方。R0 用 curl 把六个端点探通以后，这些字符串就该聚成一坨，
// 而不是散落成六份字符串加法——接口一改，改一个文件就能全测。
//
// 本文件零 Qt、零 JSON：它只产出字符串，所以夹具单测能跑进 sonora_tests。
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sonora/online/contracts/Transport.h"

namespace sonora::online::core::provider
{

    // 网易云 API 的基地址。线上是 https；测试里可以传一个假地址。
    inline constexpr const char *kNeteaseBaseUrl = "https://music.163.com";

    struct SearchParams
    {
        std::string keywords;
        int offset{0};
        int limit{30};
    };

    // 百分比编码（RFC 3986 的 unreserved 之外一律 %XX）。中文关键词必须走它：
    // 直接把 UTF-8 字节塞进 query 也能通，但一旦出现 `&`、`=`、`#` 就断。
    [[nodiscard]] std::string encodeUrlComponent(const std::string &text);

    // 搜索请求。limit 上限 100 是 R0 实测出来的（101 直接 code=400），
    // 这里夹住而不是让调用方去记。
    [[nodiscard]] contracts::HttpRequest makeSearchRequest(const std::string &baseUrl,
                                                          const SearchParams &params,
                                                          std::chrono::milliseconds timeout);

    // 请求头白名单。只有这三项：多出来的头既没用，又会把 cookie 顺手带出去。
    // 空 cookie 时不会产出 Cookie 头。
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> defaultHeaders(const std::string &cookie);

    // 取播放地址。一次最多 kMaxSongIds 首（R0 实测 200 首可行，这里留余量），
    // 多出来的由调用方分批串行发。
    // level 匿名最高到 exhigh（256 kbps）：请求 lossless 会被降级回 exhigh，
    // 所以默认就写 exhigh，省得以为是权限问题。
    [[nodiscard]] contracts::HttpRequest makeSongUrlRequest(const std::string &baseUrl,
                                                            const std::vector<std::string> &songIds,
                                                            const std::string &level,
                                                            std::chrono::milliseconds timeout);

    inline constexpr int kMaxSearchLimit = 100;
    inline constexpr int kMaxSongIds = 200;

} // namespace sonora::online::core::provider
