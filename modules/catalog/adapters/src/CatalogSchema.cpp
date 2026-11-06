#include "sonora/catalog/adapters/CatalogSchema.h"

#include <array>

namespace sonora::catalog::adapters {
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

// M5：增量扫描要判"文件变没变"，搜索要从 LIKE 换成全文索引。
//
// file_mtime 必须落库：判断一个文件变没变，最便宜的办法是比 mtime + size，
// 但这两样都只在扫描那一次拿得到，不存下来下次就得重新 stat 一万个文件。
//
// track_fts 是独立的 fts5 表（不是 external content）：body 列里存的是
// searchBody() 处理过的"汉字之间带空格"的文本，SQL 造不出来，只能由 C++ 写。
// 因此迁移只建表、不回填——历史行由 SqliteTrackRepository::rebuildSearchIndex()
// 在启动时补齐（见 host/main.cpp）。
//
// tokenize 显式写 unicode61 而不是用默认值：默认也是 unicode61，但默认参数将来
// 可能变；remove_diacritics 0 是为了别把带声调的拉丁字母折叠掉。
constexpr std::string_view kSchemaV2 = R"sql(
ALTER TABLE tracks ADD COLUMN file_mtime INTEGER NOT NULL DEFAULT 0;
ALTER TABLE tracks ADD COLUMN cover_key  TEXT    NOT NULL DEFAULT '';

CREATE VIRTUAL TABLE track_fts USING fts5(
    body,
    tokenize = 'unicode61 remove_diacritics 0'
);
)sql";

// M5 后期：封面键进投影。
//
// 视图不是表，改它的代价是一次 DROP + CREATE（几千行也是瞬间，因为不搬数据）。
// 之所以改视图而不是让读侧再 join 一次 tracks：读侧那句 SQL 是列表页的唯一入口，
// 多一个 join 就多一个将来会被忘记同步的地方。
constexpr std::string_view kSchemaV3 = R"sql(
DROP VIEW v_track_list;

CREATE VIEW v_track_list AS
SELECT t.id          AS id,
       t.title       AS title,
       ar.name       AS artist_name,
       al.title      AS album_title,
       t.duration_ms AS duration_ms,
       t.file_path   AS file_path,
       t.cover_key   AS cover_key
FROM tracks t
JOIN artists ar ON ar.id = t.artist_id
JOIN albums  al ON al.id = t.album_id;
)sql";

constexpr std::string_view kSchemaV4 = R"sql(
ALTER TABLE tracks ADD COLUMN play_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE tracks ADD COLUMN last_played_at INTEGER NOT NULL DEFAULT 0;
)sql";

constexpr std::array<Migration, 4> kMigrations{{
    {1, "曲目/艺术家/专辑三表 + 列表投影视图", kSchemaV1},
    {2, "增量扫描列 + 全文索引表", kSchemaV2},
    {3, "列表投影视图加封面键", kSchemaV3},
    {4, "播放次数与最近播放时间（由 TrackFinished 事件累计）", kSchemaV4},
}};

}  // namespace

std::span<const Migration> migrations()
{
    return kMigrations;
}

}  // namespace sonora::catalog::adapters
