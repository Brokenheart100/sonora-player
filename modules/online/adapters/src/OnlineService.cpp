#include "sonora/online/adapters/OnlineService.h"

namespace sonora::online::adapters
{

    namespace
    {
        // R1 的占位文案。它必须说清"这不是你的网络问题"，否则用户会去重装。
        // R2 把 submit() 填上之后，这条路径就只剩"真的失败了"。
        constexpr const char *kPendingSearch = "在线搜索还没接通（下一步 R2）";
        constexpr const char *kPendingPlay = "在线播放还没接通（下一步 R3）";
        constexpr const char *kPendingSignIn = "登录还没接通（下一步 R4）";
    } // namespace

    kernel::Result<void> OnlineService::handle(const contracts::RequestOnlinePage &command)
    {
        if (command.kind == contracts::SourceKind::Search && command.keywords.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "搜索关键词为空"};
        }
        if (command.limit <= 0)
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "limit 必须为正"};
        }

        jobs_.begin(command.token, command.kind, command.keywords);
        submit(command);
        return {};
    }

    void OnlineService::submit(const contracts::RequestOnlinePage &command)
    {
        // TODO(R2)：tokenBucket.take() → transport.send() → 解析 → jobs_.appendRows()。
        // 现在只把状态推到"失败"，让界面的三态（加载/有数据/错误）能被真实走到。
        reportNotWired(command.token, kPendingSearch);
    }

    void OnlineService::reportNotWired(std::uint64_t token, const char *what)
    {
        jobs_.fail(token, contracts::OnlineError{kernel::ErrorCode::Unavailable, what, false, 0});
    }

    kernel::Result<void> OnlineService::handle(const contracts::CancelOnlineJob &command)
    {
        // 取消只影响"当前 token"：旧 token 的记录留着，界面可能还拿着旧快照。
        jobs_.setPhase(command.token, contracts::JobPhase::Idle);
        jobs_.forget(command.token);
        return {};
    }

    kernel::Result<void> OnlineService::handle(const contracts::RetryOnlineJob &command)
    {
        jobs_.setPhase(command.token, contracts::JobPhase::Loading);
        reportNotWired(command.token, kPendingSearch);
        return {};
    }

    kernel::Result<void> OnlineService::handle(const contracts::ResolveAndPlayOnline &)
    {
        return kernel::Error{kernel::ErrorCode::Unavailable, kPendingPlay};
    }

    kernel::Result<void> OnlineService::handle(const contracts::SignInWithCookie &command)
    {
        // 唯一的硬校验：空 cookie 不该被写进凭据库，否则下次启动是"看起来登录了"。
        if (command.cookie.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "cookie 为空"};
        }
        return kernel::Error{kernel::ErrorCode::Unavailable, kPendingSignIn};
    }

    kernel::Result<void> OnlineService::handle(const contracts::SignOut &)
    {
        status_.signedIn = false;
        status_.accountPhase = contracts::AccountPhase::Guest;
        status_.nickname.clear();
        status_.lastError = {};
        return {};
    }

    kernel::Result<void> OnlineService::handle(const contracts::StartQrLogin &)
    {
        return kernel::Error{kernel::ErrorCode::Unavailable, kPendingSignIn};
    }

    kernel::Result<void> OnlineService::handle(const contracts::CancelQrLogin &)
    {
        qr_.phase = contracts::AccountPhase::Guest;
        qr_.message.clear();
        return {};
    }

    kernel::Result<contracts::OnlineJobState> OnlineService::handle(
        const contracts::GetOnlineJob &query) const
    {
        return kernel::Result<contracts::OnlineJobState>::ok(jobs_.state(query.token));
    }

    kernel::Result<contracts::OnlinePageDto> OnlineService::handle(
        const contracts::FetchOnlineRows &query) const
    {
        return kernel::Result<contracts::OnlinePageDto>::ok(
            jobs_.page(query.token, query.offset, query.limit));
    }

    kernel::Result<contracts::SourceStatusDto> OnlineService::handle(
        const contracts::GetSourceStatus &) const
    {
        return kernel::Result<contracts::SourceStatusDto>::ok(status_);
    }

    kernel::Result<contracts::QrLoginDto> OnlineService::handle(
        const contracts::GetQrLoginState &) const
    {
        return kernel::Result<contracts::QrLoginDto>::ok(qr_);
    }

    kernel::Result<std::vector<contracts::PlaylistDto>> OnlineService::handle(
        const contracts::GetMyPlaylists &) const
    {
        return kernel::Result<std::vector<contracts::PlaylistDto>>::ok(
            std::vector<contracts::PlaylistDto>{});
    }

} // namespace sonora::online::adapters
