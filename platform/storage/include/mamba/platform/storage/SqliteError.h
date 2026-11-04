// SQLite 的返回码 → 我们自己的 Error。
//
// 为什么要翻译：SQLite 用 30 多个码表达「哪里不对」，而应用层只关心四件事——
// 参数错了、没有这条、冲突了、暂时不可用。把 30 个码直接漏上去，结果是每个
// Handler 自己写一遍 switch，还都写得不一致。
//
// 唯一的例外是 SQLITE_CONSTRAINT：它的子码（UNIQUE / FOREIGNKEY / NOTNULL）
// 对上层是有意义的（重名曲目 vs 外键悬空是完全不同的两件事），但 M1 的仓储
// 还只需要区分「唯一键冲突」和别的冲突，等真的需要时再加子码。
#pragma once

#include <string>
#include <string_view>

#include <sqlite3.h>

#include "mamba/kernel/Error.h"

namespace mamba::platform::storage {

namespace detail {

// 只做分类，不做文案。exec() 那条路能拿到比 sqlite3_errmsg 更精确的消息
// （sqlite3_exec 的 errmsg 会带上具体哪一列冲突），但它同样需要正确的主码——
// 早先 exec() 一律返回 Internal，于是"唯一键冲突"在上层看起来像内部错误。
[[nodiscard]] inline kernel::ErrorCode classify(int rc) noexcept
{
    // 扩展码的低 8 位才是主码，直接比扩展码会漏掉所有子类型。
    switch (rc & 0xff) {
    case SQLITE_CONSTRAINT:
        return kernel::ErrorCode::Conflict;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
    case SQLITE_READONLY:
    case SQLITE_CANTOPEN:
    case SQLITE_IOERR:
    case SQLITE_FULL:
        // 这些是「现在做不了，等会儿也许能」，不是「你写错了」。
        return kernel::ErrorCode::Unavailable;
    case SQLITE_NOTFOUND:
    case SQLITE_RANGE:
        return kernel::ErrorCode::NotFound;
    case SQLITE_MISUSE:
    case SQLITE_MISMATCH:
        return kernel::ErrorCode::InvalidArgument;
    default:
        // 包括 SQLITE_CORRUPT / SQLITE_NOTADB：库坏了，调用方除了上报没别的选择。
        return kernel::ErrorCode::Internal;
    }
}

// 把 SQLite 的返回码翻译成一个可上报的错误。
//   context：出错的语句或动作，会拼进 message（"无法准备语句: no such column: id"）
//   db     ：有的话拿它要 errmsg()，比通用文案精确得多
[[nodiscard]] inline kernel::Error fromSqlite(int rc, std::string_view context, sqlite3 *db = nullptr)
{
    std::string message;
    message.reserve(context.size() + 64);
    message.append(context);
    message.append(": ");

    if (db != nullptr) {
        if (const char *detail = sqlite3_errmsg(db); detail != nullptr) {
            message.append(detail);
            message.append(" (");
            message.append(sqlite3_errstr(rc));
            message.append(")");
        } else {
            message.append(sqlite3_errstr(rc));
        }
    } else {
        message.append(sqlite3_errstr(rc));
    }

    return kernel::Error{classify(rc), std::move(message)};
}

}  // namespace detail

}  // namespace mamba::platform::storage
