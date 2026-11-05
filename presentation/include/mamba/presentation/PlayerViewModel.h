// 播放器条的 ViewModel。
//
// 这里只有一个值得注意的设计：**位置不走总线**。
// 它每 33ms 从 IPositionStream 里原子读一次，除此之外不碰任何东西——
// 既不 ask 查询，也不 emit 领域事件。把 30Hz 的数据塞进命令/查询总线，
// 等于用事务和事件循环去送一个本来只是一次内存读的数字。
//
// 其余状态（标题、时长、播放/暂停）变化频率是"每次用户操作一次"，
// 所以它们仍然走 GetPlayerSnapshot 查询，2Hz 轮询一次就够。
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "mamba/kernel/ICommandBus.h"
#include "mamba/kernel/IQueryBus.h"
#include "mamba/playback/contracts/AudioOutput.h"
#include "mamba/playback/contracts/Commands.h"

namespace mamba::presentation {

class PlayerViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString queueText READ queueText NOTIFY trackChanged)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY positionChanged)
    Q_PROPERTY(double progress READ progress NOTIFY positionChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)

public:
    PlayerViewModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                    const playback::contracts::IPositionStream &position,
                    QObject *parent = nullptr);

    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] QString artist() const { return artist_; }
    [[nodiscard]] QString queueText() const { return queueText_; }
    [[nodiscard]] bool hasTrack() const { return hasTrack_; }
    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] QString stateText() const { return stateText_; }
    [[nodiscard]] QString positionText() const { return positionText_; }
    [[nodiscard]] QString durationText() const { return durationText_; }
    [[nodiscard]] double progress() const { return progress_; }
    [[nodiscard]] int volume() const { return volume_; }

    // 重新问一次状态。命令之后调用；另外每 500ms 也会被计时器顺带调一次，
    // 否则"播完自动下一首"之后标题会一直是上一首。
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    // 0..1，来自进度条的拖动位置。
    Q_INVOKABLE void seekFraction(double fraction);
    Q_INVOKABLE void setVolume(int value);

signals:
    void trackChanged();
    void stateChanged();
    void positionChanged();
    void volumeChanged();

private:
    void onTick();
    void dispatchQuietly(const char *what, const std::function<kernel::Result<void>()> &action);

    kernel::ICommandBus &commands_;
    kernel::IQueryBus &queries_;
    const playback::contracts::IPositionStream &position_;
    QTimer timer_;

    QString title_;
    QString artist_;
    QString queueText_;
    QString stateText_ = QStringLiteral("未播放");
    QString positionText_ = QStringLiteral("0:00");
    QString durationText_ = QStringLiteral("--:--");
    bool hasTrack_ = false;
    bool playing_ = false;
    int volume_ = 100;
    double progress_ = 0.0;

    std::int64_t durationMs_ = 0;
    std::int64_t lastPositionMs_ = -1;
    int ticksSinceRefresh_ = 0;
};

}  // namespace mamba::presentation
