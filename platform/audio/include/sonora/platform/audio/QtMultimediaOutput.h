// QtMultimedia 实现：IAudioOutput + IPositionStream。
//
// 这是 M3 的**过渡引擎**。M4 会用 FFmpeg 解码 + QAudioSink 替换它，但接口不变——
// 这正是当初把端口放在 Contracts 里的原因：换引擎时上层的 Playback 一行都不用动。
//
// 位置走 std::atomic：QMediaPlayer::positionChanged 的线程不确定，而 UI 侧
// 每 33ms 读一次，中间不需要锁，也不需要把信号转发成命令。
//
// **这个头里一个 Qt 类型都没有**（QMediaPlayer 藏在 Impl 里）。原因很实际：
// 头文件里一旦写了 QAudioOutput，所有 include 它的 target 都必须能看见 Qt 的
// include 目录，而 Qt 的 includedirs 是 private 的，不会顺着 add_deps 传出去。
// 藏起来之后，host 和测试只认识端口，平台细节留在 .cpp 里。
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <sonora/playback/contracts/AudioOutput.h>

namespace sonora::platform::audio
{

    class QtMultimediaOutput final : public playback::contracts::IAudioOutput,
                                     public playback::contracts::IPositionStream
    {
    public:
        QtMultimediaOutput();
        ~QtMultimediaOutput() override;

        QtMultimediaOutput(const QtMultimediaOutput &) = delete;
        QtMultimediaOutput &operator=(const QtMultimediaOutput &) = delete;

        // ── IAudioOutput ────────────────────────────────────────────────────────
        kernel::Result<void> load(std::string_view filePathUtf8) override;
        kernel::Result<void> play() override;
        kernel::Result<void> pause() override;
        kernel::Result<void> stop() override;
        kernel::Result<void> seekMs(std::int64_t positionMs) override;
        kernel::Result<void> setVolume(std::int32_t volume) override;

        [[nodiscard]] std::int32_t volume() const noexcept override;
        [[nodiscard]] std::int64_t durationMs() const noexcept override;
        [[nodiscard]] playback::contracts::PlaybackState state() const noexcept override;

        void setStateListener(playback::contracts::StateListener listener) override;
        void setFinishedListener(playback::contracts::FinishedListener listener) override;
        void setErrorListener(playback::contracts::ErrorListener listener) override;

        // ── IPositionStream ─────────────────────────────────────────────────────
        [[nodiscard]] std::int64_t positionMs() const noexcept override;

    private:
        // Impl 定义在 .cpp 里，只有它认识 Qt。
        struct Impl;

        void setState(playback::contracts::PlaybackState state);

        std::unique_ptr<Impl> impl_;

        std::string sourcePath_;
        std::atomic<std::int64_t> positionMs_{0};
        std::atomic<std::int64_t> durationMs_{0};
        std::atomic<std::int32_t> volume_{100};
        playback::contracts::PlaybackState state_ = playback::contracts::PlaybackState::Idle;

        playback::contracts::StateListener stateListener_;
        playback::contracts::FinishedListener finishedListener_;
        playback::contracts::ErrorListener errorListener_;
    };

} // namespace sonora::platform::audio
