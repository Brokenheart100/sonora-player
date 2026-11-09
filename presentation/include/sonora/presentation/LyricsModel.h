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
#include <QTimer>
#include <QString>
#include <QVariant>

#include <vector>

#include "sonora/kernel/IQueryBus.h"
#include "sonora/lyrics/contracts/LyricsDto.h"

namespace sonora::presentation
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
        // 这份词是不是在线来的。界面右下角那句"在线歌词"认这个，
        // 不去比对 statusText 的字符串（文案会变）。
        Q_PROPERTY(bool onlineSource READ onlineSource NOTIFY linesChanged)

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
        [[nodiscard]] bool onlineSource() const { return onlineSource_; }

        // 换一首歌就调一次。路径与在线 id 都没变时直接返回（2Hz 轮询会不停喂同值）。
        //
        // 在线歌词是异步取回来的：第一次问通常只拿到"还没到"，所以这里挂一个
        // 1 秒的重试定时器，等在线模块把词取回来再问一次——取到、切歌、或者
        // 确认没有词，都会把定时器停掉。
        Q_INVOKABLE void loadFor(const QString &filePath, const QString &onlineSongId);
        // 由 PlayerViewModel 的 33ms tick 推进。位置没跨行时什么都不做，
        // 所以这里的开销是"一次整数比较"。
        void setPositionMs(qint64 positionMs);
        void clear();

        [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
        [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

        // 界面语言变了：重查一次歌词来源与状态文案。
        void retranslate();
    signals:
        void linesChanged();
        void currentIndexChanged();

    private:
        void adopt(const lyrics::contracts::LyricsView &view);
        // 重新问一次（不动 loadedPath_，只按现有参数重查）。
        void requery();
        void setCurrentIndex(int index);

        kernel::IQueryBus &queries_;
        std::vector<lyrics::contracts::LyricLine> lines_;
        // 与 lines_ 同长度的时间戳，二分只读这一份。
        std::vector<qint64> times_;
        QString statusText_;
        // 上次加载的路径，用来早退。空表示还没加载过。
        QString loadedPath_;
        QString onlineSongId_;
        bool onlineSource_ = false;
        // 在线歌词还没到时用来重试；空转时是停的。
        QTimer retryTimer_;
        int currentIndex_ = -1;
        bool visible_ = false;
    };

} // namespace sonora::presentation
