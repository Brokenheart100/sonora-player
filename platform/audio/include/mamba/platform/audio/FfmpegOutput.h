#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include <mamba/kernel/Result.h>
#include <mamba/playback/contracts/AudioOutput.h>

namespace mamba::platform::audio {

// FFmpeg 版音频输出：解码线程 → AudioRing → QAudioSink（拉模式）。
//
// 与 M3 的 QtMultimediaOutput 完全同构（同一个端口），所以 Host 里换实现只是
// 换一行 new。区别在于这里掌握了解码过程本身——M5 之后的 DSP（淡入淡出、
// 变速变调、频谱）都要挂在这条链路上，而 QMediaPlayer 不给插手的地方。
//
// 线程纪律：
//   - 解码线程：持有 FfmpegDecoder + 往 AudioRing 写
//   - Qt 音频线程：AudioOutDevice::readData 从 AudioRing 读
//   - GUI 线程：play/pause/seek/load，全部通过原子标志与条件变量驱动解码线程
//   - 三个 listener 一律**回投到 GUI 线程**再调，否则 PlaybackService 会在
//     音频线程上被改状态
class FfmpegOutput final : public playback::contracts::IAudioOutput,
                           public playback::contracts::IPositionStream {
public:
    FfmpegOutput();
    ~FfmpegOutput() override;

    FfmpegOutput(const FfmpegOutput&) = delete;
    FfmpegOutput& operator=(const FfmpegOutput&) = delete;

    // ── IAudioOutput ────────────────────────────────────────────────────────
    kernel::Result<void> load(std::string_view utf8Path) override;
    kernel::Result<void> play() override;
    kernel::Result<void> pause() override;
    kernel::Result<void> stop() override;
    kernel::Result<void> seekMs(std::int64_t ms) override;
    kernel::Result<void> setVolume(std::int32_t value) override;
    std::int32_t volume() const noexcept override;
    std::int64_t durationMs() const noexcept override;
    playback::contracts::PlaybackState state() const noexcept override;
    void setStateListener(playback::contracts::StateListener listener) override;
    void setFinishedListener(playback::contracts::FinishedListener listener) override;
    void setErrorListener(playback::contracts::ErrorListener listener) override;

    // ── IPositionStream ─────────────────────────────────────────────────────
    std::int64_t positionMs() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mamba::platform::audio
