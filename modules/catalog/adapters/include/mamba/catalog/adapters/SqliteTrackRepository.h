// 曲目聚合的持久化。
//
// 落库的每一列都来自 TrackDetails，读回来时**重新过一遍** Track::rehydrate 的校验。
// "数据是我们自己写的所以不用校验"这个理由，会在第一次手改库、第一次格式迁移之后
// 立刻失效；而那时离崩溃已经过去很久了，没人会想到是这里。
#pragma once

#include <cstdint>
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
    [[nodiscard]] kernel::Result<core::Track> findById(core::TrackId id) const override;
    [[nodiscard]] kernel::Result<bool> existsByPath(std::string_view filePath) const override;
    [[nodiscard]] kernel::Result<std::int64_t> count() const override;

private:
    platform::storage::SqliteConnection &connection_;
};

}  // namespace mamba::catalog::adapters
