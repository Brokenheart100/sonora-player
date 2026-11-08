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

    // 登录态校验（`/api/nuser/account/get`）。它既是"粘贴的 cookie 能不能用"的
    // 唯一判据，也是启动时恢复会话、以及运行中判断"是不是被踢下线"的同一根探针。
    [[nodiscard]] contracts::HttpRequest makeAccountRequest(const std::string &baseUrl,
                                                            const std::string &cookie,
                                                            std::chrono::milliseconds timeout);

    // 扫码登录：先要 unikey，再拿 unikey 轮询状态。
    // 明文接口里**没有**返回二维码图片的那种（`/api/login/qrcode/create` 是 404），
    // 二维码得我们自己按 unikey 画（见 §16.5）。
    [[nodiscard]] contracts::HttpRequest makeQrKeyRequest(const std::string &baseUrl,
                                                         std::chrono::milliseconds timeout);

    [[nodiscard]] contracts::HttpRequest makeQrPollRequest(const std::string &baseUrl,
                                                           const std::string &unikey,
                                                           std::chrono::milliseconds timeout);

    // 二维码里真正编码的文本：手机网易云扫到的就是这个链接。
    [[nodiscard]] std::string qrPayloadOf(const std::string &unikey);

    // ------------------------------------------------------------------ N1
    //
    // 只读资源的七个构造器。N0（docs/ONLINE.md §19）实测过可用性：
    //   * `playlist/track/all`、`playlist/track/ids` → 404，所以曲目一律
    //     "playlist detail 的 trackIds[] → song/detail 分批"；
    //   * `artist/songs` → 400，歌手曲目只能取 `/api/v1/artist/{id}` 的 hotSongs。
    // 下面刻意**没有** makePlaylistTracksRequest / makeArtistSongsRequest /
    // makeSimilarSongsRequest：那三个端点在 2026-11 已经是死的/不给参数。

    // 歌单/榜单详情。同一个接口：榜单本身就是一张歌单。
    [[nodiscard]] contracts::HttpRequest makePlaylistDetailRequest(const std::string &baseUrl,
                                                                   const std::string &playlistId,
                                                                   std::chrono::milliseconds timeout);

    // 批量歌曲详情（旧版字段 artists/album/duration）。ids 是 JSON 数组形状，
    // 一次最多 kMaxSongDetailsPerRequest 首。
    [[nodiscard]] contracts::HttpRequest makeSongDetailsRequest(const std::string &baseUrl,
                                                                const std::vector<std::string> &songIds,
                                                                std::chrono::milliseconds timeout);

    // 专辑详情：`/api/v1/album/{id}`，曲目在顶层 `songs[]`。
    [[nodiscard]] contracts::HttpRequest makeAlbumRequest(const std::string &baseUrl,
                                                          const std::string &albumId,
                                                          std::chrono::milliseconds timeout);

    // 歌手详情：`/api/v1/artist/{id}`，热门曲目在 `hotSongs[]`（最多 50 首）。
    [[nodiscard]] contracts::HttpRequest makeArtistRequest(const std::string &baseUrl,
                                                           const std::string &artistId,
                                                           std::chrono::milliseconds timeout);

    // 榜单列表：`/api/toplist`，一次给全（实测 65 张），没有分页参数。
    [[nodiscard]] contracts::HttpRequest makeToplistRequest(const std::string &baseUrl,
                                                            std::chrono::milliseconds timeout);

    // 推荐歌单：`/api/personalized/playlist?limit=`（实测 1..6 张，limit 上限 100）。
    [[nodiscard]] contracts::HttpRequest makePersonalizedPlaylistsRequest(const std::string &baseUrl,
                                                                          int limit,
                                                                          std::chrono::milliseconds timeout);

    // 新歌速递：`/api/personalized/newsong?limit=`，结果里每项是 `result[].song`。
    [[nodiscard]] contracts::HttpRequest makeNewSongsRequest(const std::string &baseUrl, int limit,
                                                             std::chrono::milliseconds timeout);

    // 歌词：`/api/song/lyric?id=&lv=-1&tv=-1`（N3 用，N1 先把构造器备好，
    // 免得 N3 又要在服务层拼一次字符串）。
    [[nodiscard]] contracts::HttpRequest makeLyricRequest(const std::string &baseUrl,
                                                          const std::string &songId,
                                                          std::chrono::milliseconds timeout);

    // ------------------------------------------------------------------ N2
    //
    // 登录侧：我的歌单、每日推荐、收藏/取消收藏。
    // 前两个要 cookie（`MUSIC_U`），最后一个还要 `__csrf`——**只从 cookie 读、
    // 只放表单字段**，不落日志、不进 DTO（AGENTS「在线音源」第 2 条）。

    // 我的歌单：`/api/user/playlist?uid=&limit=`。uid 来自登录后的账号信息。
    [[nodiscard]] contracts::HttpRequest makeUserPlaylistsRequest(const std::string &baseUrl,
                                                                  const std::string &userId,
                                                                  const std::string &cookie, int limit,
                                                                  std::chrono::milliseconds timeout);

    // 每日推荐歌曲：`/api/v3/discovery/recommend/songs`（匿名可用，见 N0 实测）。
    [[nodiscard]] contracts::HttpRequest makeDailySongsRequest(const std::string &baseUrl,
                                                               const std::string &cookie,
                                                               std::chrono::milliseconds timeout);

    // 收藏/取消收藏：`/api/playlist/manipulate/tracks`，表单 POST。
    // `add == true` 是加入，`false` 是移除。
    [[nodiscard]] contracts::HttpRequest makeFavoriteToggleRequest(const std::string &baseUrl,
                                                                   const std::string &playlistId,
                                                                   const std::string &songId, bool add,
                                                                   const std::string &csrf,
                                                                   const std::string &cookie,
                                                                   std::chrono::milliseconds timeout);

    inline constexpr int kMaxSearchLimit = 100;
    inline constexpr int kMaxSongIds = 200;
    // `/api/song/detail?ids=` 实测 500 首可行；取 500 与 todolist §N1-5 对齐。
    inline constexpr int kMaxSongDetailsPerRequest = 500;
    inline constexpr int kMaxCollectionLimit = 100;
    // 我的歌单上限：实测账号最多几十张，1000 是"一次拿全"的保险值。
    inline constexpr int kMaxUserPlaylistsLimit = 1000;

    // 私人 FM：一次回一首（`data[]` 里就一条）。要凑一批就多请求几次——
    // 这个端点没有 limit 参数，只能靠调用方自己攒。
    [[nodiscard]] contracts::HttpRequest makeRadioRequest(const std::string &baseUrl,
                                                          const std::string &cookie,
                                                          std::chrono::milliseconds timeout);

} // namespace sonora::online::core::provider
