#pragma once

// 解码器共用的取值小工具。只给 codec 目录下的 .cpp 用，不放 include/ ——
// 它们不是契约的一部分，改签名不需要通知任何模块。

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "sonora/online/contracts/Types.h"

namespace sonora::online::core::codec::detail
{
    namespace c = online::contracts;
    using json = nlohmann::json;


    // 数字或字符串都收：网易云有几处 id 是字符串（超过 2^53 的音频 id），
    // 同一个字段在不同接口上类型还会变，所以统一按"文本"取。
    inline std::string asText(const json &value)
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

    inline std::int64_t asInt(const json &value)
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
    inline const json &member(const json &object, const char *key)
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

    // `member()` 对缺失键返回共享的空对象，所以 `member(x, "k").is_object()`
    // 永远为真——拿它判断"有没有"会把缺键当成空对象。真要判断存在性用这个。
    inline const json *findMember(const json &object, const char *key)
    {
        if (!object.is_object())
        {
            return nullptr;
        }
        const auto it = object.find(key);
        if (it == object.end() || it->is_null() || !it->is_object())
        {
            return nullptr;
        }
        return &*it;
    }

    // 与 findMember 的区别：**数组也认**。`/api/v1/radio/get` 的 `data` 就是数组，
    // 用 findMember 找它永远拿到 nullptr（那个 helper 只认对象）。
    inline const json *findValue(const json &object, const char *key)
    {
        const auto it = object.find(key);
        if (it == object.end() || it->is_null())
        {
            return nullptr;
        }
        return &*it;
    }

    // 第一位歌手的 id（"查看歌手"要用）。搜索结果里经常是 0——那不是"Wei 0"，
    // 是网易云没给 id，当作没有。
    inline std::string firstArtistId(const json &song)
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
        const std::string id = asText(member(list->at(0), "id"));
        return id == "0" ? std::string{} : id;
    }

    inline std::string artistNames(const json &song)
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

    inline c::OnlineError mapApiCode(int code)
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

    inline c::OnlineTrackDto toTrack(const json &song)
    {
        c::OnlineTrackDto track;
        track.songId = asText(member(song, "id"));
        track.title = asText(member(song, "name"));

        const json &album = song.contains("album") ? member(song, "album") : member(song, "al");
        track.album = asText(member(album, "name"));
        track.coverUrl = asText(member(album, "picUrl"));

        track.artists = artistNames(song);
        track.artistId = firstArtistId(song);

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
    // 顶层业务码判定。只读 root 的 `code`，不递归——嵌套对象里也有 code
    // （专辑的 `album.code` 就是 0），N0 的探针脚本正是在这里误判过一次。
    // 返回 true 表示成功；失败时把 error 填好。
    inline bool topCodeOk(const json &root, contracts::OnlineError &error)
    {
        const json &node = member(root, "code");
        if (node.is_number() || node.is_string())
        {
            const auto code = static_cast<int>(asInt(node));
            // 0 与 200 都算成功：v1 系列历史上两种都出现过（§19.3）。
            if (code != 0 && code != 200)
            {
                error = mapApiCode(code);
                return false;
            }
        }
        return true;
    }

    // "一张卡片"：歌单/榜单/专辑/歌手共用同一套字段名，取不到就跳过。
    inline bool toCollection(const json &item, contracts::SourceKind kind, contracts::CollectionDto &out)
    {
        if (!item.is_object())
        {
            return false;
        }
        out.id = asText(member(item, "id"));
        if (out.id.empty())
        {
            return false;
        }
        out.name = asText(member(item, "name"));
        // 榜单用 coverImgUrl，推荐歌单用 picUrl，专辑用 picUrl，歌手用 picUrl/img1v1Url。
        out.coverUrl = asText(member(item, "coverImgUrl"));
        if (out.coverUrl.empty())
        {
            out.coverUrl = asText(member(item, "picUrl"));
        }
        if (out.coverUrl.empty())
        {
            out.coverUrl = asText(member(item, "img1v1Url"));
        }
        out.count = static_cast<std::int32_t>(asInt(member(item, "trackCount")));
        if (out.count == 0)
        {
            out.count = static_cast<std::int32_t>(asInt(member(item, "size")));
        }
        out.kind = kind;
        return true;
    }

    // 列表里的曲目对象可能被包了一层：新歌是 `result[].song`，其余是直接对象。
    inline const json &unwrapSong(const json &item)
    {
        static const json kEmpty = json::object();
        if (!item.is_object())
        {
            return kEmpty;
        }
        // 榜单/推荐里的行是 `{"song":{…}}` 包了一层；歌曲详情里就是平的歌曲对象。
        // 这里不能用 `member(item, "song").is_object()`——缺失时它返回共享空对象，
        // 同样是 object，结果把所有字段都取成空字符串。
        const json *inner = findMember(item, "song");
        return inner != nullptr ? *inner : item;
    }

    inline void appendTracks(const json &array, DecodeResult &out)
    {
        if (!array.is_array())
        {
            return;
        }
        out.rows.reserve(out.rows.size() + array.size());
        for (const auto &item : array)
        {
            const json &song = unwrapSong(item);
            if (!song.is_object())
            {
                continue;
            }
            auto track = toTrack(song);
            if (track.songId.empty())
            {
                // 没有 id 的行点不动，留着只会变成一条永远转圈的行。
                continue;
            }
            out.rows.push_back(std::move(track));
        }
    }

    inline CollectionListResult decodeCollectionArray(const std::string &body, const char *arrayKey,
                                               contracts::SourceKind kind, bool hasMoreWhenFull)
    {
        CollectionListResult out;
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
        const json &array = member(root, arrayKey);
        if (array.is_array())
        {
            out.items.reserve(array.size());
            for (const auto &item : array)
            {
                contracts::CollectionDto card;
                if (!toCollection(item, kind, card))
                {
                    continue;
                }
                // 副标题：榜单给更新频率，歌单给播放数，其它给创建者/歌手名。
                card.subtitle = asText(member(item, "updateFrequency"));
                if (card.subtitle.empty())
                {
                    card.subtitle = asText(member(item, "copywriter"));
                }
                if (card.subtitle.empty())
                {
                    const json &creator = member(item, "creator");
                    card.subtitle = asText(member(creator, "nickname"));
                }
                out.items.push_back(std::move(card));
            }
            // 卡片列表没有分页参数（toplist 一次全给），拿满即认为没有更多。
            out.hasMore = hasMoreWhenFull && array.size() >= 20;
        }
        out.raw = body;
        out.ok = true;
        return out;
    }

} // namespace sonora::online::core::codec::detail
