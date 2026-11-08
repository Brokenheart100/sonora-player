// N1：歌单 / 歌曲详情 / 专辑 / 歌手 / 新歌 / 榜单 / 推荐歌单 的解码测试。
//
// 和 online_codec.cpp 一样，body 内联成字符串常量：sonora_tests 在 CI 里工作目录
// 不固定，读文件的测试会变成"本机能过、CI 不过"。这里钉的是**形状**。
//
// 最要紧的一条在「歌曲详情」那个用例里：`/api/song/detail` 的 songs[] 是**平的**
// 歌曲对象，而榜单/推荐的 result[] 是 `{"song":{…}}` 包了一层。曾经用
// `member(item, "song").is_object()` 判断"有没有外层"，而 member() 对缺失键返回
// 共享的空对象——空对象也是 object，于是平的对象被当成外层、所有字段取成空串，
// 整页 0 行。这条测试就是为了不让它回来。
#include "doctest_support.h"

#include <string>

#include <sonora/online/contracts/Types.h>
#include <sonora/online/core/codec/NeteaseJson.h>

using namespace sonora::online::contracts;
using sonora::online::core::codec::decodeAlbum;
using sonora::online::core::codec::decodeArtist;
using sonora::online::core::codec::decodeNewSongs;
using sonora::online::core::codec::decodePersonalizedPlaylists;
using sonora::online::core::codec::decodePlaylistDetail;
using sonora::online::core::codec::decodeSongDetails;
using sonora::online::core::codec::decodeDailySongs;
using sonora::online::core::codec::decodeLyric;
using sonora::online::core::codec::decodeRadio;
using sonora::online::core::codec::decodeFavoriteToggle;
using sonora::online::core::codec::decodeToplist;
using sonora::online::core::codec::decodeUserPlaylists;

namespace {

// 平铺的歌曲对象：/api/song/detail 的 songs[]、/api/v1/album 的 songs[] 都是这个形状。
const char *kFlatSong = R"json({
  "id": 3458269805,
  "name": "烂奇迹(不小心惹了上帝)",
  "artists": [{"id": 123, "name": "郭顶"}, {"id": 456, "name": "某某"}],
  "album": {"id": 260499608, "name": "飞行器的执行周期", "picUrl": "https://p1.music.126.net/a.jpg"},
  "duration": 269000,
  "fee": 0
})json";

// 搜索结果里的 artist id 常常是 0：那是"没给"，不是"歌手编号 0"。
const char *kSearchSong = R"json({
  "id": 2668397359,
  "name": "晴天 (原唱 周杰伦)",
  "artists": [{"id": 0, "name": "周杰伦"}],
  "album": {"id": 260499608, "name": "晴天", "picUrl": "https://p1.music.126.net/b.jpg"},
  "duration": 269000,
  "fee": 0
})json";

} // namespace

TEST_CASE("在线歌单：trackIds 是对象数组，名字与曲目数照抄")
{
    const std::string body = R"({
      "code": 200,
      "playlist": {
        "id": 19723756,
        "name": "飙升榜",
        "coverImgUrl": "https://p1.music.126.net/cover.jpg",
        "description": "每天更新",
        "trackCount": 100,
        "creator": {"nickname": "网易云音乐"},
        "trackIds": [{"id": 1, "v": 0}, {"id": 2, "v": 3}, {"id": 3, "v": 0}],
        "tracks": []
      }
    })";

    const auto result = decodePlaylistDetail(body, SourceKind::Chart);

    CHECK(result.ok);
    CHECK(result.detail.name == "飙升榜");
    CHECK(result.detail.creator == "网易云音乐");
    CHECK(result.detail.trackCount == 100);
    CHECK(result.detail.kind == SourceKind::Chart);
    REQUIRE(result.trackIds.size() == 3);
    CHECK(result.trackIds[1] == "2");
}

