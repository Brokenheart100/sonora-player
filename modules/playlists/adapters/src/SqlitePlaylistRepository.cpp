#include "sonora/playlists/adapters/SqlitePlaylistRepository.h"

#include <string>
#include <utility>
#include <vector>

#include <sonora/kernel/IUnitOfWork.h>
#include <sonora/platform/storage/SqliteStatement.h>
#include <sonora/platform/storage/SqliteUnitOfWork.h>

namespace sonora::playlists::adapters {
namespace {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;
using platform::storage::SqliteStatement;

constexpr std::string_view kInsertPlaylist =
    "INSERT INTO playlist (name, created_at) VALUES (?1, ?2)";
constexpr std::string_view kSelectPlaylist = "SELECT name, created_at FROM playlist WHERE id = ?1";
constexpr std::string_view kSelectItems =
    "SELECT track_id, available FROM playlist_item WHERE playlist_id = ?1 ORDER BY pos";
constexpr std::string_view kUpdateName = "UPDATE playlist SET name = ?2 WHERE id = ?1";
constexpr std::string_view kDeleteItems = "DELETE FROM playlist_item WHERE playlist_id = ?1";
constexpr std::string_view kInsertItem =
    "INSERT INTO playlist_item (playlist_id, track_id, pos, available) VALUES (?1, ?2, ?3, ?4)";
constexpr std::string_view kDeletePlaylist = "DELETE FROM playlist WHERE id = ?1";
constexpr std::string_view kMarkUnavailable =
    "UPDATE playlist_item SET available = 0 WHERE track_id = ?1 AND available = 1";

// 列表页的投影：条目数是 COUNT，不是把条目读出来数一遍。
// 子查询而不是直接 GROUP BY：歌单可以一条都没有，LEFT JOIN 才留得住它们。
constexpr std::string_view kSelectList = R"sql(
SELECT p.id, p.name, p.created_at, COALESCE(c.n, 0)
  FROM playlist p
  LEFT JOIN (SELECT playlist_id, COUNT(*) AS n FROM playlist_item GROUP BY playlist_id) c
         ON c.playlist_id = p.id
 ORDER BY p.created_at, p.id
)sql";

}  // namespace

Result<core::PlaylistId> SqlitePlaylistRepository::insert(const core::Playlist &playlist)
{
    platform::storage::SqliteUnitOfWork unit{connection_};
    kernel::UnitOfWorkScope scope{unit};
    if (auto begun = scope.begin(); !begun) {
        return begun.error();
    }

    auto statement = connection_.prepare(kInsertPlaylist);
    if (!statement) {
        return statement.error();
    }
    auto &stmt = statement.value();
    if (auto bound = stmt.bind(1, std::string_view{playlist.name()}); !bound) {
        return bound.error();
    }
    if (auto bound = stmt.bind(2, playlist.createdAt()); !bound) {
        return bound.error();
    }
    if (auto row = stmt.step(); !row) {
        return row.error();
    }

    const auto id = core::PlaylistId::fromValue(connection_.lastInsertRowId());
    if (auto committed = scope.commit(); !committed) {
        return committed.error();
    }
    return Result<core::PlaylistId>::ok(id);
}

Result<core::Playlist> SqlitePlaylistRepository::findById(core::PlaylistId id) const
{
    auto head = connection_.prepare(kSelectPlaylist);
    if (!head) {
        return head.error();
    }
    auto &stmt = head.value();
    if (auto bound = stmt.bind(1, id.value()); !bound) {
        return bound.error();
    }
    auto row = stmt.step();
    if (!row) {
        return row.error();
    }
    if (!row.value()) {
        return Error{ErrorCode::NotFound, "没有这个歌单"};
    }
    std::string name = stmt.columnString(0);
    const auto createdAt = stmt.columnInt(1);

    auto itemsStmt = connection_.prepare(kSelectItems);
    if (!itemsStmt) {
        return itemsStmt.error();
    }
    auto &items = itemsStmt.value();
    if (auto bound = items.bind(1, id.value()); !bound) {
        return bound.error();
    }

    std::vector<core::Playlist::Item> entries;
    while (true) {
        auto item = items.step();
        if (!item) {
            return item.error();
        }
        if (!item.value()) {
            break;
        }
        core::Playlist::Item entry;
        entry.trackId = static_cast<std::uint64_t>(items.columnInt(0));
        entry.available = items.columnInt(1) != 0;
        entries.push_back(entry);
    }

    return core::Playlist::rehydrate(id, std::move(name), createdAt, std::move(entries));
}

