#include "sonora/catalog/adapters/SqliteTrackRepository.h"

#include <optional>
#include <string>
#include <utility>

#include "sonora/catalog/adapters/SearchText.h"
#include "sonora/catalog/core/domain/Track.h"

namespace sonora::catalog::adapters {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;
using platform::storage::SqliteStatement;

namespace {

// 一行曲目的列清单。新增、更新、读回三处共用，改列名时才不会漏掉一处。
constexpr std::string_view kColumns =
    "title, artist_id, album_id, track_number, disc_number, duration_ms, file_path,"
    " file_size_bytes, added_at, file_mtime, cover_key";

}  // namespace

Result<core::TrackId> SqliteTrackRepository::add(const core::Track &track)
{
    const auto &d = track.details();
    auto statement = connection_.prepare(
        "INSERT INTO tracks(title, artist_id, album_id, track_number, disc_number, duration_ms,"
        " file_path, file_size_bytes, added_at, file_mtime, cover_key)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!statement) {
        return Result<core::TrackId>::fail(statement.error());
    }
    SqliteStatement &s = statement.value();

    const std::int64_t artistId = static_cast<std::int64_t>(d.artistId.value());
    const std::int64_t albumId = static_cast<std::int64_t>(d.albumId.value());

    const struct {
        int index;
        std::int64_t value;
    } numbers[] = {{2, artistId},
                   {3, albumId},
                   {4, d.trackNumber},
                   {5, d.discNumber},
                   {6, d.durationMs},
                   {8, d.fileSizeBytes},
                   {9, d.addedAtUnix},
                   {10, d.fileMtimeUnix}};
    for (const auto &item : numbers) {
        if (auto bound = s.bind(item.index, item.value); !bound) {
            return Result<core::TrackId>::fail(bound.error());
        }
    }
    if (auto bound = s.bind(1, std::string_view{d.title}); !bound) {
        return Result<core::TrackId>::fail(bound.error());
    }
    if (auto bound = s.bind(7, std::string_view{d.filePath}); !bound) {
        return Result<core::TrackId>::fail(bound.error());
    }
    if (auto bound = s.bind(11, std::string_view{d.coverKey}); !bound) {
        return Result<core::TrackId>::fail(bound.error());
    }

    if (auto stepped = s.step(); !stepped) {
        return Result<core::TrackId>::fail(stepped.error());
    }
    const auto id = core::TrackId::fromValue(
        static_cast<std::uint64_t>(connection_.lastInsertRowId()));

    if (auto indexed = refreshSearchRow(id); !indexed) {
        return Result<core::TrackId>::fail(indexed.error());
    }
    return Result<core::TrackId>::ok(id);
}

Result<void> SqliteTrackRepository::save(const core::Track &track)
{
    if (!track.isPersisted()) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "更新未持久化的曲目"});
    }
    const auto &d = track.details();
    auto statement = connection_.prepare(
        "UPDATE tracks SET title = ?, artist_id = ?, album_id = ?, track_number = ?,"
        " disc_number = ?, duration_ms = ?, file_path = ?, file_size_bytes = ?, added_at = ?,"
        " file_mtime = ?, cover_key = ? WHERE id = ?");
    if (!statement) {
        return Result<void>::fail(statement.error());
    }
    SqliteStatement &s = statement.value();

    const struct {
        int index;
        std::int64_t value;
    } numbers[] = {{2, static_cast<std::int64_t>(d.artistId.value())},
                   {3, static_cast<std::int64_t>(d.albumId.value())},
                   {4, d.trackNumber},
                   {5, d.discNumber},
                   {6, d.durationMs},
                   {8, d.fileSizeBytes},
                   {9, d.addedAtUnix},
                   {10, d.fileMtimeUnix},
                   {12, static_cast<std::int64_t>(track.id().value())}};
    for (const auto &item : numbers) {
        if (auto bound = s.bind(item.index, item.value); !bound) {
            return Result<void>::fail(bound.error());
        }
    }
    if (auto bound = s.bind(1, std::string_view{d.title}); !bound) {
        return Result<void>::fail(bound.error());
    }
    if (auto bound = s.bind(7, std::string_view{d.filePath}); !bound) {
        return Result<void>::fail(bound.error());
    }
    if (auto bound = s.bind(11, std::string_view{d.coverKey}); !bound) {
        return Result<void>::fail(bound.error());
    }

    if (auto stepped = s.step(); !stepped) {
        return Result<void>::fail(stepped.error());
    }
    if (connection_.changes() == 0) {
        return Result<void>::fail(Error{ErrorCode::NotFound, "曲目不存在，无法更新"});
    }
    return refreshSearchRow(track.id());
}