TEST_CASE("在线歌单：没有 trackIds 时退回 tracks[]，别把整页判成空")
{
    const std::string body = R"({
      "code": 200,
      "playlist": {"id": 1, "name": "老歌单", "trackCount": 2,
        "tracks": [{"id": 11, "name": "a"}, {"id": 12, "name": "b"}]}
    })";

    const auto result = decodePlaylistDetail(body, SourceKind::Playlist);

    CHECK(result.ok);
    REQUIRE(result.trackIds.size() == 2);
    CHECK(result.trackIds[0] == "11");
}

TEST_CASE("歌曲详情：平铺的 songs[] 必须解出整行（外层包裹的坑）")
{
    const std::string body = std::string(R"({"code": 200, "songs": [)") + kFlatSong + "]}";

    const auto result = decodeSongDetails(body);

    CHECK(result.ok);
    REQUIRE(result.rows.size() == 1);
    const auto &row = result.rows.front();
    CHECK(row.songId == "3458269805");
    CHECK(row.title == "烂奇迹(不小心惹了上帝)");
    CHECK(row.artists == "郭顶 / 某某");
    CHECK(row.album == "飞行器的执行周期");
    CHECK(row.durationMs == 269000);
    CHECK(row.playable);
    // 第一位歌手的 id：给"查看歌手"用。
    CHECK(row.artistId == "123");
}

TEST_CASE("在线歌曲：artist id 为 0 时当作没有歌手信息")
{
    const std::string body = std::string(R"({"code": 200, "songs": [)") + kSearchSong + "]}";

    const auto result = decodeSongDetails(body);

    CHECK(result.ok);
    REQUIRE(result.rows.size() == 1);
    CHECK(result.rows.front().artists == "周杰伦");
    CHECK(result.rows.front().artistId.empty());
}

TEST_CASE("推荐的新歌：result[] 里每项是 {\"song\":{…}}，取里层")
{
    const std::string body = std::string(R"({"code": 200, "result": [{"id": 1, "song": )") + kFlatSong + "}]}";

    const auto result = decodeNewSongs(body);

    CHECK(result.ok);
    REQUIRE(result.rows.size() == 1);
    CHECK(result.rows.front().songId == "3458269805");
}

TEST_CASE("在线专辑：顶层 songs[] 出行，album{} 出头部")
{
    const std::string body = std::string(R"({
      "code": 200,
      "album": {"id": 260499608, "name": "飞行器的执行周期", "picUrl": "https://p/cover.jpg",
                "briefDesc": "第二张专辑", "size": 10,
                "artist": {"name": "郭顶"}, "artists": [{"name": "郭顶"}]},
      "songs": [)") + kFlatSong + "]}";

    const auto result = decodeAlbum(body);

    CHECK(result.ok);
    CHECK(result.detail.name == "飞行器的执行周期");
    CHECK(result.detail.creator == "郭顶");
    CHECK(result.detail.trackCount == 10);
    REQUIRE(result.rows.size() == 1);
    CHECK(result.rows.front().artists == "郭顶 / 某某");
}

TEST_CASE("在线歌手：hotSongs[] 出行，专辑数与歌曲数写进描述")
{
    const std::string body = std::string(R"({
      "code": 200,
      "artist": {"id": 7763, "name": "郭顶", "img1v1Url": "https://p/artist.jpg",
                 "albumSize": 7, "musicSize": 33, "briefDesc": "内地男歌手"},
      "hotSongs": [)") + kFlatSong + "]}";

    const auto result = decodeArtist(body);

    CHECK(result.ok);
    CHECK(result.detail.name == "郭顶");
    CHECK(result.detail.coverUrl == "https://p/artist.jpg");
    CHECK(result.detail.description.find("7 张专辑") != std::string::npos);
    CHECK(result.detail.description.find("33 首") != std::string::npos);
    REQUIRE(result.rows.size() == 1);
}

TEST_CASE("榜单列表：list[] 的 updateFrequency 当副标题")
{
    const std::string body = R"({
      "code": 200,
      "list": [
        {"id": 19723756, "name": "飙升榜", "updateFrequency": "每天更新",
         "coverImgUrl": "https://p/1.jpg", "trackCount": 100},
        {"id": 3779629, "name": "新歌榜", "updateFrequency": "每天更新",
         "coverImgUrl": "https://p/2.jpg", "trackCount": 100}
      ]
    })";

    const auto result = decodeToplist(body);

    CHECK(result.ok);
    REQUIRE(result.items.size() == 2);
    CHECK(result.items[0].name == "飙升榜");
    CHECK(result.items[0].subtitle == "每天更新");
    CHECK(result.items[0].kind == SourceKind::Chart);
    CHECK(result.items[0].count == 100);
    CHECK(!result.hasMore);
}

