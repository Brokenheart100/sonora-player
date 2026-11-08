// 歌单的两层：聚合的那三条不变量，和 SQL 里的顺序/不可用标记。
//
// 聚合规则用纯内存测（快、也不需要库），落库行为用内存 SQLite 测——
// 唯一 pos 索引、LEFT JOIN 的条目数、available 的置位，都只有真库能证明。
#include "doctest_support.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <sonora/playlists/adapters/PlaylistsSchema.h>
#include <sonora/playlists/adapters/SqlitePlaylistRepository.h>
#include <sonora/playlists/core/app/PlaylistService.h>
#include <sonora/playlists/core/domain/Playlist.h>
#include <sonora/platform/storage/Migrator.h>
#include <sonora/platform/storage/SqliteConnection.h>

namespace {

using namespace sonora;
using sonora::playlists::core::Playlist;
using sonora::playlists::core::PlaylistId;

struct Fixture {
    platform::storage::SqliteConnection connection;

    Fixture() : connection(open()) {}

    static platform::storage::SqliteConnection open()
    {
        auto opened = platform::storage::SqliteConnection::open(":memory:");
        REQUIRE_MESSAGE(opened.hasValue(), "打开内存库失败：" << opened.error().message());
        auto connection = std::move(opened).take();
        platform::storage::Migrator migrator{connection, playlists::adapters::kModuleName};
        const auto version = migrator.apply(playlists::adapters::migrations());
        REQUIRE(version.hasValue());
        CHECK(version.value() == static_cast<int>(playlists::adapters::migrations().size()));
        return connection;
    }
};

}  // namespace

TEST_CASE("歌单聚合：名字不能为空或纯空白")
{
    CHECK_FALSE(Playlist::create("", 0).hasValue());
    CHECK_FALSE(Playlist::create("   ", 0).hasValue());
    CHECK_FALSE(Playlist::create("\t\n", 0).hasValue());

    auto ok = Playlist::create("路上听", 1'700'000'000);
    REQUIRE(ok.hasValue());
    CHECK(ok.value().name() == "路上听");
    CHECK(ok.value().trackCount() == 0);
    CHECK_FALSE(ok.value().id().isValid()); // 还没入库

    // 改名走同一套校验。
    auto playlist = std::move(ok).take();
    CHECK_FALSE(playlist.rename(" ").hasValue());
    CHECK(playlist.name() == "路上听");
    REQUIRE(playlist.rename("夜里听").hasValue());
    CHECK(playlist.name() == "夜里听");
}

TEST_CASE("歌单聚合：同一首歌只能加一次，移走要能找回来")
{
    auto created = Playlist::create("清单", 0);
    REQUIRE(created.hasValue());
    auto playlist = std::move(created).take();

    CHECK_FALSE(playlist.addTrack(0).hasValue()); // id 0 无效
    REQUIRE(playlist.addTrack(11).hasValue());
    REQUIRE(playlist.addTrack(22).hasValue());

    // 重复加是 Conflict，不是静默忽略：静默忽略会让"加了两下"看起来像成功了。
    const auto again = playlist.addTrack(11);
    REQUIRE_FALSE(again.hasValue());
    CHECK(again.error().code() == kernel::ErrorCode::Conflict);
    CHECK(playlist.trackCount() == 2);

    CHECK_FALSE(playlist.removeTrack(99).hasValue());
    REQUIRE(playlist.removeTrack(11).hasValue());
    CHECK(playlist.trackCount() == 1);
    CHECK(playlist.items().front().trackId == 22);
}

TEST_CASE("歌单聚合：移动会夹取越界下标，并且顺序真的变了")
{
    auto created = Playlist::create("顺序", 0);
    REQUIRE(created.hasValue());
    auto playlist = std::move(created).take();
    for (std::uint64_t id : {1U, 2U, 3U}) {
        REQUIRE(playlist.addTrack(id).hasValue());
    }

    REQUIRE(playlist.moveTrack(1, 2).hasValue());
    CHECK(playlist.items()[0].trackId == 2);
    CHECK(playlist.items()[1].trackId == 3);
    CHECK(playlist.items()[2].trackId == 1);

    // 越界：夹到末尾 / 开头。
    REQUIRE(playlist.moveTrack(1, 99).hasValue());
    CHECK(playlist.items().back().trackId == 1);
    REQUIRE(playlist.moveTrack(1, -5).hasValue());
    CHECK(playlist.items().front().trackId == 1);

    CHECK_FALSE(playlist.moveTrack(77, 0).hasValue());
}

TEST_CASE("歌单聚合：曲目从库里删掉后条目还在，只是不可用")
{
    auto created = Playlist::create("占位", 0);
    REQUIRE(created.hasValue());
    auto playlist = std::move(created).take();
    REQUIRE(playlist.addTrack(5).hasValue());
    REQUIRE(playlist.addTrack(6).hasValue());

    playlist.markTrackUnavailable(5);
    CHECK(playlist.trackCount() == 2);
    CHECK_FALSE(playlist.items()[0].available);
    CHECK(playlist.items()[1].available);

    // 幂等：同一个曲目再删一次（比如重建索引时重复收到事件）不该改变什么。
    playlist.markTrackUnavailable(5);
    CHECK_FALSE(playlist.items()[0].available);
    playlist.markTrackUnavailable(999);
    CHECK(playlist.trackCount() == 2);
}

TEST_CASE("歌单仓储：建、读、改名、重排都能落库")
{
    Fixture fixture;
    playlists::adapters::SqlitePlaylistRepository repository{fixture.connection};

    auto created = Playlist::create("早上", 1'700'000'000);
    REQUIRE(created.hasValue());
    auto playlist = std::move(created).take();
    const auto id = repository.insert(playlist);
    REQUIRE(id.hasValue());
    CHECK(id.value().isValid());

    playlist.assignId(id.value());
    REQUIRE(playlist.addTrack(101).hasValue());
    REQUIRE(playlist.addTrack(202).hasValue());
    REQUIRE(repository.save(playlist).hasValue());

    auto loaded = repository.findById(id.value());
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().name() == "早上");
    REQUIRE(loaded.value().trackCount() == 2);
    CHECK(loaded.value().items()[0].trackId == 101);

    // 重排：唯一 pos 索引要求先删后插，这条用例就是在守它。
    auto reordered = std::move(loaded).take();
    REQUIRE(reordered.moveTrack(202, 0).hasValue());
    REQUIRE(repository.save(reordered).hasValue());

    auto again = repository.findById(id.value());
    REQUIRE(again.hasValue());
    CHECK(again.value().items()[0].trackId == 202);
    CHECK(again.value().items()[1].trackId == 101);
    // pos 是连续的 0、1——不是 0、2 这种"删完留下的洞"。
    CHECK(fixture.connection.queryInt("SELECT MAX(pos) FROM playlist_item").value() == 1);
    CHECK(fixture.connection.queryInt("SELECT COUNT(*) FROM playlist_item").value() == 2);
}

TEST_CASE("歌单仓储：列表带条目数，空歌单也在")
{
    Fixture fixture;
    playlists::adapters::SqlitePlaylistRepository repository{fixture.connection};

    auto empty = Playlist::create("空", 1);
    REQUIRE(empty.hasValue());
    REQUIRE(repository.insert(empty.value()).hasValue());

    auto filled = Playlist::create("满", 2);
    REQUIRE(filled.hasValue());
    auto playlist = std::move(filled).take();
    const auto id = repository.insert(playlist);
    REQUIRE(id.hasValue());
    playlist.assignId(id.value());
    REQUIRE(playlist.addTrack(1).hasValue());
    REQUIRE(playlist.addTrack(2).hasValue());
    REQUIRE(playlist.addTrack(3).hasValue());
    REQUIRE(repository.save(playlist).hasValue());

    auto rows = repository.list();
    REQUIRE(rows.hasValue());
    REQUIRE(rows.value().size() == 2);
    CHECK(rows.value()[0].name == "空");
    CHECK(rows.value()[0].trackCount == 0);
    CHECK(rows.value()[1].trackCount == 3);
}

TEST_CASE("歌单仓储：删歌单会连条目一起清掉，删不存在的返回 NotFound")
{
    Fixture fixture;
    playlists::adapters::SqlitePlaylistRepository repository{fixture.connection};

    auto created = Playlist::create("待删", 0);
    REQUIRE(created.hasValue());
    auto playlist = std::move(created).take();
    const auto id = repository.insert(playlist);
    REQUIRE(id.hasValue());
    playlist.assignId(id.value());
    REQUIRE(playlist.addTrack(7).hasValue());
    REQUIRE(repository.save(playlist).hasValue());

    REQUIRE(repository.remove(id.value()).hasValue());
    CHECK(fixture.connection.queryInt("SELECT COUNT(*) FROM playlist").value() == 0);
    CHECK(fixture.connection.queryInt("SELECT COUNT(*) FROM playlist_item").value() == 0);

    const auto missing = repository.remove(PlaylistId::fromValue(999));
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().code() == kernel::ErrorCode::NotFound);
}

