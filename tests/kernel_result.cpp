// Result<T> 的测试。
//
// 这套错误处理是「不抛异常」策略的地基，所以边界值要测满：
// 成功、失败、错误的传播、值的变换、以及 Result<void> 的两种状态。
#include "doctest_support.h"

#include <string>
#include <string_view>

#include <sonora/kernel/Result.h>


using sonora::kernel::Error;
using sonora::kernel::ErrorCode;
using sonora::kernel::Result;

namespace {

Result<int> parsePositive(int value)
{
    if (value <= 0) {
        return Result<int>::fail(Error{ErrorCode::InvalidArgument, "必须为正数"});
    }
    return Result<int>::ok(value);
}

Result<int> doubleIt(int value)
{
    if (value > 100) {
        return Result<int>::fail(Error{ErrorCode::InvalidArgument, "太大了"});
    }
    return Result<int>::ok(value * 2);
}

}  // namespace

TEST_CASE("Result：成功时可取值，error() 不该被调用")
{
    auto result = parsePositive(3);
    REQUIRE(result.hasValue());
    CHECK(result.value() == 3);
    CHECK(static_cast<bool>(result));
}

TEST_CASE("Result：失败时带上分类码与人话")
{
    auto result = parsePositive(-1);
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
    CHECK(result.error().message() == "必须为正数");
    CHECK_FALSE(static_cast<bool>(result));
}

TEST_CASE("Result：valueOr 在失败时给兜底值")
{
    CHECK(parsePositive(5).valueOr(-1) == 5);
    CHECK(parsePositive(0).valueOr(-1) == -1);
}

TEST_CASE("Result：map 成功时变换，失败时原样传递错误")
{
    const auto ok = parsePositive(4).map([](int v) { return std::to_string(v); });
    REQUIRE(ok.hasValue());
    CHECK(ok.value() == "4");

    const auto failed = parsePositive(-4).map([](int v) { return std::to_string(v); });
    REQUIRE_FALSE(failed.hasValue());
    CHECK(failed.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("Result：andThen 串起两段可能失败的逻辑，第一个失败就短路")
{
    const auto ok = parsePositive(5).andThen(doubleIt);
    REQUIRE(ok.hasValue());
    CHECK(ok.value() == 10);

    // 第二段失败：120 本身合法，但 doubleIt 认为 >100 太大
    const auto secondFails = parsePositive(120).andThen(doubleIt);
    REQUIRE_FALSE(secondFails.hasValue());
    CHECK(secondFails.error().message() == "太大了");

    // 第一段失败：第二段根本不该被调用（这里用值来观察短路）
    const auto firstFails = parsePositive(-1).andThen(doubleIt);
    REQUIRE_FALSE(firstFails.hasValue());
    CHECK(firstFails.error().message() == "必须为正数");
}

TEST_CASE("Result<void>：成功没有负载，失败带 Error")
{
    const auto ok = Result<void>::ok();
    CHECK(ok.hasValue());

    const auto bad = Result<void>::fail(Error{ErrorCode::Conflict, "已导入过"});
    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error().code() == ErrorCode::Conflict);
}

TEST_CASE("Result<void>：andThen 串联命令，前者失败则后者不执行")
{
    bool ran = false;
    const auto skipped = Result<void>::fail(Error{ErrorCode::Unavailable, "库里被锁"})
                             .andThen([&ran] {
                                 ran = true;
                                 return Result<void>::ok();
                             });

    CHECK_FALSE(skipped.hasValue());
    CHECK_FALSE(ran);

    const auto executed = Result<void>::ok().andThen([&ran] {
        ran = true;
        return Result<void>::ok();
    });
    CHECK(executed.hasValue());
    CHECK(ran);
}
