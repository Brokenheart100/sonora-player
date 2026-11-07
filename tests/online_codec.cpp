// 在线搜索的 JSON 解码测试。
//
// 为什么把 body 内联成字符串常量、而不是读 tests/fixtures/online/*.json：
// sonora_tests 要在 CI 里裸跑（工作目录不固定），读文件的测试会因为找不到
// 相对路径而变成"本机能过、CI 不过"。夹具的意义是"形状"，形状写成常量一样有效。
// 真实的完整响应留在 tests/fixtures/online/ 里供人工比对。
//
// 网易云最坑的三件事都在这里钉死：
// 1. 同一个搜索有两套字段名（/api/search/get 用 artists/album/duration，
//    /api/cloudsearch/pc 用 ar/al/dt）；
// 2. 数字会以字符串下发（"269000"、"186016"），也见过 id 是数字；
// 3. result 为 null 是"没搜到"，不是错误；而 code=-460 是"被风控拦了"，是错误。
#include "doctest_support.h"

#include <cstdint>
#include <string>

#include <sonora/online/contracts/Types.h>
#include <sonora/online/core/codec/NeteaseJson.h>

using namespace sonora::online::contracts;
using sonora::online::core::codec::decodeSearch;

namespace {

// /api/search/get 的真实形状（截取 2 首，字段与线上响应一致）。
const char *kLegacyBody = R"({
  "result": {
    "songCount": 335,
    "hasMore": true,
    "songs": [
      {
        "id": 186016,
        "name": "晴天",
        "artists": [{"id": 6452, "name": "周杰伦"}, {"id": 0, "name": "Jay Chou"}],
        "album": {"id": 18875, "name": "叶惠美", "picUrl": "https://p1.music.126.net/a.jpg"},
        "duration": 269000,
        "fee": 0,
        "alias": ["Sunny Day"],
        "mvid": 0
      },
      {
        "id": 108914,
        "name": "七里香",
        "artists": [{"id": 6452, "name": "周杰伦"}],
        "album": {"id": 18875, "name": "七里香", "picUrl": ""},
        "duration": 299000,
        "fee": 1
      }
    ]
  },
  "code": 200
})";

// /api/cloudsearch/pc 的真实形状：字段名整套换掉，还多带 privilege。
const char *kCloudBody = R"({
  "result": {
    "songCount": 1,
    "songs": [
      {
        "id": 1945894789,
        "name": "最伟大的作品",
        "ar": [{"id": 6452, "name": "周杰伦"}],
        "al": {"id": 138045, "name": "最伟大的作品", "picUrl": "https://p1.music.126.net/b.jpg"},
        "dt": 253000,
        "fee": 1,
        "privilege": {"id": 1945894789, "fee": 1, "st": -200},
        "alia": [],
        "mv": 0,
        "pop": 100
      }
    ]
  },
  "code": 200
})";

} // namespace

TEST_CASE("在线搜索解码：legacy 形状的两首")
{
    const auto decoded = decodeSearch(kLegacyBody);
    CHECK(decoded.ok);
    CHECK(decoded.total == 335);
    CHECK(decoded.hasMore);
    REQUIRE(decoded.rows.size() == 2);

    const auto &first = decoded.rows[0];
    CHECK(first.songId == "186016");
    CHECK(first.title == "晴天");
    CHECK(first.artists == "周杰伦 / Jay Chou"); // 多个歌手用 " / " 连接
    CHECK(first.album == "叶惠美");
    CHECK(first.durationMs == 269000);
    CHECK(first.coverUrl == "https://p1.music.126.net/a.jpg");
    CHECK(first.playable); // fee == 0

    const auto &second = decoded.rows[1];
    CHECK(second.songId == "108914");
    CHECK_FALSE(second.playable); // fee == 1：VIP/付费，R3 才细分
    CHECK(second.coverUrl.empty()); // 空串封面不该被当成 URL
}

TEST_CASE("在线搜索解码：cloudsearch 形状（ar/al/dt）")
{
    const auto decoded = decodeSearch(kCloudBody);
    CHECK(decoded.ok);
    REQUIRE(decoded.rows.size() == 1);

    const auto &row = decoded.rows[0];
    CHECK(row.songId == "1945894789"); // 超过 2^53，必须是字符串
    CHECK(row.title == "最伟大的作品");
    CHECK(row.artists == "周杰伦");
    CHECK(row.album == "最伟大的作品");
    CHECK(row.durationMs == 253000); // 来自 dt
    CHECK_FALSE(row.playable);
}

