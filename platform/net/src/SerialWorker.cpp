#include "sonora/platform/net/SerialWorker.h"

#include <atomic>
#include <utility>

#include <QMetaObject>
#include <QThread>

namespace sonora::platform::net
{

    class SerialWorker::Impl
    {
    public:
        QThread thread;
        // 只用来给任务一个"住在 worker 线程里"的接收者。它自己不需要信号槽，
        // 也不需要 Q_OBJECT —— QMetaObject::invokeMethod 的 functor 重载对
        // 任意 QObject 都成立，这样整个工程就不必为一个内部类跑一遍 moc。
        QObject *context{nullptr};
        std::atomic_bool stopped{false};
    };

    SerialWorker::SerialWorker() : impl_(std::make_unique<Impl>())
    {
        impl_->thread.setObjectName(QStringLiteral("sonora-net"));
        impl_->thread.start();
        impl_->context = new QObject();
        impl_->context->moveToThread(&impl_->thread);
    }

    SerialWorker::~SerialWorker()
    {
        stop();
    }

    void SerialWorker::post(std::function<void()> task)
    {
        if (!task || impl_->stopped.load(std::memory_order_acquire) || impl_->context == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(impl_->context, std::move(task), Qt::QueuedConnection);
    }

    void SerialWorker::stop(int timeoutMs)
    {
        if (impl_->stopped.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        if (impl_->context == nullptr)
        {
            return;
        }

        // 在 worker 线程里删自己的接收者（affinity 一致），然后退出事件循环。
        // 直接在主线程 delete 一个 affinity 在别的线程的 QObject 是未定义行为。
        QObject *context = impl_->context;
        impl_->context = nullptr;
        QMetaObject::invokeMethod(
            context,
            [this, context]() {
                delete context;
                impl_->thread.quit();
            },
            Qt::QueuedConnection);

        if (QThread::currentThread() != &impl_->thread)
        {
            impl_->thread.wait(timeoutMs);
        }
    }

    bool SerialWorker::isRunning() const
    {
        return impl_->thread.isRunning() && !impl_->stopped.load(std::memory_order_acquire);
    }

    bool SerialWorker::isWorkerThread() const
    {
        return QThread::currentThread() == &impl_->thread;
    }

} // namespace sonora::platform::net
