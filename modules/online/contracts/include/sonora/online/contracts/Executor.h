// 任务执行器端口：把"这件事要在 worker 线程上做"变成一次投递。
//
// 为什么单独一个端口：`online/adapters` 不许链 Qt（它要被 sonora_tests 直接
// 编进去跑），可是"发请求必须在 net worker 线程上"是平台事实。中间垫一层
// 一个方法的接口，adapters 只认识 `post()`，线程从哪来它不关心。
//
// 约定：
//   * `post()` 立刻返回，不阻塞调用方（调用方通常在 GUI 线程，堵 5ms 就是掉帧）；
//   * 任务串行执行（一条线程一个队列），所以任务之间不用加锁；
//   * 实现方负责"进程退出前把队列排空"。
#pragma once

#include <functional>

namespace sonora::online::contracts
{

    class IExecutor
    {
    public:
        virtual ~IExecutor() = default;

        virtual void post(std::function<void()> task) = 0;
    };

} // namespace sonora::online::contracts
