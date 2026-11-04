// 线程切换的唯一出口。
//
// 规矩很简单：**领域层不开线程，只投任务**。扫描一万首歌要时间，但那是
// Infrastructure 的事；Handler 只管说「这件事做完之后，把结果交给主线程」。
//
// 这样做的直接好处：Handler 全部可以在单测里同步跑（用 ImmediateTaskQueue），
// 不需要线程、不需要条件变量、不会有随机失败的测试。
#pragma once

#include <functional>
#include <utility>

namespace mamba::kernel {

class ITaskQueue {
public:
    virtual ~ITaskQueue() = default;

    // 投递到主线程/工作线程执行。实现方负责线程安全。
    virtual void post(std::function<void()> task) = 0;

    // 当前是不是已经在目标线程上。已经在就直接执行，省一次投递（也避免顺序颠倒）。
    [[nodiscard]] virtual bool isOnTargetThread() const noexcept = 0;

    // 便利封装：能就地执行就就地执行。
    void run(std::function<void()> task)
    {
        if (isOnTargetThread()) {
            task();
            return;
        }
        post(std::move(task));
    }
};

// 测试与单线程场景用：立即执行，无队列。
class ImmediateTaskQueue final : public ITaskQueue {
public:
    void post(std::function<void()> task) override { task(); }
    [[nodiscard]] bool isOnTargetThread() const noexcept override { return true; }
};

}  // namespace mamba::kernel
