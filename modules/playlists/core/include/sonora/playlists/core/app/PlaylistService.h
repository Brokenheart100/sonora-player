// 歌单的应用服务。
//
// 六个命令对应六个"取聚合 → 改 → 存"的动作。没有缓存：歌单的读取频率与
// 曲库不是一个量级（用户不会在歌单页里滚动五千行），每次读库反而少一套
// 失效逻辑。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sonora/kernel/IClock.h>
#include <sonora/kernel/Result.h>

#include "sonora/playlists/contracts/Playlists.h"
#include "sonora/playlists/core/domain/Playlist.h"
#include "sonora/playlists/core/ports/PlaylistRepository.h"

namespace sonora::playlists::core {

class PlaylistService {
public:
    // clock 可以为空：测试里不需要真实时间，为空时用系统时钟。
    explicit PlaylistService(IPlaylistRepository &repository, const kernel::IClock *clock = nullptr);

    PlaylistService(const PlaylistService &) = delete;
    PlaylistService &operator=(const PlaylistService &) = delete;

    [[nodiscard]] kernel::Result<PlaylistId> create(std::string name);
    [[nodiscard]] kernel::Result<void> rename(PlaylistId id, std::string name);
    [[nodiscard]] kernel::Result<void> remove(PlaylistId id);

    [[nodiscard]] kernel::Result<void> addTrack(PlaylistId id, std::uint64_t trackId);
    [[nodiscard]] kernel::Result<void> removeTrack(PlaylistId id, std::uint64_t trackId);
    [[nodiscard]] kernel::Result<void> moveTrack(PlaylistId id, std::uint64_t trackId,
                                                 std::int32_t toIndex);

    // 曲库删了歌：所有歌单里指向它的条目置为不可用。返回受影响的条目数（给日志用）。
    [[nodiscard]] kernel::Result<std::int64_t> markTrackUnavailable(std::uint64_t trackId);

    [[nodiscard]] kernel::Result<std::vector<contracts::PlaylistDto>> list() const;
    [[nodiscard]] kernel::Result<std::vector<contracts::PlaylistEntryDto>> tracks(PlaylistId id) const;

private:
    [[nodiscard]] std::int64_t nowUnix() const;

    IPlaylistRepository &repository_;
    const kernel::IClock *clock_ = nullptr;
};

}  // namespace sonora::playlists::core
