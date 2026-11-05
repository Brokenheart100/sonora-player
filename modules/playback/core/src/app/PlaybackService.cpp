#include <mamba/playback/core/app/PlaybackService.h>

#include <algorithm>

#include <mamba/kernel/Error.h>

namespace mamba::playback::core {
namespace {

std::int32_t clampVolume(std::int32_t value) noexcept {
    return std::clamp(value, 0, 100);
}

}  // namespace

PlaybackService::PlaybackService(contracts::IAudioOutput& output, kernel::ILogger* logger)
    : output_(output), logger_(logger) {
    (void)output_.setVolume(volume_);

    // 播完自动下一首。注意这个回调是在**音频线程**上被调起来的，
    // 所以它只改状态并驱动一次装载——不做文件 IO、不做查询。
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
    if (!queue_.advance()) {
        (void)output_.stop();
        return kernel::Error{kernel::ErrorCode::NotFound, "已经是最后一首"};
    }
    return loadCurrentAndPlay();
}

kernel::Result<void> PlaybackService::previous() {
    // M3：严格"上一首"。业界那条"播过 3 秒先回开头"的惯例需要读播放位置，
    // 而位置在 IPositionStream 旁路里、服务当前不持有它——留到 M6 接进来。
    if (!queue_.retreat()) {
        return output_.seekMs(0);
    }
    return loadCurrentAndPlay();
}

contracts::PlayerSnapshot PlaybackService::snapshot() const {
    contracts::PlayerSnapshot snapshot;
    snapshot.volume = volume_;
    snapshot.state = output_.state();
    snapshot.durationMs = output_.durationMs();
    snapshot.queueIndex = queue_.index();
    snapshot.queueSize = queue_.size();

    if (const auto* entry = queue_.current(); entry != nullptr) {
        snapshot.hasTrack = true;
        snapshot.trackId = entry->trackId;
        snapshot.title = entry->title;
        snapshot.artistName = entry->artistName;
    }
    return snapshot;
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
    if (queue_.advance()) {
        (void)loadCurrentAndPlay();
        return;
    }
    // 队列播完：必须显式停掉输出。否则引擎还停在"正在播"的状态，
    // 界面上的按钮就永远显示"暂停"，位置也卡在末尾不再刷新。
    (void)output_.stop();
    if (logger_ != nullptr) {
        logger_->info("队列播完：共 {} 首", queue_.size());
    }
}

}  // namespace mamba::playback::core
