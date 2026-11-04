// IUnitOfWork 的 SQLite 实现。
//
// 薄得几乎不像个类，这是故意的：事务边界属于应用层（一个命令一个事务），
// 存储层只需要把 begin/commit/rollback 忠实转给 SQLite，**不负责决定**什么时候开。
// 一旦这里开始"自动重试"或"嵌套计数"，事务边界就跑到基础设施里去了。
//
// 不支持嵌套：begin() 在已激活时返回 Conflict，而不是悄悄开一个 SAVEPOINT。
// 需要 SAVEPOINT 的地方（比如批量导入里的分批提交）是另一个概念，别混。
#pragma once

#include <optional>

#include "mamba/kernel/IUnitOfWork.h"
#include "mamba/kernel/Result.h"
#include "mamba/platform/storage/SqliteConnection.h"
#include "mamba/platform/storage/SqliteTransaction.h"

namespace mamba::platform::storage {

class SqliteUnitOfWork final : public kernel::IUnitOfWork {
public:
    explicit SqliteUnitOfWork(SqliteConnection &connection) : connection_(connection) {}

    [[nodiscard]] kernel::Result<void> begin() override
    {
        if (transaction_.has_value() && transaction_->active()) {
            return kernel::Result<void>::fail(
                kernel::Error{kernel::ErrorCode::Conflict, "事务已经开着，不支持嵌套"});
        }

        transaction_.emplace(connection_);
        if (!transaction_->active()) {
            transaction_.reset();
            return kernel::Result<void>::fail(
                kernel::Error{kernel::ErrorCode::Unavailable, "无法开启事务（库被占用？）"});
        }
        return kernel::Result<void>::ok();
    }

    [[nodiscard]] kernel::Result<void> commit() override
    {
        if (!transaction_.has_value() || !transaction_->active()) {
            return kernel::Result<void>::fail(
                kernel::Error{kernel::ErrorCode::Conflict, "没有激活的事务可以提交"});
        }

        auto result = transaction_->commit();
        transaction_.reset();
        return result;
    }

    void rollback() noexcept override
    {
        if (transaction_.has_value()) {
            transaction_->rollback();
            transaction_.reset();
        }
    }

    [[nodiscard]] bool active() const noexcept override
    {
        return transaction_.has_value() && transaction_->active();
    }

private:
    SqliteConnection &connection_;
    std::optional<SqliteTransaction> transaction_;
};

}  // namespace mamba::platform::storage
