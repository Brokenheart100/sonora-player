// 网易云响应的解析：JSON 字符串 → 契约 DTO。只此一处。
//
// 三条规矩：
//   * **不用异常**。`json::parse(..., allow_exceptions=false)`，解析失败就是一条
//     `Internal`，不会把整个进程掀掉。
//   * **字段缺失/为 null 一律降级**，不抛也不猜：`name` 为 null 就当空串，
//     `duration`/`dt` 缺就当 0（界面显示 `--:--`），`id` 是数字还是字符串都收。
//   * **不认识的值原样保留**：超长标题不截断（截断是显示层的事），
//     `alias` 之类这次用不上的字段干脆不读。
//
// 放在 core 而不是 adapters：这里零 Qt、零 IO，夹具单测能直接跑进 sonora_tests。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sonora/online/contracts/Types.h"

namespace sonora::online::core::codec
{

    struct DecodeResult
    {
        // 解析成功且业务码是 200。false 时看 `error`。
        bool ok{false};
        contracts::OnlineError error;
        std::vector<contracts::OnlineTrackDto> rows;
        std::int32_t total{0};
        bool hasMore{false};

        // 专辑/歌手/歌单这类"有头部的曲目页"顺带把头部带回来：
        // 详情页标题不必为了一行名字再发一次请求。
        contracts::CollectionDetailDto detail;

        // 原始响应体。只由 adapters 填（codec 自己不认识 HTTP），
        // 用来把"刚拿到的这一页"原样存进缓存。
        std::string raw;
    };

    // `/api/search/get` 与 `/api/cloudsearch/pc` 两种形状都认：
    // 前者是 `artists/album/duration`，后者是 `ar/al/dt`（R0 两种都抓了夹具）。
    [[nodiscard]] DecodeResult decodeSearch(const std::string &body);

    struct UrlEntry
    {
        std::string songId;
        std::string url;
        std::int32_t bitrate{0};
    };

    struct UrlResult
    {
        bool ok{false};
        contracts::OnlineError error;
        // 只包含真正能播的项：url 非空且 code == 200。
        // code 404（版权下架）与 -110（会员）会被静默跳过 —— 整批里死一两首是常态，
        // 报错会让用户以为全都不能放。
        std::vector<UrlEntry> entries;
    };

    // `/api/song/enhance/player/url/v1` 的响应。
    [[nodiscard]] UrlResult decodeSongUrls(const std::string &body);

    struct AccountInfo
    {
        std::string userId;
        std::string nickname;
        std::int32_t vipType{0};
    };

    struct AccountResult
    {
        bool ok{false};
        contracts::OnlineError error;
        // `loggedIn == false` 且 `ok == true`：cookie 有效但网易云不认这个会话
        // （`account` 为 null）。调用方据此把状态推回 Guest，而不是报一个
        // 看起来像网络故障的错。
        bool loggedIn{false};
        AccountInfo account;
    };

    // `/api/nuser/account/get` 的响应。登录态的唯一权威判定：这条返回什么，
    // 就决定 UI 上那个账号胶囊写"未登录"还是昵称。
    [[nodiscard]] AccountResult decodeAccount(const std::string &body);

    struct QrKeyResult
    {
        bool ok{false};
        contracts::OnlineError error;
        std::string unikey;
    };

    // `/api/login/qrcode/unikey` 的响应。
    [[nodiscard]] QrKeyResult decodeQrKey(const std::string &body);

    struct QrPollResult
    {
        bool ok{false};
        contracts::OnlineError error;
        // 800 过期 / 801 等扫码 / 802 已扫待确认 / 803 成功。
        // 这四个不是 HTTP 错误，是业务进度，所以都不算 `error`。
        int code{0};
        std::string message;
        // 803 时从响应头 `Set-Cookie` 里拼出来的 "MUSIC_U=…; __csrf=…"，
        // 其余状态为空。
        std::string cookie;
    };

    // `/api/login/qrcode/client/login` 的响应。成功时的 cookie 只在响应头里，
    // 所以要连 headers 一起传进来（键统一小写）。
    [[nodiscard]] QrPollResult decodeQrPoll(const std::string &body,
                                           const std::vector<std::pair<std::string, std::string>> &headers);

    // ------------------------------------------------------------------ N1
    //
    // 只读资源。四个"曲目页"共用 `DecodeResult`，另外两个"卡片列表"用下面的
    // `CollectionListResult`：它们没有行状态机，只要一个数组。
    //
    // 统一规矩（N0 的教训，见 docs/ONLINE.md §19.3）：
    //   * **只认顶层 `code`**——嵌套对象里也有 `code`，抓第一个会把专辑误判成 0；
    //   * `code` 0 与 200 都算成功（v1 系列历史上两种都出现过）；
    //   * 字段缺失就跳过该行，不抛异常；
    //   * 旧版 `artists/album/duration` 与新版 `ar/al/dt` 都认（`toTrack` 已处理）。

    struct CollectionListResult
    {
        bool ok{false};
        contracts::OnlineError error;
        std::vector<contracts::CollectionDto> items;
        bool hasMore{false};
        std::string raw;
    };

    struct PlaylistDetailResult
    {
        bool ok{false};
        contracts::OnlineError error;
        contracts::CollectionDetailDto detail;
        // `playlist.trackIds[].id`，顺序即歌单顺序。**不信 `tracks`**：
        // N0 实测 trackCount=100 时 `tracks` 只有前 10 首。
        std::vector<std::string> trackIds;
        std::string raw;
    };

    // `/api/v6/playlist/detail`：头部 + trackIds。`kind` 由调用方给（榜单也是它）。
    [[nodiscard]] PlaylistDetailResult decodePlaylistDetail(const std::string &body,
                                                            contracts::SourceKind kind);

    // `/api/song/detail?ids=[…]`（旧版字段）。rows 顺序按响应顺序，调用方自己按
    // trackIds 重排——上游偶尔不保证顺序。
    [[nodiscard]] DecodeResult decodeSongDetails(const std::string &body);

    // `/api/v1/album/{id}`：`songs[]` 是曲目，`album{}` 是头部。
    [[nodiscard]] DecodeResult decodeAlbum(const std::string &body);

    // `/api/v1/artist/{id}`：`hotSongs[]` 是热门曲目，`artist{}` 是头部。
    [[nodiscard]] DecodeResult decodeArtist(const std::string &body);

    // `/api/personalized/newsong?limit=`：每项是 `result[].song`（内层才是曲目）。
    [[nodiscard]] DecodeResult decodeNewSongs(const std::string &body);

    // `/api/toplist`：`list[]`，每张榜就是一张歌单（点进去再走 decodePlaylistDetail）。
    [[nodiscard]] CollectionListResult decodeToplist(const std::string &body);

    // `/api/personalized/playlist?limit=`：`result[]`。
    [[nodiscard]] CollectionListResult decodePersonalizedPlaylists(const std::string &body);

} // namespace sonora::online::core::codec