TEST_CASE("歌单仓储：曲库删歌时只把条目标成不可用，并返回影响行数")
{
    Fixture fixture;
    playlists::adapters::SqlitePlaylistRepository repository{fixture.connection};

    auto created = Playlist::create("跨模块", 0);
    REQUIRE(created.hasValue());
    auto playlist = std::move(created).take();
    const auto id = repository.insert(playlist);
    REQUIRE(id.hasValue());
    playlist.assignId(id.value());
    REQUIRE(playlist.addTrack(42).hasValue());
    REQUIRE(playlist.addTrack(43).hasValue());
    REQUIRE(repository.save(playlist).hasValue());

    const auto affected = repository.markTrackUnavailable(42);
    REQUIRE(affected.hasValue());
    CHECK(affected.value() == 1);
    // 重复置位不再算"受影响"：这条 UPDATE 带了 available = 1 的条件。
    CHECK(repository.markTrackUnavailable(42).value() == 0);

    auto loaded = repository.findById(id.value());
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().trackCount() == 2);
    CHECK_FALSE(loaded.value().items()[0].available);
    CHECK(loaded.value().items()[1].available);
}

TEST_CASE("歌单服务：DTO 的 pos 是当前顺序，不是历史下标")
{
    Fixture fixture;
    playlists::adapters::SqlitePlaylistRepository repository{fixture.connection};
    playlists::core::PlaylistService service{repository};

    auto created = service.create("服务");
    REQUIRE(created.hasValue());
    REQUIRE(service.addTrack(created.value(), 10).hasValue());
    REQUIRE(service.addTrack(created.value(), 20).hasValue());
    REQUIRE(service.addTrack(created.value(), 30).hasValue());

    // 把 30 挪到最前：DTO 的 pos 必须跟着变成 0、1、2。
    REQUIRE(service.moveTrack(created.value(), 30, 0).hasValue());

    auto entries = service.tracks(created.value());
    REQUIRE(entries.hasValue());
    REQUIRE(entries.value().size() == 3);
    CHECK(entries.value()[0].trackId == 30);
    CHECK(entries.value()[0].pos == 0);
    CHECK(entries.value()[1].trackId == 10);
    CHECK(entries.value()[1].pos == 1);

    CHECK_FALSE(service.addTrack(created.value(), 10).hasValue());
    CHECK_FALSE(service.tracks(PlaylistId::fromValue(4242)).hasValue());
}
