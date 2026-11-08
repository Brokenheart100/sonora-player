// 在线音源的编排层：命令/查询在这里变成"状态表 + 传输 + 缓存"的调用。
//
// 线程模型（docs/ONLINE.md §4.1）：命令与查询都跑在 GUI 线程，网络跑在
// `SerialWorker` 那条线程。两边只通过一个加锁的 pending 队列交接结果——
// 于是 `JobRegistry` 与那唯一的 SQLite 连接始终只被 GUI 线程碰，不需要锁，
// 也不需要把 Qt 的回主线程机制引进 adapters。
//
// 时序：命令登记 token 并投任务 → 响应落到 pending → 界面 50ms 一次
// `GetOnlineJob` 时顺手排空 → token 对不上就丢弃（`JobRegistry` 裁决）。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "sonora/kernel/IClock.h"
#include "sonora/kernel/Result.h"
#include "sonora/online/adapters/NeteaseProvider.h"
#include "sonora/online/adapters/SqliteOnlineStore.h"
#include "sonora/online/contracts/Commands.h"
#include "sonora/online/contracts/Executor.h"
#include "sonora/online/contracts/Queries.h"
#include "sonora/online/contracts/SecretStore.h"
#include "sonora/online/contracts/Transport.h"
#include "sonora/online/contracts/Types.h"
#include "sonora/online/core/app/Diagnostics.h"
#include "sonora/online/core/app/JobRegistry.h"
#include "sonora/online/core/policy/Backoff.h"
#include "sonora/online/core/policy/FailureCounter.h"
#include "sonora/online/core/policy/TokenBucket.h"
#include "sonora/platform/storage/SqliteConnection.h"

namespace sonora::online::adapters
{

    class OnlineService final
    {
    public:
        OnlineService(contracts::IHttpTransport &transport, contracts::IExecutor &executor,
                      platform::storage::SqliteConnection &db, contracts::ISecretStore &secrets,
                      const kernel::IClock &clock);

        // 启动时恢复会话：读密文 → 问网易云"这还认吗" → 结果落到 status_。
        // 组合根在注册完命令/查询之后调一次；不调也能跑，只是每次启动都要重新登录。
        void restoreSession();

        // ---- 命令：全部同步、< 5ms，只记账 ----
        kernel::Result<void> handle(const contracts::RequestOnlinePage &command);
        // 拉一屏"卡片"：推荐歌单 / 榜单 / 新歌速递。结果落进内存里的
        // `collections_`，界面随后用 FetchCollections 取——中间不做分页，
        // 这三类一屏就是全部（推荐 30 张、榜单 65 张、新歌 30 首）。
        kernel::Result<void> handle(const contracts::LoadCollections &command);
        // 拉一次"我的歌单"。未登录时**直接把任务挡在门外**（不消耗令牌、不记错误），
        // 界面据此显示"登录后可见"。写操作收藏/取消收藏同在这条通道上。
        kernel::Result<void> handle(const contracts::LoadMyPlaylists &command);
        kernel::Result<void> handle(const contracts::ToggleFavorite &command);
        kernel::Result<void> handle(const contracts::CancelOnlineJob &command);
        kernel::Result<void> handle(const contracts::RetryOnlineJob &command);
        kernel::Result<void> handle(const contracts::ResolveAndPlayOnline &command);
        kernel::Result<void> handle(const contracts::SignInWithCookie &command);
        kernel::Result<void> handle(const contracts::SignOut &command);
        kernel::Result<void> handle(const contracts::StartQrLogin &command);
        kernel::Result<void> handle(const contracts::CancelQrLogin &command);

        // ---- 查询：只读内存里的状态表，< 1ms ----
        // `GetOnlineJob` 是唯一会顺带排空 pending 的查询——界面 50ms 轮询它一次，
        // 这就足够把 worker 线程带回来的结果搬进状态表。
        [[nodiscard]] kernel::Result<contracts::OnlineJobState> handle(const contracts::GetOnlineJob &query);
        [[nodiscard]] kernel::Result<contracts::OnlinePageDto> handle(const contracts::FetchOnlineRows &query);
        [[nodiscard]] kernel::Result<contracts::OnlinePlaybackDto> handle(const contracts::GetOnlinePlayback &query);
        // 这个查询不再是 const：它顺带排空登录回调（同一个 50ms 轮询里完成）。
        [[nodiscard]] kernel::Result<contracts::SourceStatusDto> handle(const contracts::GetSourceStatus &query);
        // 也不再是 const：它顺带按 1.5 秒节奏发下一次扫码轮询。
        [[nodiscard]] kernel::Result<contracts::QrLoginDto> handle(const contracts::GetQrLoginState &query);
        // 这个查询也不再是 const：它顺带排空"我的歌单"回包（仍在同一个 50ms 轮询里）。
        [[nodiscard]] kernel::Result<std::vector<contracts::PlaylistDto>> handle(
            const contracts::GetMyPlaylists &query);
        // 卡片列表与详情都是纯内存读：`LoadCollections` / `RequestOnlinePage` 已经
        // 把结果放在服务里了，这里只是换个形状交出去。
        [[nodiscard]] kernel::Result<contracts::CollectionPageDto> handle(
            const contracts::FetchCollections &query);
        [[nodiscard]] kernel::Result<contracts::CollectionDetailDto> handle(
            const contracts::FetchCollectionDetail &query) const;
        // 一次性诊断快照：只在用户点开面板时取（内部要查两次 COUNT，不进轮询）。
        [[nodiscard]] kernel::Result<contracts::OnlineDiagnosticsDto> handle(
            const contracts::GetOnlineDiagnostics &query);

