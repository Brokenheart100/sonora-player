#include "sonora/platform/storage/Migrator.h"

#include <chrono>
#include <ctime>

#include "sonora/platform/storage/SqliteError.h"
#include "sonora/platform/storage/SqliteStatement.h"
#include "sonora/platform/storage/SqliteTransaction.h"

namespace sonora::platform::storage {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

namespace {

std::int64_t nowSeconds(const kernel::IClock *clock)
{
    if (clock != nullptr) {
        return std::chrono::duration_cast<std::chrono::seconds>(clock->now().time_since_epoch())
            .count();
    }
    return static_cast<std::int64_t>(std::time(nullptr));
}

}  // namespace

Migrator::Migrator(SqliteConnection &connection, std::string_view module, const kernel::IClock *clock)
    : connection_(connection), module_(module), clock_(clock)
{
}

Result<void> Migrator::ensureVersionTable()
{
    // IF NOT EXISTS：每个模块都会调一次，重复建表必须无害。
    return connection_.exec(
        "CREATE TABLE IF NOT EXISTS __module_version("
        "  module     TEXT PRIMARY KEY,"
        "  version    INTEGER NOT NULL,"
        "  applied_at INTEGER NOT NULL"
        ");");
}

Result<int> Migrator::currentVersion()
{
    if (auto result = ensureVersionTable(); !result) {
        return Result<int>::fail(result.error());
    }

    auto prepared = connection_.prepare("SELECT version FROM __module_version WHERE module = ?1;");
    if (!prepared) {
        return Result<int>::fail(prepared.error());
    }

    auto &statement = prepared.value();
    if (auto bound = statement.bind(1, module_); !bound) {
        return Result<int>::fail(bound.error());
    }

    auto row = statement.step();
    if (!row) {
        return Result<int>::fail(row.error());
    }
    if (!row.value()) {
        return Result<int>::ok(0);  // 新库：还没跑过任何迁移
    }
    return Result<int>::ok(static_cast<int>(statement.columnInt(0)));
}

Result<void> Migrator::setVersion(int version)
{
    auto prepared = connection_.prepare(
        "INSERT INTO __module_version(module, version, applied_at) VALUES(?1, ?2, ?3) "
        "ON CONFLICT(module) DO UPDATE SET version = excluded.version, "
        "                                applied_at = excluded.applied_at;");
    if (!prepared) {
        return Result<void>::fail(prepared.error());
    }

    auto &statement = prepared.value();
    if (auto bound = statement.bind(1, module_); !bound) {
        return bound;
    }
    if (auto bound = statement.bind(2, static_cast<std::int64_t>(version)); !bound) {
        return bound;
    }
    if (auto bound = statement.bind(3, nowSeconds(clock_)); !bound) {
        return bound;
    }

    auto row = statement.step();
    if (!row) {
        return Result<void>::fail(row.error());
    }
    return Result<void>::ok();
}

Result<int> Migrator::apply(std::span<const Migration> migrations)
{
    auto started = currentVersion();
    if (!started) {
        return Result<int>::fail(started.error());
    }

    int version = started.value();

    // 先校验顺序：跳号或倒退的迁移表是编码错误，不该等到执行一半才发现。
    int previous = version;
    for (const auto &migration : migrations) {
        if (migration.version <= previous && migration.version != version + 1) {
            if (migration.version <= version) {
                previous = migration.version;
                continue;  // 已经跑过的老条目，跳过
            }
            return Result<int>::fail(
                Error{ErrorCode::InvalidArgument, "迁移版本号必须连续递增（1、2、3…）"});
        }
        previous = migration.version;
    }

    // 整体一个事务：要么全部到位，要么库还是原样。
    SqliteTransaction transaction(connection_);
    if (!transaction.active()) {
        return Result<int>::fail(
            Error{ErrorCode::Unavailable, "无法开启迁移事务（库被占用？）"});
    }

    for (const auto &migration : migrations) {
        if (migration.version <= version) {
            continue;
        }

        if (auto result = connection_.exec(migration.sql); !result) {
            transaction.rollback();
            return Result<int>::fail(result.error());
        }
        if (auto result = setVersion(migration.version); !result) {
            transaction.rollback();
            return Result<int>::fail(result.error());
        }
        version = migration.version;
    }

    if (auto committed = transaction.commit(); !committed) {
        return Result<int>::fail(committed.error());
    }
    return Result<int>::ok(version);
}

}  // namespace sonora::platform::storage
