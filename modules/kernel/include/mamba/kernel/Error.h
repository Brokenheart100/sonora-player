// 领域错误。
//
// 两条规矩：
//   1. 领域层**不抛异常**。失败走返回值（Result<T>），因为「文件读不了」是正常
//      业务分支，不是异常情况；异常只留给程序员错误（越界、空指针解引用）。
//   2. Error 不是异常类，它是一份值：一个分类码 + 一句给人看的话。
//      跨层传递时上层只能读，不能改语义。
#pragma once

#include <string>
#include <utility>

namespace mamba::kernel {

enum class ErrorCode {
    None = 0,
    InvalidArgument,   // 参数不合法（空标题、负时长、非法路径）
    NotFound,          // 要找的东西不存在（没有这个 handler、没有这一行）
    Conflict,          // 状态冲突（重复导入、版本不匹配）
    Unavailable,       // 外部依赖暂时不可用（数据库被锁、设备没了）
    Internal,          // 不该发生的内部错误（bug）
};

// 名字故意不叫 toString()：doctest 的 DOCTEST_STRINGIFY 会用无限定调用 toString(v)，
// 而 ADL 会把本函数拉进候选集——非模板的精确匹配会赢过 doctest 的模板，
// 结果是断言失败时两个操作数都变成 const char*，MSVC 报 C2110（加两个指针）。
[[nodiscard]] constexpr const char *toName(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::None:            return "None";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::NotFound:        return "NotFound";
    case ErrorCode::Conflict:        return "Conflict";
    case ErrorCode::Unavailable:     return "Unavailable";
    case ErrorCode::Internal:        return "Internal";
    }
    return "Unknown";
}

class Error {
public:
    Error() = default;
    Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string &message() const noexcept { return message_; }
    [[nodiscard]] bool isEmpty() const noexcept { return code_ == ErrorCode::None; }

private:
    ErrorCode code_ = ErrorCode::None;
    std::string message_;
};

}  // namespace mamba::kernel
