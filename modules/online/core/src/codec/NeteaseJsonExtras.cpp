#include "sonora/online/core/codec/NeteaseJson.h"

#include "NeteaseJsonDetail.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sonora::online::core::codec
{
    using namespace detail;

    // —— 我的歌单、每日推荐、私人 FM、歌词、收藏写操作（N2–N4）——
    PlaylistsResult decodeUserPlaylists(const std::string &body)
    {
        PlaylistsResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "响应不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }

        const json &array = member(root, "playlist");
        if (!array.is_array())
        {
            // 301/未登录已经由 topCodeOk 拦住了；走到这里说明形状变了。
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "接口可能已变更，稍后再试", true, 0};
            return out;
        }

        out.items.reserve(array.size());
        for (const auto &item : array)
        {
            if (!item.is_object())
            {
                continue;
            }
            contracts::PlaylistDto dto;
            dto.playlistId = asText(member(item, "id"));
            if (dto.playlistId.empty())
            {
                continue;
            }
            dto.name = asText(member(item, "name"));
            dto.trackCount = static_cast<std::int32_t>(asInt(member(item, "trackCount")));
            dto.coverUrl = asText(member(item, "coverImgUrl"));
            // specialType 只在这里用一次：5 就是"我喜欢的音乐"。
            if (asInt(member(item, "specialType")) == 5)
            {
                dto.name = "我喜欢的音乐";
            }
            out.items.push_back(std::move(dto));
        }
        out.ok = true;
        out.raw = body;
        return out;
    }

    DecodeResult decodeDailySongs(const std::string &body)
    {
        DecodeResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "响应不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }

        const json *dataPtr = findMember(root, "data");
        if (dataPtr == nullptr)
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "接口可能已变更，稍后再试", true, 0};
            return out;
        }
        appendTracks(member(*dataPtr, "dailySongs"), out);
        out.ok = true;
        out.total = static_cast<int>(out.rows.size());
        out.raw = body;
        return out;
    }


    DecodeResult decodeRadio(const std::string &body)
    {
        // 注意 code 是 0：这个端点是老 v1 风格，和其它几个一样只认顶层 code。
        DecodeResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "响应不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }

        const json *dataPtr = findValue(root, "data");
        if (dataPtr == nullptr || !dataPtr->is_array())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "接口可能已变更，稍后再试", true, 0};
            return out;
        }
        appendTracks(*dataPtr, out);
        out.ok = true;
        out.total = static_cast<int>(out.rows.size());
        out.raw = body;
        return out;
    }

    LyricResult decodeLyric(const std::string &body)
    {
        LyricResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "响应不是合法 JSON", false, 0};
            return out;
        }
        // `nolyric`/`uncollected` 是"确实没词"，不是失败：接口就是这么回答的。
        const bool noLyric = root.contains("nolyric") && root["nolyric"].is_boolean() && root["nolyric"].get<bool>();
        const bool uncollected =
            root.contains("uncollected") && root["uncollected"].is_boolean() && root["uncollected"].get<bool>();
        if (!noLyric && !uncollected && !topCodeOk(root, out.error))
        {
            return out;
        }

        if (const json *lrc = findMember(root, "lrc"))
        {
            out.lrc = asText(member(*lrc, "lyric"));
        }
        if (const json *translated = findMember(root, "tlyric"))
        {
            out.translated = asText(member(*translated, "lyric"));
        }
        // 只有时间标签没有字的"空词"也按纯音乐处理：拿它去解析只会得到一堆空行。
        out.instrumental = noLyric || uncollected || out.lrc.empty();
        out.ok = true;
        return out;
    }

    FavoriteResult decodeFavoriteToggle(const std::string &body)
    {
        FavoriteResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "响应不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }
        out.ok = true;
        return out;
    }

} // namespace sonora::online::core::codec
