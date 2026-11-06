#include "sonora/platform/storage/SqliteTransaction.h"

namespace sonora::platform::storage {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

SqliteTransaction::SqliteTransaction(SqliteConnection &connection) : connection_(&connection)
{
    if (!connection.isOpen()) {
        return;
    }

    if (auto result = connection.exec("BEGIN IMMEDIATE;"); !result) {
        active_ = false;
        return;
    }
    active_ = true;
}

SqliteTransaction::~SqliteTransaction()
{
    // 没提交 = 回滚。这一条让「中途 return」不会把事务留在开着的状态。
    rollback();
}

SqliteTransaction::SqliteTransaction(SqliteTransaction &&other) noexcept
    : connection_(other.connection_), active_(other.active_)
{
    other.connection_ = nullptr;
    other.active_ = false;
}

SqliteTransaction &SqliteTransaction::operator=(SqliteTransaction &&other) noexcept
{
    if (this != &other) {
        rollback();
        connection_ = other.connection_;
        active_ = other.active_;
        other.connection_ = nullptr;
        other.active_ = false;
    }
    return *this;
}

Result<void> SqliteTransaction::commit()
{
    if (!active_ || connection_ == nullptr) {
        return Result<void>::fail(Error{ErrorCode::Conflict, "没有激活的事务"});
    }

    auto result = connection_->exec("COMMIT;");
    active_ = false;

    if (!result) {
        // 提交失败（例如延迟约束被触发）时事务状态已不确定，主动回滚收尾。
        (void)connection_->exec("ROLLBACK;");
        return Result<void>::fail(Error{ErrorCode::Unavailable, "提交事务失败，已回滚"});
    }
    return Result<void>::ok();
}

void SqliteTransaction::rollback() noexcept
{
    if (active_ && connection_ != nullptr) {
        (void)connection_->exec("ROLLBACK;");
    }
    active_ = false;
}

}  // namespace sonora::platform::storage
