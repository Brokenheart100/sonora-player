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
        [[nodiscard]] kernel::Result<std::vector<contracts::PlaylistDto>> handle(
            const contracts::GetMyPlaylists &query) const;
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
