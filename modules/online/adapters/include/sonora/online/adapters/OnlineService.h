// 在线音源的编排层：命令/查询在这里变成"状态表 + 传输 + 缓存"的调用。
//
// R1 只做到骨架：状态机、参数校验、错误文案都已就位，真正的 HTTP 请求
// 留到 R2（`submit()` 里那一处）。这样做的理由不是偷懒——先把**线程边界与
// 错误面**固定下来，R2 换进去的只是"谁来填行"，接口一行都不用动。
//
// 这一层是 adapters：允许链 `platform/net` 与 `platform/storage`，
// 但**不许出现 QML、不许自己开线程**（线程在 SerialWorker 里）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sonora/kernel/Result.h"
#include "sonora/online/contracts/Commands.h"
#include "sonora/online/contracts/Queries.h"
#include "sonora/online/contracts/Types.h"
#include "sonora/online/core/app/JobRegistry.h"

namespace sonora::online::adapters
{

    class OnlineService final
    {
    public:
        OnlineService() = default;

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
        [[nodiscard]] kernel::Result<contracts::OnlineJobState> handle(
            const contracts::GetOnlineJob &query) const;
        [[nodiscard]] kernel::Result<contracts::OnlinePageDto> handle(
            const contracts::FetchOnlineRows &query) const;
        [[nodiscard]] kernel::Result<contracts::SourceStatusDto> handle(
            const contracts::GetSourceStatus &query) const;
        [[nodiscard]] kernel::Result<contracts::QrLoginDto> handle(
            const contracts::GetQrLoginState &query) const;
        [[nodiscard]] kernel::Result<std::vector<contracts::PlaylistDto>> handle(
            const contracts::GetMyPlaylists &query) const;

    private:
        // R2 的落点：把 command 变成一个投给 SerialWorker 的抓取任务。
        void submit(const contracts::RequestOnlinePage &command);
        void reportNotWired(std::uint64_t token, const char *what);

        core::app::JobRegistry jobs_;
        contracts::SourceStatusDto status_{};
        contracts::QrLoginDto qr_{};
    };

} // namespace sonora::online::adapters
