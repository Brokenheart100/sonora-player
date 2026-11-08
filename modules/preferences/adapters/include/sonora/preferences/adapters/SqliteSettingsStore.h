// 键值存储的 SQLite 实现。
//
// 只有三条语句：全量读、插入或更新、删除。故意不做"只读某几个键"——
// 设置的总量是几十行，全量读一次比维护一套按键查询的接口便宜。
#pragma once

#include <string_view>
#include <vector>

#include <sonora/kernel/Result.h>
#include <sonora/platform/storage/SqliteConnection.h>

#include "sonora/preferences/contracts/Settings.h"
#include "sonora/preferences/core/ports/SettingsStore.h"

namespace sonora::preferences::adapters {

class SqliteSettingsStore final : public core::ISettingsStore {
public:
    explicit SqliteSettingsStore(platform::storage::SqliteConnection &connection)
        : connection_(connection)
    {
    }

    [[nodiscard]] kernel::Result<std::vector<contracts::SettingValue>> loadAll() const override;
    [[nodiscard]] kernel::Result<void> upsert(std::string_view key, std::string_view value) override;
    [[nodiscard]] kernel::Result<void> remove(std::string_view key) override;

private:
    platform::storage::SqliteConnection &connection_;
};

}  // namespace sonora::preferences::adapters
