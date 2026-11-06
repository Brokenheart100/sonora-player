#include <sonora/platform/audio/QtMultimediaOutput.h>

#include <algorithm>
#include <utility>

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QString>
#include <QUrl>

#include <sonora/kernel/Error.h>

namespace sonora::platform::audio {

// ── Impl：Qt 的地盘 ────────────────────────────────────────────────────────
// 一个文件里放两个"实体"：外壳管状态机与监听者，Impl 管 QMediaPlayer 的生老病死。
// 回调统一从这里拐回外壳，外壳的公开头因此不需要知道 Qt 的存在。
struct QtMultimediaOutput::Impl {
    explicit Impl(QtMultimediaOutput& ownerRef) : owner(ownerRef) {}

    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);

    QtMultimediaOutput& owner;
    QMediaPlayer player;
    QAudioOutput audioOutput;
};

void QtMultimediaOutput::Impl::onPlaybackStateChanged(QMediaPlayer::PlaybackState state) {
    switch (state) {
        case QMediaPlayer::PlayingState:
            owner.setState(playback::contracts::PlaybackState::Playing);
            break;
        case QMediaPlayer::PausedState:
            owner.setState(playback::contracts::PlaybackState::Paused);
            break;
        case QMediaPlayer::StoppedState:
            // 播完（EndOfMedia）也会回到 StoppedState：只有真的装载过才叫 Stopped，
            // 否则是 Idle——这两者在界面上长得不一样（一个显示 0:00，一个显示 --:--）。
            owner.setState(owner.sourcePath_.empty() ? playback::contracts::PlaybackState::Idle
                                                     : playback::contracts::PlaybackState::Stopped);
            break;
    }
}

void QtMultimediaOutput::Impl::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    switch (status) {
        case QMediaPlayer::LoadedMedia:
        case QMediaPlayer::BufferedMedia:
            // 装载完成。状态仍由 playbackStateChanged 决定（用户可能是暂停着装载的）。
            break;
        case QMediaPlayer::EndOfMedia:
            owner.positionMs_.store(owner.durationMs_.load(std::memory_order_relaxed),
                                    std::memory_order_relaxed);
            owner.setState(playback::contracts::PlaybackState::Stopped);
            if (owner.finishedListener_) {
                owner.finishedListener_();
            }
            break;
        case QMediaPlayer::InvalidMedia:
            owner.setState(playback::contracts::PlaybackState::Failed);
            if (owner.errorListener_) {
                owner.errorListener_("无法识别的媒体格式：" + owner.sourcePath_);
            }
            break;
        default:
            break;
    }
}

QtMultimediaOutput::QtMultimediaOutput() : impl_(std::make_unique<Impl>(*this)) {
    auto& player = impl_->player;
    player.setAudioOutput(&impl_->audioOutput);
    impl_->audioOutput.setVolume(1.0F);

    QObject::connect(&player, &QMediaPlayer::playbackStateChanged, &player,
                     [this](QMediaPlayer::PlaybackState state)
                     { impl_->onPlaybackStateChanged(state); });

    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &player,
                     [this](QMediaPlayer::MediaStatus status)
                     { impl_->onMediaStatusChanged(status); });

    QObject::connect(&player, &QMediaPlayer::positionChanged, &player,
                     [this](qint64 position)
                     { positionMs_.store(position, std::memory_order_relaxed); });

    QObject::connect(&player, &QMediaPlayer::durationChanged, &player,
                     [this](qint64 duration)
                     { durationMs_.store(duration, std::memory_order_relaxed); });

    QObject::connect(&player, &QMediaPlayer::errorOccurred, &player,
                     [this](QMediaPlayer::Error error, const QString& message)
                     {
                         if (error == QMediaPlayer::NoError) {
                             return;
                         }
                         setState(playback::contracts::PlaybackState::Failed);
                         if (errorListener_) {
                             errorListener_(message.toUtf8().toStdString());
                         }
                     });
}

QtMultimediaOutput::~QtMultimediaOutput() {
    impl_->player.stop();
    impl_->player.setAudioOutput(nullptr);
    impl_->player.setSource(QUrl());
}

kernel::Result<void> QtMultimediaOutput::load(std::string_view filePathUtf8) {
    if (filePathUtf8.empty()) {
        return kernel::Error{kernel::ErrorCode::InvalidArgument, "文件路径为空"};
    }
    sourcePath_ = std::string{filePathUtf8};
    positionMs_.store(0, std::memory_order_relaxed);
    durationMs_.store(0, std::memory_order_relaxed);
    setState(playback::contracts::PlaybackState::Loading);

    const auto url = QUrl::fromLocalFile(QString::fromUtf8(sourcePath_.c_str(),
                                                           static_cast<int>(sourcePath_.size())));
    impl_->player.setSource(url);

    if (!impl_->player.isAvailable()) {
        setState(playback::contracts::PlaybackState::Failed);
        return kernel::Error{kernel::ErrorCode::Unavailable, "媒体服务不可用（缺多媒体插件？）"};
    }
    return {};
}

kernel::Result<void> QtMultimediaOutput::play() {
    if (sourcePath_.empty()) {
        return kernel::Error{kernel::ErrorCode::InvalidArgument, "还没有装载文件"};
    }
    impl_->player.play();
    return {};
}

kernel::Result<void> QtMultimediaOutput::pause() {
    impl_->player.pause();
    return {};
}

kernel::Result<void> QtMultimediaOutput::stop() {
    impl_->player.stop();
    positionMs_.store(0, std::memory_order_relaxed);
    return {};
}

kernel::Result<void> QtMultimediaOutput::seekMs(std::int64_t positionMs) {
    if (sourcePath_.empty()) {
        return kernel::Error{kernel::ErrorCode::InvalidArgument, "还没有装载文件"};
    }
    const auto duration = durationMs_.load(std::memory_order_relaxed);
    const auto clamped = std::clamp<std::int64_t>(positionMs, 0, duration > 0 ? duration : 0);
    impl_->player.setPosition(clamped);
    positionMs_.store(clamped, std::memory_order_relaxed);
    return {};
}

kernel::Result<void> QtMultimediaOutput::setVolume(std::int32_t volume) {
    const auto clamped = std::clamp(volume, 0, 100);
    volume_.store(clamped, std::memory_order_relaxed);
    // Qt 的音量是线性的 0..1，但人耳是对数的。平方≈听感更接近滑块。
    const auto linear = static_cast<float>(clamped) / 100.0F;
    impl_->audioOutput.setVolume(linear * linear);
    return {};
}

std::int32_t QtMultimediaOutput::volume() const noexcept {
    return volume_.load(std::memory_order_relaxed);
}

std::int64_t QtMultimediaOutput::durationMs() const noexcept {
    return durationMs_.load(std::memory_order_relaxed);
}

std::int64_t QtMultimediaOutput::positionMs() const noexcept {
    return positionMs_.load(std::memory_order_relaxed);
}

playback::contracts::PlaybackState QtMultimediaOutput::state() const noexcept { return state_; }

void QtMultimediaOutput::setStateListener(playback::contracts::StateListener listener) {
    stateListener_ = std::move(listener);
}

void QtMultimediaOutput::setFinishedListener(playback::contracts::FinishedListener listener) {
    finishedListener_ = std::move(listener);
}

void QtMultimediaOutput::setErrorListener(playback::contracts::ErrorListener listener) {
    errorListener_ = std::move(listener);
}

void QtMultimediaOutput::setState(playback::contracts::PlaybackState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    if (stateListener_) {
        stateListener_(state);
    }
}

}  // namespace sonora::platform::audio
