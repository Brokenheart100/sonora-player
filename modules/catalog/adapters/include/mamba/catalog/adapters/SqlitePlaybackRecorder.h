// 把"放完一次"记进曲库：play_count + 1、last_played_at 更新。
//
// 它是 TrackFinished 事件的订阅者。为什么记在 Catalog 而不是 Playback：
// 播放次数是曲目的事实，不是播放器会话的事实；播放器关了它还得在。
#pragma once

#include <cstdint>

#include "mamba/kernel/Result.h"
#include "mamba/platform/storage/SqliteConnection.h"

namespace mamba::catalog::adapters {

class SqlitePlaybackRecorder {
public:
    explicit SqlitePlaybackRecorder(platform::storage::SqliteConnection& connection)
        : connection_(connection) {}

    /// 给某首歌记一次播放。曲目不存在返回 NotFound（不报错也不崩：
    /// 库可能刚被清空，而事件是在清空之前发的）。
    [[nodiscard]] kernel::Result<void> record(std::uint64_t trackId,
                                              std::int64_t playedAtUnix);

private:
    platform::storage::SqliteConnection& connection_;
};

} // namespace mamba::catalog::adapters
