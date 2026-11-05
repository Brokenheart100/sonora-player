// 歌词列表的 ViewModel。
//
// 两个刻意的设计：
//
// 1. **当前行由这里算，不向歌词模块要。** Presentation 只依赖各模块的
//    *.Contracts（见 AGENTS.md 规矩 3），看不到 Lyrics.Core 的 LyricTimeline。
//    所以这里自己维护一份时间戳数组 + 一次二分。代价是"同一时间戳多行落到后
//    一行"这条规则要在两处保持一致（LyricTimeline::indexAt 与本文件同名逻辑），
//    收益是表现层不必为了一个整数去依赖领域。
//
// 2. **歌词按整份加载，不按行查询。** 一份 LRC 顶多几十 KB、几百行，
//    切歌时读一次比分页、按需、滚动加载都省事；一次 beginResetModel 之后
//    就只剩"哪一行是当前行"这一个 30Hz 的问题。
#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <vector>

#include "mamba/kernel/IQueryBus.h"
#include "mamba/lyrics/contracts/LyricsDto.h"

namespace mamba::presentation
{

    class LyricsModel final : public QAbstractListModel
    {
        Q_OBJECT
        Q_PROPERTY(int count READ count NOTIFY linesChanged)
        // 有歌词才显示。Instrumental（纯音乐）与 NotFound 都是 false，
        // 界面不必自己去分辨状态。
        Q_PROPERTY(bool visible READ visible NOTIFY linesChanged)
        // 给界面看的一句话：来源、"纯音乐"、或失败原因。
        Q_PROPERTY(QString statusText READ statusText NOTIFY linesChanged)
        Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)

    public:
        enum Role
        {
            TextRole = Qt::UserRole + 1,
            TranslationRole,
            ActiveRole,
        };
        Q_ENUM(Role)

        explicit LyricsModel(kernel::IQueryBus &queries, QObject *parent = nullptr);

        [[nodiscard]] int count() const { return static_cast<int>(lines_.size()); }
        [[nodiscard]] bool visible() const { return visible_; }
        [[nodiscard]] QString statusText() const { return statusText_; }
        [[nodiscard]] int currentIndex() const { return currentIndex_; }

        // 换一首歌就调一次。同一个路径重复调直接返回（2Hz 轮询会不停喂同一个值）。
        Q_INVOKABLE void loadFor(const QString &filePath);
        // 由 PlayerViewModel 的 33ms tick 推进。位置没跨行时什么都不做，
        // 所以这里的开销是"一次整数比较"。
        void setPositionMs(qint64 positionMs);
        void clear();

        [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
        [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    signals:
        void linesChanged();
        void currentIndexChanged();

    private:
        void adopt(const lyrics::contracts::LyricsView &view);
        void setCurrentIndex(int index);

        kernel::IQueryBus &queries_;
        std::vector<lyrics::contracts::LyricLine> lines_;
        // 与 lines_ 同长度的时间戳，二分只读这一份。
        std::vector<qint64> times_;
        QString statusText_;
        // 上次加载的路径，用来早退。空表示还没加载过。
        QString loadedPath_;
        int currentIndex_ = -1;
        bool visible_ = false;
    };

} // namespace mamba::presentation
