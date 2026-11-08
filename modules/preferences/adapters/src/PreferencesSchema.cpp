#include "sonora/preferences/adapters/PreferencesSchema.h"

namespace sonora::preferences::adapters {
namespace {

using platform::storage::Migration;

// 一张表就够。key 是主键（TEXT，比较按二进制，正好让 "playback/volume"
// 这种带斜杠的命名不受 locale 影响）。
//
// updated_at 用 unixepoch()（SQLite 3.38+）：时间戳的时区/格式由库决定，
// 比在 C++ 里传一个"我们的秒数"更不容易出现两套时间的偏差。
constexpr std::string_view kSchemaV1 = R"sql(
CREATE TABLE kv (
    key        TEXT PRIMARY KEY,
    value      TEXT NOT NULL,
    updated_at INTEGER NOT NULL DEFAULT 0
);
)sql";

constexpr Migration kMigrations[] = {
    Migration{1, "创建 kv 表", kSchemaV1},
};

}  // namespace

std::span<const Migration> migrations() { return kMigrations; }

}  // namespace sonora::preferences::adapters
