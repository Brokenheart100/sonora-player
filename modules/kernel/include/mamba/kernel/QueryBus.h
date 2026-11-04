// 查询总线实现。
//
// 与 CommandBus 同构，唯一差别是返回值进 std::any。
// 查询**不测耗时上限**：读操作本来就慢（万行分页），拿 5ms 去卡它只会刷日志。
// 它真正的性能约束在数据层——分页 + 索引 + 投影表，见 M5。
#pragma once

#include <any>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "mamba/kernel/IQueryBus.h"

namespace mamba::kernel {

class QueryBus final : public IQueryBus {
public:
    // 由调用方显式给出返回类型：ask<TResult>() 的类型必须与这里一致，
    // 不一致会在 ask 处得到一个清晰的 Internal 错误，而不是崩溃。
    template <class TQuery, class TResult, class F>
    void on(F &&handler)
    {
        std::function<Result<TResult>(const TQuery &)> fn(std::forward<F>(handler));
        table_[std::type_index(typeid(TQuery))] = [fn = std::move(fn)](const void *query) -> std::any {
            return std::any(fn(*static_cast<const TQuery *>(query)));
        };
    }

    [[nodiscard]] std::any askRaw(const void *query, const std::type_info &type) override
    {
        const auto it = table_.find(std::type_index(type));
        if (it == table_.end()) {
            return {};
        }
        return it->second(query);
    }

    [[nodiscard]] std::size_t handlerCount() const noexcept { return table_.size(); }

private:
    std::unordered_map<std::type_index, std::function<std::any(const void *)>> table_;
};

}  // namespace mamba::kernel
