// 命令总线端口。
//
// 为什么要有总线，而不是 ViewModel 直接 new 一个 Handler？
// 因为 Sonora.Presentation 在构建上**看不见**任何 *.Core——它连 Handler 的头文件
// 都 include 不到。总线是这个「物理隔离」的配套：表现层只会说「我要执行这条命令」，
// 谁执行、执行多久、开不开线程，它一概不知道也不需要知道。
//
// 类型擦除用 typeid 做键：一条命令一个 handler，没有继承体系、没有 vtable 链。
// 这不是「设计模式」，是把编译期信息换成查表——一次哈希查找，纳秒级。
#pragma once

#include <typeinfo>
#include <utility>

#include "sonora/kernel/Result.h"

namespace sonora::kernel {

class ICommandBus {
public:
    virtual ~ICommandBus() = default;

    // 给总线自己用（实现里查表）。业务代码永远调下面的模板版本。
    [[nodiscard]] virtual Result<void> dispatchRaw(const void *command,
                                                   const std::type_info &type) = 0;

    template <class TCommand>
    [[nodiscard]] Result<void> dispatch(const TCommand &command)
    {
        return dispatchRaw(&command, typeid(TCommand));
    }
};

}  // namespace sonora::kernel
