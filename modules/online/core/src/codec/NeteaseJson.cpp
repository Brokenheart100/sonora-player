#include "sonora/online/core/codec/NeteaseJson.h"

#include <iostream>

#include "sonora/online/core/auth/CookieHeader.h"

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

        // `member()` 对缺失键返回共享的空对象，所以 `member(x, "k").is_object()`
        // 永远为真——拿它判断"有没有"会把缺键当成空对象。真要判断存在性用这个。
        const json *findMember(const json &object, const char *key)
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
        const json *findValue(const json &object, const char *key)
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
        std::string firstArtistId(const json &song)
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

    // ------------------------------------------------------------------ N1

    namespace
    {
        // 顶层业务码判定。只读 root 的 `code`，不递归——嵌套对象里也有 code
        // （专辑的 `album.code` 就是 0），N0 的探针脚本正是在这里误判过一次。
        // 返回 true 表示成功；失败时把 error 填好。
        bool topCodeOk(const json &root, contracts::OnlineError &error)
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
        bool toCollection(const json &item, contracts::SourceKind kind, contracts::CollectionDto &out)
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
        const json &unwrapSong(const json &item)
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

        void appendTracks(const json &array, DecodeResult &out)
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

        CollectionListResult decodeCollectionArray(const std::string &body, const char *arrayKey,
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
    } // namespace

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

    // ------------------------------------------------------------------ N2

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
