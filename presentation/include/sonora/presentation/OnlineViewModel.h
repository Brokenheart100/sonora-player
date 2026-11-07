// 在线页的 ViewModel。
//
// 它只做三件事：**发命令**（搜索/翻页/重试/播放）、**每 50ms 问一次状态**、
// 把行数据投影成 QML 能直接绑定的 role。它不知道网易云长什么样——
// 连 URL 都没见过，因为直链在 R3 之前根本不存在于这条链上。
//
// 轮询而不是回调：Qt 的信号槽跨线程会让"回调在哪个线程执行"变成一个
// 每个使用者都要重新想一遍的问题（见 docs/ONLINE.md §4.1）。
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <cstdint>
#include <functional>
#include <vector>

#include "sonora/kernel/ICommandBus.h"
#include "sonora/kernel/IQueryBus.h"
#include "sonora/online/contracts/Types.h"
#include "sonora/presentation/RowsModel.h"

namespace sonora::presentation
{

    // 在线结果列表。role 名必须与 `comic/TrackRow.qml` 的 required 属性逐字相同，
    // 否则 QML 侧不会报错，只会静默绑定成空串。
    class OnlineListModel final : public RowsModel
    {
        Q_OBJECT

    public:
        enum Role
        {
            SongIdRole = Qt::UserRole + 1,
            TitleRole,
            ArtistRole,
            AlbumRole,
            DurationRole,
            CoverRole,
            QualityRole,
            PlayableRole,
        };

        explicit OnlineListModel(QObject *parent = nullptr);

        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
        [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

        void setRows(const std::vector<online::contracts::OnlineTrackDto> &rows);
        void clearRows();
        [[nodiscard]] const std::vector<online::contracts::OnlineTrackDto> &rows() const
        {
            return rows_;
        }

    private:
        std::vector<online::contracts::OnlineTrackDto> rows_;
    };

    class OnlineViewModel final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(OnlineListModel *results READ results CONSTANT)
        Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
        Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
        Q_PROPERTY(QString accountText READ accountText NOTIFY statusChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
        Q_PROPERTY(bool hasMore READ hasMore NOTIFY statusChanged)
        Q_PROPERTY(bool signedIn READ signedIn NOTIFY statusChanged)
        Q_PROPERTY(bool retryable READ retryable NOTIFY statusChanged)

    public:
        OnlineViewModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                        QObject *parent = nullptr);

        [[nodiscard]] OnlineListModel *results() { return &model_; }
        [[nodiscard]] QString statusText() const { return statusText_; }
        [[nodiscard]] QString errorText() const { return errorText_; }
        [[nodiscard]] QString accountText() const { return accountText_; }
        [[nodiscard]] bool busy() const { return busy_; }
        [[nodiscard]] bool hasMore() const { return hasMore_; }
        [[nodiscard]] bool signedIn() const { return signedIn_; }
        [[nodiscard]] bool retryable() const { return retryable_; }

        // 回车触发：立刻搜（词没变且有结果时不重发）。
        Q_INVOKABLE void search(const QString &keywords);
        // 输入框每变一次调用：280ms 内没有新输入才真的发请求。
        // 防抖放在 ViewModel 而不是 QML，是因为"连打 5 个字只发 1 次请求"
        // 要能用单测和日志证明（docs/ONLINE.md R2-3）。
        Q_INVOKABLE void queueSearch(const QString &keywords);
        Q_INVOKABLE void loadMore();
        Q_INVOKABLE void retry();
        // 点某一行：把整页 id 一起交出去（下一首就是列表的下一行）。
        Q_INVOKABLE void play(int index);
        Q_INVOKABLE void signOut();
        // 点账号胶囊但还没登录时调用。R4 之前它只把"还没接通"写在错误行里，
        // 而不是弹一个假的登录框。
        Q_INVOKABLE void signInHint();

    signals:
        void statusChanged();

    private:
        void commitSearch(const QString &keywords);
        void onTick();
        void refreshStatus();
        void refreshRows();
        void dispatchQuietly(const std::function<kernel::Result<void>()> &action);

        kernel::ICommandBus &commands_;
        kernel::IQueryBus &queries_;
        QTimer timer_;
        // 单发：输入停下来 280ms 才响。和 50ms 的轮询计时器是两回事，别混。
        QTimer debounce_;
        QString pendingKeywords_;

        std::uint64_t token_ = 0;
        std::uint64_t seenRevision_ = 0;
        QString keywords_;

        QString statusText_ = QStringLiteral("输入关键词，回车搜索");
        QString errorText_;
        QString accountText_ = QStringLiteral("未登录");
        bool busy_ = false;
        bool hasMore_ = false;
        bool signedIn_ = false;
        bool retryable_ = false;

        OnlineListModel model_;
    };

} // namespace sonora::presentation
