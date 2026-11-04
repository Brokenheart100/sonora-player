// 事务。RAII：没 commit 就析构 = 回滚。
//
// 用 BEGIN IMMEDIATE 而不是裸 BEGIN：裸 BEGIN 是 deferred，先拿读锁、
// 写的时候再升级——两个写者在升级那一步互等，SQLITE_BUSY 就这么来的。
// IMMEDIATE 一上来就拿写锁，冲突立刻可见，而不是在事务中间炸。
#pragma once

#include "mamba/kernel/Result.h"
#include "mamba/platform/storage/SqliteConnection.h"

namespace mamba::platform::storage {

class SqliteTransaction {
public:
    // 失败（比如库被锁）时 active() 为 false，调用方应先看它。
    explicit SqliteTransaction(SqliteConnection &connection);
    ~SqliteTransaction();

    SqliteTransaction(SqliteTransaction &&other) noexcept;
    SqliteTransaction &operator=(SqliteTransaction &&other) noexcept;
    SqliteTransaction(const SqliteTransaction &) = delete;
    SqliteTransaction &operator=(const SqliteTransaction &) = delete;

    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] kernel::Result<void> commit();
    void rollback() noexcept;

private:
    SqliteConnection *connection_ = nullptr;
    bool active_ = false;
};

}  // namespace mamba::platform::storage
