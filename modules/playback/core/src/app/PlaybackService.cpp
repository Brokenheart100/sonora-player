#include <mamba/playback/core/app/PlaybackService.h>

#include <algorithm>
#include <chrono>

#include <mamba/contracts/PlaybackEvents.h>
#include <mamba/kernel/Error.h>

namespace mamba::playback::core {
namespace {

std::int32_t clampVolume(std::int32_t value) noexcept {
    return std::clamp(value, 0, 100);
}

/// 「播过 3 秒，上一首先回开头」——业界惯例，用户按上一首时其实想重听这曲。
constexpr std::int64_t kRewindThresholdMs = 3000;

} // namespace

PlaybackService::PlaybackService(contracts::IAudioOutput& output, kernel::ILogger* logger,
                                 const contracts::IPositionStream* position,
                                 kernel::IEventBus* events)
    : output_(output), logger_(logger), position_(position), events_(events) {
    (void)output_.setVolume(volume_);

    // 播完自动下一首。这个回调**最终落在 GUI 线程**上：FfmpegOutput 的
    // emitFinished() 是队列化调用，投回主线程之后才调它
    // （见 platform/audio/src/FfmpegOutput.cpp 的 emitFinished）。
    // 所以这里同步发事件、同步写一次库都是安全的，不需要额外投任务队列。
    output_.setFinishedListener([this] { handleFinished(); });

    output_.setStateListener([this](contracts::PlaybackState state) {
        if (state == contracts::PlaybackState::Failed && logger_ != nullptr) {
            logger_->warn("音频输出进入 Failed 状态：{}", queue_.current() != nullptr
                                                               ? queue_.current()->filePath
                                                               : std::string{"<无曲目>"});
        }
    });
}

PlaybackService::~PlaybackService() {
    // 摘掉回调：本对象先死时，别让 output 回调打进已经析构的 this。
    output_.setFinishedListener(nullptr);
    output_.setStateListener(nullptr);
    output_.setErrorListener(nullptr);
}

kernel::Result<void> PlaybackService::playTracks(const contracts::PlayTracks& command) {
    if (command.entries.empty()) {
        return kernel::Error{kernel::ErrorCode::InvalidArgument, "播放列表是空的"};
    }
    queue_.setEntries(command.entries, command.startIndex);
    return loadCurrentAndPlay();
}

kernel::Result<void> PlaybackService::togglePlayPause() {
    switch (output_.state()) {
        case contracts::PlaybackState::Playing:
            return output_.pause();

        case contracts::PlaybackState::Paused:
            return output_.play();

        case contracts::PlaybackState::Loading:
        case contracts::PlaybackState::Idle:
        case contracts::PlaybackState::Stopped:
        case contracts::PlaybackState::Failed:
            // 没装载过、或者刚播完：重新装载当前曲目再放。
            if (queue_.current() == nullptr) {
                return kernel::Error{kernel::ErrorCode::InvalidArgument, "还没有选曲"};
            }
            return loadCurrentAndPlay();
    }
    return kernel::Error{kernel::ErrorCode::Internal, "未知的播放状态"};
}

kernel::Result<void> PlaybackService::seekTo(std::int64_t positionMs) {
    if (queue_.current() == nullptr) {
        return kernel::Error{kernel::ErrorCode::InvalidArgument, "还没有选曲"};
    }
    const auto duration = output_.durationMs();
    const auto clamped = std::clamp<std::int64_t>(positionMs, 0, duration > 0 ? duration : 0);
    return output_.seekMs(clamped);
}

kernel::Result<void> PlaybackService::setVolume(std::int32_t volume) {
    volume_ = clampVolume(volume);
    return output_.setVolume(volume_);
}

kernel::Result<void> PlaybackService::next() {
    if (!queue_.advance(AdvanceReason::Manual)) {
        (void)output_.stop();
        return kernel::Error{kernel::ErrorCode::NotFound, "已经是最后一首"};
    }
    return loadCurrentAndPlay();
}

kernel::Result<void> PlaybackService::previous() {
    // 惯例：播过 3 秒就先回本曲开头，再按一次才真的换上一首。
    if (position_ != nullptr && position_->positionMs() > kRewindThresholdMs) {
        return output_.seekMs(0);
    }
    if (!queue_.retreat()) {
        return output_.seekMs(0);
    }
    return loadCurrentAndPlay();
}

kernel::Result<void> PlaybackService::setMode(contracts::PlaybackMode mode) {
    // 换模式不动播放：正在放的这首保持不动（PlayQueue 保证这点）。
    queue_.setMode(mode);
    if (logger_ != nullptr) {
        logger_->info("播放模式：{}", contracts::toName(mode));
    }
    return kernel::Result<void>::ok();
}

kernel::Result<void> PlaybackService::jumpTo(std::uint64_t trackId) {
    const auto& entries = queue_.entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].trackId != trackId) {
            continue;
        }
        if (!queue_.jumpTo(static_cast<std::int32_t>(i))) {
            break;
        }
        return loadCurrentAndPlay();
    }
    return kernel::Error{kernel::ErrorCode::NotFound, "队列里没有这首歌"};
}

