// Playlists 的命令（写侧意图）。
//
// 每条命令对应聚合上的一个动作，而不是"一次 SQL 更新"：
// 加一首歌要检查重复、要排在末尾；移动要重排 pos。这些规则住在聚合里。
#pragma once

#include <cstdint>
#include <string>

namespace sonora::playlists::contracts {

// 新建歌单。名字为空 → InvalidArgument；重名**允许**（用户可以有两个"路上听"）。
struct CreatePlaylist {
    std::string name;
};

struct RenamePlaylist {
    std::int64_t playlistId = 0;
    std::string name;
};

struct DeletePlaylist {
    std::int64_t playlistId = 0;
};

// 加一首曲目（trackId 来自曲库）。已经在里面 → Conflict，不重复加。
struct AddTrackToPlaylist {
    std::int64_t playlistId = 0;
    std::uint64_t trackId = 0;
};

// 移除一条。不在里面 → NotFound。
struct RemoveTrackFromPlaylist {
    std::int64_t playlistId = 0;
    std::uint64_t trackId = 0;
};

// 把条目挪到 toIndex（按当前顺序的下标，移动前的位置）。越界会被夹到 [0, size-1]。
struct MovePlaylistTrack {
    std::int64_t playlistId = 0;
    std::uint64_t trackId = 0;
    std::int32_t toIndex = 0;
};

// 曲库发出 TrackRemoved 时由组合根翻译成这条命令：条目不删、只标不可用。
struct MarkTrackUnavailable {
    std::uint64_t trackId = 0;
};

}  // namespace sonora::playlists::contracts
