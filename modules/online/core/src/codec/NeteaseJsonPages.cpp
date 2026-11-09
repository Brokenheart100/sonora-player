#include "sonora/online/core/codec/NeteaseJson.h"

#include "NeteaseJsonDetail.h"

#include <nlohmann/json.hpp>

#include <string>

namespace sonora::online::core::codec
{
    using namespace detail;

    // —— 第二批只读资源：歌单、歌曲详情、专辑、歌手、新歌、榜单、推荐（N1）——
    PlaylistDetailResult decodePlaylistDetail(const std::string &body, contracts::SourceKind kind)
    {
        PlaylistDetailResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "歌单详情不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }
        const json *playlistPtr = findMember(root, "playlist");
        if (playlistPtr == nullptr)
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "歌单详情里没有 playlist", false, 0};
            return out;
        }
        const json &playlist = *playlistPtr;

        auto &detail = out.detail;
        detail.id = asText(member(playlist, "id"));
        detail.name = asText(member(playlist, "name"));
        detail.coverUrl = asText(member(playlist, "coverImgUrl"));
        detail.description = asText(member(playlist, "description"));
        detail.creator = asText(member(member(playlist, "creator"), "nickname"));
        detail.trackCount = static_cast<std::int32_t>(asInt(member(playlist, "trackCount")));
        detail.kind = kind;
        detail.ok = true;

        const json &ids = member(playlist, "trackIds");
        if (ids.is_array())
        {
            out.trackIds.reserve(ids.size());
            for (const auto &entry : ids)
            {
                // 形状是 `{"id":123,"v":0,"at":...,"alg":null}`，也可能退化成纯数字。
                std::string id = asText(member(entry, "id"));
                if (id.empty())
                {
                    id = asText(entry);
                }
                if (!id.empty())
                {
                    out.trackIds.push_back(std::move(id));
                }
            }
        }
        if (out.trackIds.empty())
        {
            // 老歌单可能只给 `tracks`（最多 10 首）。退而求其次，至少让首屏有东西。
            const json &tracks = member(playlist, "tracks");
            if (tracks.is_array())
            {
                for (const auto &song : tracks)
                {
                    const std::string id = asText(member(song, "id"));
                    if (!id.empty())
                    {
                        out.trackIds.push_back(id);
                    }
                }
            }
        }

        out.raw = body;
        out.ok = true;
        return out;
    }

    DecodeResult decodeSongDetails(const std::string &body)
    {
        DecodeResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "歌曲详情不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }
        const json &songs = member(root, "songs");
        appendTracks(songs, out);
        out.raw = body;
        out.ok = true;
        return out;
    }

    DecodeResult decodeAlbum(const std::string &body)
    {
        DecodeResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "专辑详情不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }
        const json &album = member(root, "album");
        auto &detail = out.detail;
        detail.id = asText(member(album, "id"));
        detail.name = asText(member(album, "name"));
        detail.coverUrl = asText(member(album, "picUrl"));
        detail.description = asText(member(album, "briefDesc"));
        // 专辑的歌手在 `album.artist.name`；`album.artists[]` 是同一份数据的数组形态。
        detail.creator = asText(member(member(album, "artist"), "name"));
        if (detail.creator.empty())
        {
            const json &artists = member(album, "artists");
            if (artists.is_array() && !artists.empty())
            {
                detail.creator = asText(member(artists.front(), "name"));
            }
        }
        detail.trackCount = static_cast<std::int32_t>(asInt(member(album, "size")));
        detail.kind = contracts::SourceKind::Album;
        detail.ok = !detail.id.empty();

        appendTracks(member(root, "songs"), out);
        if (out.rows.empty())
        {
            appendTracks(member(album, "songs"), out);
        }
        out.raw = body;
        out.ok = true;
        return out;
    }

    DecodeResult decodeArtist(const std::string &body)
    {
        DecodeResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "歌手详情不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }
        const json &artist = member(root, "artist");
        auto &detail = out.detail;
        detail.id = asText(member(artist, "id"));
        detail.name = asText(member(artist, "name"));
        detail.coverUrl = asText(member(artist, "picUrl"));
        if (detail.coverUrl.empty())
        {
            detail.coverUrl = asText(member(artist, "img1v1Url"));
        }
        detail.description = asText(member(artist, "briefDesc"));
        // 歌手页标题下那行字用"专辑数 / 歌曲数"，比空着强。
        const std::int32_t albums = static_cast<std::int32_t>(asInt(member(artist, "albumSize")));
        const std::int32_t songs = static_cast<std::int32_t>(asInt(member(artist, "musicSize")));
        if (albums > 0 || songs > 0)
        {
            detail.description += (detail.description.empty() ? "" : "\n");
            detail.description += std::to_string(albums) + " 张专辑 · " + std::to_string(songs) + " 首";
        }
        detail.trackCount = songs;
        detail.kind = contracts::SourceKind::Artist;
        detail.ok = !detail.id.empty();

        appendTracks(member(root, "hotSongs"), out);
        out.raw = body;
        out.ok = true;
        return out;
    }

    DecodeResult decodeNewSongs(const std::string &body)
    {
        DecodeResult out;
        const json root = json::parse(body, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            out.error = contracts::OnlineError{kernel::ErrorCode::Upstream, "新歌列表不是合法 JSON", false, 0};
            return out;
        }
        if (!topCodeOk(root, out.error))
        {
            return out;
        }
        appendTracks(member(root, "result"), out);
        out.raw = body;
        out.ok = true;
        return out;
    }

    CollectionListResult decodeToplist(const std::string &body)
    {
        // hasMore=false：`/api/toplist` 一次给全（实测 65 张），没有分页参数。
        return decodeCollectionArray(body, "list", contracts::SourceKind::Chart, false);
    }

    CollectionListResult decodePersonalizedPlaylists(const std::string &body)
    {
        return decodeCollectionArray(body, "result", contracts::SourceKind::Playlist, false);
    }


} // namespace sonora::online::core::codec
