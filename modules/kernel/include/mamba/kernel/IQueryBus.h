// 查询总线端口。
//
// 与命令总线对称，但返回值是 std::any 包着的 Result<TResult>：
// 查询的返回类型因查询而异，而虚函数不能是模板。查表 + any_cast 是最省事、
// 也最容易在没有注册时给出清晰错误（而不是段错误）的做法。
//
// 读侧的存在感很弱——这正是重点：查询不产生副作用，所以不需要事务、不需要
// 事件、不需要延迟。它只是一次投影表上的 SELECT。
#pragma once

#include <any>
#include <typeinfo>

#include "mamba/kernel/Error.h"
#include "mamba/kernel/Result.h"

namespace mamba::kernel
{

    class IQueryBus
    {
    public:
        virtual ~IQueryBus() = default;

        [[nodiscard]] virtual std::any askRaw(const void *query, const std::type_info &type) = 0;

        template <class TResult, class TQuery>
        [[nodiscard]] Result<TResult> ask(const TQuery &query)
        {
            std::any boxed = askRaw(&query, typeid(TQuery));

            if (!boxed.has_value())
            {
                return Result<TResult>::fail(
                    Error{ErrorCode::NotFound, "没有为这个查询注册 handler"});
            }

            auto *result = std::any_cast<Result<TResult>>(&boxed);
            if (result == nullptr)
            {
                // 注册了 handler 但返回类型对不上：这是 bug，不是业务失败。
                return Result<TResult>::fail(
                    Error{ErrorCode::Internal, "查询 handler 的返回类型与 ask<T>() 不一致"});
            }
            return *result;
        }
    };

} // namespace mamba::kernel
