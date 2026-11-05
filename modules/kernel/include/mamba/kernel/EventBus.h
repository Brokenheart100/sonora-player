// 同步事件总线：按**事件的静态类型**分发给订阅者。
//
// 刻意做得小：
//   - 没有退订（订阅者随进程活，组合根里一次性接好）
//   - 没有队列（同步调用；要异步就自己投 ITaskQueue）
//   - 没有优先级、没有取消、没有"事件回放"
// 这些东西一旦加进来，就该换成正经的消息中间件，而不是在进程里手搓。
#pragma once

#include <functional>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mamba/kernel/IEventBus.h>

namespace mamba::kernel {

class EventBus final : public IEventBus {
public:
    template <class TEvent, class THandler>
    void subscribe(THandler&& handler) {
        static_assert(std::is_base_of_v<IDomainEvent, TEvent>,
                      "订阅的事件必须派生自 kernel::IDomainEvent");

        auto& slot = handlers_[std::type_index{typeid(TEvent)}];
        slot.push_back(
            [fn = std::forward<THandler>(handler)](const IDomainEvent& event) mutable {
                fn(static_cast<const TEvent&>(event));
            });
    }

    void publish(const IDomainEvent& event) override {
        const auto found = handlers_.find(std::type_index{typeid(event)});
        if (found == handlers_.end()) {
            return;
        }
        // 拷贝一份再遍历：订阅者里如果又注册了新的订阅者，
        // 不该让"这一轮"看到变化（否则遍历容器边改边用）。
        const auto snapshot = found->second;
        for (const auto& handler : snapshot) {
            handler(event);
        }
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const IDomainEvent&)>>>
        handlers_;
};

} // namespace mamba::kernel
