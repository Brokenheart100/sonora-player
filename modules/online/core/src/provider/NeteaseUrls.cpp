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

} // namespace sonora::online::core::provider
