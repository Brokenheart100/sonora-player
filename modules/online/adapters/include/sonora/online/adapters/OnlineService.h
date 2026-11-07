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
#include "sonora/online/contracts/Transport.h"
#include "sonora/online/contracts/Types.h"
#include "sonora/online/core/app/JobRegistry.h"
#include "sonora/platform/storage/SqliteConnection.h"

namespace sonora::online::adapters
{

    class OnlineService final
    {
    public:
        OnlineService(contracts::IHttpTransport &transport, contracts::IExecutor &executor,
                      platform::storage::SqliteConnection &db, const kernel::IClock &clock);

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
        [[nodiscard]] kernel::Result<contracts::SourceStatusDto> handle(const contracts::GetSourceStatus &query) const;
        [[nodiscard]] kernel::Result<contracts::QrLoginDto> handle(const contracts::GetQrLoginState &query) const;
        [[nodiscard]] kernel::Result<std::vector<contracts::PlaylistDto>> handle(
            const contracts::GetMyPlaylists &query) const;

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

        // 把一份解码结果落进状态表（顺带写缓存）。只在 GUI 线程调用。
        void apply(std::uint64_t token, core::codec::DecodeResult result, const std::string &keywords,
                   std::int32_t offset);

        void drain();

        core::app::JobRegistry jobs_;
        NeteaseProvider provider_;
        SqliteOnlineStore store_;
        const kernel::IClock &clock_;

        std::mutex pendingMutex_;
        std::vector<Pending> pending_;

        std::unordered_map<std::uint64_t, std::uint64_t> inflight_;              // token → requestId
        std::unordered_map<std::uint64_t, contracts::RequestOnlinePage> requests_; // token → 原命令（重试要参数）

        contracts::SourceStatusDto status_{};
        contracts::QrLoginDto qr_{};
    };

} // namespace sonora::online::adapters
