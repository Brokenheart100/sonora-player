#include "mamba/presentation/PlayerViewModel.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <QVariantMap>

#include "mamba/playback/contracts/Queries.h"
#include "mamba/presentation/LyricsModel.h"

namespace mamba::presentation {
namespace {

// 与 TrackListModel 里的同名函数一致：毫秒 → "3:45"。放两处是刻意的——
// 一个纯函数不值得为它开一个公共头，重复 6 行比多一个 target 便宜。
QString formatClock(std::int64_t milliseconds) {
    if (milliseconds <= 0) {
        return QStringLiteral("--:--");
    }
    const auto totalSeconds = milliseconds / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

QString fromUtf8(const std::string &text) { return QString::fromStdString(text); }

}  // namespace

PlayerViewModel::PlayerViewModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                                 const playback::contracts::IPositionStream &position,
                                 QObject *parent)
    : QObject(parent), commands_(commands), queries_(queries), position_(position) {
    // 33ms ≈ 30Hz。比刷新率略快一点，肉眼就是连续的；再快只是白烧 CPU。
    timer_.setInterval(33);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this] { onTick(); });
    timer_.start();

    refresh();
}

void PlayerViewModel::onTick() {
    // ── 位置：只做一次原子读，不碰总线 ──────────────────────────────────────
    const auto current = position_.positionMs();
    if (current != lastPositionMs_) {
        lastPositionMs_ = current;
        positionText_ = formatClock(current);
        progress_ = durationMs_ > 0
                        ? static_cast<double>(current) / static_cast<double>(durationMs_)
                        : 0.0;
        // 歌词跟着同一个位置走：33ms 一次整数比较，跨行了才 emit。
        if (lyrics_ != nullptr) {
            lyrics_->setPositionMs(current);
        }
        emit positionChanged();
    }

    // ── 其余状态：2Hz 问一次（30 次/秒问 DTO 是浪费）────────────────────────
    if (++ticksSinceRefresh_ >= 15) {
        ticksSinceRefresh_ = 0;
        refresh();
    }
}

void PlayerViewModel::refresh() {
    const auto snapshot = queries_.ask<playback::contracts::PlayerSnapshot>(
        playback::contracts::GetPlayerSnapshot{});
    if (!snapshot) {
        stateText_ = fromUtf8(snapshot.error().message());
        emit stateChanged();
        return;
    }
    const auto &value = snapshot.value();

    const auto newTitle = QString::fromStdString(value.title);
    const auto newArtist = QString::fromStdString(value.artistName);
    const auto newQueue =
        value.queueSize > 0
            ? QStringLiteral("%1 / %2").arg(value.queueIndex + 1).arg(value.queueSize)
            : QString{};
    if (newTitle != title_ || newArtist != artist_ || newQueue != queueText_ ||
        value.hasTrack != hasTrack_) {
        title_ = newTitle;
        artist_ = newArtist;
        queueText_ = newQueue;
        hasTrack_ = value.hasTrack;
        emit trackChanged();
    }

    durationMs_ = value.durationMs;
    durationText_ = formatClock(durationMs_);

    // 换歌就换歌词。loadFor() 自己会对同一路径早退，所以放在 2Hz 的 refresh 里
    // 不会重复查库——它只在 filePath 真的变了才动一次。
    if (lyrics_ != nullptr) {
        lyrics_->loadFor(QString::fromStdString(value.filePath));
    }

    const auto nowPlaying = value.state == playback::contracts::PlaybackState::Playing;
    const auto newStateText = QString::fromUtf8(toName(value.state).data(),
                                                static_cast<int>(toName(value.state).size()));
    if (nowPlaying != playing_ || newStateText != stateText_) {
        playing_ = nowPlaying;
        stateText_ = newStateText;
        emit stateChanged();
    }

    if (value.volume != volume_) {
        volume_ = value.volume;
        emit volumeChanged();
    }

    if (value.mode != mode_) {
        mode_ = value.mode;
        modeLabel_ = QString::fromUtf8(playback::contracts::modeLabel(mode_).data(),
                                       static_cast<int>(playback::contracts::modeLabel(mode_).size()));
        emit modeChanged();
    }

    // 队列只在"换了队列 / 换了当前曲"时重建。2Hz 无条件重建会白扔一堆 QVariant。
    const auto queueView = queries_.ask<playback::contracts::QueueView>(
        playback::contracts::GetPlayQueue{});
    if (queueView) {
        const auto &view = queueView.value();
        if (queue_.size() != static_cast<int>(view.items.size()) ||
            currentTrackId_ != (view.currentIndex >= 0 &&
                                        view.currentIndex < static_cast<std::int32_t>(view.items.size())
                                    ? view.items[static_cast<std::size_t>(view.currentIndex)].trackId
                                    : 0)) {
            QVariantList rebuilt;
            rebuilt.reserve(static_cast<int>(view.items.size()));
            for (const auto &item : view.items) {
                QVariantMap row;
                row.insert(QStringLiteral("trackId"), QVariant::fromValue<qulonglong>(item.trackId));
                row.insert(QStringLiteral("title"), QString::fromStdString(item.title));
                row.insert(QStringLiteral("artist"), QString::fromStdString(item.artistName));
                row.insert(QStringLiteral("current"), item.current);
                rebuilt.append(row);
            }
            currentTrackId_ =
                view.currentIndex >= 0 &&
                        view.currentIndex < static_cast<std::int32_t>(view.items.size())
                    ? view.items[static_cast<std::size_t>(view.currentIndex)].trackId
                    : 0;
            queue_ = std::move(rebuilt);
            emit queueChanged();
        }
    }

    emit positionChanged();
}

