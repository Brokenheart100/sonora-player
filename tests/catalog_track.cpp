// Track 聚合的测试。
//
// 这些断言的价值在于：它们描述的是**不变量**，而不是实现。
// 换掉仓储、换掉 UI，这些断言一个字都不用改——它们就是"曲目记录说得出口"的定义。
#include "doctest_support.h"

#include <string>


#include <sonora/catalog/core/domain/Credit.h>
#include <sonora/catalog/core/domain/Events.h>
#include <sonora/catalog/core/domain/Track.h>
#include <sonora/kernel/Error.h>

using sonora::catalog::core::Album;
using sonora::catalog::core::Artist;
using sonora::catalog::core::Track;
using sonora::catalog::core::TrackAdded;
using sonora::catalog::core::TrackDetails;
using sonora::catalog::core::TrackId;
using sonora::catalog::core::trimTitle;
using sonora::kernel::ErrorCode;

namespace {

// 每个用例都要一份合法数据，写成一处的代价远小于复制十遍。
TrackDetails validDetails()
{
    TrackDetails details;
    details.title = "夜航西飞";
    details.filePath = R"(D:\Music\夜航西飞.flac)";
    details.durationMs = 254000;
    details.trackNumber = 3;
    details.discNumber = 1;
    details.fileSizeBytes = 31'000'000;
    details.addedAtUnix = 1'760'000'000;
    return details;
}

}  // namespace

TEST_CASE("trimTitle：去首尾空白，含全角空格")
{
    CHECK(trimTitle("  夜航西飞  ") == "夜航西飞");
    CHECK(trimTitle("\t\n夜航西飞\r\n") == "夜航西飞");
    CHECK(trimTitle("　夜航西飞　") == "夜航西飞");          // U+3000
    CHECK(trimTitle("夜 航 西 飞") == "夜 航 西 飞");        // 中间不动
    CHECK(trimTitle("   ") == "");
    CHECK(trimTitle("") == "");
}

TEST_CASE("Track::add：新曲目没有 id（要等仓储落库才分配）")
{
    auto track = Track::add(validDetails());
    REQUIRE(track.hasValue());
    CHECK_FALSE(track.value().isPersisted());
    CHECK(track.value().title() == "夜航西飞");
}

TEST_CASE("Track::add：缺少标题或路径要报 InvalidArgument")
{
    auto noTitle = validDetails();
    noTitle.title = "   ";
    auto result = Track::add(noTitle);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);

    auto noPath = validDetails();
    noPath.filePath.clear();
    CHECK_FALSE(Track::add(noPath).hasValue());
}

TEST_CASE("Track::add：负数时长/轨号是错误，不是可忽略的字段")
{
    auto negativeDuration = validDetails();
    negativeDuration.durationMs = -1;
    CHECK_FALSE(Track::add(negativeDuration).hasValue());

    auto negativeTrack = validDetails();
    negativeTrack.trackNumber = -5;
    CHECK_FALSE(Track::add(negativeTrack).hasValue());
}

TEST_CASE("Track::rehydrate：重建必须带有效 id")
{
    auto withId = Track::rehydrate(TrackId::fromValue(7), validDetails());
    REQUIRE(withId.hasValue());
    CHECK(withId.value().isPersisted());
    CHECK(withId.value().id().value() == 7);

    auto withoutId = Track::rehydrate(TrackId{}, validDetails());
    REQUIRE_FALSE(withoutId.hasValue());
    CHECK(withoutId.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("Track：标题在构造时被规范化，不是只在显示时")
{
    auto details = validDetails();
    details.title = "  夜航西飞　";
    auto track = Track::add(details);
    REQUIRE(track.hasValue());
    CHECK(track.value().title() == "夜航西飞");
}

TEST_CASE("Track::rename：改名同样过校验，空标题被拒绝且不改变原值")
{
    auto track = Track::add(validDetails());
    REQUIRE(track.hasValue());

    CHECK_FALSE(track.value().rename("   ").hasValue());
    CHECK(track.value().title() == "夜航西飞");   // 失败不该有副作用

    REQUIRE(track.value().rename("  北纬三十度  ").hasValue());
    CHECK(track.value().title() == "北纬三十度");
}

TEST_CASE("Album / Artist：名字非空，重建要 id")
{
    auto album = Album::add("  海边的卡夫卡  ");
    REQUIRE(album.hasValue());
    CHECK(album.value().title() == "海边的卡夫卡");
    CHECK_FALSE(album.value().isPersisted());

    CHECK_FALSE(Album::add("   ").hasValue());
    CHECK_FALSE(Album::rehydrate(sonora::catalog::core::AlbumId{}, "x").hasValue());

    auto artist = Artist::add("  坂本龙一 ");
    REQUIRE(artist.hasValue());
    CHECK(artist.value().name() == "坂本龙一");
    CHECK_FALSE(Artist::add("").hasValue());
}

TEST_CASE("TrackAdded：事件名是过去时，且只带 id")
{
    const TrackAdded event{TrackId::fromValue(42)};
    CHECK(event.eventName() == "catalog.track_added");
    CHECK(event.id().value() == 42);
}
