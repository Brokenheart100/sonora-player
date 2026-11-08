#include "sonora/preferences/adapters/SqliteSettingsStore.h"

#include <string>
#include <utility>
#include <vector>

namespace sonora::preferences::adapters {
namespace {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

// 升序读出来只是为了让"启动日志里的设置"和"用户看设置页"顺序一致，
// 没有语义上的要求。
constexpr std::string_view kSelectAll = "SELECT key, value FROM kv ORDER BY key";

// 一句 upsert：SQLite 的 ON CONFLICT 语法，3.24 起就有。
// 分开写 INSERT + UPDATE 会多一次往返，也会多一个"插了一半失败"的窗口。
constexpr std::string_view kUpsert =
    "INSERT INTO kv (key, value, updated_at) VALUES (?1, ?2, unixepoch()) "
    "ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = excluded.updated_at";

constexpr std::string_view kRemove = "DELETE FROM kv WHERE key = ?1";

}  // namespace

Result<std::vector<contracts::SettingValue>> SqliteSettingsStore::loadAll() const
{
    auto statement = connection_.prepare(kSelectAll);
    if (!statement) {
        return statement.error();
    }

    std::vector<contracts::SettingValue> entries;
    auto &stmt = statement.value();
    while (true) {
        auto row = stmt.step();
        if (!row) {
            return row.error();
        }
        if (!row.value()) {
            break;
        }
        contracts::SettingValue value;
        value.key = stmt.columnString(0);
        value.value = stmt.columnString(1);
        value.found = true;
        entries.push_back(std::move(value));
    }
    return Result<std::vector<contracts::SettingValue>>::ok(std::move(entries));
}

Result<void> SqliteSettingsStore::upsert(std::string_view key, std::string_view value)
{
    auto statement = connection_.prepare(kUpsert);
    if (!statement) {
        return statement.error();
    }

    auto &stmt = statement.value();
    if (auto bound = stmt.bind(1, key); !bound) {
        return bound.error();
    }
    if (auto bound = stmt.bind(2, value); !bound) {
        return bound.error();
    }
    auto row = stmt.step();
    if (!row) {
        return row.error();
    }
    return {};
}

Result<void> SqliteSettingsStore::remove(std::string_view key)
{
    auto statement = connection_.prepare(kRemove);
    if (!statement) {
        return statement.error();
    }

    auto &stmt = statement.value();
    if (auto bound = stmt.bind(1, key); !bound) {
        return bound.error();
    }
    auto row = stmt.step();
    if (!row) {
        return row.error();
    }
    // 删一个不存在的键：changes() == 0，但对调用方来说语义一样——"现在它不在了"。
    return {};
}

}  // namespace sonora::preferences::adapters
