// 领域事件的公共底座。
//
// 领域事件是**已经发生的事实**（TrackImported、PlaybackStarted），不是命令，
// 因此：命名用过去式、不可撤销、不可拒绝。
//
// 这里只放一个虚析构，因为事件的具体内容由派生类型携带，内核不该规定字段。
// 唯一强制的是「可多态销毁」，否则订阅者按基类指针持有事件时会漏析构。
#pragma once

#include <string_view>

namespace mamba::kernel {

class IDomainEvent {
public:
    virtual ~IDomainEvent() = default;

    // 事件名，用于日志与订阅者按名字路由。派生类用 static constexpr 提供。
    [[nodiscard]] virtual std::string_view eventName() const noexcept = 0;
};

}  // namespace mamba::kernel
