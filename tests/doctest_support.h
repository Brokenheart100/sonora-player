// doctest 的「枚举打印」补丁。
//
// 为什么需要它：doctest 在断言失败时要打印两个操作数，默认拿 operator<< 或
// 内存 dump；对 enum class 它两者都退化成 stringifyBinaryExpr 里的 String + 枚举，
// 在 MSVC 上直接编译不过（C2110 '+' 不能加两个指针）。第一次是在
// kernel_result.cpp 里比较 ErrorCode 时撞上的。
//
// 修法不是在每个测试里把枚举转成字符串（那会让断言失去可读性），而是在这里
// 给两个枚举各补一个 StringMaker 特化——只写一次，所有测试都受益。
#pragma once

// 顺序很重要：doctest.h 只**前置声明** std::ostream，而一旦某个断言要 stringify
// std::string（几乎必然会），模板实例化就需要完整的 ostream 定义。
// 症状很难从错误信息看出来：C2027「使用未定义类型 std::basic_ostream」+
// 一串 __msvc_string_view.hpp 里的 badbit/eq_int_type 报错。
// 所以：所有测试文件都只 include 本文件，不要直接 include doctest.h。
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

#include <doctest/doctest.h>

#include <sonora/kernel/Error.h>
#include <sonora/kernel/ILogger.h>

namespace doctest {

template <>
struct StringMaker<sonora::kernel::ErrorCode> {
    static String convert(const sonora::kernel::ErrorCode &value)
    {
        const std::string_view text = sonora::kernel::toName(value);
        return String(text.data(), static_cast<unsigned>(text.size()));
    }
};

template <>
struct StringMaker<sonora::kernel::LogLevel> {
    static String convert(const sonora::kernel::LogLevel &value)
    {
        return String(sonora::kernel::toName(value));
    }
};

}  // namespace doctest
