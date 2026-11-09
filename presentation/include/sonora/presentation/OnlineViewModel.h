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
            ArtistIdRole,
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
        // 当前这页行数据来自本地缓存（离线时唯一能看到的旧结果）。
        Q_PROPERTY(bool cachedResult READ cachedResult NOTIFY statusChanged)
        Q_PROPERTY(bool signingIn READ signingIn NOTIFY statusChanged)
        Q_PROPERTY(QString loginError READ loginError NOTIFY statusChanged)
        // 扫码登录：矩阵按格问，省得把二维数组搬进 QVariantList。
        Q_PROPERTY(bool qrActive READ qrActive NOTIFY qrChanged)
        Q_PROPERTY(QString qrMessage READ qrMessage NOTIFY qrChanged)
        Q_PROPERTY(int qrSize READ qrSize NOTIFY qrChanged)
        // 诊断面板：一次性快照，不进 50ms 轮询（取一次要查两次 COUNT）。
        Q_PROPERTY(QVariantList myPlaylists READ myPlaylists NOTIFY myPlaylistsChanged)
        Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)
        // 推荐歌单 / 榜单两种卡片列表。卡片本身很少（≤ 65），一张模型够用；
        // 界面按当前标签页决定给 model 换哪一页数据。
        Q_PROPERTY(OnlineCollectionModel *collections READ collections CONSTANT)
        Q_PROPERTY(QString collectionTitle READ collectionTitle NOTIFY collectionChanged)
        Q_PROPERTY(QString collectionSubtitle READ collectionSubtitle NOTIFY collectionChanged)
        // 详情页顶上那张大封面。拿不到就留空，界面退回画一张"没有封面"的纸片。
        Q_PROPERTY(QString collectionCover READ collectionCover NOTIFY collectionChanged)
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
        [[nodiscard]] bool cachedResult() const { return cachedResult_; }
        [[nodiscard]] bool signingIn() const { return signingIn_; }
        [[nodiscard]] QString loginError() const { return loginError_; }
        [[nodiscard]] bool qrActive() const { return qrActive_; }
        [[nodiscard]] QString qrMessage() const { return qrMessage_; }
        [[nodiscard]] int qrSize() const { return qrSize_; }
        [[nodiscard]] QVariantMap diagnostics() const { return diagnostics_; }
        [[nodiscard]] OnlineCollectionModel *collections() { return &collections_; }
        [[nodiscard]] QString collectionTitle() const { return collectionTitle_; }
        [[nodiscard]] QString collectionSubtitle() const { return collectionSubtitle_; }
        [[nodiscard]] QString collectionCover() const { return collectionCover_; }
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
        // "播放全部" = 从第一首起播。单独一个方法是为了 QML 不必知道 index 语义。
        Q_INVOKABLE void playAll();
        // 加入队列：只解析直链、追加到队尾，不打断正在播的那首
        // （playWhenIdle = false —— 空队列时才起播）。
        Q_INVOKABLE void enqueue(int index);
        Q_INVOKABLE void enqueueAll();
        // 查看歌手：靠 `artistId` 开一个 Artist 资源页；搜索结果的 artistId
        // 常常是 0，那就什么都不做，由 QML 把菜单项灰掉。
        Q_INVOKABLE void openArtist(int index);
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

        // "我的歌单"：未登录时界面自己显示"登录后可见"，这里仍然可以调，
        // 服务层会直接返回空列表而不发请求。
        [[nodiscard]] QVariantList myPlaylists() const { return myPlaylists_; }
        Q_INVOKABLE void loadMyPlaylists();
        // 每日推荐：和"推荐/榜单"一样进详情页，只是不需要 collectionId。
        Q_INVOKABLE void openDailyRecommend();

        // 收藏：右键某一行 → 先弹确认，再发写命令。不做"乐观更新"，
        // 失败时界面本来就没改过，只需要把错误摆出来。
        Q_PROPERTY(bool favoritePromptVisible READ favoritePromptVisible NOTIFY favoriteChanged)
        Q_PROPERTY(QString favoritePromptText READ favoritePromptText NOTIFY favoriteChanged)
        [[nodiscard]] bool favoritePromptVisible() const { return favoritePromptVisible_; }
        [[nodiscard]] QString favoritePromptText() const { return favoritePromptText_; }
        Q_INVOKABLE void requestFavorite(int rowIndex);
        Q_INVOKABLE void confirmFavorite();
        Q_INVOKABLE void cancelFavorite();

        // ---- N4：私人 FM ----
        // FM 是"追加式队列"：播到末尾由播放服务喊饿，组合根再要一批。
        // 这里只管"进入 FM 模式 / 喜欢 / 不感兴趣"三件事。
        Q_PROPERTY(bool fmMode READ fmMode NOTIFY fmChanged)
        [[nodiscard]] bool fmMode() const noexcept { return fmMode_; }
        Q_INVOKABLE void startPersonalFm();
        Q_INVOKABLE void leaveFm();
        // songId 由 QML 从 `Player.onlineSongId` 传进来：VM 不持有播放器，
        // 也不该为了两个按钮去认识播放模块。
        Q_INVOKABLE void likeFmSong(const QString &songId);
        Q_INVOKABLE void dislikeFmSong(const QString &songId);

        // 界面语言变了：重算账号区与空闲态状态行。
        void retranslate();
    signals:
        void fmChanged();
        void statusChanged();
        void qrChanged();
        void diagnosticsChanged();
        void collectionChanged();
        void myPlaylistsChanged();
        void favoriteChanged();

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

        QString statusText_ = tr("输入关键词，回车搜索");
        QString errorText_;
        QString accountText_ = tr("未登录");
        bool busy_ = false;
        bool hasMore_ = false;
        bool signedIn_ = false;
        bool retryable_ = false;
        bool cachedResult_ = false;
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
        QString collectionCover_;
        // "加入队列"复用了取直链那条异步流程：解析回来时按 enqueueIds_ 挑行，
        // 派发 AppendTracks 而不是 PlayTracks。空 vector = 当前不是在排队。
        bool enqueueOnly_{false};
        // 一次性提示（如"已加入队列"）会被 50ms 一拍的进度刷新立刻盖掉；
        // 用"按住 N 拍"给它留出可读时间（100 拍 ≈ 5 秒）。
        int statusHoldTicks_{0};
        std::vector<std::string> enqueueIds_;
        // 卡片列表：哪些 kind 已经请求过、各自看到第几版。
        QHash<int, std::uint64_t> seenCollectionRevisions_;
        // 当前要看哪一类卡片（-1 = 不看）；界面切标签页时改它。
        int collectionsWanted_ = -1;

        // "我的歌单"：登录后才拉；未登录时这个列表保持空，界面显示"登录后可见"。
        QVariantList myPlaylists_;
        bool myPlaylistsWanted_ = false;
        int myPlaylistsTick_ = 0;
        bool favoritePromptVisible_ = false;
        QString favoritePromptText_;
        std::string favoriteSongId_;
        std::string favoritePlaylistId_;
        // FM 模式与"进来时顺手拉到的喜欢列表 id"。
        bool fmMode_{false};
        std::string fmPlaylistId_;
        std::string pendingLikeSongId_;
        std::size_t myPlaylistsSeen_ = 0;
        void publishMyPlaylists(const std::vector<online::contracts::PlaylistDto> &items);

        OnlineListModel model_;
        OnlineCollectionModel collections_;

    };

} // namespace sonora::presentation
