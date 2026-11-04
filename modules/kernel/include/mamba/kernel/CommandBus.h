// 命令总线实现。
//
// 只做三件事：查表、调用、测耗时。**不在这里做线程切换**——Handler 该同步返回
// 就同步返回，慢活自己投 ITaskQueue。理由见 docs/ARCHITECTURE.md §5.3：
// 一旦总线开始偷偷开线程，命令的成功/失败就不再同步可见，调用方就得分两条路写，
// 于是「偶尔不报错」的 bug 就出现了。
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "mamba/kernel/ICommandBus.h"
#include "mamba/kernel/ILogger.h"

namespace mamba::kernel {

class CommandBus final : public ICommandBus {
public:
    // thresholdMs：同步耗时上限。超过就记一条 warn（debug 下相当于断言）。
    // 5ms 是人能察觉不到的边界，也是 docs/IMPLEMENTATION.md 里的性能预算。
    explicit CommandBus(ILogger *logger = nullptr, double slowThresholdMs = 5.0)
        : logger_(logger), slowThresholdMs_(slowThresholdMs)
    {
    }

    // 注册：给一个能处理 TCommand 的可调用对象。
    template <class TCommand, class F>
    void on(F &&handler)
    {
        std::function<Result<void>(const TCommand &)> fn(std::forward<F>(handler));
        table_[std::type_index(typeid(TCommand))] =
            [fn = std::move(fn)](const void *command) { return fn(*static_cast<const TCommand *>(command)); };
    }

    // 注册：给一个带 handle(const TCommand&) 的 handler 对象（应用层的标准形态）。
    template <class TCommand, class THandler>
    void on(std::shared_ptr<THandler> handler)
    {
        on<TCommand>([handler](const TCommand &command) { return handler->handle(command); });
    }

    [[nodiscard]] Result<void> dispatchRaw(const void *command,
                                           const std::type_info &type) override
    {
        const auto it = table_.find(std::type_index(type));
        if (it == table_.end()) {
            return Result<void>::fail(
                Error{ErrorCode::NotFound, "没有为这条命令注册 handler"});
        }

        const auto started = std::chrono::steady_clock::now();
        Result<void> result = it->second(command);
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();

        if (elapsed > slowThresholdMs_ && logger_ != nullptr) {
            logger_->warn("命令同步耗时 {:.2f}ms（上限 {:.1f}ms）——慢活应该投 ITaskQueue",
                          elapsed, slowThresholdMs_);
        }
        return result;
    }

    [[nodiscard]] std::size_t handlerCount() const noexcept { return table_.size(); }

private:
    std::unordered_map<std::type_index, std::function<Result<void>(const void *)>> table_;
    ILogger *logger_ = nullptr;
    double slowThresholdMs_ = 5.0;
};

}  // namespace mamba::kernel
