#include "mamba/platform/storage/SqliteStatement.h"

#include <limits>

#include "mamba/platform/storage/SqliteError.h"

namespace mamba::platform::storage {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

namespace {

Result<void> notPrepared()
{
    return Result<void>::fail(Error{ErrorCode::InvalidArgument, "语句尚未准备"});
}

}  // namespace

SqliteStatement::~SqliteStatement()
{
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

SqliteStatement::SqliteStatement(SqliteStatement &&other) noexcept : db_(other.db_), stmt_(other.stmt_)
{
    other.db_ = nullptr;
    other.stmt_ = nullptr;
}

SqliteStatement &SqliteStatement::operator=(SqliteStatement &&other) noexcept
{
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        db_ = other.db_;
        stmt_ = other.stmt_;
        other.db_ = nullptr;
        other.stmt_ = nullptr;
    }
    return *this;
}

Result<void> SqliteStatement::bind(int index, std::int64_t value)
{
    if (stmt_ == nullptr) {
        return notPrepared();
    }
    const int rc = sqlite3_bind_int64(stmt_, index, value);
    if (rc != SQLITE_OK) {
        return Result<void>::fail(detail::fromSqlite(rc, "绑定整数参数", db_));
    }
    return Result<void>::ok();
}

Result<void> SqliteStatement::bind(int index, double value)
{
    if (stmt_ == nullptr) {
        return notPrepared();
    }
    const int rc = sqlite3_bind_double(stmt_, index, value);
    if (rc != SQLITE_OK) {
        return Result<void>::fail(detail::fromSqlite(rc, "绑定浮点参数", db_));
    }
    return Result<void>::ok();
}

Result<void> SqliteStatement::bind(int index, std::string_view text)
{
    if (stmt_ == nullptr) {
        return notPrepared();
    }
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "文本参数太长"});
    }
    // SQLITE_TRANSIENT：让 SQLite 自己拷一份。少了它，绑定的是临时对象内部的指针，
    // 步进时读到的就是悬空内存——这类 bug 只在特定编译器/优化级别上现形。
    const int rc = sqlite3_bind_text(stmt_, index, text.data(), static_cast<int>(text.size()),
                                     SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return Result<void>::fail(detail::fromSqlite(rc, "绑定文本参数", db_));
    }
    return Result<void>::ok();
}

Result<void> SqliteStatement::bindNull(int index)
{
    if (stmt_ == nullptr) {
        return notPrepared();
    }
    const int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) {
        return Result<void>::fail(detail::fromSqlite(rc, "绑定 NULL", db_));
    }
    return Result<void>::ok();
}

Result<void> SqliteStatement::bindBlob(int index, const void *data, int size)
{
    if (stmt_ == nullptr) {
        return notPrepared();
    }
    const int rc = sqlite3_bind_blob(stmt_, index, data, size, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return Result<void>::fail(detail::fromSqlite(rc, "绑定二进制参数", db_));
    }
    return Result<void>::ok();
}

Result<void> SqliteStatement::reset()
{
    if (stmt_ == nullptr) {
        return notPrepared();
    }
    // sqlite3_reset 返回的是「上一次执行」的结果码，成功与否都用它为 SQLITE_OK 判断，
    // 所以这里不把它当错误源，只清空错误状态再重置。
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    return Result<void>::ok();
}

Result<bool> SqliteStatement::step()
{
    if (stmt_ == nullptr) {
        return Result<bool>::fail(Error{ErrorCode::InvalidArgument, "语句尚未准备"});
    }
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return Result<bool>::ok(true);
    }
    if (rc == SQLITE_DONE) {
        return Result<bool>::ok(false);
    }
    return Result<bool>::fail(detail::fromSqlite(rc, "执行语句", db_));
}

std::int64_t SqliteStatement::columnInt(int index) const
{
    return stmt_ != nullptr ? sqlite3_column_int64(stmt_, index) : 0;
}

double SqliteStatement::columnDouble(int index) const
{
    return stmt_ != nullptr ? sqlite3_column_double(stmt_, index) : 0.0;
}

bool SqliteStatement::isNull(int index) const
{
    return stmt_ == nullptr || sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

std::string_view SqliteStatement::columnText(int index) const
{
    if (stmt_ == nullptr) {
        return {};
    }
    const auto *text = sqlite3_column_text(stmt_, index);
    if (text == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(stmt_, index);
    return std::string_view{reinterpret_cast<const char *>(text), static_cast<std::size_t>(size)};
}

std::string SqliteStatement::columnString(int index) const
{
    const std::string_view view = columnText(index);
    return std::string(view);
}

int SqliteStatement::columnCount() const noexcept
{
    return stmt_ != nullptr ? sqlite3_column_count(stmt_) : 0;
}

std::string_view SqliteStatement::columnName(int index) const
{
    if (stmt_ == nullptr) {
        return {};
    }
    const char *name = sqlite3_column_name(stmt_, index);
    return name != nullptr ? std::string_view{name} : std::string_view{};
}

}  // namespace mamba::platform::storage
