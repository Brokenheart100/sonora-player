#include "mamba/catalog/adapters/SqliteTrackRepository.h"

#include <string>

#include "mamba/catalog/core/domain/Track.h"

namespace mamba::catalog::adapters {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;
using platform::storage::SqliteStatement;

Result<core::TrackId> SqliteTrackRepository::add(const core::Track &track)
{
    const auto &d = track.details();
    auto statement = connection_.prepare(
        "INSERT INTO tracks(title, artist_id, album_id, track_number, disc_number, duration_ms,"
        " file_path, file_size_bytes, added_at)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!statement) {
        return Result<core::TrackId>::fail(statement.error());
    }
    SqliteStatement &s = statement.value();

    const std::int64_t artistId = static_cast<std::int64_t>(d.artistId.value());
    const std::int64_t albumId = static_cast<std::int64_t>(d.albumId.value());

    const struct {
        int index;
        std::int64_t value;
    } numbers[] = {{2, artistId},   {3, albumId},  {4, d.trackNumber}, {5, d.discNumber},
                   {6, d.durationMs}, {8, d.fileSizeBytes}, {9, d.addedAtUnix}};
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

    if (auto stepped = s.step(); !stepped) {
        return Result<core::TrackId>::fail(stepped.error());
    }
    return Result<core::TrackId>::ok(
        core::TrackId::fromValue(static_cast<std::uint64_t>(connection_.lastInsertRowId())));
}

Result<core::Track> SqliteTrackRepository::findById(core::TrackId id) const
{
    auto statement = connection_.prepare(
        "SELECT title, artist_id, album_id, track_number, disc_number, duration_ms, file_path,"
        " file_size_bytes, added_at FROM tracks WHERE id = ?");
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

Result<std::int64_t> SqliteTrackRepository::count() const
{
    // queryInt 不给绑定参数，所以这里的 COUNT(*) 只能无条件——需要条件的计数走读侧投影。
    return connection_.queryInt("SELECT COUNT(*) FROM tracks");
}

}  // namespace mamba::catalog::adapters
