// `IExecutor` 的 Qt 实现：把任务交给 `SerialWorker` 的那条线程。
//
// 这一层存在只为了一件事：`online/adapters` 与 `online/core` 都不许链 Qt，
// 但它们要"把请求发出去"。于是端口放在 contracts（`IExecutor`），
// 真正会碰 `QThread` 的实现放在这里（platform/net 是全项目唯一带 QtNetwork 的 target）。
#pragma once

#include <functional>

#include "sonora/online/contracts/Executor.h"
#include "sonora/platform/net/SerialWorker.h"

namespace sonora::platform::net
{

    class SerialExecutor final : public online::contracts::IExecutor
    {
    public:
        // `worker` 由调用方持有：它的生命周期必须比所有投递出去的任务长，
        // 顺序是先 `worker.stop()` 再销毁任何用到 executor 的对象。
        explicit SerialExecutor(SerialWorker &worker) noexcept : worker_(worker) {}

        void post(std::function<void()> task) override
        {
            if (!task)
            {
                return;
            }
            worker_.post(std::move(task));
        }

    private:
        SerialWorker &worker_;
    };

} // namespace sonora::platform::net
