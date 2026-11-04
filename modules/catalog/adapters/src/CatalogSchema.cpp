#include "mamba/catalog/adapters/CatalogSchema.h"

#include <array>

namespace mamba::catalog::adapters {
namespace {

using platform::storage::Migration;

// 三个聚合根三张表 + 一个列表投影视图。
//
// 为什么列表要视图而不是每次手写 join：M5 的物化投影表直接替换它就行，
// 读侧代码（SqliteTrackListReader）只改一行 SQL 名字，DTO 不用动。
//
// 注意 SQLITE_DQS=0：字符串字面量只能写单引号，双引号一律当标识符。
// 这是好事——写错引号在开发期就报错，而不是在用户库里变成"列名不存在"。
constexpr std::string_view kSchemaV1 = R"sql(
CREATE TABLE artists (
    id   INTEGER PRIMARY KEY,
    name TEXT    NOT NULL UNIQUE
);

CREATE TABLE albums (
    id    INTEGER PRIMARY KEY,
    title TEXT    NOT NULL UNIQUE
);

CREATE TABLE tracks (
    id              INTEGER PRIMARY KEY,
    title           TEXT    NOT NULL,
    artist_id       INTEGER NOT NULL REFERENCES artists(id),
    album_id        INTEGER NOT NULL REFERENCES albums(id),
    track_number    INTEGER NOT NULL DEFAULT 0,
    disc_number     INTEGER NOT NULL DEFAULT 0,
    duration_ms     INTEGER NOT NULL DEFAULT 0,
    file_path       TEXT    NOT NULL UNIQUE,
    file_size_bytes INTEGER NOT NULL DEFAULT 0,
    added_at        INTEGER NOT NULL DEFAULT 0
);

-- UNIQUE(file_path) 自带索引，是导入去重的主路径（每次导入每个文件一次查询）。
CREATE INDEX idx_tracks_artist ON tracks(artist_id);
CREATE INDEX idx_tracks_album  ON tracks(album_id);
CREATE INDEX idx_tracks_added  ON tracks(added_at DESC);

CREATE VIEW v_track_list AS
SELECT t.id          AS id,
       t.title       AS title,
       ar.name       AS artist_name,
       al.title      AS album_title,
       t.duration_ms AS duration_ms,
       t.file_path   AS file_path
FROM tracks t
JOIN artists ar ON ar.id = t.artist_id
JOIN albums  al ON al.id = t.album_id;
)sql";

constexpr std::array<Migration, 1> kMigrations{{
    {1, "曲目/艺术家/专辑三表 + 列表投影视图", kSchemaV1},
}};

}  // namespace

std::span<const Migration> migrations()
{
    return kMigrations;
}

}  // namespace mamba::catalog::adapters
