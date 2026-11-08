#include "sonora/presentation/Format.h"
#include "sonora/presentation/PlayerViewModel.h"

#include <QCoreApplication>

#include <algorithm>
#include <functional>
#include <utility>

#include <QUrl>
#include <QVariantMap>

#include "sonora/playback/contracts/Queries.h"
#include "sonora/presentation/LyricsModel.h"

namespace sonora::presentation
{
    namespace
    {

        // 状态是给用户看的，不是给日志看的：`toName` 是英文枚举名，直接上界面会
        // 冒出一句 "idle" 挂在中文界面里。
        QString stateLabel(const playback::contracts::PlaybackState state)
        {
            switch (state)
            {
            case playback::contracts::PlaybackState::Idle:
                return QCoreApplication::translate("PlayerViewModel", "就绪");
            case playback::contracts::PlaybackState::Loading:
                return QCoreApplication::translate("PlayerViewModel", "装载中");
            case playback::contracts::PlaybackState::Buffering:
                return QCoreApplication::translate("PlayerViewModel", "缓冲中");
            case playback::contracts::PlaybackState::Playing:
                return QCoreApplication::translate("PlayerViewModel", "播放中");
            case playback::contracts::PlaybackState::Paused:
                return QCoreApplication::translate("PlayerViewModel", "已暂停");
            case playback::contracts::PlaybackState::Stopped:
                return QCoreApplication::translate("PlayerViewModel", "已停止");
            case playback::contracts::PlaybackState::Failed:
                return QCoreApplication::translate("PlayerViewModel", "出错");
            }
            return QString();
        }

        QString fromUtf8(const std::string &text) { return QString::fromStdString(text); }

        // 播放模式按钮上的词。契约层给的是中文常量（core 层不碰 Qt），
        // 这里按模式映射一次，翻译走本类的上下文。
        QString modeText(const playback::contracts::PlaybackMode mode)
        {
            switch (mode)
            {
            case playback::contracts::PlaybackMode::Sequential:
                return QCoreApplication::translate("PlayerViewModel", "顺序");
            case playback::contracts::PlaybackMode::RepeatAll:
                return QCoreApplication::translate("PlayerViewModel", "循环");
            case playback::contracts::PlaybackMode::RepeatOne:
                return QCoreApplication::translate("PlayerViewModel", "单曲");
            case playback::contracts::PlaybackMode::Shuffle:
                return QCoreApplication::translate("PlayerViewModel", "随机");
            }
            return QCoreApplication::translate("PlayerViewModel", "顺序");
        }


    } // namespace