TEST_CASE("推荐歌单：picUrl 当封面，copywriter 当副标题")
{
    const std::string body = R"({
      "code": 200,
      "result": [
        {"id": 509265479, "name": "后摇", "picUrl": "https://p/pl.jpg",
         "copywriter": "编辑推荐", "trackCount": 111}
      ]
    })";

    const auto result = decodePersonalizedPlaylists(body);

    CHECK(result.ok);
    REQUIRE(result.items.size() == 1);
    CHECK(result.items[0].coverUrl == "https://p/pl.jpg");
    CHECK(result.items[0].subtitle == "编辑推荐");
    CHECK(result.items[0].kind == SourceKind::Playlist);
}

TEST_CASE("在线资源解码：上游返回 301/400 时是错误，不是空列表")
{
    const auto notSignedIn = decodeToplist(R"({"code": 301, "msg": "需要登录"})");
    CHECK(!notSignedIn.ok);
    CHECK(notSignedIn.error.code == sonora::kernel::ErrorCode::Unauthenticated);

    const auto badParam = decodePersonalizedPlaylists(R"({"code": 400, "msg": "参数错误"})");
    CHECK(!badParam.ok);
}

// ── N2：登录侧（我的歌单 / 每日推荐 / 收藏） ─────────────────────────────

TEST_CASE("我的歌单：specialType 5 就是「我喜欢的音乐」")
{
    const char *body = R"json({
      "code": 200,
      "playlist": [
        {"id": 501, "name": "每日私藏", "coverImgUrl": "https://p1.music.126.net/a.jpg", "trackCount": 88},
        {"id": 502, "name": "我喜欢的音乐", "coverImgUrl": "https://p1.music.126.net/b.jpg", "trackCount": 213, "specialType": 5}
      ]
    })json";

    const auto result = decodeUserPlaylists(body);

    CHECK(result.ok);
    REQUIRE(result.items.size() == 2);
    CHECK(result.items[0].playlistId == "501");
    CHECK(result.items[0].trackCount == 88);
    // 服务端也会给个名字，但那是"用户自定义的歌单名"；喜欢列表要按 specialType 认。
    CHECK(result.items[1].name == "我喜欢的音乐");
    CHECK(result.items[1].playlistId == "502");
}

TEST_CASE("每日推荐：曲目在 data.dailySongs[]，不是顶层 songs[]")
{
    const char *body = R"json({
      "code": 200,
      "data": {
        "dailySongs": [
          {"id": 11, "name": "麻雀", "ar": [{"id": 1, "name": "李荣浩"}],
           "al": {"id": 2, "name": "麻雀", "picUrl": "https://p/a.jpg"}, "dt": 221000},
          {"id": 12, "name": "晴天", "ar": [{"id": 3, "name": "周杰伦"}],
           "al": {"id": 4, "name": "叶惠美", "picUrl": "https://p/b.jpg"}, "dt": 269000}
        ]
      }
    })json";

    const auto result = decodeDailySongs(body);

    CHECK(result.ok);
    REQUIRE(result.rows.size() == 2);
    CHECK(result.rows[0].songId == "11");
    CHECK(result.rows[0].title == "麻雀");
    CHECK(result.rows[0].artists == "李荣浩");
    CHECK(result.rows[1].durationMs == 269000);
}

TEST_CASE("每日推荐：少了 data 就报「接口可能已变更」，不要当成空列表")
{
    const auto result = decodeDailySongs(R"({"code": 200, "songs": []})");

    CHECK(!result.ok);
    CHECK(result.error.code == sonora::kernel::ErrorCode::Upstream);
    CHECK(result.error.retryable);
}

