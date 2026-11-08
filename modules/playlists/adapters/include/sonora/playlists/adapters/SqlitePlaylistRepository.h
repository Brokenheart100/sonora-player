// 歌单聚合的 SQLite 实现。
//
// 事务边界在仓储里，而不是在 handler 里：一次 save 要删掉旧条目再按新顺序插入，
// 中间那半秒如果被别人读到，用户会看到"点一下歌单空了"。handler 只管校验与转调
// （AGENTS 第 6 条的"薄壳"），它没有资格替仓储决定几条语句该在同一个事务里。
#pragma once

#include <cstdint>
#include <vector>

#include <sonora/kernel/Result.h>
#include <sonora/platform/storage/SqliteConnection.h>

#include "sonora/playlists/contracts/Playlists.h"
#include "sonora/playlists/core/domain/Playlist.h"
#include "sonora/playlists/core/ports/PlaylistRepository.h"

namespace sonora::playlists::adapters {

class SqlitePlaylistRepository final : public core::IPlaylistRepository {
public:
    explicit SqlitePlaylistRepository(platform::storage::SqliteConnection &connection)
        : connection_(connection)
    {
    }

    [[nodiscard]] kernel::Result<core::PlaylistId> insert(const core::Playlist &playlist) override;
    [[nodiscard]] kernel::Result<core::Playlist> findById(core::PlaylistId id) const override;
    [[nodiscard]] kernel::Result<void> save(const core::Playlist &playlist) override;
    [[nodiscard]] kernel::Result<std::vector<contracts::PlaylistDto>> list() const override;
    [[nodiscard]] kernel::Result<void> remove(core::PlaylistId id) override;
    [[nodiscard]] kernel::Result<std::int64_t> markTrackUnavailable(std::uint64_t trackId) override;

private:
    platform::storage::SqliteConnection &connection_;
};

}  // namespace sonora::playlists::adapters
