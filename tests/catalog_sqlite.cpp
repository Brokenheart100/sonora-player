// M1 的端到端验收：真 SQLite、真文件系统、真迁移。
//
// 与 tests/catalog_import.cpp 的分工：那边用假端口验证**应用层逻辑**，
// 这边用真适配器验证"这些话说到 SQL 上还算不算数"。两边都过，才敢说
// 导入链路是通的——只测假端口会漏掉 UNIQUE、外键、事务可见性这些真问题。
#include "doctest_support.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "mamba/catalog/adapters/BasicMetadataReader.h"
#include "mamba/catalog/adapters/CatalogSchema.h"
#include "mamba/catalog/adapters/FileSystemScanner.h"
#include "mamba/catalog/adapters/SqliteCreditDirectory.h"
#include "mamba/catalog/adapters/SqlitePlaybackRecorder.h"
#include "mamba/catalog/adapters/SqliteTrackListReader.h"
#include "mamba/catalog/adapters/SqliteTrackRepository.h"
#include "mamba/catalog/contracts/Commands.h"
#include "mamba/catalog/contracts/Queries.h"
#include "mamba/catalog/core/app/ImportFolderHandler.h"
#include "mamba/contracts/PlaybackEvents.h"
#include "mamba/kernel/EventBus.h"
#include "mamba/kernel/IClock.h"
#include "mamba/platform/storage/Migrator.h"
#include "mamba/platform/storage/SqliteConnection.h"
#include "mamba/platform/storage/SqliteUnitOfWork.h"

