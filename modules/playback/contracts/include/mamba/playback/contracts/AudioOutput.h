// 音频输出端口 —— 归**消费方**所有（Playback），实现方是 Platform.Audio。
//
// 为什么放在 Contracts 而不是 Core：实现方（platform/audio）只允许依赖本模块的
// 公开契约，不许依赖 Core——它不该知道 PlayQueue 的存在。放 Contracts 之后，
// 依赖箭头是 platform/audio → Playback.Contracts，方向仍然指向抽象。
//
// 这里的类型全部是纯 C++（std::string / int64 / std），没有 Qt、没有 FFmpeg。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <mamba/kernel/Result.h>

namespace mamba::playback::contracts {

enum class PlaybackState : std::uint8_t {
    Idle,     // 什么都没装载
    Loading,  // 正在打开文件（网络流会更久）
    Playing,
    Paused,
    Stopped,  // 播完了或主动停
    Failed,   // 打开或解码失败
};

// 叫 toName 不叫 toString：kernel 里有一次教训——doctest 的无限定 toString
// 会通过 ADL 把我们的 toString 拉进候选集，非模板精确匹配赢过它的模板，
// 结果 CHECK(enum == enum) 编译失败。
constexpr std::string_view toName(PlaybackState state) noexcept {
    switch (state) {
        case PlaybackState::Idle: return "idle";
        case PlaybackState::Loading: return "loading";
        case PlaybackState::Playing: return "playing";
        case PlaybackState::Paused: return "paused";
        case PlaybackState::Stopped: return "stopped";
        case PlaybackState::Failed: return "failed";
    }
    return "unknown";
}

// 播放结束的回调。**在音频/UI 线程上被调用**，实现方不许在里面做重活：
// 只该改状态，真正的"下一首"由上层决定。
using StateListener = std::function<void(PlaybackState)>;
using FinishedListener = std::function<void()>;
using ErrorListener = std::function<void(std::string)>;

/// 一次只能装一首。要"播放队列"是 Playback 模块的事，不是播放器的事。
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    /// 装载一个本地文件（UTF-8 路径）。成功不代表已开始播放。
    virtual kernel::Result<void> load(std::string_view filePathUtf8) = 0;

    virtual kernel::Result<void> play() = 0;
    virtual kernel::Result<void> pause() = 0;
    virtual kernel::Result<void> stop() = 0;

    /// 定位。超出 [0, durationMs] 由实现方夹取，不返回错误。
    virtual kernel::Result<void> seekMs(std::int64_t positionMs) = 0;

    /// 0..100，超出夹取。
    virtual kernel::Result<void> setVolume(std::int32_t volume) = 0;

    [[nodiscard]] virtual std::int32_t volume() const noexcept = 0;
    [[nodiscard]] virtual std::int64_t durationMs() const noexcept = 0;
    [[nodiscard]] virtual PlaybackState state() const noexcept = 0;

    // ── 回调：实现方在状态变化时通知上层 ────────────────────────────────────
    virtual void setStateListener(StateListener listener) = 0;
    virtual void setFinishedListener(FinishedListener listener) = 0;
    virtual void setErrorListener(ErrorListener listener) = 0;
};

/// 播放位置的**高频旁路**（docs/ARCHITECTURE.md §7.1）。
///
/// 位置每秒要刷新 30 次；把它塞进命令总线或领域事件，等于用事务和事件总线
/// 去送一个本来只是一次原子读的数字。所以：音频侧写一个 std::atomic，
/// UI 侧 30Hz 读——没有锁、没有队列、没有事件。
class IPositionStream {
public:
    virtual ~IPositionStream() = default;

    /// 无锁读。允许被 UI 线程高频调用。
    [[nodiscard]] virtual std::int64_t positionMs() const noexcept = 0;
};

}  // namespace mamba::playback::contracts
