#include "sonora/online/core/codec/NeteaseJson.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sonora::online::core::codec
{
    namespace
    {
        namespace c = online::contracts;
        using json = nlohmann::json;

        // 数字或字符串都收：网易云有几处 id 是字符串（超过 2^53 的音频 id），
        // 同一个字段在不同接口上类型还会变，所以统一按"文本"取。
        std::string asText(const json &value)
        {
            if (value.is_string())
            {
                return value.get<std::string>();
            }
            if (value.is_number_integer())
            {
                return std::to_string(value.get<std::int64_t>());
            }
            if (value.is_number_unsigned())
            {
                return std::to_string(value.get<std::uint64_t>());
            }
            if (value.is_number_float())
            {
                // 只对整数形态的浮点做转换；真正带小数的值不该出现在 id/时长上。
                return std::to_string(static_cast<std::int64_t>(value.get<double>()));
            }
            return {};
        }

        std::int64_t asInt(const json &value)
        {
            if (value.is_number_integer())
            {
                return value.get<std::int64_t>();
            }
            if (value.is_number_unsigned())
            {
                return static_cast<std::int64_t>(value.get<std::uint64_t>());
            }
            if (value.is_string())
            {
                // "269000" 这种也要认：有接口把时长序列化成字符串。
                try
                {
                    return std::stoll(value.get<std::string>());
                }
                catch (...)
                {
                    return 0;
                }
            }
            return 0;
        }

        // 安全取子对象：字段不存在、是 null、类型不对都返回空对象。
        const json &member(const json &object, const char *key)
        {
            static const json kEmpty = json::object();
            if (!object.is_object())
            {
                return kEmpty;
            }
            const auto it = object.find(key);
            if (it == object.end() || it->is_null())
            {
                return kEmpty;
            }
            return *it;
        }

        std::string artistNames(const json &song)
        {
            const json *list = nullptr;
            if (song.contains("artists") && song.at("artists").is_array())
            {
                list = &song.at("artists");
            }
            else if (song.contains("ar") && song.at("ar").is_array())
            {
                list = &song.at("ar");
            }
            if (list == nullptr || list->empty())
            {
                return {};
            }
            std::string out;
            for (const auto &artist : *list)
            {
                const std::string name = asText(member(artist, "name"));
                if (name.empty())
                {
                    continue;
                }
                if (!out.empty())
                {
                    out += " / ";
                }
                out += name;
            }
            return out;
        }

        c::OnlineError mapApiCode(int code)
        {
            c::OnlineError error;
            switch (code)
            {
            case 301:
                error.code = kernel::ErrorCode::Unauthenticated;
                error.message = "需要登录";
                error.retryable = false;
                break;
            case 400:
                error.code = kernel::ErrorCode::InvalidArgument;
                error.message = "参数不对（关键词为空，或 limit 超过 100）";
                error.retryable = false;
                break;
            case -460:
                // R0 探到的风控码。换网络或等一会儿就好，所以算可重试。
                error.code = kernel::ErrorCode::RateLimited;
                error.message = "被网易云风控拦截（-460），稍后再试";
                error.retryable = true;
                error.retryAfterMs = 3000;
                break;
            default:
                error.code = kernel::ErrorCode::Upstream;
                error.message = "网易云返回 code=" + std::to_string(code);
                error.retryable = false;
                break;
            }
            return error;
        }

        c::OnlineTrackDto toTrack(const json &song)
        {
            c::OnlineTrackDto track;
            track.songId = asText(member(song, "id"));
            track.title = asText(member(song, "name"));

            const json &album = song.contains("album") ? member(song, "album") : member(song, "al");
            track.album = asText(member(album, "name"));
            track.coverUrl = asText(member(album, "picUrl"));

            track.artists = artistNames(song);

            std::int64_t durationMs = asInt(member(song, "duration"));
            if (durationMs == 0)
            {
                durationMs = asInt(member(song, "dt"));
            }
            track.durationMs = durationMs;

            // R2 只有搜索结果，拿不到播放地址，这里只是"看起来能不能播"：
            // 0 = 免费、8 = 低音质免费；其余（1 VIP / 4 需购买）先标成不可播。
            // 真能不能播由 R3 取地址接口的 code 决定（200 才有 url）。
            const std::int64_t fee = asInt(member(song, "fee"));
            track.playable = (fee == 0 || fee == 8);
            return track;
        }

    } // namespace

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

} // namespace sonora::online::core::codec
