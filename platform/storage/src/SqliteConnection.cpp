#include "sonora/platform/storage/SqliteConnection.h"

#include <limits>
#include <utility>

#include "sonora/platform/storage/SqliteError.h"

namespace sonora::platform::storage {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

namespace {

// 打开后的固定设定。集中在这里，避免"某个连接忘了开外键"这种事。
Result<void> applyConnectionPragmas(SqliteConnection &connection)
{
    // 外键约束默认是**关**的（为兼容 2001 年的库）。不开它，悬空的 track_id
    // 会一直安静地存在，直到某天列表里出现一首"没有曲名的歌"。
    if (auto result = connection.exec("PRAGMA foreign_keys = ON;"); !result) {
        return result;
    }

    // WAL：读写不互斥。:memory: 库不支持 WAL，会返回 "memory" 而不是报错，
    // 所以这里不能把结果当失败看。
    (void)connection.queryText("PRAGMA journal_mode = WAL;");

    // NORMAL：崩溃不会损坏数据库，最多丢最后一次提交。曲库能重扫，不值得为
    // 一次 fsync 付 10ms 的写延迟。
    (void)connection.exec("PRAGMA synchronous = NORMAL;");

    return Result<void>::ok();
}

}  // namespace

SqliteConnection::~SqliteConnection()
{
    close();
}

SqliteConnection::SqliteConnection(SqliteConnection &&other) noexcept : db_(other.db_)
{
    other.db_ = nullptr;
}

SqliteConnection &SqliteConnection::operator=(SqliteConnection &&other) noexcept
{
    if (this != &other) {
        close();
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

Result<SqliteConnection> SqliteConnection::open(std::string_view utf8Path, SqliteOpenMode mode)
{
    int flags = 0;
    switch (mode) {
    case SqliteOpenMode::ReadOnly:
        flags = SQLITE_OPEN_READONLY;
        break;
    case SqliteOpenMode::ReadWrite:
        flags = SQLITE_OPEN_READWRITE;
        break;
    case SqliteOpenMode::ReadWriteCreate:
        flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        break;
    }

    sqlite3 *db = nullptr;
    const int rc = sqlite3_open_v2(std::string(utf8Path).c_str(), &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        auto error = detail::fromSqlite(rc, "无法打开数据库", db);
        if (db != nullptr) {
            // 打开失败时 db 句柄仍可能非空，必须关掉，否则每失败一次泄漏一个句柄。
            sqlite3_close_v2(db);
        }
        return Result<SqliteConnection>::fail(std::move(error));
    }

    SqliteConnection connection;
    connection.db_ = db;

    // 等锁而不是立刻报错：曲库扫描与 UI 查询会撞上，2 秒足够让前者写完了。
    sqlite3_busy_timeout(db, 2000);

    if (auto result = applyConnectionPragmas(connection); !result) {
        return Result<SqliteConnection>::fail(result.error());
    }

    return Result<SqliteConnection>::ok(std::move(connection));
}

void SqliteConnection::close() noexcept
{
    if (db_ != nullptr) {
        // close_v2：还有未 finalize 的语句时它会标记为"待关闭"，等语句释放后自动关，
        // 而不是返回 SQLITE_BUSY 让调用方陷入重试循环。
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

Result<void> SqliteConnection::exec(std::string_view sql)
{
    if (db_ == nullptr) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "连接未打开"});
    }

    char *errorMessage = nullptr;
    const std::string text(sql);
    const int rc = sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &errorMessage);

    if (rc != SQLITE_OK) {
        std::string message = "执行 SQL 失败: ";
        if (errorMessage != nullptr) {
            message.append(errorMessage);
            sqlite3_free(errorMessage);
        } else {
            message.append(sqlite3_errstr(rc));
        }
        return Result<void>::fail(Error{detail::classify(rc), std::move(message)});
    }
    return Result<void>::ok();
}

Result<SqliteStatement> SqliteConnection::prepare(std::string_view sql)
{
    if (db_ == nullptr) {
        return Result<SqliteStatement>::fail(Error{ErrorCode::InvalidArgument, "连接未打开"});
    }
    if (sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Result<SqliteStatement>::fail(Error{ErrorCode::InvalidArgument, "SQL 太长"});
    }

    sqlite3_stmt *stmt = nullptr;
    const char *tail = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, &tail);
    if (rc != SQLITE_OK) {
        return Result<SqliteStatement>::fail(detail::fromSqlite(rc, "无法准备语句", db_));
    }

    // 一次只准备一条：剩下的若不全是空白，说明调用方传了两条语句，
    // 静默忽略第二条会造成"明明写了却没生效"的诡异现象。
    if (tail != nullptr) {
        const std::string_view rest{tail};
        const auto firstMeaningful = rest.find_first_not_of(" \t\r\n;");
        if (firstMeaningful != std::string_view::npos) {
            if (stmt != nullptr) {
                sqlite3_finalize(stmt);
            }
            return Result<SqliteStatement>::fail(
                Error{ErrorCode::InvalidArgument, "一次只能准备一条语句，请用 exec() 执行多条"});
        }
    }

    if (stmt == nullptr) {
        // 传了空语句或纯注释：不是错误，但没有可执行的东西。
        return Result<SqliteStatement>::fail(Error{ErrorCode::InvalidArgument, "语句为空"});
    }

    return Result<SqliteStatement>::ok(SqliteStatement{db_, stmt});
}

Result<std::int64_t> SqliteConnection::queryInt(std::string_view sql)
{
    auto prepared = prepare(sql);
    if (!prepared) {
        return Result<std::int64_t>::fail(prepared.error());
    }

    auto row = prepared.value().step();
    if (!row) {
        return Result<std::int64_t>::fail(row.error());
    }
    if (!row.value()) {
        return Result<std::int64_t>::fail(Error{ErrorCode::NotFound, "查询没有任何行"});
    }
    return Result<std::int64_t>::ok(prepared.value().columnInt(0));
}

Result<std::string> SqliteConnection::queryText(std::string_view sql)
{
    auto prepared = prepare(sql);
    if (!prepared) {
        return Result<std::string>::fail(prepared.error());
    }

    auto row = prepared.value().step();
    if (!row) {
        return Result<std::string>::fail(row.error());
    }
    if (!row.value()) {
        return Result<std::string>::fail(Error{ErrorCode::NotFound, "查询没有任何行"});
    }
    return Result<std::string>::ok(prepared.value().columnString(0));
}

std::int64_t SqliteConnection::lastInsertRowId() const noexcept
{
    return db_ != nullptr ? sqlite3_last_insert_rowid(db_) : 0;
}

int SqliteConnection::changes() const noexcept
{
    return db_ != nullptr ? sqlite3_changes(db_) : 0;
}

}  // namespace sonora::platform::storage