Result<void> SqlitePlaylistRepository::save(const core::Playlist &playlist)
{
    if (!playlist.id().isValid()) {
        return Error{ErrorCode::Internal, "保存一个还没有 id 的歌单（应先 insert）"};
    }

    platform::storage::SqliteUnitOfWork unit{connection_};
    kernel::UnitOfWorkScope scope{unit};
    if (auto begun = scope.begin(); !begun) {
        return begun.error();
    }

    auto rename = connection_.prepare(kUpdateName);
    if (!rename) {
        return rename.error();
    }
    auto &stmt = rename.value();
    if (auto bound = stmt.bind(1, playlist.id().value()); !bound) {
        return bound.error();
    }
    if (auto bound = stmt.bind(2, std::string_view{playlist.name()}); !bound) {
        return bound.error();
    }
    if (auto row = stmt.step(); !row) {
        return row.error();
    }
    if (connection_.changes() == 0) {
        return Error{ErrorCode::NotFound, "这个歌单已经不在了"};
    }

    // 先删光再插：见 PlaylistsSchema.cpp 里关于唯一 pos 索引的说明。
    // 这里**不能**用 connection_.exec()：exec 不绑定参数，?1 会落成 NULL，
    // DELETE 一行也删不掉、SQLite 还不报错，随后按新顺序插入就撞唯一索引。
    auto wipe = connection_.prepare(kDeleteItems);
    if (!wipe) {
        return wipe.error();
    }
    auto &wipeStmt = wipe.value();
    if (auto bound = wipeStmt.bind(1, playlist.id().value()); !bound) {
        return bound.error();
    }
    if (auto wiped = wipeStmt.step(); !wiped) {
        return wiped.error();
    }

    auto insert = connection_.prepare(kInsertItem);
    if (!insert) {
        return insert.error();
    }
    auto &itemStmt = insert.value();
    for (std::size_t index = 0; index < playlist.items().size(); ++index) {
        const auto &item = playlist.items()[index];
        if (auto reset = itemStmt.reset(); !reset) {
            return reset.error();
        }
        if (auto bound = itemStmt.bind(1, playlist.id().value()); !bound) {
            return bound.error();
        }
        if (auto bound = itemStmt.bind(2, static_cast<std::int64_t>(item.trackId)); !bound) {
            return bound.error();
        }
        if (auto bound = itemStmt.bind(3, static_cast<std::int64_t>(index)); !bound) {
            return bound.error();
        }
        if (auto bound = itemStmt.bind(4, static_cast<std::int64_t>(item.available ? 1 : 0));
            !bound) {
            return bound.error();
        }
        if (auto row = itemStmt.step(); !row) {
            return row.error();
        }
    }

    if (auto committed = scope.commit(); !committed) {
        return committed.error();
    }
    return {};
}

Result<std::vector<contracts::PlaylistDto>> SqlitePlaylistRepository::list() const
{
    auto statement = connection_.prepare(kSelectList);
    if (!statement) {
        return statement.error();
    }

    std::vector<contracts::PlaylistDto> rows;
    auto &stmt = statement.value();
    while (true) {
        auto row = stmt.step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            break;
        }
        contracts::PlaylistDto dto;
        dto.playlistId = stmt.columnInt(0);
        dto.name = stmt.columnString(1);
        dto.createdAt = stmt.columnInt(2);
        dto.trackCount = static_cast<std::int32_t>(stmt.columnInt(3));
        rows.push_back(std::move(dto));
    }
    return Result<std::vector<contracts::PlaylistDto>>::ok(std::move(rows));
}

Result<void> SqlitePlaylistRepository::remove(core::PlaylistId id)
{
    platform::storage::SqliteUnitOfWork unit{connection_};
    kernel::UnitOfWorkScope scope{unit};
    if (auto begun = scope.begin(); !begun) {
        return begun.error();
    }

    auto items = connection_.prepare(kDeleteItems);
    if (!items) {
        return items.error();
    }
    if (auto bound = items.value().bind(1, id.value()); !bound) {
        return bound.error();
    }
    if (auto row = items.value().step(); !row) {
        return row.error();
    }

    auto head = connection_.prepare(kDeletePlaylist);
    if (!head) {
        return head.error();
    }
    if (auto bound = head.value().bind(1, id.value()); !bound) {
        return bound.error();
    }
    if (auto row = head.value().step(); !row) {
        return row.error();
    }
    if (connection_.changes() == 0) {
        return Error{ErrorCode::NotFound, "没有这个歌单"};
    }

    if (auto committed = scope.commit(); !committed) {
        return committed.error();
    }
    return {};
}

Result<std::int64_t> SqlitePlaylistRepository::markTrackUnavailable(std::uint64_t trackId)
{
    auto statement = connection_.prepare(kMarkUnavailable);
    if (!statement) {
        return statement.error();
    }
    auto &stmt = statement.value();
    if (auto bound = stmt.bind(1, static_cast<std::int64_t>(trackId)); !bound) {
        return bound.error();
    }
    if (auto row = stmt.step(); !row) {
        return row.error();
    }
    return Result<std::int64_t>::ok(static_cast<std::int64_t>(connection_.changes()));
}

}  // namespace sonora::playlists::adapters
