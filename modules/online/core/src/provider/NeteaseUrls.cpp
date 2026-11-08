#include "sonora/online/core/provider/NeteaseUrls.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace sonora::online::core::provider
{
    namespace
    {
        // 0-9 A-Z a-z 以及 -._~ 不编码，其余一律 %XX。
        bool isUnreserved(unsigned char ch)
        {
            return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                   ch == '-' || ch == '.' || ch == '_' || ch == '~';
        }
    } // namespace

    std::string encodeUrlComponent(const std::string &text)
    {
        std::string out;
        out.reserve(text.size() + text.size() / 2);
        for (const char raw : text)
        {
            const auto ch = static_cast<unsigned char>(raw);
            if (isUnreserved(ch))
            {
                out.push_back(static_cast<char>(ch));
                continue;
            }
            static constexpr char hex[] = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0x0F]);
        }
        return out;
    }

    contracts::HttpRequest makeSearchRequest(const std::string &baseUrl, const SearchParams &params,
                                             std::chrono::milliseconds timeout)
    {
        const int limit = std::clamp(params.limit, 1, kMaxSearchLimit);
        const int offset = std::max(params.offset, 0);

        contracts::HttpRequest request;
        request.method = contracts::HttpMethod::Get;
        request.transferTimeout = timeout;
        // cloudsearch 而不是 /api/search/get：只有它带封面（al.picUrl）与 privilege，
        // 老接口的 album 里只有 picId，算不出可用的封面 URL（R0 实测两种形状）。
        request.url = baseUrl + "/api/cloudsearch/pc?s=" + encodeUrlComponent(params.keywords) +
                      "&type=1&limit=" + std::to_string(limit) + "&offset=" + std::to_string(offset);
        return request;
    }

    contracts::HttpRequest makeSongUrlRequest(const std::string &baseUrl,
                                              const std::vector<std::string> &songIds,
                                              const std::string &level,
                                              std::chrono::milliseconds timeout)
    {
        // ids 是 JSON 数组形状的参数：ids=[186016,108914]。用它而不是 ids=186016
        // 是因为后者只能取一首，200 首要发 200 个请求。
        std::string ids = "[";
        const std::size_t limit = std::min<std::size_t>(songIds.size(), static_cast<std::size_t>(kMaxSongIds));
        for (std::size_t index = 0; index < limit; ++index)
        {
            if (index != 0)
            {
                ids.push_back(',');
            }
            ids += songIds[index];
        }
        ids.push_back(']');

        contracts::HttpRequest request;
        request.method = contracts::HttpMethod::Get;
        request.transferTimeout = timeout;
        request.url = baseUrl + "/api/song/enhance/player/url/v1?ids=" + ids + "&level=" + level +
                      "&encodeType=aac";
        return request;
    }

    std::vector<std::pair<std::string, std::string>> defaultHeaders(const std::string &cookie)
    {
        std::vector<std::pair<std::string, std::string>> headers;
        headers.emplace_back("Referer", "https://music.163.com/");
        // 不带 UA 时偶尔会吃到 -460（R0 记录过这个现象），所以固定一个桌面浏览器的。
        headers.emplace_back("User-Agent",
                             "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                             "(KHTML, like Gecko) Chrome/126.0 Safari/537.36");
        if (!cookie.empty())
        {
            headers.emplace_back("Cookie", cookie);
        }
        return headers;
    }

    contracts::HttpRequest makeAccountRequest(const std::string &baseUrl, const std::string &cookie,
                                              std::chrono::milliseconds timeout)
    {
        contracts::HttpRequest request;
        request.method = contracts::HttpMethod::Get;
        request.url = baseUrl + "/api/nuser/account/get";
        request.headers = defaultHeaders(cookie);
        request.transferTimeout = timeout;
        return request;
    }

    contracts::HttpRequest makeQrKeyRequest(const std::string &baseUrl, std::chrono::milliseconds timeout)
    {
        contracts::HttpRequest request;
        request.method = contracts::HttpMethod::Post;
        request.url = baseUrl + "/api/login/qrcode/unikey?type=1";
        request.headers = defaultHeaders({});
        request.transferTimeout = timeout;
        return request;
    }

    contracts::HttpRequest makeQrPollRequest(const std::string &baseUrl, const std::string &unikey,
                                             std::chrono::milliseconds timeout)
    {
        contracts::HttpRequest request;
        request.method = contracts::HttpMethod::Post;
        request.url = baseUrl + "/api/login/qrcode/client/login?key=" + encodeUrlComponent(unikey) + "&type=1";
        request.headers = defaultHeaders({});
        request.transferTimeout = timeout;
        return request;
    }

    std::string qrPayloadOf(const std::string &unikey)
    {
        return "https://music.163.com/login?codekey=" + unikey;
    }

    // ------------------------------------------------------------------ N1

    namespace
    {
        contracts::HttpRequest getRequest(const std::string &url, const std::string &cookie,
                                          std::chrono::milliseconds timeout)
        {
            contracts::HttpRequest request;
            request.method = contracts::HttpMethod::Get;
            request.url = url;
            request.headers = defaultHeaders(cookie);
            request.transferTimeout = timeout;
            return request;
        }
    } // namespace

    contracts::HttpRequest makePlaylistDetailRequest(const std::string &baseUrl,
                                                     const std::string &playlistId,
                                                     std::chrono::milliseconds timeout)
    {
        // v6 而不是 v3：v3 会额外塞 urls/privileges，那两坨我们不用，白白多解析几 MB。
        return getRequest(baseUrl + "/api/v6/playlist/detail?id=" + encodeUrlComponent(playlistId), {},
                          timeout);
    }

    contracts::HttpRequest makeSongDetailsRequest(const std::string &baseUrl,
                                                  const std::vector<std::string> &songIds,
                                                  std::chrono::milliseconds timeout)
    {
        std::string ids = "[";
        const std::size_t limit =
            std::min<std::size_t>(songIds.size(), static_cast<std::size_t>(kMaxSongDetailsPerRequest));
        for (std::size_t index = 0; index < limit; ++index)
        {
            if (index != 0)
            {
                ids.push_back(',');
            }
            ids += songIds[index];
        }
        ids.push_back(']');
        // `[` `]` 是 gen-delims，原样塞进查询串会被服务端当成坏参数
        // （实测 `/api/song/detail?ids=[1,2]` 回 code 400，编码成 %5B…%5D 才回 200）。
        return getRequest(baseUrl + "/api/song/detail?ids=" + encodeUrlComponent(ids), {}, timeout);
    }

    contracts::HttpRequest makeAlbumRequest(const std::string &baseUrl, const std::string &albumId,
                                            std::chrono::milliseconds timeout)
    {
        return getRequest(baseUrl + "/api/v1/album/" + encodeUrlComponent(albumId), {}, timeout);
    }

    contracts::HttpRequest makeArtistRequest(const std::string &baseUrl, const std::string &artistId,
                                             std::chrono::milliseconds timeout)
    {
        return getRequest(baseUrl + "/api/v1/artist/" + encodeUrlComponent(artistId), {}, timeout);
    }

    contracts::HttpRequest makeToplistRequest(const std::string &baseUrl, std::chrono::milliseconds timeout)
    {
        return getRequest(baseUrl + "/api/toplist", {}, timeout);
    }

    contracts::HttpRequest makePersonalizedPlaylistsRequest(const std::string &baseUrl, int limit,
                                                            std::chrono::milliseconds timeout)
    {
        const int clamped = std::clamp(limit, 1, kMaxCollectionLimit);
        return getRequest(baseUrl + "/api/personalized/playlist?limit=" + std::to_string(clamped), {},
                          timeout);
    }

    contracts::HttpRequest makeNewSongsRequest(const std::string &baseUrl, int limit,
                                               std::chrono::milliseconds timeout)
    {
        const int clamped = std::clamp(limit, 1, kMaxCollectionLimit);
        return getRequest(baseUrl + "/api/personalized/newsong?limit=" + std::to_string(clamped), {},
                          timeout);
    }

    contracts::HttpRequest makeLyricRequest(const std::string &baseUrl, const std::string &songId,
                                            std::chrono::milliseconds timeout)
    {
        // lv=-1 / tv=-1 是"原歌词 + 翻译"的全量形态（R0 实测 lrc.lyric 1471 字符）。
        return getRequest(baseUrl + "/api/song/lyric?id=" + encodeUrlComponent(songId) + "&lv=-1&tv=-1",
                          {}, timeout);
    }

    // ------------------------------------------------------------------ N2
    //
    // 登录侧。三个端点都要求 cookie，参数与路径来自 docs/ONLINE.md §19/§20 的实测。

    contracts::HttpRequest makeUserPlaylistsRequest(const std::string &baseUrl, const std::string &userId,
                                                    const std::string &cookie, int limit,
                                                    std::chrono::milliseconds timeout)
    {
        // uid 是数字，但仍然编码：万一有人把空串传进来，宁可服务端回 301 也不要拼出
        // `?uid=&limit=` 这种看起来正常的 URL。
        const int clamped = std::clamp(limit, 1, kMaxUserPlaylistsLimit);
        return getRequest(baseUrl + "/api/user/playlist?uid=" + encodeUrlComponent(userId) +
                              "&limit=" + std::to_string(clamped),
                          cookie, timeout);
    }

    contracts::HttpRequest makeDailySongsRequest(const std::string &baseUrl, const std::string &cookie,
                                                 std::chrono::milliseconds timeout)
    {
        // 每日推荐歌曲。N0 实测**匿名也回 31 首**（与旧文档"需登录"不符），
        // 所以这里不判断登录态——匿名能拿到就当匿名可用，拿不到再降级。
        return getRequest(baseUrl + "/api/v3/discovery/recommend/songs", cookie, timeout);
    }

    contracts::HttpRequest makeFavoriteToggleRequest(const std::string &baseUrl,
                                                     const std::string &playlistId,
                                                     const std::string &songId, bool add,
                                                     const std::string &csrf,
                                                     const std::string &cookie,
                                                     std::chrono::milliseconds timeout)
    {
        // 写操作：表单 POST。`trackIds` 是 JSON 数组字面量，**放在 body 里**，
        // 不需要 URL 编码（编码会把 `[` 变成 %5B，服务端照样认，但 body 里没必要）。
        // `__csrf` 从 cookie 里取出来只放表单字段，不进日志、不进任何 DTO。
        std::string body = "op=";
        body += add ? "add" : "del";
        body += "&pid=";
        body += encodeUrlComponent(playlistId);
        body += "&trackIds=[";
        body += songId;
        body += "]";
        if (!csrf.empty())
        {
            body += "&csrf_token=";
            body += encodeUrlComponent(csrf);
        }

        contracts::HttpRequest request;
        request.method = contracts::HttpMethod::Post;
        request.url = baseUrl + "/api/playlist/manipulate/tracks";
        request.headers = defaultHeaders(cookie);
        request.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
        request.body = std::move(body);
        request.transferTimeout = timeout;
        return request;
    }


    contracts::HttpRequest makeRadioRequest(const std::string &baseUrl, const std::string &cookie,
                                            std::chrono::milliseconds timeout)
    {
        // 私人 FM：一次只回一首，没有 limit 参数。凑一批就多请求几次（见 OnlineService）。
        contracts::HttpRequest request;
        request.method = contracts::HttpMethod::Get;
        request.url = baseUrl + "/api/v1/radio/get";
        request.headers = defaultHeaders(cookie);
        request.transferTimeout = timeout;
        return request;
    }

} // namespace sonora::online::core::provider
