// 播放服务：把"队列"和"一个音频输出"接起来的应用层对象。
//
// 它不是 Handler 本身，而是几个 Handler 共用的状态持有者——命令总线是同步的，
// 而播放状态必须活在命令之外（否则每来一个命令都要重建底层播放器）。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include <sonora/kernel/IEventBus.h>
#include <sonora/kernel/ILogger.h>
#include <sonora/kernel/Result.h>
#include <sonora/playback/contracts/AudioOutput.h>
#include <sonora/playback/contracts/Commands.h>
#include <sonora/playback/contracts/Queries.h>
#include <sonora/playback/core/domain/PlayQueue.h>

namespace sonora::playback::core {

class PlaybackService {
public:
    /// output 的生命周期必须长于本对象：服务在析构前不会自动摘回调，
    /// 所以由组合根保证"先建 output，再建 service"。
    ///
    /// position 可以为空（测试里就没有）：它只用来实现"播过 3 秒，
    /// 上一首先回开头"这条惯例，没有也不影响别的。
    ///
    /// events 也可以为空（测试里大多数用例不看事件）：它只用来在
    /// 「一首自然播完」时发布 TrackFinished，让 Catalog 去累计播放次数。
    explicit PlaybackService(contracts::IAudioOutput& output, kernel::ILogger* logger = nullptr,
                             const contracts::IPositionStream* position = nullptr,
                             kernel::IEventBus* events = nullptr);
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
    kernel::Result<void> setMode(contracts::PlaybackMode mode);
    kernel::Result<void> jumpTo(std::uint64_t trackId);
    kernel::Result<void> appendTracks(const contracts::AppendTracks& command);

    // ── 查询 ────────────────────────────────────────────────────────────────
    [[nodiscard]] contracts::PlayerSnapshot snapshot() const;
    [[nodiscard]] contracts::QueueView queueView() const;
    [[nodiscard]] const PlayQueue& queue() const noexcept { return queue_; }

    /// 「音量或播放模式变了」的通知口。
    ///
    /// 播放模块**不认识**设置模块（Preferences 是另一个业务模块），所以这里只开
    /// 一个回调；谁来存、存到哪儿由组合根决定。没有这个口，"上次拖到 40% 的音量"
    /// 每次启动都会回到 100。
    ///
    /// 回调在调用 setVolume/setMode 的线程上同步执行——它只写一行键值，
    /// 在 5ms 预算里没问题。别在里面重排数据库。
    using SettingsListener = std::function<void(std::int32_t volume, contracts::PlaybackMode mode)>;
    void onSettingsChanged(SettingsListener listener) { settingsListener_ = std::move(listener); }

private:
    kernel::Result<void> loadCurrentAndPlay();
    void handleFinished();

    contracts::IAudioOutput& output_;
    kernel::ILogger* logger_ = nullptr;
    const contracts::IPositionStream* position_ = nullptr;
    kernel::IEventBus* events_ = nullptr;
    PlayQueue queue_;
    std::int32_t volume_ = 100;
    // 追加式队列停在末尾、已经喊过 QueueNeedsMore：等新一批来了直接接上开播。
    bool waitingMore_ = false;
    // 组合根注入的设置回写口（音量/模式变化时调一次）。可以为空。
    SettingsListener settingsListener_{};
};

} // namespace sonora::playback::core