Result<core::Track> SqliteTrackRepository::findById(core::TrackId id) const
{
    auto statement = connection_.prepare("SELECT " + std::string(kColumns)
                                         + " FROM tracks WHERE id = ?");
    if (!statement) {
        return Result<core::Track>::fail(statement.error());
    }
    SqliteStatement &s = statement.value();
    if (auto bound = s.bind(1, static_cast<std::int64_t>(id.value())); !bound) {
        return Result<core::Track>::fail(bound.error());
    }
    auto row = s.step();
    if (!row) {
        return Result<core::Track>::fail(row.error());
    }
    if (!row.value()) {
        return Result<core::Track>::fail(Error{ErrorCode::NotFound, "曲目不存在"});
    }

    core::TrackDetails details;
    details.title = s.columnString(0);
    details.artistId = core::ArtistId::fromValue(static_cast<std::uint64_t>(s.columnInt(1)));
    details.albumId = core::AlbumId::fromValue(static_cast<std::uint64_t>(s.columnInt(2)));
    details.trackNumber = static_cast<int>(s.columnInt(3));
    details.discNumber = static_cast<int>(s.columnInt(4));
    details.durationMs = s.columnInt(5);
    details.filePath = s.columnString(6);
    details.fileSizeBytes = s.columnInt(7);
    details.addedAtUnix = s.columnInt(8);
    details.fileMtimeUnix = s.columnInt(9);
    details.coverKey = s.columnString(10);

    return core::Track::rehydrate(id, std::move(details));
}

Result<bool> SqliteTrackRepository::existsByPath(std::string_view filePath) const
{
    auto statement = connection_.prepare("SELECT 1 FROM tracks WHERE file_path = ? LIMIT 1");
    if (!statement) {
        return Result<bool>::fail(statement.error());
    }
    if (auto bound = statement.value().bind(1, filePath); !bound) {
        return Result<bool>::fail(bound.error());
    }
    auto row = statement.value().step();
    if (!row) {
        return Result<bool>::fail(row.error());
    }
    return Result<bool>::ok(row.value());
}

Result<std::optional<core::TrackFileState>> SqliteTrackRepository::findFileState(
    std::string_view filePath) const
{
    auto statement = connection_.prepare(
        "SELECT id, file_mtime, file_size_bytes, added_at FROM tracks WHERE file_path = ? LIMIT 1");
    if (!statement) {
        return Result<std::optional<core::TrackFileState>>::fail(statement.error());
    }
    if (auto bound = statement.value().bind(1, filePath); !bound) {
        return Result<std::optional<core::TrackFileState>>::fail(bound.error());
    }
    auto row = statement.value().step();
    if (!row) {
        return Result<std::optional<core::TrackFileState>>::fail(row.error());
    }
    if (!row.value()) {
        return Result<std::optional<core::TrackFileState>>::ok(std::nullopt);
    }
    core::TrackFileState state;
    state.id = core::TrackId::fromValue(
        static_cast<std::uint64_t>(statement.value().columnInt(0)));
    state.fileMtimeUnix = statement.value().columnInt(1);
    state.fileSizeBytes = statement.value().columnInt(2);
    state.addedAtUnix = statement.value().columnInt(3);
    return Result<std::optional<core::TrackFileState>>::ok(state);
}

Result<std::int64_t> SqliteTrackRepository::count() const
{
    // queryInt 不给绑定参数，所以这里的 COUNT(*) 只能无条件——需要条件的计数走读侧投影。
    return connection_.queryInt("SELECT COUNT(*) FROM tracks");
}

