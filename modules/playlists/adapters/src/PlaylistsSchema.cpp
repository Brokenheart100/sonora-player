#include "sonora/playlists/adapters/PlaylistsSchema.h"

namespace sonora::playlists::adapters {
namespace {

using platform::storage::Migration;

// (playlist_id, track_id) 做主键：同一首歌在一个歌单里只能有一个位置。
// (playlist_id, pos) 唯一：顺序不能有两个 3。
//
// 关于唯一 pos 的一条实现约束：保存时**先删光再按新顺序插**。逐条 UPDATE pos
// 会在中途撞唯一索引（把第 1 条挪到第 2 条的位置那一下，两条都是 2）。
// 这不是取巧：条目数量是人能管理的量级，全量重写的代价可以忽略。
constexpr std::string_view kSchemaV1 = R"sql(
CREATE TABLE playlist (
    id         INTEGER PRIMARY KEY,
    name       TEXT    NOT NULL,
    created_at INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE playlist_item (
    playlist_id INTEGER NOT NULL,
    track_id    INTEGER NOT NULL,
    pos         INTEGER NOT NULL,
    available   INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY (playlist_id, track_id)
);

CREATE UNIQUE INDEX idx_playlist_item_pos ON playlist_item(playlist_id, pos);

CREATE INDEX idx_playlist_item_track ON playlist_item(track_id);
)sql";

constexpr Migration kMigrations[] = {
    Migration{1, "创建 playlist 与 playlist_item", kSchemaV1},
};

}  // namespace

std::span<const Migration> migrations() { return kMigrations; }

}  // namespace sonora::playlists::adapters