namespace {

namespace fs = std::filesystem;
using namespace mamba;
using namespace mamba::catalog;

// Windows 上最贵的一课：fs::path 从 std::string 构造时按**当前 ANSI 代码页**
// 解释这些字节，不是按 UTF-8。于是"夜航西飞"（6 个字节的 UTF-8）会被当成 GBK
// 解成别的字，落盘的文件名就是乱码——而且看不出来，因为库里读回来也是一致的乱码。
// 所以凡是从 std::string 到 path、或反过来，都必须走这两个函数。
fs::path u8Path(const std::string &utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string u8String(const fs::path &path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

// 一个自清理的临时工作区：目录 + 库文件。
class Sandbox {
public:
    Sandbox()
    {
        // 测试工作区放在**工程目录内**，不碰 %TEMP%：这个环境里 %TEMP% 会
        // 直接 Access is denied（沙箱限制），而把测试数据写在仓库里还有个好处
        // ——失败之后能直接去看那个库文件到底长什么样。
        std::error_code ec;
        root_ = fs::current_path(ec) / ".test_tmp" / "mamba_m1_it";
        fs::remove_all(root_, ec);
        fs::create_directories(root_ / "music", ec);
        dbPath_ = u8String(root_ / "library.sqlite");
    }

    ~Sandbox()
    {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    Sandbox(const Sandbox &) = delete;
    Sandbox &operator=(const Sandbox &) = delete;

    [[nodiscard]] std::string dbPath() const { return dbPath_; }
    [[nodiscard]] std::string musicDir() const { return u8String(root_ / "music"); }

    // 造 count 个空音频文件。扩展名必须是真的，否则扫描器就不认识它们了。
    void makeFiles(int count) const
    {
        for (int i = 1; i <= count; ++i) {
            const auto name = "夜航西飞 - 曲目 " + std::to_string(i) + ".flac";
            std::ofstream out{root_ / "music" / u8Path(name), std::ios::binary};
            out << "fake audio payload " << i;
        }
    }

    // 单造一个名字里带标点的文件（用于验证 % 不是通配符）。
    void makeFile(const std::string &name) const
    {
        std::ofstream out{root_ / "music" / u8Path(name), std::ios::binary};
        out << "fake audio payload";
    }

private:
    fs::path root_;
    std::string dbPath_;
};

// 整套真适配器。连接的所有权在这里，其余都是引用。
struct Stack {
    platform::storage::SqliteConnection connection;
    std::unique_ptr<platform::storage::SqliteUnitOfWork> unit;
    std::unique_ptr<adapters::SqliteCreditDirectory> credits;
    std::unique_ptr<adapters::SqliteTrackRepository> tracks;
    std::unique_ptr<adapters::SqliteTrackListReader> listReader;
    adapters::FileSystemScanner scanner;
    adapters::BasicMetadataReader metadata;
    kernel::SystemClock clock;

    explicit Stack(platform::storage::SqliteConnection conn) : connection(std::move(conn))
    {
        unit = std::make_unique<platform::storage::SqliteUnitOfWork>(connection);
        credits = std::make_unique<adapters::SqliteCreditDirectory>(connection);
        tracks = std::make_unique<adapters::SqliteTrackRepository>(connection);
        listReader = std::make_unique<adapters::SqliteTrackListReader>(connection);
    }

    [[nodiscard]] core::ImportFolderHandler handler()
    {
        return core::ImportFolderHandler{scanner, metadata, *credits, *tracks, *unit, clock, nullptr};
    }
};

}  // namespace

TEST_CASE("端到端：迁移 → 导入 20 首 → 列表 20 行 → 重开仍在")
{
    Sandbox sandbox;
    sandbox.makeFiles(20);

    // ── 第一次打开：迁移 + 导入 ────────────────────────────────────────────
    {
        auto opened = platform::storage::SqliteConnection::open(sandbox.dbPath());
        if (!opened) {
            MESSAGE("打开失败：" << opened.error().message() << " | 路径：" << sandbox.dbPath());
        }
        REQUIRE(opened.hasValue());
        Stack stack{std::move(opened).take()};

        platform::storage::Migrator migrator{stack.connection, adapters::kModuleName};
        const auto version = migrator.apply(adapters::migrations());
        REQUIRE(version.hasValue());
        // v2 = 增量扫描列 + 全文索引表，v3 = 列表视图补 cover_key（M5），
        // v4 = 播放次数与最近播放时间（M6）。
        CHECK(version.value() == 4);

        auto handler = stack.handler();
        const auto imported = handler.import(catalog::contracts::ImportFolder{sandbox.musicDir()});
        REQUIRE(imported.hasValue());
        CHECK(imported.value().filesFound == 20);
        CHECK(imported.value().imported == 20);
        CHECK(imported.value().duplicates == 0);
        CHECK(imported.value().failed == 0);

        // 列表：一条 SQL 出 20 个 DTO，艺术家名与专辑名已经拼好。
        catalog::contracts::ListTracks query;
        const auto page = stack.listReader->list(query);
        REQUIRE(page.hasValue());
        CHECK(page.value().totalCount == 20);
        CHECK(page.value().rows.size() == 20);
        CHECK(page.value().rows.front().artistName == "夜航西飞");
        // 文件名是"夜航西飞 - 曲目 N.flac"，BasicMetadataReader 会把它拆成艺术家 + 标题。
        CHECK(page.value().rows.front().title.rfind("曲目 ", 0) == 0);

        // 分页：第二页只剩 5 行，总数不变。
        catalog::contracts::ListTracks second;
        second.offset = 15;
        second.limit = 10;
        const auto tail = stack.listReader->list(second);
        REQUIRE(tail.hasValue());
        CHECK(tail.value().totalCount == 20);
        CHECK(tail.value().rows.size() == 5);

        // 重扫同一个目录：一条都不新增，全部走"未变"（不读标签）。
        const auto again = handler.import(catalog::contracts::ImportFolder{sandbox.musicDir()});
        REQUIRE(again.hasValue());
        CHECK(again.value().imported == 0);
        CHECK(again.value().unchanged == 20);
        CHECK(again.value().duplicates == 0);
    }

    // ── 重新打开：数据还在，迁移是幂等的 ───────────────────────────────────
    {
        auto reopened = platform::storage::SqliteConnection::open(sandbox.dbPath());
        REQUIRE(reopened.hasValue());
        Stack stack{std::move(reopened).take()};

        platform::storage::Migrator migrator{stack.connection, adapters::kModuleName};
        // 再跑一遍迁移不该执行任何脚本，版本保持在 v4。
        const auto version = migrator.apply(adapters::migrations());
        REQUIRE(version.hasValue());
        CHECK(version.value() == 4);

        const auto count = stack.tracks->count();
        REQUIRE(count.hasValue());
        CHECK(count.value() == 20);

        catalog::contracts::ListTracks query;
        const auto page = stack.listReader->list(query);
        REQUIRE(page.hasValue());
        CHECK(page.value().rows.size() == 20);
    }
}

TEST_CASE("搜索：中文子串逐字命中，标点不是通配符")
{
    Sandbox sandbox;
    sandbox.makeFiles(12);
    // 名字里带百分号的一个文件：搜 "100%" 必须只命中它，而不是"被当成通配符"。
    sandbox.makeFile("100% - 情书.flac");

    auto opened = platform::storage::SqliteConnection::open(sandbox.dbPath());
    REQUIRE(opened.hasValue());
    Stack stack{std::move(opened).take()};

    platform::storage::Migrator migrator{stack.connection, adapters::kModuleName};
    REQUIRE(migrator.apply(adapters::migrations()).hasValue());

    auto handler = stack.handler();
    REQUIRE(handler.import(catalog::contracts::ImportFolder{sandbox.musicDir()}).hasValue());

    catalog::contracts::ListTracks query;
    query.searchText = "曲目 1";
    const auto found = stack.listReader->list(query);
    REQUIRE(found.hasValue());
    // 曲目 1、10、11、12 —— 前缀 token 让 "1" 也命中 "10/11/12"，
    // 所以是 4 行而不是 1 行（跟 LIKE '%曲目 1%' 的答案一致）。
    CHECK(found.value().rows.size() == 4);

    // 词中间的子串：FTS5 短语"相邻且有序"，所以 "航西" 也命中 "夜 航 西 飞"。
    catalog::contracts::ListTracks middle;
    middle.searchText = "航西";
    const auto hitMiddle = stack.listReader->list(middle);
    REQUIRE(hitMiddle.hasValue());
    CHECK(hitMiddle.value().rows.size() == 12);

    // 百分号只是分隔符：切出 token "100"，命中那一个文件。
    catalog::contracts::ListTracks percent;
    percent.searchText = "100%";
    const auto hitPercent = stack.listReader->list(percent);
    REQUIRE(hitPercent.hasValue());
    CHECK(hitPercent.value().rows.size() == 1);

    // 只有标点的查询退化成"不筛"——不能变成 FTS5 语法错误，也不能返回 0 行。
    catalog::contracts::ListTracks punctuation;
    punctuation.searchText = "%";
    const auto hitAll = stack.listReader->list(punctuation);
    REQUIRE(hitAll.hasValue());
    CHECK(hitAll.value().totalCount == 13);
}

TEST_CASE("外键：艺术家/专辑的 id 指向真实行，join 不会丢行")
{
    Sandbox sandbox;
    sandbox.makeFiles(3);

    auto opened = platform::storage::SqliteConnection::open(sandbox.dbPath());
    REQUIRE(opened.hasValue());
    Stack stack{std::move(opened).take()};

    platform::storage::Migrator migrator{stack.connection, adapters::kModuleName};
    REQUIRE(migrator.apply(adapters::migrations()).hasValue());

    auto handler = stack.handler();
    REQUIRE(handler.import(catalog::contracts::ImportFolder{sandbox.musicDir()}).hasValue());

    // 三首同一艺术家、同一个"未知专辑"（文件名里没有专辑信息）。
    const auto artists = stack.connection.queryInt("SELECT COUNT(*) FROM artists");
    REQUIRE(artists.hasValue());
    CHECK(artists.value() == 1);

    const auto albums = stack.connection.queryInt("SELECT COUNT(*) FROM albums");
    REQUIRE(albums.hasValue());
    CHECK(albums.value() == 1);

    // 视图返回的行数必须等于 tracks 的行数：少一行就说明有孤儿外键。
    const auto joined = stack.connection.queryInt("SELECT COUNT(*) FROM v_track_list");
    REQUIRE(joined.hasValue());
    CHECK(joined.value() == 3);
}

TEST_CASE("播放次数：TrackFinished 事件累计进 play_count，重启后仍在")
{
    Sandbox sandbox;
    sandbox.makeFiles(3);

    auto opened = platform::storage::SqliteConnection::open(sandbox.dbPath());
    REQUIRE(opened.hasValue());
    Stack stack{std::move(opened).take()};

    platform::storage::Migrator migrator{stack.connection, adapters::kModuleName};
    REQUIRE(migrator.apply(adapters::migrations()).hasValue());

    auto handler = stack.handler();
    REQUIRE(handler.import(catalog::contracts::ImportFolder{sandbox.musicDir()}).hasValue());

    adapters::SqlitePlaybackRecorder recorder{stack.connection};
    kernel::EventBus events;
    int stored = 0;
    int missing = 0;
    events.subscribe<::mamba::contracts::TrackFinished>(
        [&recorder, &stored, &missing](const ::mamba::contracts::TrackFinished &event)
        {
            if (recorder.record(event.trackId, event.playedAtUnix))
            {
                ++stored;
            }
            else
            {
                ++missing;
            }
        });

    // 同一首放两次。事件是"已经发生的事实"，订阅者只负责记账。
    events.publish(::mamba::contracts::TrackFinished{1, 1700000000});
    events.publish(::mamba::contracts::TrackFinished{1, 1700000500});
    // 不存在的曲目：记不上，但不许抛、不许崩（边放边删是允许的）。
    events.publish(::mamba::contracts::TrackFinished{999, 1700000900});

    CHECK(stored == 2);
    CHECK(missing == 1);

    const auto plays = stack.connection.queryInt("SELECT play_count FROM tracks WHERE id = 1");
    REQUIRE(plays.hasValue());
    CHECK(plays.value() == 2);

    const auto lastPlayed =
        stack.connection.queryInt("SELECT last_played_at FROM tracks WHERE id = 1");
    REQUIRE(lastPlayed.hasValue());
    CHECK(lastPlayed.value() == 1700000500);

    // 没有曲目多出来：事件不是"新增"，只改既有行。
    const auto count = stack.tracks->count();
    REQUIRE(count.hasValue());
    CHECK(count.value() == 3);
}
