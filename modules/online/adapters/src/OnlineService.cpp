#include "sonora/online/adapters/OnlineService.h"

#include <algorithm>
#include <utility>

#include "sonora/online/core/provider/NeteaseUrls.h"

namespace sonora::online::adapters
{
    namespace c = online::contracts;

    namespace
    {
        constexpr const char *kProvider = "ncm";

        c::OnlineError notWired(const char *what)
        {
            c::OnlineError error;
            error.code = kernel::ErrorCode::Unavailable;
            error.message = what;
            error.retryable = false;
            return error;
        }
    } // namespace

    OnlineService::OnlineService(c::IHttpTransport &transport, c::IExecutor &executor,
                                 platform::storage::SqliteConnection &db, const kernel::IClock &clock)
        : provider_(transport, executor), store_(db, clock), clock_(clock)
    {
        status_.provider = kProvider;
        status_.enabled = true;
        status_.signedIn = false;
        status_.accountPhase = c::AccountPhase::Guest;
    }

    // ---------------------------------------------------------------- 命令

    kernel::Result<void> OnlineService::handle(const c::RequestOnlinePage &command)
    {
        if (command.kind != c::SourceKind::Search)
        {
            auto token = command.token;
            jobs_.begin(token, command.kind, command.keywords);
            requests_[token] = command;
            jobs_.fail(token, notWired("只接通了搜索；歌单/收藏/最近留 R5"));
            return {};
        }
        if (command.keywords.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "搜索关键词为空"};
        }

        // token 正常由界面递增；传 0 时补一个，免得所有请求挤在同一条记录上。
        std::uint64_t token = command.token;
        if (token == 0)
        {
            token = jobs_.currentToken() + 1;
        }

        auto request = command;
        request.token = token;
        request.limit = std::clamp<std::int32_t>(command.limit, 1, core::provider::kMaxSearchLimit);

        jobs_.begin(token, c::SourceKind::Search, request.keywords);
        requests_[token] = request;

        // 缓存先查：改一个字再改回来是最常见的输入模式，命中一次就省一次请求。
        // 命中时不在这里解码：命令 handler 的预算是 5ms，30 行的 JSON 解码 +
        // 落 ViewModel 会把预算吃掉，所以只把原文塞进待处理队列，交给 50ms 轮询
        // 的那次 drain() 解码（它本来就要读队列）。
        if (const auto cached = store_.loadSearch(kProvider, request.keywords, request.offset))
        {
            Pending pending;
            pending.token = token;
            pending.keywords = request.keywords;
            pending.offset = request.offset;
            pending.rawForDecode = *cached;
            {
                std::lock_guard<std::mutex> guard(pendingMutex_);
                pending_.push_back(std::move(pending));
            }
            return {};
        }

        submit(request, false);
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::CancelOnlineJob &command)
    {
        if (const auto it = inflight_.find(command.token); it != inflight_.end())
        {
            provider_.cancel(it->second);
            inflight_.erase(it);
        }
        jobs_.setPhase(command.token, c::JobPhase::Idle);
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::RetryOnlineJob &command)
    {
        const auto it = requests_.find(command.token);
        if (it == requests_.end())
        {
            return kernel::Error{kernel::ErrorCode::NotFound, "没有这个任务"};
        }
        jobs_.setPhase(command.token, c::JobPhase::Loading);
        submit(it->second, /*bypassCache=*/true);
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::ResolveAndPlayOnline &)
    {
        return kernel::Error{kernel::ErrorCode::Unavailable, "在线播放还没接通（下一步 R3）"};
    }

    kernel::Result<void> OnlineService::handle(const c::SignInWithCookie &command)
    {
        if (command.cookie.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "cookie 为空"};
        }
        return kernel::Error{kernel::ErrorCode::Unavailable, "登录还没接通（下一步 R4）"};
    }

    kernel::Result<void> OnlineService::handle(const c::SignOut &)
    {
        provider_.setCookie({});
        status_.signedIn = false;
        status_.nickname.clear();
        status_.accountPhase = c::AccountPhase::Guest;
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::StartQrLogin &)
    {
        return kernel::Error{kernel::ErrorCode::Unavailable, "扫码登录还没接通（下一步 R4）"};
    }

    kernel::Result<void> OnlineService::handle(const c::CancelQrLogin &)
    {
        qr_.phase = c::AccountPhase::Guest;
        return {};
    }

    // ---------------------------------------------------------------- 查询

    kernel::Result<c::OnlineJobState> OnlineService::handle(const c::GetOnlineJob &query)
    {
        drain();
        return kernel::Result<c::OnlineJobState>::ok(jobs_.state(query.token));
    }

    kernel::Result<c::OnlinePageDto> OnlineService::handle(const c::FetchOnlineRows &query)
    {
        return kernel::Result<c::OnlinePageDto>::ok(jobs_.page(query.token, query.offset, query.limit));
    }

    kernel::Result<c::SourceStatusDto> OnlineService::handle(const c::GetSourceStatus &) const
    {
        return kernel::Result<c::SourceStatusDto>::ok(status_);
    }

    kernel::Result<c::QrLoginDto> OnlineService::handle(const c::GetQrLoginState &) const
    {
        return kernel::Result<c::QrLoginDto>::ok(qr_);
    }

    kernel::Result<std::vector<c::PlaylistDto>> OnlineService::handle(const c::GetMyPlaylists &) const
    {
        return kernel::Result<std::vector<c::PlaylistDto>>::ok({});
    }

    // ---------------------------------------------------------------- 内部

    void OnlineService::submit(const c::RequestOnlinePage &command, bool bypassCache)
    {
        core::provider::SearchParams params;
        params.keywords = command.keywords;
        params.offset = command.offset;
        params.limit = command.limit;

        const std::uint64_t token = command.token;
        const std::string keywords = command.keywords;
        const std::int32_t offset = command.offset;

        const std::uint64_t requestId = provider_.startSearch(
            params, [this, token, keywords, offset](core::codec::DecodeResult result) {
                // 这里是 worker 线程：只许塞队列，不许碰 jobs_/store_/status_。
                std::lock_guard<std::mutex> guard(pendingMutex_);
                Pending pending;
                pending.token = token;
                pending.result = std::move(result);
                pending.keywords = keywords;
                pending.offset = offset;
                pending_.push_back(std::move(pending));
            });

        inflight_[token] = requestId;
        (void)bypassCache;
    }

    void OnlineService::apply(std::uint64_t token, core::codec::DecodeResult result, const std::string &keywords,
                              std::int32_t offset)
    {
        if (!result.ok)
        {
            jobs_.fail(token, result.error);
            return;
        }

        if (result.rows.empty() && offset == 0)
        {
            jobs_.setPhase(token, c::JobPhase::Empty);
            return;
        }

        jobs_.appendRows(token, std::move(result.rows), result.hasMore);

        if (!result.raw.empty())
        {
            store_.saveSearch(kProvider, keywords, offset, result.raw);
            store_.trimSearch();
        }
    }

    void OnlineService::drain()
    {
        std::vector<Pending> ready;
        {
            std::lock_guard<std::mutex> guard(pendingMutex_);
            ready.swap(pending_);
        }
        for (auto &item : ready)
        {
            if (const auto it = inflight_.find(item.token); it != inflight_.end())
            {
                inflight_.erase(it);
            }
            if (!item.rawForDecode.empty())
            {
                item.result = core::codec::decodeSearch(item.rawForDecode);
                item.result.raw = std::move(item.rawForDecode);
            }
            apply(item.token, std::move(item.result), item.keywords, item.offset);
        }
    }

} // namespace sonora::online::adapters
