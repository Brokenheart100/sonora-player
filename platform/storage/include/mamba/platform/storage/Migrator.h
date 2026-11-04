// 按模块独立的迁移执行器。
//
// 为什么不是一张全局 __schema_version：十个模块共用一个版本号，任何一个模块
// 加一列都得让所有模块的迁移脚本重新编号，冲突必然发生。这里每个模块一张
// 记录行，各自从 1 数起，互不干扰：
//
//     CREATE TABLE __module_version(
//         module     TEXT PRIMARY KEY,
//         version    INTEGER NOT NULL,
//         applied_at INTEGER NOT NULL      -- unix 秒，够用且无时区歧义
//     );
//
// 迁移脚本只许**追加**，不许改历史条目：已经跑过的版本再改，用户库和开发库
// 就永久性地不一致了，而且没有任何自动手段能发现。
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "mamba/kernel/IClock.h"
#include "mamba/kernel/Result.h"
#include "mamba/platform/storage/SqliteConnection.h"

namespace mamba::platform::storage {

struct Migration {
    int version = 0;
    std::string_view description;
    // 可以是一条，也可以是多条语句（sqlite3_exec 支持分号分隔）。
    // 注意：不用绑定参数的 DDL 就别想放参数了。
    std::string_view sql;
};

class Migrator {
public:
    // clock 可为空：M1 阶段没人需要可注入的时间。等出现"回放迁移"这种需求再加。
    Migrator(SqliteConnection &connection, std::string_view module, const kernel::IClock *clock = nullptr);

    // 建表（幂等）+ 返回当前版本；新库返回 0。
    [[nodiscard]] kernel::Result<int> currentVersion();

    // 按 version 升序执行所有 version > current 的迁移，**整体在一个事务里**。
    // 任何一条失败则整体回滚，版本号不变——不会留下"跑了一半"的库。
    [[nodiscard]] kernel::Result<int> apply(std::span<const Migration> migrations);

    [[nodiscard]] std::string_view module() const noexcept { return module_; }

private:
    [[nodiscard]] kernel::Result<void> ensureVersionTable();
    [[nodiscard]] kernel::Result<void> setVersion(int version);

    SqliteConnection &connection_;
    std::string_view module_;
    const kernel::IClock *clock_ = nullptr;
};

}  // namespace mamba::platform::storage
