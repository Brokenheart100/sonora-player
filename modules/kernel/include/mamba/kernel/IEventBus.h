// 事件总线的接口。
//
// 为什么事件不走命令总线：命令是"请做某事"，可以拒绝、有返回值；
// 事件是"已经发生了"，不可撤销、没有返回值。混在一条通道上，
// 迟早有人把事件当命令去 await 结果。
#pragma once

#include <mamba/kernel/IDomainEvent.h>

namespace mamba::kernel {

class IEventBus {
public:
    virtual ~IEventBus() = default;

    /// 同步分发：所有订阅者在**当前线程**依次被调用。
    /// 也因此，发布事件的地方必须是"已经把事务提交完"的地方。
    virtual void publish(const IDomainEvent& event) = 0;
};

} // namespace mamba::kernel