Result<void> SqliteTrackRepository::refreshSearchRow(core::TrackId id)
{
    // 索引体要三张表的列（标题 + 艺术家名 + 专辑名），而聚合只持 id。
    // 这不是"为了省事把 JOIN 写进写侧"——它读的是刚刚写进去的那一行，走主键。
    auto statement = connection_.prepare(
        "SELECT t.title, ar.name, al.title FROM tracks t"
        " JOIN artists ar ON ar.id = t.artist_id"
        " JOIN albums  al ON al.id = t.album_id"
        " WHERE t.id = ?");
    if (!statement) {
        return Result<void>::fail(statement.error());
    }
    const auto idValue = static_cast<std::int64_t>(id.value());
    if (auto bound = statement.value().bind(1, idValue); !bound) {
        return Result<void>::fail(bound.error());
    }
    auto row = statement.value().step();
    if (!row) {
        return Result<void>::fail(row.error());
    }
    if (!row.value()) {
        // 行已经被删了（理论上不会走到，删除路径目前不存在）。索引里也不该留。
        auto erase = connection_.prepare("DELETE FROM track_fts WHERE rowid = ?");
        if (!erase) {
            return Result<void>::fail(erase.error());
        }
        if (auto bound = erase.value().bind(1, idValue); !bound) {
            return Result<void>::fail(bound.error());
        }
        if (auto stepped = erase.value().step(); !stepped) {
            return Result<void>::fail(stepped.error());
        }
        return Result<void>::ok();
    }

    const std::string body = searchBody(statement.value().columnString(0),
                                        statement.value().columnString(1),
                                        statement.value().columnString(2));

    // 只写一列：tracks.search_body。track_fts 由 Schema V5 的触发器同步
    // （AFTER UPDATE OF search_body），所以这里不再有"两份真相要同时改"的问题。
    auto update = connection_.prepare("UPDATE tracks SET search_body = ? WHERE id = ?");
    if (!update) {
        return Result<void>::fail(update.error());
    }
    if (auto bound = update.value().bind(1, std::string_view{body}); !bound) {
        return Result<void>::fail(bound.error());
    }
    if (auto bound = update.value().bind(2, idValue); !bound) {
        return Result<void>::fail(bound.error());
    }
    if (auto stepped = update.value().step(); !stepped) {
        return Result<void>::fail(stepped.error());
    }
    if (connection_.changes() == 0) {
        // 读取与更新之间那行被删了：触发器不会响（new 都没有），索引得手动清。
        auto erase = connection_.prepare("DELETE FROM track_fts WHERE rowid = ?");
        if (!erase) {
            return Result<void>::fail(erase.error());
        }
        if (auto bound = erase.value().bind(1, idValue); !bound) {
            return Result<void>::fail(bound.error());
        }
        if (auto stepped = erase.value().step(); !stepped) {
            return Result<void>::fail(stepped.error());
        }
    }
    return Result<void>::ok();
}

Result<std::int64_t> SqliteTrackRepository::rebuildSearchIndex()
{
    // 不再先清空 fts：老库升级到 V5 时 search_body 全是空串，
    // 逐行 UPDATE 会把触发器当成"补写"，等于幂等重建；清空反而会在
    // 中途失败时留下"曲目在、索引空"的半截状态。
    return rebuildSearchIndexFromTracks();
}

Result<std::int64_t> SqliteTrackRepository::rebuildSearchIndexFromTracks()
{
    auto select = connection_.prepare(
        "SELECT t.id, t.title, ar.name, al.title FROM tracks t"
        " JOIN artists ar ON ar.id = t.artist_id"
        " JOIN albums  al ON al.id = t.album_id");
    if (!select) {
        return Result<std::int64_t>::fail(select.error());
    }
    auto update = connection_.prepare("UPDATE tracks SET search_body = ? WHERE id = ?");
    if (!update) {
        return Result<std::int64_t>::fail(update.error());
    }

    std::int64_t indexed = 0;
    for (;;) {
        auto row = select.value().step();
        if (!row) {
            return Result<std::int64_t>::fail(row.error());
        }
        if (!row.value()) {
            break;
        }
        const auto id = select.value().columnInt(0);
        const std::string body = searchBody(select.value().columnString(1),
                                            select.value().columnString(2),
                                            select.value().columnString(3));
        if (auto bound = update.value().bind(1, std::string_view{body}); !bound) {
            return Result<std::int64_t>::fail(bound.error());
        }
        if (auto bound = update.value().bind(2, id); !bound) {
            return Result<std::int64_t>::fail(bound.error());
        }
        if (auto stepped = update.value().step(); !stepped) {
            return Result<std::int64_t>::fail(stepped.error());
        }
        if (auto cleared = update.value().reset(); !cleared) {
            return Result<std::int64_t>::fail(cleared.error());
        }
        ++indexed;
    }
    return Result<std::int64_t>::ok(indexed);
}

}  // namespace sonora::catalog::adapters