void PlayerViewModel::toggle() {
    dispatchQuietly("toggle", [this] { return commands_.dispatch(playback::contracts::TogglePlayPause{}); });
    refresh();
}

void PlayerViewModel::next() {
    dispatchQuietly("next", [this] { return commands_.dispatch(playback::contracts::PlayNext{}); });
    refresh();
}

void PlayerViewModel::previous() {
    dispatchQuietly("previous",
                    [this] { return commands_.dispatch(playback::contracts::PlayPrevious{}); });
    refresh();
}

void PlayerViewModel::seekFraction(double fraction) {
    if (durationMs_ <= 0) {
        return;
    }
    const auto clamped = std::clamp(fraction, 0.0, 1.0);
    playback::contracts::SeekTo command;
    command.positionMs = static_cast<std::int64_t>(clamped * static_cast<double>(durationMs_));
    dispatchQuietly("seek", [this, command] { return commands_.dispatch(command); });
}

void PlayerViewModel::setVolume(int value) {
    const auto clamped = std::clamp(value, 0, 100);
    playback::contracts::SetVolume command;
    command.value = clamped;
    dispatchQuietly("volume", [this, command] { return commands_.dispatch(command); });
    if (clamped != volume_) {
        volume_ = clamped;
        emit volumeChanged();
    }
}

void PlayerViewModel::cycleMode() {
    playback::contracts::SetPlaybackMode command;
    command.mode = playback::contracts::nextMode(mode_);
    dispatchQuietly("mode",
                    [this, command] { return commands_.dispatch(command); });
    refresh();
}

void PlayerViewModel::jumpTo(qulonglong trackId) {
    playback::contracts::JumpToTrack command;
    command.trackId = static_cast<std::uint64_t>(trackId);
    dispatchQuietly("jump",
                    [this, command] { return commands_.dispatch(command); });
    refresh();
}

void PlayerViewModel::dispatchQuietly(const char *what,
                                      const std::function<kernel::Result<void>()> &action) {
    const auto result = action();
    if (!result) {
        // 命令失败不该让界面卡住：显示出错原因，继续用上一次的快照。
        stateText_ = QStringLiteral("%1 失败：%2")
                         .arg(QString::fromUtf8(what), fromUtf8(result.error().message()));
        emit stateChanged();
    }
}

}  // namespace mamba::presentation
