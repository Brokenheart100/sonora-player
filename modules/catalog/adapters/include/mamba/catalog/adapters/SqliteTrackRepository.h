// 曲目聚合的持久化。
//
// 落库的每一列都来自 TrackDetails，读回来时**重新过一遍** Track::rehydrate 的校验。
// "数据是我们自己写的所以不用校验"这个理由，会在第一次手改库、第一次格式迁移之后
// 立刻失效；而那时离崩溃已经过去很久了，没人会想到是这里。
//
// 另外它顺手维护全文索引（track_fts）：写曲目和写索引必须是同一次写入的两个动作，
// 分成两个地方做，迟早会出现"列表里有、搜不到"的幽灵行。
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "mamba/catalog/core/ports/Ports.h"
#include "mamba/platform/storage/SqliteConnection.h"

namespace mamba::catalog::adapters {

class SqliteTrackRepository final : public core::ITrackRepository {
public:
    explicit SqliteTrackRepository(platform::storage::SqliteConnection &connection)
        : connection_(connection)
    {
    }

    [[nodiscard]] kernel::Result<core::TrackId> add(const core::Track &track) override;
    [[nodiscard]] kernel::Result<void> save(const core::Track &track) override;
    [[nodiscard]] kernel::Result<core::Track> findById(core::TrackId id) const override;
    [[nodiscard]] kernel::Result<bool> existsByPath(std::string_view filePath) const override;
    [[nodiscard]] kernel::Result<std::optional<core::TrackFileState>> findFileState(
        std::string_view filePath) const override;
    [[nodiscard]] kernel::Result<std::int64_t> count() const override;

    // 把全文索引与 tracks 表对齐。返回重建的行数。
    //
    // 为什么需要它：索引体要用 C++ 算（汉字逐字拆开），SQL 表达不了，
    // 所以迁移脚本只能建表、没法回填历史行。启动时发现两边数量不一致就在这里补。
    [[nodiscard]] kernel::Result<std::int64_t> rebuildSearchIndex();

private:
    // 单行的索引体重算（新增/更新后调用）。
    [[nodiscard]] kernel::Result<void> refreshSearchRow(core::TrackId id);
    // 按 SQL 结果重建一批索引行。rebuildSearchIndex 的实现在这里。
    [[nodiscard]] kernel::Result<std::int64_t> rebuildSearchIndexFromTracks();

    platform::storage::SqliteConnection &connection_;
};

}  // namespace mamba::catalog::adapters
