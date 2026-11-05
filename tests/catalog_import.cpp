// M1 的纵向切片测试：一条命令穿过总线 → 应用层 → 五个端口。
//
// 这里用假端口（内存实现）而不是真 SQLite，是为了把"应用层逻辑对不对"和
// "SQL 写得对不对"分开验证。真存储层的测试在 tests/storage_sqlite.cpp 与
// tests/catalog_sqlite.cpp 里，两者都不需要对方存在。
#include "doctest_support.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mamba/catalog/contracts/Commands.h"
#include "mamba/catalog/contracts/Queries.h"
#include "mamba/catalog/core/app/ImportFolderHandler.h"
#include "mamba/catalog/core/app/ListTracksHandler.h"
#include "mamba/catalog/core/ports/Ports.h"
#include "mamba/kernel/CommandBus.h"
#include "mamba/kernel/QueryBus.h"

using namespace mamba;
using namespace mamba::catalog;
using namespace mamba::catalog::core;

namespace
{

    class FakeScanner final : public ILibraryScanner
    {
    public:
        std::vector<std::string> files;
        mutable int calls = 0;

        kernel::Result<std::vector<std::string>> scan(std::string_view folderPath) const override
        {
            ++calls;
            if (folderPath == "/missing")
            {
                return kernel::Result<std::vector<std::string>>::fail(
                    kernel::Error{kernel::ErrorCode::NotFound, "目录不存在"});
            }
            return kernel::Result<std::vector<std::string>>::ok(files);
        }
    };

    class FakeReader final : public IMetadataReader
    {
    public:
        // 路径 → 元数据；不在表里的路径视为"标签读不出来"，返回失败。
        std::map<std::string, TrackMetadata, std::less<>> table;

        kernel::Result<TrackMetadata> read(std::string_view filePath) const override
        {
            const auto it = table.find(filePath);
            if (it == table.end())
            {
                return kernel::Result<TrackMetadata>::fail(
                    kernel::Error{kernel::ErrorCode::InvalidArgument, "不是音频文件"});
            }
            return kernel::Result<TrackMetadata>::ok(it->second);
        }
    };

    class FakeCredits final : public ICreditDirectory
    {
    public:
        std::vector<std::string> artists;

        kernel::Result<ArtistId> resolveArtist(std::string_view name) override
        {
            const auto it = std::find(artists.begin(), artists.end(), name);
            if (it != artists.end())
            {
                return kernel::Result<ArtistId>::ok(ArtistId::fromValue(
                    static_cast<std::uint64_t>(std::distance(artists.begin(), it) + 1)));
            }
            artists.emplace_back(name);
            return kernel::Result<ArtistId>::ok(
                ArtistId::fromValue(static_cast<std::uint64_t>(artists.size())));
        }

        kernel::Result<AlbumId> resolveAlbum(std::string_view /*title*/) override
        {
            return kernel::Result<AlbumId>::ok(AlbumId::fromValue(1));
        }
    };

    class FakeTrackRepository final : public ITrackRepository
    {
    public:
        std::vector<std::pair<std::uint64_t, std::string>> rows; // (id, filePath)