    private:
        struct Pending
        {
            std::uint64_t token{0};
            core::codec::DecodeResult result;
            // 命中缓存时只搬原始 JSON，解码留到 drain()：命令 handler 有 5ms 预算。
            std::string rawForDecode;
            std::string keywords;
            std::int32_t offset{0};
        };

        // 投一个抓取任务。`bypassCache` 供重试用——重试的语义是"别给我缓存"。
        void submit(const contracts::RequestOnlinePage &command, bool bypassCache);

        // 取地址的回包：worker 线程只往这里塞，GUI 线程的轮询顺手排空。
        struct PendingPlayback
        {
            core::codec::UrlResult result;
            std::int32_t startIndex{0};
        };
        void drainPlayback();

        // 把一份解码结果落进状态表（顺带写缓存）。只在 GUI 线程调用。
        void apply(std::uint64_t token, core::codec::DecodeResult result, const std::string &keywords,
                   std::int32_t offset);

        // ---- N1：只读资源 ----
        // 一条回包通道装三种结果，按到达顺序处理（`drainPages()` 在 50ms 轮询里跑）。
        struct PendingPage
        {
            std::uint64_t token{0};
            contracts::SourceKind kind{contracts::SourceKind::Search};
            std::string key;
            std::int32_t offset{0};
            std::int32_t limit{0};
            core::codec::DecodeResult result;
        };
        struct PendingCollection
        {
            contracts::SourceKind kind{contracts::SourceKind::Playlist};
            core::codec::CollectionListResult result;
        };
        struct PendingPlaylistDetail
        {
            std::uint64_t token{0};
            std::string collectionId;
            contracts::SourceKind kind{contracts::SourceKind::Playlist};
            core::codec::PlaylistDetailResult result;
        };

        // 按 kind 分发一页请求：搜索原路、歌单/榜单两段、专辑/歌手/新歌一次取全后在内存切片。
        kernel::Result<void> startPage(const contracts::RequestOnlinePage &command);
        // 歌单/榜单的第二段：拿到 trackIds 后按 500 一批取歌曲详情。
        void startPlaylistBatch(std::uint64_t token, contracts::SourceKind kind, const std::string &collectionId,
                                const std::vector<std::string> &trackIds, std::int32_t offset, std::int32_t limit);
        void drainPages();
        void applyPlaylistDetail(const PendingPlaylistDetail &item);
        void applyPage(PendingPage &item);
        void applyCollectionList(const PendingCollection &item);
        // 退避 + 令牌两道闸门；挡住时自己 jobs_.fail 并返回 false。
        bool gate(std::uint64_t token, contracts::SourceKind kind, std::int64_t now);
        [[nodiscard]] static std::string pageKey(contracts::SourceKind kind, const std::string &collectionId);
        [[nodiscard]] static std::string whereOf(contracts::SourceKind kind);

        // 歌单/榜单的中间状态：详情头部 + 全量 trackIds，跨页复用。
        struct PlaylistPlan
        {
            contracts::CollectionDetailDto detail;
            std::vector<std::string> trackIds;
        };
        std::unordered_map<std::string, PlaylistPlan> playlists_;
        // 专辑/歌手/新歌一次取全（都 ≤ 100 行），分页只是在内存里切片。
        std::unordered_map<std::string, std::vector<contracts::OnlineTrackDto>> fullPages_;
        std::unordered_map<std::string, contracts::CollectionDetailDto> details_;
        std::unordered_map<int, contracts::CollectionPageDto> collections_; // key: SourceKind 的整数

        // ---- N2：登录侧 ----
        struct PendingUserPlaylists
        {
            core::codec::PlaylistsResult result;
        };
        struct PendingFavorite
        {
            core::codec::FavoriteResult result;
            bool add{true};
        };
        void drainMyPlaylists();
        void drainFavorite();
        // 从当前会话 cookie 里取 `__csrf`。**只用于拼表单字段**，不缓存、不写日志。
        [[nodiscard]] std::string csrfToken() const;

