#include "sonora/catalog/adapters/SqlitePlaybackRecorder.h"

#include <string_view>

#include "sonora/kernel/Error.h"
#include "sonora/platform/storage/SqliteError.h"
#include "sonora/platform/storage/SqliteStatement.h"

namespace sonora::catalog::adapters {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;
using platform::storage::SqliteStatement;

Result<void> SqlitePlaybackRecorder::record(std::uint64_t trackId,
                                            std::int64_t playedAtUnix) {
    auto statement =
        connection_.prepare("UPDATE tracks SET play_count = play_count + 1,"
                            " last_played_at = ? WHERE id = ?");
    if (!statement) {
        return Result<void>::fail(statement.error());
    }
    SqliteStatement& s = statement.value();

    if (auto bound = s.bind(1, playedAtUnix); !bound) {
        return Result<void>::fail(bound.error());
    }
    if (auto bound = s.bind(2, static_cast<std::int64_t>(trackId)); !bound) {
        return Result<void>::fail(bound.error());
    }
    if (auto stepped = s.step(); !stepped) {
        return Result<void>::fail(stepped.error());
    }
    if (connection_.changes() == 0) {
        // 曲目不存在不是"系统坏了"：边放边删是允许的，播放器不该因此停下。
        return Result<void>::fail(Error{ErrorCode::NotFound, "曲目不存在，播放次数没记上"});
    }
    return Result<void>::ok();
}

} // namespace sonora::catalog::adapters
