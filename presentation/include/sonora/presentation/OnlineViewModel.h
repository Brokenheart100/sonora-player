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
#include <QVariantMap>

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

    // 在线"卡片"（推荐歌单 / 榜单）。role 名与 `comic/CollectionCard.qml` 的
    // required 属性逐字相同——对不上不报错，只会静默绑定成空串。
    class OnlineCollectionModel final : public RowsModel
    {
        Q_OBJECT

    public:
        enum Role
        {
            IdRole = Qt::UserRole + 1,
            NameRole,
            CoverRole,
            SubtitleRole,
            CountRole,
        };

        explicit OnlineCollectionModel(QObject *parent = nullptr);

        [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
        [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

        void setItems(const std::vector<online::contracts::CollectionDto> &items);
        [[nodiscard]] const std::vector<online::contracts::CollectionDto> &items() const { return items_; }

    private:
        std::vector<online::contracts::CollectionDto> items_;
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
        Q_PROPERTY(bool signingIn READ signingIn NOTIFY statusChanged)
        Q_PROPERTY(QString loginError READ loginError NOTIFY statusChanged)
        // 扫码登录：矩阵按格问，省得把二维数组搬进 QVariantList。
        Q_PROPERTY(bool qrActive READ qrActive NOTIFY qrChanged)
        Q_PROPERTY(QString qrMessage READ qrMessage NOTIFY qrChanged)
        Q_PROPERTY(int qrSize READ qrSize NOTIFY qrChanged)
        // 诊断面板：一次性快照，不进 50ms 轮询（取一次要查两次 COUNT）。
        Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
        // 推荐歌单 / 榜单两种卡片列表。卡片本身很少（≤ 65），一张模型够用；
        // 界面按当前标签页决定给 model 换哪一页数据。
        Q_PROPERTY(OnlineCollectionModel *collections READ collections CONSTANT)
        Q_PROPERTY(QString collectionTitle READ collectionTitle NOTIFY collectionChanged)
        Q_PROPERTY(QString collectionSubtitle READ collectionSubtitle NOTIFY collectionChanged)
        Q_PROPERTY(bool collectionActive READ collectionActive NOTIFY collectionChanged)

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
        [[nodiscard]] bool signingIn() const { return signingIn_; }
        [[nodiscard]] QString loginError() const { return loginError_; }
        [[nodiscard]] bool qrActive() const { return qrActive_; }
        [[nodiscard]] QString qrMessage() const { return qrMessage_; }
        [[nodiscard]] int qrSize() const { return qrSize_; }
        [[nodiscard]] QVariantMap diagnostics() const { return diagnostics_; }
        [[nodiscard]] OnlineCollectionModel *collections() { return &collections_; }
        [[nodiscard]] QString collectionTitle() const { return collectionTitle_; }
        [[nodiscard]] QString collectionSubtitle() const { return collectionSubtitle_; }
        [[nodiscard]] bool collectionActive() const
        {
            return pageKind_ != online::contracts::SourceKind::Search;
        }
        Q_INVOKABLE [[nodiscard]] bool qrCell(int row, int col) const;

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
        // 粘贴 cookie 登录。校验是异步的：这里只负责发命令，
        // 成败由 refreshStatus() 从 SourceStatusDto 里读回来（signingIn/loginError）。
        Q_INVOKABLE void signIn(const QString &cookie);
        // 扫码登录：start 之后轮询在 onTick 里进行（1.5s 一次由 OnlineService 掌握）。
        Q_INVOKABLE void startQrLogin();
        Q_INVOKABLE void cancelQrLogin();
        // 打开诊断面板时调一次；面板关着就不要调（它要查两次 COUNT）。
        Q_INVOKABLE void refreshDiagnostics();
        // 拉一屏卡片："playlist"（推荐歌单）或 "chart"（榜单）。
        Q_INVOKABLE void loadCollections(const QString &kind);
        // 进一张卡片：把结果列表切到那一页（歌单/榜单/专辑/歌手/新歌共用同一条路径）。
        Q_INVOKABLE void openCollection(const QString &kind, const QString &id, const QString &title);
        Q_INVOKABLE void closeCollection();

    signals:
        void statusChanged();
        void qrChanged();
        void diagnosticsChanged();
        void collectionChanged();

    private:
        void commitSearch(const QString &keywords);
        void onTick();
        void refreshStatus();
        void refreshRows();
        void applyPlayback(const online::contracts::OnlinePlaybackDto &playback);
        void rescanFailedPlayback();
        void dispatchQuietly(const std::function<kernel::Result<void>()> &action);

        kernel::ICommandBus &commands_;
        kernel::IQueryBus &queries_;
        QTimer timer_;
        // 单发：输入停下来 280ms 才响。和 50ms 的轮询计时器是两回事，别混。
        QTimer debounce_;
        QString pendingKeywords_;

        std::uint64_t token_ = 0;
        std::uint64_t seenRevision_ = 0;
        // 取地址回来的那批，靠 revision 去重：没变就不重建播放队列。
        std::uint64_t seenPlaybackRevision_ = 0;
        // 点的是哪一首。地址回来时按 songId 找回它在队列里的位置——
        // 中间有几首取不到地址会被跳过，原来的下标不能直接用。
        std::string playbackSongId_;
        // 直链过期重解析：记住最后播的那首，连续失败两次就停手。
        std::string lastPlaybackSongId_;
        int reResolveAttempts_ = 0;
        QString keywords_;

        QString statusText_ = QStringLiteral("输入关键词，回车搜索");
        QString errorText_;
        QString accountText_ = QStringLiteral("未登录");
        bool busy_ = false;
        bool hasMore_ = false;
        bool signedIn_ = false;
        bool retryable_ = false;
        bool signingIn_ = false;
        QString loginError_;
        // 扫码登录的界面态。矩阵只在 session 变化时更新一次。
        bool qrActive_ = false;
        QString qrMessage_;
        std::vector<std::string> qrRows_;
        int qrSize_ = 0;
        std::uint64_t qrSession_ = 0;
        QVariantMap diagnostics_;

        // 当前结果列表属于哪一类资源：搜索之外，`token_` 那条轮询同样管它，
        // 所以翻页/重试/播放都不用分两套代码。
        online::contracts::SourceKind pageKind_{online::contracts::SourceKind::Search};
        std::string pageCollectionId_;
        QString collectionTitle_;
        QString collectionSubtitle_;
        // 卡片列表：哪些 kind 已经请求过、各自看到第几版。
        QHash<int, std::uint64_t> seenCollectionRevisions_;
        // 当前要看哪一类卡片（-1 = 不看）；界面切标签页时改它。
        int collectionsWanted_ = -1;

        OnlineListModel model_;
        OnlineCollectionModel collections_;
    };

} // namespace sonora::presentation
