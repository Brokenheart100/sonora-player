// Playlist 聚合的持久化端口。
//
// 没有 findAll() 返回聚合：列表页要的是 DTO（名字 + 条目数），不该把每个
// 歌单的全部条目都读进来（AGENTS 第 10 条的原话是"仓储不提供 findAll()"，
// 这里给的是两条各司其职的读路径）。
#pragma once

#include <cstdint>
#include <vector>

#include <sonora/kernel/Result.h>

#include "sonora/playlists/contracts/Playlists.h"
#include "sonora/playlists/core/domain/Playlist.h"

namespace sonora::playlists::core {

class IPlaylistRepository {
public:
    virtual ~IPlaylistRepository() = default;

    // 写入一个新聚合（id 为 0），返回数据库分配的 id。
    [[nodiscard]] virtual kernel::Result<PlaylistId> insert(const Playlist &playlist) = 0;

    // 按 id 读回聚合（含全部条目）。没有 → NotFound。
    [[nodiscard]] virtual kernel::Result<Playlist> findById(PlaylistId id) const = 0;

    // 保存聚合：名字 + 全部条目（条目不多的场景下，整表重写比逐条 diff 稳）。
    [[nodiscard]] virtual kernel::Result<void> save(const Playlist &playlist) = 0;

    // 列表页的投影：只有名字、条目数、创建时间。
    [[nodiscard]] virtual kernel::Result<std::vector<contracts::PlaylistDto>> list() const = 0;

    // 删歌单（连同它的条目）。
    [[nodiscard]] virtual kernel::Result<void> remove(PlaylistId id) = 0;

    // 曲库删了一首歌：所有歌单里指向它的条目置为不可用。幂等。
    [[nodiscard]] virtual kernel::Result<std::int64_t> markTrackUnavailable(std::uint64_t trackId) = 0;
};

}  // namespace sonora::playlists::core
