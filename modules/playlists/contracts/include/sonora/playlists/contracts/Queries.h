// Playlists 的查询（读侧意图）。
#pragma once

#include <cstdint>
#include <vector>

#include "sonora/playlists/contracts/Playlists.h"

namespace sonora::playlists::contracts {

// 全部歌单，按创建时间升序。
struct ListPlaylists {};

// 一个歌单的条目（按 pos 升序）。
struct GetPlaylistTracks {
    std::int64_t playlistId = 0;
};

// 查询的返回值直接是 vector：歌单的数量与条目数都是"人能管理的量级"，
// 分页在这里只会让调用方多写两层循环。
using PlaylistList = std::vector<PlaylistDto>;
using PlaylistTrackList = std::vector<PlaylistEntryDto>;

}  // namespace sonora::playlists::contracts
