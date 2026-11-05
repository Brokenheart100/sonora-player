// 播放服务：把"队列"和"一个音频输出"接起来的应用层对象。
//
// 它不是 Handler 本身，而是几个 Handler 共用的状态持有者——命令总线是同步的，
// 而播放状态必须活在命令之外（否则每来一个命令都要重建底层播放器）。
#pragma once

#include <cstdint>
#include <string>

#include <mamba/kernel/ILogger.h>
#include <mamba/kernel/Result.h>
#include <mamba/playback/contracts/AudioOutput.h>
#include <mamba/playback/contracts/Commands.h>
#include <mamba/playback/core/domain/PlayQueue.h>

namespace mamba::playback::core {

class PlaybackService {
public:
    /// output 的生命周期必须长于本对象：服务在析构前不会自动摘回调，
    /// 所以由组合根保证"先建 output，再建 service"。
    explicit PlaybackService(contracts::IAudioOutput& output, kernel::ILogger* logger = nullptr);
    ~PlaybackService();

    PlaybackService(const PlaybackService&) = delete;
    PlaybackService& operator=(const PlaybackService&) = delete;

    // ── 命令 ────────────────────────────────────────────────────────────────
    kernel::Result<void> playTracks(const contracts::PlayTracks& command);
    kernel::Result<void> togglePlayPause();
    kernel::Result<void> seekTo(std::int64_t positionMs);
    kernel::Result<void> setVolume(std::int32_t volume);
    kernel::Result<void> next();
    kernel::Result<void> previous();

    // ── 查询 ────────────────────────────────────────────────────────────────
    [[nodiscard]] contracts::PlayerSnapshot snapshot() const;
    [[nodiscard]] const PlayQueue& queue() const noexcept { return queue_; }

private:
    kernel::Result<void> loadCurrentAndPlay();
    void handleFinished();

    contracts::IAudioOutput& output_;
    kernel::ILogger* logger_ = nullptr;
    PlayQueue queue_;
    std::int32_t volume_ = 100;
};

}  // namespace mamba::playback::core