TEST_CASE("在线搜索解码：result 为 null 是空结果，不是错误")
{
    const auto decoded = decodeSearch(R"({"result": null, "code": 200})");
    CHECK(decoded.ok);
    CHECK(decoded.error.code == sonora::kernel::ErrorCode::None);
    CHECK(decoded.rows.empty());
    CHECK(decoded.total == 0);
    CHECK_FALSE(decoded.hasMore);
}

TEST_CASE("在线搜索解码：-460 风控是可重试的错误")
{
    const auto decoded = decodeSearch(R"({"code": -460, "message": "Cheating"})");
    CHECK_FALSE(decoded.ok);
    
    CHECK(decoded.error.code == sonora::kernel::ErrorCode::RateLimited);
    CHECK(decoded.error.retryable);
    CHECK(decoded.error.retryAfterMs == 3000);
    CHECK(decoded.rows.empty());
}

TEST_CASE("在线搜索解码：301 是未登录，400 是参数错")
{
    const auto unauthenticated = decodeSearch(R"({"code": 301})");
    CHECK_FALSE(unauthenticated.ok);
    
    CHECK(unauthenticated.error.code == sonora::kernel::ErrorCode::Unauthenticated);
    CHECK_FALSE(unauthenticated.error.retryable);

    const auto badRequest = decodeSearch(R"({"code": 400})");
    CHECK_FALSE(badRequest.ok);
    
    CHECK(badRequest.error.code == sonora::kernel::ErrorCode::InvalidArgument);
}

TEST_CASE("在线搜索解码：数字被序列化成字符串也能读")
{
    const auto decoded = decodeSearch(R"({
      "code": 200,
      "result": {
        "songCount": "1",
        "songs": [{
          "id": "186016",
          "name": "晴天",
          "artists": [{"name": "周杰伦"}],
          "album": {"name": "叶惠美"},
          "duration": "269000",
          "fee": "0"
        }]
      }
    })");
    CHECK(decoded.ok);
    REQUIRE(decoded.rows.size() == 1);
    CHECK(decoded.rows[0].songId == "186016");
    CHECK(decoded.rows[0].durationMs == 269000);
    CHECK(decoded.total == 1);
}

TEST_CASE("在线搜索解码：字段为 null 时降级，不让整页失败")
{
    const auto decoded = decodeSearch(R"({
      "code": 200,
      "result": {
        "songs": [
          {"id": 1, "name": null, "artists": null, "album": null, "duration": null},
          {"id": 2, "name": "有名字", "artists": [{"name": null}], "album": {"name": null}}
        ]
      }
    })");
    CHECK(decoded.ok);
    REQUIRE(decoded.rows.size() == 2);
    CHECK(decoded.rows[0].title.empty());
    CHECK(decoded.rows[0].artists.empty());
    CHECK(decoded.rows[0].album.empty());
    CHECK(decoded.rows[0].durationMs == 0);
    CHECK(decoded.rows[1].title == "有名字");
    CHECK(decoded.rows[1].artists.empty()); // 歌手名是 null 时不产出 "null" 字样
}

TEST_CASE("在线搜索解码：超长标题原样保留")
{
    std::string title(300, 'x');
    const auto body = std::string(R"({"code":200,"result":{"songs":[{"id":3,"name":")") + title +
                        R"(","artists":[{"name":"A"}],"album":{"name":"B"}}]}})";
    const auto decoded = decodeSearch(body);
    CHECK(decoded.ok);
    REQUIRE(decoded.rows.size() == 1);
    CHECK(decoded.rows[0].title.size() == 300);
}

TEST_CASE("在线搜索解码：没有 hasMore 时按满 20 首推断")
{
    std::string songs;
    for (int i = 0; i < 20; ++i)
    {
        if (!songs.empty())
        {
            songs += ',';
        }
        songs += R"({"id":)" + std::to_string(1000 + i) + R"(,"name":"n"})";
    }
    const auto decoded = decodeSearch(R"({"code":200,"result":{"songs":[)" + songs + R"(]}})");
    CHECK(decoded.ok);
    CHECK(decoded.rows.size() == 20);
    CHECK(decoded.hasMore); // 满页按"可能还有"处理，下一页由 offset 决定
}