contracts::PlayerSnapshot PlaybackService::snapshot() const {
    contracts::PlayerSnapshot snapshot;
    snapshot.volume = volume_;
    snapshot.state = output_.state();
    snapshot.durationMs = output_.durationMs();
    snapshot.mode = queue_.mode();
    snapshot.queueIndex = queue_.index();
    snapshot.queueSize = queue_.size();

    if (const auto* entry = queue_.current(); entry != nullptr) {
        snapshot.hasTrack = true;
        snapshot.trackId = entry->trackId;
        snapshot.title = entry->title;
        snapshot.artistName = entry->artistName;
        snapshot.filePath = entry->filePath;
        snapshot.coverPath = entry->coverPath;
    }
    return snapshot;
}

contracts::QueueView PlaybackService::queueView() const {
    contracts::QueueView view;
    view.mode = queue_.mode();

    const auto currentIndex = queue_.index();
    const auto& entries = queue_.entries();
    view.items.reserve(queue_.order().size());
    for (const auto entryIndex : queue_.order()) {
        if (entryIndex < 0 || entryIndex >= static_cast<std::int32_t>(entries.size())) {
            continue;
        }
        const auto& entry = entries[static_cast<std::size_t>(entryIndex)];
        contracts::QueueItemView item;
        item.trackId = entry.trackId;
        item.title = entry.title;
        item.artistName = entry.artistName;
        item.current = entryIndex == currentIndex;
        if (item.current) {
            view.currentIndex = static_cast<std::int32_t>(view.items.size());
        }
        view.items.push_back(std::move(item));
    }
    return view;
}

kernel::Result<void> PlaybackService::loadCurrentAndPlay() {
    const auto* entry = queue_.current();
    if (entry == nullptr) {
        (void)output_.stop();
        return kernel::Error{kernel::ErrorCode::NotFound, "队列里没有当前曲目"};
    }
    if (auto loaded = output_.load(entry->filePath); !loaded) {
        if (logger_ != nullptr) {
            logger_->error("装载失败：{}（{}）", entry->filePath, loaded.error().message());
        }
        return std::move(loaded);  // 不自动跳过：一首坏文件不该让整个队列静默跳空
    }
    (void)output_.setVolume(volume_);
    return output_.play();
}

void PlaybackService::handleFinished() {
    // 先把"这首歌放完了"这件事发出去，再推进队列。
    // 顺序不能反：推进之后 queue_.current() 已经指向下一首了，
    // 事件的 trackId 就会记错人。
    if (events_ != nullptr && queue_.current() != nullptr) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        // 注意这里的 mamba:: 前缀：本文件在 mamba::playback::core 里，
        // 裸写 contracts:: 会解析到 playback 自己的契约，而不是跨模块的那份。
        const mamba::contracts::TrackFinished finished{queue_.current()->trackId, now};
        events_->publish(finished);
    }

    // AutoFinished 在单曲循环下返回 true 但游标不动 → 重新装载同一首。
    if (!queue_.advance(AdvanceReason::AutoFinished)) {
        // 队列播完：必须显式停掉输出。否则引擎还停在"正在播"的状态，
        // 界面上的按钮就永远显示"暂停"，位置也卡在末尾不再刷新。
        (void)output_.stop();
        if (logger_ != nullptr) {
            logger_->info("队列播完：共 {} 首", queue_.size());
        }
        return;
    }

    // 自动推进要能扛住"文件在播放前一刻被删掉"：一首装载失败就接着试下一首，
    // 最多绕一轮（不然整个队列都坏了会转成死循环）。
    // 手动切歌不走这条路——用户点"下一首"时应该立刻被告知失败，而不是静默跳走。
    const auto limit = queue_.size();
    for (std::int32_t attempt = 0; attempt < limit; ++attempt) {
        const auto loaded = loadCurrentAndPlay();
        if (loaded) {
            return;
        }
        if (logger_ != nullptr) {
            logger_->warn("装载失败，跳过这首：{}", loaded.error().message());
        }
        if (!queue_.advance(AdvanceReason::AutoFinished)) {
            break;
        }
    }

    (void)output_.stop();
    if (logger_ != nullptr) {
        logger_->warn("整个队列都放不出来，已停下");
    }
}

} // namespace mamba::playback::core