        std::string sessionCookie_;
        std::string userId_;
        std::vector<contracts::PlaylistDto> myPlaylists_;
        contracts::OnlineError myPlaylistsError_;
        bool myPlaylistsLoading_{false};
        bool myPlaylistsLoaded_{false};
        std::uint64_t myPlaylistsRevision_{0};

        std::mutex pageMutex_;
        std::vector<PendingPage> pagePending_;
        std::vector<PendingCollection> collectionPending_;
        std::vector<PendingPlaylistDetail> playlistDetailPending_;
        std::vector<PendingUserPlaylists> userPlaylistsPending_;
        std::vector<PendingFavorite> favoritePending_;

        void drain();
        // 失败留痕的唯一入口：计数器 + 环形缓冲 + 退避闸门，三件事一起做，
        // 免得某个失败分支只记了日志、忘了让退避生效。
        void noteFailure(const contracts::OnlineError &error, std::string where);
        void noteSuccess() noexcept { failures_.recordSuccess(); }
        // 给"上游整体不可达"加一句人话（docs/ONLINE.md §5.3 最后一行）：
        // 单次失败照旧显示原始消息；连续失败到第二回，多半是接口变了/域名没了，
        // 而不是用户网断了——这两种情况用户能做的事完全不同。
        [[nodiscard]] contracts::OnlineError withHint(contracts::OnlineError error) const;
        [[nodiscard]] std::int64_t nowMs() const noexcept;

        // 登录校验的回包（粘贴 cookie 与启动恢复共用一条通道）。
        struct PendingAuth
        {
            std::string cookie;
            core::codec::AccountResult result;
        };
        void drainAuth();

        // 扫码登录：unikey 与轮询状态各走一条回包通道（worker 线程只入队）。
        struct PendingQrKey
        {
            core::codec::QrKeyResult result;
        };
        struct PendingQrPoll
        {
            core::codec::QrPollResult result;
        };
        void drainQrLogin();
        void applyQrKey(const core::codec::QrKeyResult &result);
        void applyQrPoll(const core::codec::QrPollResult &result);
        // 校验通过之后的落盘顺序：先密文、再会话行（docs/ONLINE.md §6 / R4-6）。
        kernel::Result<void> persistSession(const std::string &cookie, const core::codec::AccountInfo &account);
        void clearSession();
        [[nodiscard]] std::string loadStoredNickname() const;

        // ---- 扫码登录 ----（只在 GUI 线程改，回包用 qrMutex_ 保护）
        std::mutex qrMutex_;
        std::vector<PendingQrKey> qrKeyPending_;
        std::vector<PendingQrPoll> qrPollPending_;
        std::string qrKey_;
        std::int64_t qrNextPollMs_{0};
        bool qrPollInFlight_{false};
        std::uint64_t qrSession_{0};

        core::app::JobRegistry jobs_;
        NeteaseProvider provider_;
        SqliteOnlineStore store_;
        // 降级留痕。放在服务里而不是 provider 里：provider 不知道"用户看到的是什么"，
        // 而 §5.3 的分类正是按用户可见形态分的。
        core::app::Diagnostics diag_;
        // 令牌桶与退避闸门都是纯策略对象，时间由 nowMs() 喂进来（可确定性测试）。
        core::policy::TokenBucket bucket_{3.0, 6.0};
        core::policy::FailureCounter failures_;
        core::policy::BackoffPolicy backoff_{};
        const kernel::IClock &clock_;
        contracts::ISecretStore &secrets_;
        // 会话行的读写在 GUI 线程，与 store_ 共用同一条连接（不做连接池）。
        platform::storage::SqliteConnection &db_;

        std::mutex pendingMutex_;
        std::vector<Pending> pending_;

        std::unordered_map<std::uint64_t, std::uint64_t> inflight_;              // token → requestId
        std::unordered_map<std::uint64_t, contracts::RequestOnlinePage> requests_; // token → 原命令（重试要参数）

        contracts::SourceStatusDto status_{};
        contracts::QrLoginDto qr_{};

        std::mutex playbackMutex_;
        std::vector<PendingPlayback> playbackPending_;
        // 表现层按 revision 去重：只有这个数变了才重装播放队列。
        contracts::OnlinePlaybackDto playback_{};
        std::uint64_t playbackRevision_{0};

        std::mutex authMutex_;
        std::vector<PendingAuth> authPending_;
        std::uint64_t authRequestId_{0};
        // 校验通过前暂存明文：只有确认可用才落盘，失败只进 lastError。
        std::string pendingCookie_;
    };

} // namespace sonora::online::adapters
