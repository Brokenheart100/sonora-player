#include "sonora/platform/audio/FfmpegOutput.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QCoreApplication>
#include <QDebug>
#include <QMediaDevices>
#include <QMetaObject>

#include "sonora/platform/audio/AudioOutDevice.h"
#include "sonora/platform/audio/AudioRing.h"
#include "sonora/platform/audio/FfmpegDecoder.h"

namespace sonora::platform::audio {

using playback::contracts::ErrorListener;
using playback::contracts::FinishedListener;
using playback::contracts::PlaybackState;
using playback::contracts::StateListener;

namespace {

kernel::Error makeError(kernel::ErrorCode code, std::string message)
{
    return kernel::Error{code, std::move(message)};
}

}  // namespace

struct FfmpegOutput::Impl {
    // ── 线程模型 ────────────────────────────────────────────────────────────
    // 打开文件（avformat 探测、读容器头）对未缓存的文件是几百毫秒级，
    // 绝不能在命令线程上做——`Handler < 5ms` 那条规矩就是被这一步打破的
    // （实测 224.97ms，PlayerSnapshot 之外的第一条 warn 就是它）。
    //
    // 所以：decoder 归解码线程独占，open 也在它上面跑；QAudioSink 属于 Qt 对象，
    // 只能在 GUI 线程建，于是解码线程开完文件后"请求"GUI 线程建 sink，
    // 自己在 readyCv 上等（等的是自己，不是命令线程）。

    FfmpegDecoder decoder;

    // 2 秒 @ 48kHz 立体声（向上取整到 262144 个样本）。深度决定两件事：
    // 解码抖动能吸收多少，以及 seek 之后旧声音会不会残留。
    AudioRing ring{48000 * 2 * 2};

    std::unique_ptr<AudioOutDevice> device;
    std::unique_ptr<QAudioSink> sink;

    std::thread worker;
    std::mutex wakeMutex;
    std::condition_variable wake;

    std::mutex readyMutex;
    std::condition_variable readyCv;
    bool sinkReady = false;

    std::atomic<bool> running{false};
    std::atomic<bool> playing{false};
    std::atomic<bool> decoderOpen{false};
    std::atomic<std::int64_t> seekRequestMs{-1};   // < 0 表示没有请求
    std::atomic<std::int32_t> volume{80};
    std::atomic<std::int64_t> durationMs{0};
    std::atomic<std::int64_t> seekStartSamples{0};
    // 跳转起点（毫秒）。位置现在以 QAudioSink 的 processedUSecs() 为基准，
    // 得靠这个基点把它换算成"这首歌里的绝对时间"。
    std::atomic<std::int64_t> seekStartMs{0};
    std::atomic<std::int64_t> writtenSamples{0};
    std::atomic<std::int64_t> lastPositionMs{0};
    std::atomic<PlaybackState> state{PlaybackState::Idle};

    // 只有解码线程写、GUI 线程只读的"待打开路径"。
    std::mutex requestMutex;
    std::string pendingPath;
    bool openRequested = false;

    int sampleRate = 48000;
    int channels = 2;

    StateListener onState;
    FinishedListener onFinished;
    ErrorListener onError;

    // 队列化回调可能比对象活得久（GUI 线程还没跑到就析构了），每个回调先看这个。
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    std::int64_t positionMs() const noexcept
    {
        const std::int64_t total = durationMs.load(std::memory_order_relaxed);

        // 真正的播放头是 QAudioSink 自己报的 processedUSecs()：它数的是"已经喂给
        // 声卡、放出来了"的微秒数。
        //
        // 之前用的是"环里被拉走的样本数"，那个数比耳朵听到的早整整两段缓冲
        // （我们的环最多压 2.9 秒，sink 内部再压一截），表现就是歌词抢先唱、
        // 进度条也超前。这是 M8 之后用户报的"歌词不同步"的根因。
        if (sink != nullptr) {
            const std::int64_t base = seekStartMs.load(std::memory_order_relaxed);
            const auto playedUs = sink->processedUSecs();
            const std::int64_t played = static_cast<std::int64_t>(playedUs / 1000);
            std::int64_t ms = base + std::max<std::int64_t>(0, played);
            if (total > 0 && ms > total) {
                ms = total;
            }
            return ms;
        }

        // sink 还没建好（load 刚返回、GUI 线程还没跑到 buildAudio）：退回样本计数。
        if (sampleRate <= 0) {
            return 0;
        }
        const std::int64_t written = writtenSamples.load(std::memory_order_acquire);
        const auto available = static_cast<std::int64_t>(ring.availableToRead());
        const std::int64_t consumed = std::max<std::int64_t>(0, written - available);
        // 两个量都是**样本**，必须先加再换成毫秒。分两步写会把 seek 起点当成
        // 毫秒直接用——跳到 50% 时位置会显示成 45:28（样本数当毫秒）。
        const std::int64_t samples = seekStartSamples.load(std::memory_order_relaxed) + consumed;
        std::int64_t ms = samples * 1000 / sampleRate;
        // 第二道闸：容器跳转的落点不精确，宁可让进度条停在末尾，也不要越过总时长。
        if (total > 0 && ms > total) {
            ms = total;
        }
        return ms;
    }