TEST_CASE("收藏：只看顶层 code，不看 body 里的 status")
{
    const auto added = decodeFavoriteToggle(R"({"code": 200, "status": 1})");
    CHECK(added.ok);

    const auto notSignedIn = decodeFavoriteToggle(R"({"code": 301, "msg": "需要登录"})");
    CHECK(!notSignedIn.ok);
    CHECK(notSignedIn.error.code == sonora::kernel::ErrorCode::Unauthenticated);

    const auto badParam = decodeFavoriteToggle(R"({"code": 400, "msg": "参数错误"})");
    CHECK(!badParam.ok);
}

TEST_CASE("在线歌词：原词与翻译分开取，翻译可能为空")
{
    const auto both = decodeLyric(R"({"code": 200,
        "lrc": {"version": 1, "lyric": "[00:01.00]Hello\n[00:03.00]World"},
        "tlyric": {"version": 1, "lyric": "[00:01.00]你好\n[00:03.00]世界"}})");

    CHECK(both.ok);
    CHECK(both.instrumental == false);
    CHECK(both.lrc.find("Hello") != std::string::npos);
    CHECK(both.translated.find("你好") != std::string::npos);

    const auto noTranslation = decodeLyric(R"({"code": 200, "lrc": {"lyric": "[00:01.00]词"}, "tlyric": {"lyric": ""}})");
    CHECK(noTranslation.ok);
    CHECK(noTranslation.translated.empty());
}

TEST_CASE("在线歌词：nolyric / uncollected 是「没有词」，不是失败")
{
    const auto instrumental = decodeLyric(R"({"code": 200, "nolyric": true})");
    CHECK(instrumental.ok);
    CHECK(instrumental.instrumental);
    CHECK(instrumental.lrc.empty());

    const auto uncollected = decodeLyric(R"({"code": 200, "uncollected": true, "lrc": {"lyric": ""}})");
    CHECK(uncollected.ok);
    CHECK(uncollected.instrumental);
}

TEST_CASE("在线歌词：缺 lrc 字段也按纯音乐处理，不抛也不崩")
{
    const auto missing = decodeLyric(R"({"code": 200})");
    CHECK(missing.ok);
    CHECK(missing.instrumental);
    CHECK(missing.lrc.empty());

    const auto notObject = decodeLyric("[]");
    CHECK(!notObject.ok);
}

TEST_CASE("私人 FM：/api/v1/radio/get 顶层没有 code，data 是数组")
{
    // 实测形状：{"popAdjust":false,"data":[{...一首...}]}，**没有顶层 code**。
    // 早期用 findMember 找 data 永远拿不到（那个 helper 只认对象），
    // 所以这里同时钉住"数组也能取到"这件事。
    const auto one = decodeRadio(R"({"popAdjust": false, "data": [
        {"name": "Undefined", "id": 3451951517,
         "artists": [{"name": "PerVade"}],
         "album": {"name": "某专辑", "picUrl": "http://p1.music.126.net/a.jpg"},
         "duration": 233000}]})");

    CHECK(one.ok);
    REQUIRE(one.rows.size() == 1);
    CHECK(one.rows.front().songId == "3451951517");
    CHECK(one.rows.front().title == "Undefined");
    CHECK(one.rows.front().artists == "PerVade");
    CHECK(one.rows.front().durationMs == 233000);

    // v1 风格：code 0 也是成功
    const auto zero = decodeRadio(R"({"code": 0, "data": [{"id": 7, "name": "x"}]})");
    CHECK(zero.ok);
    REQUIRE(zero.rows.size() == 1);
    CHECK(zero.rows.front().songId == "7");
}

TEST_CASE("私人 FM：data 不是数组时报上游变了，而不是静默空列表")
{
    const auto missing = decodeRadio(R"({"popAdjust": false})");
    CHECK(!missing.ok);
    CHECK(missing.error.retryable);

    const auto wrongShape = decodeRadio(R"({"data": {"songs": []}})");
    CHECK(!wrongShape.ok);

    const auto notJson = decodeRadio("<html>502</html>");
    CHECK(!notJson.ok);
}
