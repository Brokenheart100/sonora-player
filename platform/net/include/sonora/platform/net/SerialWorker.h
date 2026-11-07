// 一条线程 + 一个串行队列。
//
// 为什么不用现成的：Kernel 的 `ITaskQueue` 管的是"业务 handler 必须在哪个线程跑"，
// 而这里要的是"一条常驻线程 + 一个 Qt 事件循环"，好让 QNetworkAccessManager 有
// 稳定的 thread affinity。把它塞进 Kernel 会污染"零依赖原语"那层（详见
// docs/ONLINE.md §4.2：那里写清了为什么不做 `SerialTaskQueue` 的泛化版本）。
#pragma once

#include <functional>
#include <memory>

#include <QObject>

namespace sonora::platform::net
{

    class SerialWorker final
    {
    public:
        SerialWorker();
        ~SerialWorker();

        SerialWorker(const SerialWorker &) = delete;
        SerialWorker &operator=(const SerialWorker &) = delete;

        // 线程安全。任务按投递顺序串行执行。`stop()` 之后投进来的任务被直接丢弃。
        void post(std::function<void()> task);

        // 幂等。会等已经在跑的任务结束（最多 timeoutMs），然后停掉事件循环。
        void stop(int timeoutMs = 5000);

        [[nodiscard]] bool isRunning() const;

        // 仅用于断言/日志：判断某个线程是不是这条 worker 线程。
        [[nodiscard]] bool isWorkerThread() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace sonora::platform::net
