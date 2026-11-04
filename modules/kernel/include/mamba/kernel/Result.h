// Result<T> —— 「有值或者有错」的返回值。
//
// 为什么不用 std::expected？见 docs/DEPENDENCIES.md §3：MSVC 上 std::expected
// 要开 /std:c++23preview，为一个类型不值得把整个项目的语言标准推上去。
// 这里用 std::variant<T, Error> 实现同一套语义，等 C++23 落地再换。
//
// 用法：
//     Result<Track> makeTrack(...);
//     auto track = makeTrack(...);
//     if (!track) { log(track.error().message()); }
//     use(track.value());
#pragma once

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "mamba/kernel/Error.h"

namespace mamba::kernel {

template <class T>
class [[nodiscard]] Result {
public:
    using value_type = T;

    static Result ok(T value) { return Result{std::move(value)}; }
    static Result fail(Error error) { return Result{std::move(error)}; }

    [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] T &value() & { return std::get<0>(storage_); }
    [[nodiscard]] const T &value() const & { return std::get<0>(storage_); }
    [[nodiscard]] T &&take() && { return std::get<0>(std::move(storage_)); }

    // 只有在 !hasValue() 时才有意义。返回引用而不是拷贝：错误信息可能很长。
    [[nodiscard]] const Error &error() const & noexcept { return std::get<1>(storage_); }

    [[nodiscard]] T valueOr(T fallback) const &
    {
        return hasValue() ? std::get<0>(storage_) : std::move(fallback);
    }

    // 成功时变换值，失败时把错误原样往后传。
    template <class F>
    [[nodiscard]] auto map(F &&f) const & -> Result<std::invoke_result_t<F, const T &>>
    {
        if (!hasValue()) {
            return Result<std::invoke_result_t<F, const T &>>::fail(error());
        }
        return Result<std::invoke_result_t<F, const T &>>::ok(f(std::get<0>(storage_)));
    }

    template <class F>
    [[nodiscard]] auto andThen(F &&f) const & -> std::invoke_result_t<F, const T &>
    {
        using Next = std::invoke_result_t<F, const T &>;
        if (!hasValue()) {
            return Next::fail(error());
        }
        return f(std::get<0>(storage_));
    }

private:
    explicit Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
    explicit Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    std::variant<T, Error> storage_;
};

// 无返回值的命令用这个。用 optional<Error> 而不是补一个 Unit 类型：
// 「没有错」就是 empty，语义最直白。
template <>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;

    static Result ok() { return Result{}; }
    static Result fail(Error error) { return Result{std::move(error)}; }

    [[nodiscard]] bool hasValue() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }

    // 只有在 !hasValue() 时调用。
    [[nodiscard]] const Error &error() const noexcept { return *error_; }

    template <class F>
    [[nodiscard]] Result<void> andThen(F &&f) const
    {
        if (!hasValue()) {
            return Result<void>::fail(error());
        }
        return f();
    }

private:
    Result() = default;
    explicit Result(Error error) : error_(std::move(error)) {}

    std::optional<Error> error_;
};

}  // namespace mamba::kernel
