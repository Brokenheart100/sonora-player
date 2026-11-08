// Result<T> —— 「有值或者有错」的返回值。
//
// 实现是 std::expected<T, Error> 的薄包装（C++23）：存储、异常安全、移动语义
// 都交给标准库，这里只留项目自己的名字和几个便利成员（ok/fail/hasValue/take）。
// 之所以不直接把 std::expected 铺到所有签名上：`Result<T>` 带着领域错误类型
// `kernel::Error` 的语义，500 多处调用点也靠 `Result<T>::ok(...)` 读起来更直白。
//
// 用法：
//     Result<Track> makeTrack(...);
//     auto track = makeTrack(...);
//     if (!track) { log(track.error().message()); }
//     use(track.value());
#pragma once

#include <expected>
#include <type_traits>
#include <utility>

#include "sonora/kernel/Error.h"

namespace sonora::kernel {

template <class T>
class [[nodiscard]] Result {
public:
    using value_type = T;

    static Result ok(T value) { return Result{std::move(value)}; }
    static Result fail(Error error) { return Result{std::move(error)}; }

    [[nodiscard]] bool hasValue() const noexcept { return storage_.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] T &value() & { return *storage_; }
    [[nodiscard]] const T &value() const & { return *storage_; }
    [[nodiscard]] T &&take() && { return *std::move(storage_); }

    // 只有在 !hasValue() 时才有意义。返回引用而不是拷贝：错误信息可能很长。
    [[nodiscard]] const Error &error() const & noexcept { return storage_.error(); }

    [[nodiscard]] T valueOr(T fallback) const &
    {
        return storage_.has_value() ? *storage_ : std::move(fallback);
    }

    // 隐式构造：让 `return Error{...};` 直接表示失败。
    // 只对 Error 隐式；成功值仍然必须走 ok(...)，避免 int 之类被悄悄当成 T。
    Result(Error error) : storage_(std::unexpect, std::move(error)) {}

    // 成功时变换值，失败时把错误原样往后传。
    template <class F>
    [[nodiscard]] auto map(F &&f) const & -> Result<std::invoke_result_t<F, const T &>>
    {
        using Next = Result<std::invoke_result_t<F, const T &>>;
        if (!hasValue()) {
            return Next::fail(error());
        }
        return Next::ok(f(*storage_));
    }

    template <class F>
    [[nodiscard]] auto andThen(F &&f) const & -> std::invoke_result_t<F, const T &>
    {
        using Next = std::invoke_result_t<F, const T &>;
        if (!hasValue()) {
            return Next::fail(error());
        }
        return f(*storage_);
    }

private:
    explicit Result(T value) : storage_(std::move(value)) {}

    std::expected<T, Error> storage_;
};

// 无返回值的命令用这个。std::expected<void, Error> 的空状态就是成功，
// 与「没有错」的语义正好对齐。
template <>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;

    static Result ok() { return Result{}; }
    static Result fail(Error error) { return Result{std::move(error)}; }

    // 同 Result<T>：`return Error{...};` 表示失败，`return {};` 表示成功。
    // 后者对无返回值的命令是最高频的一句，值得为它开一个公有默认构造。
    Result() = default;
    Result(Error error) : storage_(std::unexpect, std::move(error)) {}

    [[nodiscard]] bool hasValue() const noexcept { return storage_.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }

    // 只有在 !hasValue() 时调用。
    [[nodiscard]] const Error &error() const noexcept { return storage_.error(); }

    template <class F>
    [[nodiscard]] Result<void> andThen(F &&f) const
    {
        if (!hasValue()) {
            return Result<void>::fail(error());
        }
        return f();
    }

private:
    std::expected<void, Error> storage_;
};

}  // namespace sonora::kernel
