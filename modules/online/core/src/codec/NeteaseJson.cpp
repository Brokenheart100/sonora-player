#include "sonora/online/core/codec/NeteaseJson.h"

#include "NeteaseJsonDetail.h"

#include "sonora/online/core/auth/CookieHeader.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sonora::online::core::codec
{
    using namespace detail;

    // —— 搜索、播放地址、登录校验、扫码（R0–R4）——
    DecodeResult decodeSearch(const std::string &body)
    {
        DecodeResult out;

        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error.code = kernel::ErrorCode::Internal;
            out.error.message = "响应不是合法 JSON";
            return out;
        }

        const std::int64_t code = asInt(member(root, "code"));
        if (code != 0 && code != 200)
        {
            out.error = mapApiCode(static_cast<int>(code));
            return out;
        }

        const json &result = member(root, "result");
        if (!result.is_object())
        {
            // result 为 null（网易云在"零结果"时偶尔这么回）：这是空结果，不是错误。
            out.ok = true;
            return out;
        }

        out.total = static_cast<std::int32_t>(asInt(member(result, "songCount")));

        if (result.contains("hasMore") && result.at("hasMore").is_boolean())
        {
            out.hasMore = result.at("hasMore").get<bool>();
        }
        else
        {
            // cloudsearch 不给 hasMore：按"拿满一页就还有下一页"推断，
            // 下一页真的没数据时会被判成空结果，代价只是多一次请求。
            const auto songs = member(result, "songs");
            out.hasMore = songs.is_array() && songs.size() >= 20;
        }

        const json &songs = member(result, "songs");
        if (songs.is_array())
        {
            out.rows.reserve(songs.size());
            for (const auto &song : songs)
            {
                if (!song.is_object())
                {
                    continue;
                }
                auto track = toTrack(song);
                if (track.songId.empty())
                {
                    // 没有 id 的行没法解析播放地址，直接丢；留着只会变成一条点不动的行。
                    continue;
                }
                out.rows.push_back(std::move(track));
            }
        }

        out.ok = true;
        return out;
    }

    UrlResult decodeSongUrls(const std::string &body)
    {
        UrlResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "取播放地址的响应不是 JSON"};
            return out;
        }

        const json &codeNode = member(root, "code");
        if (codeNode.is_number() || codeNode.is_string())
        {
            const auto code = static_cast<int>(asInt(codeNode));
            if (code != 200)
            {
                out.error = mapApiCode(code);
                return out;
            }
        }

        const json &data = member(root, "data");
        if (data.is_array())
        {
            out.entries.reserve(data.size());
            for (const auto &item : data)
            {
                if (!item.is_object())
                {
                    continue;
                }
                // 每首自己有 code：200 才是真能播。404 是版权下架、-110 是会员曲，
                // 整批里死一两首是常态，跳过而不是让整次播放失败。
                const auto itemCode = static_cast<int>(asInt(member(item, "code")));
                if (itemCode != 200)
                {
                    continue;
                }
                const auto url = asText(member(item, "url"));
                if (url.empty())
                {
                    continue;
                }
                UrlEntry entry;
                entry.songId = asText(member(item, "id"));
                entry.url = url;
                entry.bitrate = static_cast<std::int32_t>(asInt(member(item, "br")) / 1000);
                out.entries.push_back(std::move(entry));
            }
        }

        out.ok = true;
        return out;
    }

    AccountResult decodeAccount(const std::string &body)
    {
        AccountResult out;
        const auto root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error.code = kernel::ErrorCode::Upstream;
            out.error.message = "网易云返回的不是 JSON";
            out.error.retryable = true;
            return out;
        }

        const auto code = asInt(member(root, "code"));
        if (code != 200)
        {
            out.error = mapApiCode(code);
            // 登录场景下 301 的含义很具体：cookie 过期或被顶掉。通用文案
            // "需要登录"放在这里，用户会以为是没点登录按钮。
            if (code == 301)
            {
                out.error.message = "这个 cookie 已经失效，请重新登录后复制一次";
            }
            return out;
        }

        // 注意：不能走 member()——它把 null 归一成空对象，于是"account 为 null"
        // 会被误判成"已登录但没有昵称"。这里必须看原始节点。
        const auto accountIt = root.find("account");
        if (accountIt == root.end() || !accountIt->is_object())
        {
            // 没登录：这是"事实"，不是"错误"。
            out.ok = true;
            out.loggedIn = false;
            return out;
        }

        out.ok = true;
        out.loggedIn = true;
        const auto &account = *accountIt;
        out.account.userId = asText(member(account, "id"));
        const auto profile = member(root, "profile");
        if (profile.is_object())
        {
            out.account.nickname = asText(member(profile, "nickname"));
            out.account.vipType = static_cast<std::int32_t>(asInt(member(profile, "vipType")));
        }
        if (out.account.nickname.empty())
        {
            // profile 缺字段时退到手机号/用户名，总比空着好看。
            out.account.nickname = asText(member(account, "userName"));
        }
        return out;
    }

    namespace
    {
        // 803 时 cookie 只在 `Set-Cookie` 里，而且拆成两条
        // （MUSIC_U 与 __csrf），这里合回成一条请求头。
        std::string cookieFromHeaders(const std::vector<std::pair<std::string, std::string>> &headers)
        {
            auth::CookieParts parts;
            for (const auto &header : headers)
            {
                if (header.first != "set-cookie")
                {
                    continue;
                }
                const auto semicolon = header.second.find(';');
                const std::string first = header.second.substr(0, semicolon);
                const auto equal = first.find('=');
                if (equal == std::string::npos)
                {
                    continue;
                }
                const std::string name = first.substr(0, equal);
                const std::string value = first.substr(equal + 1);
                if (name == "MUSIC_U")
                {
                    parts.musicU = value;
                }
                else if (name == "__csrf")
                {
                    parts.csrf = value;
                }
            }
            if (!parts.usable())
            {
                return {};
            }
            return auth::buildCookieHeader(parts);
        }
    } // namespace

    QrKeyResult decodeQrKey(const std::string &body)
    {
        QrKeyResult out;
        const auto root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error.code = kernel::ErrorCode::Upstream;
            out.error.message = "网易云返回的不是 JSON";
            out.error.retryable = true;
            return out;
        }
        const auto code = asInt(member(root, "code"));
        if (code != 200)
        {
            out.error = mapApiCode(code);
            return out;
        }
        out.unikey = asText(member(root, "unikey"));
        if (out.unikey.empty())
        {
            out.error.code = kernel::ErrorCode::Upstream;
            out.error.message = "网易云没有返回扫码 key";
            out.error.retryable = true;
            return out;
        }
        out.ok = true;
        return out;
    }


    QrPollResult decodeQrPoll(const std::string &body,
                              const std::vector<std::pair<std::string, std::string>> &headers)
    {
        QrPollResult out;
        const auto root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error.code = kernel::ErrorCode::Upstream;
            out.error.message = "网易云返回的不是 JSON";
            out.error.retryable = true;
            return out;
        }
        out.code = asInt(member(root, "code"));
        out.message = asText(member(root, "message"));
        // 800/801/802/803 是进度而不是错误：全都算 ok，由调用方按 code 分支。
        if (out.code >= 800)
        {
            out.ok = true;
            if (out.code == 803)
            {
                out.cookie = cookieFromHeaders(headers);
                if (out.cookie.empty())
                {
                    // 授权成功却没给 MUSIC_U：服务端换了字段，我们宁可报错也不要
                    // 假装登录成功（否则界面会显示已登录但所有请求都是匿名的）。
                    out.ok = false;
                    out.error.code = kernel::ErrorCode::Upstream;
                    out.error.message = "扫码成功但没拿到 MUSIC_U，请重试";
                    out.error.retryable = true;
                }
            }
            return out;
        }
        out.error = mapApiCode(out.code);
        return out;
    }


} // namespace sonora::online::core::codec