        kernel::Result<TrackId> add(const Track &track) override
        {
            const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &row)
                                         { return row.second == track.filePath(); });
            if (it != rows.end())
            {
                return kernel::Result<TrackId>::fail(
                    kernel::Error{kernel::ErrorCode::Conflict, "file_path 已存在"});
            }
            rows.emplace_back(rows.size() + 1, track.filePath());
            return kernel::Result<TrackId>::ok(TrackId::fromValue(rows.size()));
        }

        kernel::Result<Track> findById(TrackId /*id*/) const override
        {
            return kernel::Result<Track>::fail(
                kernel::Error{kernel::ErrorCode::NotFound, "不是这次测试关心的路径"});
        }

        kernel::Result<bool> existsByPath(std::string_view filePath) const override
        {
            const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &row)
                                         { return row.second == filePath; });
            return kernel::Result<bool>::ok(it != rows.end());
        }

        kernel::Result<std::int64_t> count() const override
        {
            return kernel::Result<std::int64_t>::ok(static_cast<std::int64_t>(rows.size()));
        }
    };

    class FakeTrackListReader final : public ITrackListReader
    {
    public:
        std::vector<contracts::TrackDto> rows;

        kernel::Result<contracts::TrackListResult> list(
            const contracts::ListTracks &query) const override
        {
            contracts::TrackListResult result;
            result.totalCount = static_cast<std::int64_t>(rows.size());
            const auto begin = std::min<std::size_t>(static_cast<std::size_t>(query.offset), rows.size());
            const auto end = std::min<std::size_t>(begin + static_cast<std::size_t>(query.limit),
                                                   rows.size());
            result.rows.assign(rows.begin() + static_cast<std::ptrdiff_t>(begin),
                               rows.begin() + static_cast<std::ptrdiff_t>(end));
            return kernel::Result<contracts::TrackListResult>::ok(std::move(result));
        }
    };

    class FakeUnitOfWork final : public kernel::IUnitOfWork
    {
    public:
        int commits = 0;
        int rollbacks = 0;
        bool open = false;

        kernel::Result<void> begin() override
        {
            open = true;
            return kernel::Result<void>::ok();
        }

        kernel::Result<void> commit() override
        {
            open = false;
            ++commits;
            return kernel::Result<void>::ok();
        }

        void rollback() noexcept override
        {
            open = false;
            ++rollbacks;
        }

        [[nodiscard]] bool active() const noexcept override { return open; }
    };

    class FixedClock final : public kernel::IClock
    {
    public:
        kernel::Result<void> unused() { return kernel::Result<void>::ok(); }

        [[nodiscard]] std::chrono::system_clock::time_point now() const override
        {
            return std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}};
        }

        [[nodiscard]] std::chrono::steady_clock::time_point steadyNow() const override
        {
            return {};
        }
    };

    // 一次测试用的整套装置。
    struct Fixture
    {
        FakeScanner scanner;
        FakeReader reader;
        FakeCredits credits;
        FakeTrackRepository tracks;
        FakeUnitOfWork unit;
        FixedClock clock;

        ImportFolderHandler makeHandler()
        {
            return ImportFolderHandler{scanner, reader, credits, tracks, unit, clock, nullptr};
        }

        void seed(const std::string &folder, int count)
        {
            for (int i = 1; i <= count; ++i)
            {
                const auto path = folder + "/track" + std::to_string(i) + ".flac";
                scanner.files.push_back(path);
                TrackMetadata meta;
                meta.title = "曲目 " + std::to_string(i);
                meta.artist = (i % 2 == 0) ? "夜航西飞" : "陈升";
                meta.album = "测试专辑";
                meta.durationMs = 180000 + i;
                meta.fileSizeBytes = 1024 * i;
                reader.table.emplace(path, std::move(meta));
            }
        }
    };

} // namespace

TEST_CASE("导入 20 个文件：全部入库，一个事务")
{
    Fixture f;
    f.seed("/music", 20);
    auto handler = f.makeHandler();

    const auto result = handler.import(contracts::ImportFolder{"/music"});
    REQUIRE(result.hasValue());

    const auto &summary = result.value();
    CHECK(summary.filesFound == 20);
    CHECK(summary.imported == 20);
    CHECK(summary.duplicates == 0);
    CHECK(summary.failed == 0);
    CHECK(f.tracks.rows.size() == 20);
    CHECK(f.unit.commits == 1);
    CHECK(f.unit.rollbacks == 0);
}

TEST_CASE("重复导入同一个文件夹：全部记为跳过，不重复落库")
{
    Fixture f;
    f.seed("/music", 20);
    auto handler = f.makeHandler();

    REQUIRE(handler.import(contracts::ImportFolder{"/music"}).hasValue() == true);
    const auto second = handler.import(contracts::ImportFolder{"/music"});
    REQUIRE(second.hasValue());

    CHECK(second.value().imported == 0);
    CHECK(second.value().duplicates == 20);
    CHECK(f.tracks.rows.size() == 20);
}

TEST_CASE("坏文件只计数，不中断整批")
{
    Fixture f;
    f.seed("/music", 20);
    // 第 7 个文件读不出标签：从 reader 表里删掉，模拟损坏或非音频。
    f.reader.table.erase("/music/track7.flac");
    auto handler = f.makeHandler();

    const auto result = handler.import(contracts::ImportFolder{"/music"});
    REQUIRE(result.hasValue());
    CHECK(result.value().filesFound == 20);
    CHECK(result.value().imported == 19);
    CHECK(result.value().failed == 1);
    CHECK(f.tracks.rows.size() == 19);
}

