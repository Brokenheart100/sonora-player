// 准备好的语句。RAII：析构时 finalize。
//
// 为什么不用 SQLiteCpp 之类：见 docs/DEPENDENCIES.md §3——它们的 sqlite3_external
// 在 Windows 上会去拉一个没有 FTS5 的 sqlite3 包，等于把「搜索能不能用中文」
// 交给一个我们控制不了的开关。这一百多行是为此付的代价，很划算。
//
// 绑定下标从 **1** 开始（SQLite 的规定，不是笔误）。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "sonora/kernel/Result.h"

namespace sonora::platform::storage {

class SqliteStatement {
public:
    SqliteStatement() = default;
    ~SqliteStatement();

    SqliteStatement(SqliteStatement &&other) noexcept;
    SqliteStatement &operator=(SqliteStatement &&other) noexcept;

    // 语句是资源的独占句柄，拷贝没有意义。
    SqliteStatement(const SqliteStatement &) = delete;
    SqliteStatement &operator=(const SqliteStatement &) = delete;

    [[nodiscard]] bool isPrepared() const noexcept { return stmt_ != nullptr; }

    [[nodiscard]] kernel::Result<void> bind(int index, std::int64_t value);
    [[nodiscard]] kernel::Result<void> bind(int index, double value);
    [[nodiscard]] kernel::Result<void> bind(int index, std::string_view text);
    [[nodiscard]] kernel::Result<void> bindNull(int index);
    [[nodiscard]] kernel::Result<void> bindBlob(int index, const void *data, int size);

    // 回到「未执行、参数保留」的状态，可以换参数再跑。
    [[nodiscard]] kernel::Result<void> reset();

    // true = 取到一行；false = 执行完毕（SQLITE_DONE）。
    // 注意语义：false 不是失败，失败会走 Error 分支。
    [[nodiscard]] kernel::Result<bool> step();

    [[nodiscard]] std::int64_t columnInt(int index) const;
    [[nodiscard]] double columnDouble(int index) const;
    [[nodiscard]] bool isNull(int index) const;
    // 视图指向 SQLite 的内部缓冲，下一次 step()/reset()/析构 之前有效。
    [[nodiscard]] std::string_view columnText(int index) const;
    [[nodiscard]] std::string columnString(int index) const;

    [[nodiscard]] int columnCount() const noexcept;
    [[nodiscard]] std::string_view columnName(int index) const;

    [[nodiscard]] sqlite3_stmt *handle() const noexcept { return stmt_; }

private:
    friend class SqliteConnection;

    SqliteStatement(sqlite3 *db, sqlite3_stmt *stmt) noexcept : db_(db), stmt_(stmt) {}

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
};

}  // namespace sonora::platform::storage