    PlayerViewModel::PlayerViewModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                                     const playback::contracts::IPositionStream &position,
                                     QObject *parent)
        : QObject(parent), commands_(commands), queries_(queries), position_(position)
    {
        // 33ms ≈ 30Hz。比刷新率略快一点，肉眼就是连续的；再快只是白烧 CPU。
        timer_.setInterval(33);
        timer_.setTimerType(Qt::PreciseTimer);
        connect(&timer_, &QTimer::timeout, this, [this]
                { onTick(); });
        timer_.start();

        refresh();
    }

    void PlayerViewModel::onTick()
    {
        // ── 位置：只做一次原子读，不碰总线 ──────────────────────────────────────
        const auto current = position_.positionMs();
        if (current != lastPositionMs_)
        {
            lastPositionMs_ = current;
            positionText_ = formatDuration(current);
            progress_ = durationMs_ > 0
                            ? static_cast<double>(current) / static_cast<double>(durationMs_)
                            : 0.0;
            // 歌词跟着同一个位置走：33ms 一次整数比较，跨行了才 emit。
            if (lyrics_ != nullptr)
            {
                lyrics_->setPositionMs(current);
            }
            emit positionChanged();
        }

        // 缓冲百分比只在网络流上有意义（本地文件永远是 -1）。跟着位置一起刷，
        // 因为两者都是"解码线程写、GUI 线程读"的原子量，成本一样。
        const auto buffered = position_.bufferedPercent();
        if (buffered != buffered_)
        {
            buffered_ = buffered;
            emit positionChanged();
        }

        // ── 其余状态：2Hz 问一次（30 次/秒问 DTO 是浪费）────────────────────────
        if (++ticksSinceRefresh_ >= 15)
        {
            ticksSinceRefresh_ = 0;
            refresh();
        }
    }

    void PlayerViewModel::refresh()
    {
        const auto snapshot = queries_.ask<playback::contracts::PlayerSnapshot>(
            playback::contracts::GetPlayerSnapshot{});
        if (!snapshot)
        {
            stateText_ = fromUtf8(snapshot.error().message());
            emit stateChanged();
            return;
        }
        const auto &value = snapshot.value();

        const auto newTitle = QString::fromStdString(value.title);
        const auto newArtist = QString::fromStdString(value.artistName);
        // 封面给的是绝对路径，QML 的 Image.source 只认 URL——
        // 裸路径会被当成相对地址解析，症状是封面全不显示。
        // 在线曲目的封面本来就是 URL（CDN 直链），再套 fromLocalFile 会变成
        // `file:http://…`，QML 打一条 "Cannot open" 就放弃——所以先认协议头。
        const auto coverText = QString::fromStdString(value.coverPath);
        const bool coverIsUrl = coverText.startsWith(QLatin1String("http://")) ||
                                coverText.startsWith(QLatin1String("https://")) ||
                                coverText.startsWith(QLatin1String("qrc:/"));
        const auto newCover = coverText.isEmpty() ? QString{}
                              : coverIsUrl      ? coverText
                                                : QUrl::fromLocalFile(coverText).toString();
        const auto newSongId = QString::fromStdString(value.onlineSongId);
        const auto newQueue =
            value.queueSize > 0
                ? QStringLiteral("%1 / %2").arg(value.queueIndex + 1).arg(value.queueSize)
                : QString{};
        if (newTitle != title_ || newArtist != artist_ || newQueue != queueText_ ||
            value.hasTrack != hasTrack_ || newCover != coverUrl_ || newSongId != onlineSongId_)
        {
            title_ = newTitle;
            artist_ = newArtist;
            queueText_ = newQueue;
            hasTrack_ = value.hasTrack;
            coverUrl_ = newCover;
            onlineSongId_ = newSongId;
            emit trackChanged();
        }

        durationMs_ = value.durationMs;
        durationText_ = formatDuration(durationMs_);

        // 换歌就换歌词。loadFor() 自己会对同一路径早退，所以放在 2Hz 的 refresh 里
        // 不会重复查库——它只在 filePath 真的变了才动一次。
        if (lyrics_ != nullptr)
        {
            lyrics_->loadFor(QString::fromStdString(value.filePath), newSongId);
        }

        const auto nowPlaying = value.state == playback::contracts::PlaybackState::Playing;
        const auto newFailed = value.state == playback::contracts::PlaybackState::Failed;
        const auto newStateText = stateLabel(value.state);
        if (nowPlaying != playing_ || newFailed != failed_ || newStateText != stateText_)
        {
            playing_ = nowPlaying;
            failed_ = newFailed;
            stateText_ = newStateText;
            emit stateChanged();
        }

        if (value.volume != volume_)
        {
            volume_ = value.volume;
            emit volumeChanged();
        }

        // 每次 tick 都重算标签：语言切换后不必等用户点一下按钮才更新。
        const auto newModeLabel = modeText(value.mode);
        if (value.mode != mode_ || newModeLabel != modeLabel_)
        {
            mode_ = value.mode;
            modeLabel_ = newModeLabel;
            emit modeChanged();
        }

        // 队列只在"换了队列 / 换了当前曲"时重建。2Hz 无条件重建会白扔一堆 QVariant。
        const auto queueView = queries_.ask<playback::contracts::QueueView>(
            playback::contracts::GetPlayQueue{});
        if (queueView)
        {
            const auto &view = queueView.value();
            if (queue_.size() != static_cast<int>(view.items.size()) ||
                currentTrackId_ != (view.currentIndex >= 0 &&
                                            view.currentIndex < static_cast<std::int32_t>(view.items.size())
                                        ? view.items[static_cast<std::size_t>(view.currentIndex)].trackId
                                        : 0))
            {
                QVariantList rebuilt;
                rebuilt.reserve(static_cast<int>(view.items.size()));
                for (const auto &item : view.items)
                {
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

    void PlayerViewModel::toggle()
    {
        dispatchQuietly("toggle", [this]
                        { return commands_.dispatch(playback::contracts::TogglePlayPause{}); });
        refresh();
    }

    void PlayerViewModel::next()
    {
        dispatchQuietly("next", [this]
                        { return commands_.dispatch(playback::contracts::PlayNext{}); });
        refresh();
    }

    void PlayerViewModel::previous()
    {
        dispatchQuietly("previous",
                        [this]
                        { return commands_.dispatch(playback::contracts::PlayPrevious{}); });
        refresh();
    }

    void PlayerViewModel::seekFraction(double fraction)
    {
        if (durationMs_ <= 0)
        {
            return;
        }
        const auto clamped = std::clamp(fraction, 0.0, 1.0);
        playback::contracts::SeekTo command;
        command.positionMs = static_cast<std::int64_t>(clamped * static_cast<double>(durationMs_));
        dispatchQuietly("seek", [this, command]
                        { return commands_.dispatch(command); });
    }

    void PlayerViewModel::setVolume(int value)
    {
        const auto clamped = std::clamp(value, 0, 100);
        playback::contracts::SetVolume command;
        command.value = clamped;
        dispatchQuietly("volume", [this, command]
                        { return commands_.dispatch(command); });
        if (clamped != volume_)
        {
            volume_ = clamped;
            emit volumeChanged();
        }
    }

    void PlayerViewModel::cycleMode()
    {
        playback::contracts::SetPlaybackMode command;
        command.mode = playback::contracts::nextMode(mode_);
        dispatchQuietly("mode",
                        [this, command]
                        { return commands_.dispatch(command); });
        refresh();
    }

    void PlayerViewModel::jumpTo(qulonglong trackId)
    {
        playback::contracts::JumpToTrack command;
        command.trackId = static_cast<std::uint64_t>(trackId);
        dispatchQuietly("jump",
                        [this, command]
                        { return commands_.dispatch(command); });
        refresh();
    }

    void PlayerViewModel::dispatchQuietly(const char *what,
                                          const std::function<kernel::Result<void>()> &action)
    {
        const auto result = action();
        if (!result)
        {
            // 命令失败不该让界面卡住：显示出错原因，继续用上一次的快照。
            stateText_ = tr("%1 失败：%2")
                             .arg(QString::fromUtf8(what), fromUtf8(result.error().message()));
            emit stateChanged();
        }
    }

} // namespace sonora::presentation