TEST_CASE("标题缺失时用文件名兜底")
{
    Fixture f;
    f.scanner.files.push_back("/music/夜航西飞 - 01.flac");
    TrackMetadata meta;
    meta.title = ""; // 标签里没写标题
    meta.artist = "夜航西飞";
    f.reader.table.emplace("/music/夜航西飞 - 01.flac", meta);
    auto handler = f.makeHandler();

    const auto result = handler.import(contracts::ImportFolder{"/music"});
    REQUIRE(result.hasValue());
    CHECK(result.value().imported == 1);
    CHECK(f.tracks.rows.size() == 1);
}

TEST_CASE("艺术家缺失时落到「未知艺术家」，不是空串")
{
    Fixture f;
    f.scanner.files.push_back("/music/a.flac");
    TrackMetadata meta;
    meta.title = "无艺术家的歌";
    f.reader.table.emplace("/music/a.flac", meta);
    auto handler = f.makeHandler();

    REQUIRE(handler.import(contracts::ImportFolder{"/music"}).hasValue() == true);
    REQUIRE(f.credits.artists.size() == 1);
    CHECK(f.credits.artists.front() == "未知艺术家");
}

TEST_CASE("空路径与不存在目录分别给出参数错误与未找到")
{
    Fixture f;
    auto handler = f.makeHandler();

    const auto empty = handler.import(contracts::ImportFolder{});
    REQUIRE(!empty.hasValue());
    CHECK(empty.error().code() == kernel::ErrorCode::InvalidArgument);

    const auto missing = handler.import(contracts::ImportFolder{"/missing"});
    REQUIRE(!missing.hasValue());
    CHECK(missing.error().code() == kernel::ErrorCode::NotFound);
    CHECK(f.tracks.rows.empty());
}

TEST_CASE("命令穿过总线：注册后 dispatch 返回成功，未注册返回 NotFound")
{
    Fixture f;
    f.seed("/music", 3);
    auto handler = f.makeHandler();

    kernel::CommandBus bus{nullptr, 5.0};
    CHECK(bus.handlerCount() == 0);

    // 未注册时总线必须自己报错，而不是崩溃——这是"表现层看不见 handler"的代价与保障。
    const auto before = bus.dispatch(contracts::ImportFolder{"/music"});
    REQUIRE(!before.hasValue());
    CHECK(before.error().code() == kernel::ErrorCode::NotFound);

    bus.on<contracts::ImportFolder>(
        [&handler](const contracts::ImportFolder &command)
        { return handler.handle(command); });
    CHECK(bus.handlerCount() == 1);

    const auto after = bus.dispatch(contracts::ImportFolder{"/music"});
    CHECK(after.hasValue());
    CHECK(f.tracks.rows.size() == 3);
}

TEST_CASE("查询穿过总线：ListTracks 返回分页后的 DTO")
{
    FakeTrackListReader reader;
    for (int i = 0; i < 25; ++i)
    {
        contracts::TrackDto dto;
        dto.id = static_cast<std::uint64_t>(i + 1);
        dto.title = "曲目 " + std::to_string(i + 1);
        dto.artistName = "夜航西飞";
        dto.albumTitle = "测试专辑";
        dto.durationMs = 200000;
        dto.filePath = "/music/" + std::to_string(i + 1) + ".flac";
        reader.rows.push_back(std::move(dto));
    }

    const ListTracksHandler handler{reader};
    kernel::QueryBus bus;

    contracts::ListTracks query;
    query.offset = 20;
    query.limit = 10;
    bus.on<contracts::ListTracks, contracts::TrackListResult>(
        [&handler](const contracts::ListTracks &q)
        { return handler.handle(q); });

    const auto page = bus.ask<contracts::TrackListResult>(query);
    REQUIRE(page.hasValue());
    CHECK(page.value().totalCount == 25);
    CHECK(page.value().rows.size() == 5); // 25 首里的最后一页

    contracts::ListTracks illegal;
    illegal.limit = 0;
    const auto rejected = bus.ask<contracts::TrackListResult>(illegal);
    REQUIRE(!rejected.hasValue());
    CHECK(rejected.error().code() == kernel::ErrorCode::InvalidArgument);
}
