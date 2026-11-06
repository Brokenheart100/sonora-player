// 数据库连接。一个连接 = 一个 sqlite3*，非线程安全（SQLite 默认串行化除外）。
//
// 使用约定（M1 起生效，别的地方别自作主张）：
//   * 全局只有**一个**连接，住在组合根里，属于写线程；
//   * 读操作也走这个连接（WAL 下读写不互斥），不另开连接池——
//     一万首曲库的读是毫秒级查询，不是 I/O 瓶颈，连接池只会引入可见性问题。
//
// 路径用 UTF-8 的 std::string 而不是 std::filesystem::path：sqlite3_open_v2
// 要的就是 UTF-8，而 path 在 Windows 上要经 u8string 转一道，绕这个弯没有收益。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "sonora/kernel/Result.h"
#include "sonora/platform/storage/SqliteStatement.h"

namespace sonora::platform::storage {

enum class SqliteOpenMode {
    ReadOnly,          // 只读打开，文件不存在则失败
    ReadWrite,         // 可读写，文件不存在则失败
    ReadWriteCreate,   // 可读写，文件不存在则创建（正常打开方式）
};

class SqliteConnection {
public:
    SqliteConnection() = default;
    ~SqliteConnection();

    SqliteConnection(SqliteConnection &&other) noexcept;
    SqliteConnection &operator=(SqliteConnection &&other) noexcept;

    SqliteConnection(const SqliteConnection &) = delete;
    SqliteConnection &operator=(const SqliteConnection &) = delete;

    // utf8Path 传 ":memory:" 就是内存库（测试用）。
    [[nodiscard]] static kernel::Result<SqliteConnection> open(std::string_view utf8Path,
                                                               SqliteOpenMode mode = SqliteOpenMode::ReadWriteCreate);

    [[nodiscard]] bool isOpen() const noexcept { return db_ != nullptr; }
    void close() noexcept;

    // 执行不取结果的 SQL（建表、PRAGMA、INSERT 没有参数时）。
    [[nodiscard]] kernel::Result<void> exec(std::string_view sql);

    [[nodiscard]] kernel::Result<SqliteStatement> prepare(std::string_view sql);

    // 便利方法：单值查询。主要用于 PRAGMA、COUNT(*) 和迁移里的版本号。
    [[nodiscard]] kernel::Result<std::int64_t> queryInt(std::string_view sql);
    [[nodiscard]] kernel::Result<std::string> queryText(std::string_view sql);

    [[nodiscard]] std::int64_t lastInsertRowId() const noexcept;
    [[nodiscard]] int changes() const noexcept;

    // 给需要直接调 SQLite 的地方（迁移执行器、将来的备份功能）。
    [[nodiscard]] sqlite3 *handle() const noexcept { return db_; }

private:
    sqlite3 *db_ = nullptr;
};

}  // namespace sonora::platform::storage