    void emitState(PlaybackState next)
    {
        state.store(next, std::memory_order_relaxed);
        auto callback = onState;
        if (!callback) {
            return;
        }
        auto flag = alive;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [flag, callback, next] {
                if (flag->load()) {
                    callback(next);
                }
            },
            Qt::QueuedConnection);
    }

    void emitFinished()
    {
        auto callback = onFinished;
        if (!callback) {
            return;
        }
        auto flag = alive;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [flag, callback] {
                if (flag->load()) {
                    callback();
                }
            },
            Qt::QueuedConnection);
    }

    void emitError(const std::string& message)
    {
        auto callback = onError;
        if (!callback) {
            return;
        }
        auto flag = alive;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [flag, callback, message] {
                if (flag->load()) {
                    callback(message);
                }
            },
            Qt::QueuedConnection);
    }

    void fail(const std::string& message)
    {
        decoderOpen.store(false, std::memory_order_release);
        playing.store(false);
        if (decoder.cancelled()) {
            // 读被打断 = 用户切歌/停止/关窗，不是文件坏了。安静回到 Idle，
            // 别弹错误、也别写 warn —— 否则每次切歌都会在日志里留下一条假的失败。
            emitState(PlaybackState::Idle);
            return;
        }
        qWarning("[ffmpeg] %s", message.c_str());
        emitState(PlaybackState::Failed);
        emitError(message);
    }

    // ── GUI 线程：按解码线程给的格式把 sink 建起来 ──────────────────────────
    void buildAudio(int rate, int channelCount)
    {
        const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
        if (outputDevice.isNull()) {
            fail("没有可用的音频输出设备");
            return;
        }

        QAudioFormat audioFormat;
        audioFormat.setSampleRate(rate);
        audioFormat.setChannelCount(channelCount);
        audioFormat.setSampleFormat(QAudioFormat::Float);

        auto deviceFormat = AudioOutDevice::SampleFormat::Int16;
        if (outputDevice.isFormatSupported(audioFormat)) {
            deviceFormat = AudioOutDevice::SampleFormat::Float32;
        } else {
            // 老设备的 WASAPI 共享模式只认 int16：在设备里转一次，
            // 好过让整条链路都降级。
            audioFormat.setSampleFormat(QAudioFormat::Int16);
            if (!outputDevice.isFormatSupported(audioFormat)) {
                fail("输出设备不支持这个采样格式");
                return;
            }
            qInfo("[ffmpeg] 设备只支持 int16，启用转换");
        }

        device = std::make_unique<AudioOutDevice>(ring, channelCount, deviceFormat);
        sink = std::make_unique<QAudioSink>(outputDevice, audioFormat);
        sink->setVolume(static_cast<qreal>(volume.load()) / 100.0);

        // 设备侧缓冲定死 125ms。默认值在 Windows 共享模式下可以到几百毫秒，
        // 那段时间里"已经喂进去"和"耳朵听到"是两个时间点——进度条和歌词都会跑在前面
        // （用户报的"歌词不同步"有一半是它）。太小又会欠载爆音，125ms 是折中。
        const int bytesPerFrame = channelCount * (deviceFormat == AudioOutDevice::SampleFormat::Float32 ? 4 : 2);
        const int bufferBytes = std::max(8192, (rate / 8) * bytesPerFrame);
        sink->setBufferSize(bufferBytes);

        {
            std::lock_guard<std::mutex> lock(readyMutex);
            sinkReady = true;
        }
        readyCv.notify_all();

        // 用户可能已经按了播放（load 与 play 在同一个命令里连着调用）。
        if (playing.load() && sink->state() != QAudio::ActiveState) {
            sink->start(device.get());
        }
        emitState(playing.load() ? PlaybackState::Playing : PlaybackState::Idle);
    }

    // ── 解码线程：打开文件，然后把建 sink 的活交给 GUI 线程 ─────────────────
    bool openOnWorker(const std::string& path)
    {
        auto opened = decoder.open(path);
        if (!opened.hasValue()) {
            fail(opened.error().message());
            return false;
        }
        decoderOpen.store(true, std::memory_order_release);
        const auto format = opened.value();
        sampleRate = format.sampleRate;
        channels = format.channels;
        durationMs.store(format.durationMs);
        seekStartSamples.store(0);
        seekStartMs.store(0);
        writtenSamples.store(0);
        lastPositionMs.store(0);
        ring.clear();

        {
            std::lock_guard<std::mutex> lock(readyMutex);
            sinkReady = false;
        }

        const int rate = format.sampleRate;
        const int chans = format.channels;
        auto flag = alive;
        auto* self = this;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [flag, self, rate, chans] {
                if (flag->load()) {
                    self->buildAudio(rate, chans);
                }
            },
            Qt::QueuedConnection);
        return true;
    }

    bool applySeek(std::int64_t ms)
    {
        auto result = decoder.seekTo(ms);
        if (!result.hasValue()) {
            fail(result.error().message());
            return false;
        }
        // 顺序不能反：先清环形缓冲再重置计数，否则 UI 会读到
        // "位置倒流 + 缓冲里还是旧歌"的组合。
        ring.clear();
        writtenSamples.store(0, std::memory_order_release);
        seekStartSamples.store(ms * sampleRate / 1000, std::memory_order_relaxed);
        seekStartMs.store(ms, std::memory_order_relaxed);
        lastPositionMs.store(ms);

        // 光清我们自己的环不够：QAudioSink 内部和 WASAPI 里还压着跳转前的声音，
        // 而且实测直接继续灌数据会让 sink 僵住约十秒（环写满、它不取）。
        // reset() 丢掉设备侧缓冲，再 start() 重新对齐——顺带解决"跳完还听见上一段"。
        QMetaObject::invokeMethod(qApp, [this] {
            if (sink != nullptr && device != nullptr) {
                sink->reset();
                if (playing.load(std::memory_order_acquire)) {
                    sink->start(device.get());
                }
            }
        }, Qt::QueuedConnection);
        return true;
    }

    void workerLoop()
    {
        using clock = std::chrono::steady_clock;
        clock::time_point endReachedAt{};
        bool endNoticed = false;
        // 解码这一轮一点数据都没产出、又不是播完 —— 从哪一刻开始的。
        // 网络读在等数据时就是这样（av_read_frame 内部阻塞），本地文件几乎不会。
        clock::time_point stallSince{};

        while (running.load(std::memory_order_acquire)) {
            // 1) 有没有待打开的文件
            std::string path;
            bool shouldOpen = false;
            {
                std::lock_guard<std::mutex> lock(requestMutex);
                if (openRequested) {
                    shouldOpen = true;
                    openRequested = false;
                    path = pendingPath;
                    pendingPath.clear();
                }
            }
            if (shouldOpen) {
                endNoticed = false;
                if (!openOnWorker(path)) {
                    return;
                }
            }

            if (!decoderOpen.load(std::memory_order_acquire)) {
                waitForWork(std::chrono::milliseconds(20));
                continue;
            }

            // 2) sink 还没建好就等着（建 sink 在 GUI 线程上）
            {
                std::unique_lock<std::mutex> lock(readyMutex);
                if (!readyCv.wait_for(lock, std::chrono::milliseconds(20),
                                      [this] { return sinkReady || !running.load(); })) {
                    continue;
                }
                if (!sinkReady) {
                    continue;
                }
            }

            if (!playing.load(std::memory_order_acquire)) {
                waitForWork(std::chrono::milliseconds(20));
                continue;
            }

            const std::int64_t requested = seekRequestMs.exchange(-1);
            if (requested >= 0) {
                if (!applySeek(requested)) {
                    return;
                }
                endNoticed = false;
            }

            auto pumped = decoder.pump(ring);
            if (!pumped.hasValue()) {
                fail(pumped.error().message());
                return;
            }
            if (pumped.value() > 0) {
                writtenSamples.fetch_add(static_cast<std::int64_t>(pumped.value()),
                                         std::memory_order_acq_rel);
                endNoticed = false;
                stallSince = clock::time_point{};
                if (state.load(std::memory_order_relaxed) == PlaybackState::Buffering) {
                    // 数据回来了：从"缓冲中"回到"播放中"，界面上的提示随之消失。
                    emitState(PlaybackState::Playing);
                }
            } else if (!decoder.atEnd()) {
                const auto now = clock::now();
                if (stallSince == clock::time_point{}) {
                    stallSince = now;
                } else if (now - stallSince > std::chrono::milliseconds(400)
                           && state.load(std::memory_order_relaxed) == PlaybackState::Playing) {
                    // 连续 400ms 没解出数据，又没到结尾：网络在等。报"缓冲中"，
                    // 而不是让界面一直显示"播放中"（用户听到的是卡住）。
                    emitState(PlaybackState::Buffering);
                }
            }

            if (decoder.atEnd() && ring.empty()) {
                // sink 内部还压着几百毫秒的缓冲，立刻报"播完"会切得突兀。
                if (!endNoticed) {
                    endNoticed = true;
                    endReachedAt = clock::now();
                } else if (clock::now() - endReachedAt > std::chrono::milliseconds(250)) {
                    playing.store(false);
                    emitState(PlaybackState::Stopped);
                    emitFinished();
                    return;
                }
            }

            if (pumped.value() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
    }

    void waitForWork(std::chrono::milliseconds span)
    {
        std::unique_lock<std::mutex> lock(wakeMutex);
        wake.wait_for(lock, span, [this] {
            std::lock_guard<std::mutex> reqLock(requestMutex);
            return !running.load() || playing.load() || seekRequestMs.load() >= 0
                || openRequested;
        });
    }

    void stopWorker()
    {
        // 顺序很重要：先打断，再清 running。
        // av_read_frame / avformat_open_input 在网络上是阻塞的，不打断的话
        // join() 会一直等（http 没设 rw_timeout 时能等十几秒以上），
        // 表现就是"切歌时界面卡住、只能杀进程"。
        decoder.cancel();
        running.store(false, std::memory_order_release);
        wake.notify_all();
        readyCv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
    }

    void teardownAudio()
    {
        {
            std::lock_guard<std::mutex> lock(readyMutex);
            sinkReady = false;
        }
        if (sink != nullptr) {
            sink->stop();
            sink.reset();
        }
        device.reset();
    }
};

FfmpegOutput::FfmpegOutput()
    : impl_(std::make_unique<Impl>())
{
}

FfmpegOutput::~FfmpegOutput()
{
    impl_->alive->store(false);
    impl_->playing.store(false);
    impl_->stopWorker();
    impl_->teardownAudio();
    impl_->decoder.close();
    impl_->decoderOpen.store(false, std::memory_order_release);
}

kernel::Result<void> FfmpegOutput::load(std::string_view utf8Path)
{
    // 只做"登记 + 起线程"，真正耗时的事在解码线程上。
    impl_->playing.store(false, std::memory_order_release);
    impl_->stopWorker();
    impl_->teardownAudio();
    impl_->decoder.close();
    impl_->decoderOpen.store(false, std::memory_order_release);
    impl_->ring.clear();
    impl_->durationMs.store(0);
    impl_->seekStartSamples.store(0);
    impl_->seekStartMs.store(0);
    impl_->writtenSamples.store(0);
    impl_->lastPositionMs.store(0);

    {
        std::lock_guard<std::mutex> lock(impl_->requestMutex);
        impl_->pendingPath.assign(utf8Path);
        impl_->openRequested = true;
    }

    impl_->state.store(PlaybackState::Loading);
    impl_->running.store(true, std::memory_order_release);
    impl_->worker = std::thread([impl = impl_.get()] { impl->workerLoop(); });

    impl_->emitState(PlaybackState::Loading);
    return kernel::Result<void>::ok();
}

kernel::Result<void> FfmpegOutput::play()
{
    impl_->playing.store(true, std::memory_order_release);
    impl_->wake.notify_all();

    // sink 可能还没建好（load 刚返回），buildAudio 里会补上 start。
    auto *sink = impl_->sink.get();
    if (sink != nullptr) {
        if (sink->state() == QAudio::SuspendedState) {
            // 从暂停回来必须走 resume()。对已 suspend 的 sink 调 start() 只会
            // 打出 "QAudioSink::start() called while already started"，一点声音都不出。
            sink->resume();
        } else if (sink->state() != QAudio::ActiveState) {
            sink->start(impl_->device.get());
        }
        // 状态必须发出去。之前这里不发，PlaybackService 就一直停在 Paused：
        // 界面上按钮还写着「播放」、再按一次又走 play() 分支——用户看到的就是
        // "按了播放没反应、播放/暂停乱跳"（实测 resume 之后 p=false、面板显示已暂停，
        // 而声音已经在放了）。
        impl_->emitState(PlaybackState::Playing);
    }
    return kernel::Result<void>::ok();
}

kernel::Result<void> FfmpegOutput::pause()
{
    impl_->playing.store(false, std::memory_order_release);
    if (impl_->sink != nullptr) {
        impl_->sink->suspend();
    }
    impl_->emitState(PlaybackState::Paused);
    return kernel::Result<void>::ok();
}

kernel::Result<void> FfmpegOutput::stop()
{
    impl_->playing.store(false, std::memory_order_release);
    impl_->stopWorker();
    impl_->teardownAudio();
    impl_->decoder.close();
    impl_->decoderOpen.store(false, std::memory_order_release);
    impl_->ring.clear();
    impl_->seekStartSamples.store(0);
    impl_->seekStartMs.store(0);
    impl_->writtenSamples.store(0);
    impl_->lastPositionMs.store(0);
    // 不清 durationMs：播完之后界面上还该看得见"这首歌有多长"，
    // 显示 --:-- 会让人以为刚刚那首不存在。下一次 openOnWorker 会覆盖它。
    impl_->emitState(PlaybackState::Idle);
    return kernel::Result<void>::ok();
}

kernel::Result<void> FfmpegOutput::seekMs(std::int64_t ms)
{
    const std::int64_t duration = impl_->durationMs.load();
    const std::int64_t target = std::clamp<std::int64_t>(ms, 0, duration > 0 ? duration : std::max<std::int64_t>(ms, 0));

    // 真正动解码器的是解码线程（它独占 decoder），这里只递请求。
    impl_->seekRequestMs.store(target);
    impl_->wake.notify_all();
    return kernel::Result<void>::ok();
}

kernel::Result<void> FfmpegOutput::setVolume(std::int32_t value)
{
    const std::int32_t clamped = std::clamp<std::int32_t>(value, 0, 100);
    impl_->volume.store(clamped);
    if (impl_->sink != nullptr) {
        impl_->sink->setVolume(static_cast<qreal>(clamped) / 100.0);
    }
    return kernel::Result<void>::ok();
}

std::int32_t FfmpegOutput::volume() const noexcept
{
    return impl_->volume.load();
}

std::int64_t FfmpegOutput::durationMs() const noexcept
{
    const std::int64_t value = impl_->durationMs.load();
    // 0 在界面上会被当成"零长度的歌"。用 -1 明确表达"时长未知"（网络流、
    // 没写总长的容器），formatClock 对 <= 0 一律显示 --:--。
    return value > 0 ? value : -1;
}

std::int32_t FfmpegOutput::bufferedPercent() const noexcept
{
    // 解码线程在每次读到 packet 后刷新这个原子；本地文件会是 -1（无意义）。
    return impl_->decoder.bufferedPercent();
}

PlaybackState FfmpegOutput::state() const noexcept
{
    return impl_->state.load();
}

std::int64_t FfmpegOutput::positionMs() const noexcept
{
    return impl_->positionMs();
}

void FfmpegOutput::setStateListener(playback::contracts::StateListener listener)
{
    impl_->onState = std::move(listener);
}

void FfmpegOutput::setFinishedListener(playback::contracts::FinishedListener listener)
{
    impl_->onFinished = std::move(listener);
}

void FfmpegOutput::setErrorListener(playback::contracts::ErrorListener listener)
{
    impl_->onError = std::move(listener);
}

}  // namespace sonora::platform::audio
