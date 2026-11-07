#include "sonora/online/adapters/OnlineService.h"

#include "sonora/online/core/auth/QrCode.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "sonora/online/core/auth/CookieHeader.h"
#include "sonora/online/core/provider/NeteaseUrls.h"
#include "sonora/platform/storage/SqliteStatement.h"

namespace sonora::online::adapters
{

    namespace
    {
        std::int64_t nowMillis(const kernel::IClock &clock)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(clock.now().time_since_epoch()).count();
        }
    } // namespace

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
                                 platform::storage::SqliteConnection &db, c::ISecretStore &secrets,
                                 const kernel::IClock &clock)
        : provider_(transport, executor), store_(db, clock), clock_(clock), secrets_(secrets), db_(db)
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

    kernel::Result<void> OnlineService::handle(const c::ResolveAndPlayOnline &command)
    {
        if (command.songIds.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有可播放的曲目"};
        }

        // 单批上限 kMaxSongIds：一次取 200 首的地址是 R0 实测能跑通的形状，
        // 超过的部分先不取（R3 只做"点一首放整页"，队列超长时后面的进队列再补）。
        std::vector<std::string> ids;
        const std::size_t count =
            std::min<std::size_t>(command.songIds.size(), static_cast<std::size_t>(core::provider::kMaxSongIds));
        ids.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            ids.push_back(command.songIds[index]);
        }

        const std::int32_t startIndex = command.startIndex;
        provider_.startResolveUrls(ids, [this, startIndex](core::codec::UrlResult result) {
            // worker 线程：只入队，不碰状态表（那唯一的 SQLite 连接在主线程）。
            std::lock_guard<std::mutex> lock(playbackMutex_);
            playbackPending_.push_back(PendingPlayback{std::move(result), startIndex});
        });
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::SignInWithCookie &command)
    {
        const auto parts = core::auth::parseCookieHeader(command.cookie);
        if (parts.musicU.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument,
                                 "这段文本里没有 MUSIC_U，请复制完整的 Cookie 头"};
        }

        // 只留两个键：用户粘进来的可能是整段 header，别的键既没用又可能带敏感值。
        std::string cookie = core::auth::buildCookieHeader(parts);
        pendingCookie_ = cookie;
        status_.lastError = c::OnlineError{};

        // 校验是异步的：验过了才落盘（R4-4）。回包走 authPending_，
        // 由 50ms 轮询里的 drainAuth() 处理。
        authRequestId_ = provider_.startValidateAccount(
            cookie, [this, cookie = std::move(cookie)](core::codec::AccountResult result) {
                std::lock_guard<std::mutex> lock(authMutex_);
                authPending_.push_back(PendingAuth{cookie, std::move(result)});
            });
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::SignOut &)
    {
        provider_.setCookie({});
        pendingCookie_.clear();
        clearSession();
        status_.signedIn = false;
        status_.nickname.clear();
        status_.accountPhase = c::AccountPhase::Guest;
        status_.lastError = c::OnlineError{};
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::StartQrLogin &)
    {
        // 明文接口只给 unikey，二维码本地画（见 docs/ONLINE.md §16.5）。
        ++qrSession_;
        qr_ = c::QrLoginDto{};
        qr_.session = qrSession_;
        qr_.phase = c::AccountPhase::QrWaiting;
        qr_.message = "正在生成二维码…";
        qrKey_.clear();
        qrNextPollMs_ = 0;
        qrPollInFlight_ = false;

        provider_.startQrKey([this](core::codec::QrKeyResult result) {
            const std::scoped_lock lock{qrMutex_};
            qrKeyPending_.push_back({std::move(result)});
        });
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::CancelQrLogin &)
    {
        qr_.phase = c::AccountPhase::Guest;
        qr_.rows.clear();
        qrKey_.clear();
        qrPollInFlight_ = false;
        return {};
    }

    // ---------------------------------------------------------------- 查询

    kernel::Result<c::OnlineJobState> OnlineService::handle(const c::GetOnlineJob &query)
    {
        drain();
        drainAuth();
        return kernel::Result<c::OnlineJobState>::ok(jobs_.state(query.token));
    }

    kernel::Result<c::OnlinePageDto> OnlineService::handle(const c::FetchOnlineRows &query)
    {
        return kernel::Result<c::OnlinePageDto>::ok(jobs_.page(query.token, query.offset, query.limit));
    }

    kernel::Result<c::OnlinePlaybackDto> OnlineService::handle(const c::GetOnlinePlayback &)
    {
        drainPlayback();
        return kernel::Result<c::OnlinePlaybackDto>::ok(playback_);
    }

    void OnlineService::drainPlayback()
    {
        std::vector<PendingPlayback> incoming;
        {
            std::lock_guard<std::mutex> lock(playbackMutex_);
            incoming.swap(playbackPending_);
        }
        if (incoming.empty())
        {
            return;
        }

        // 一次只认最后一份：用户可能连点了好几首，前面的结果已经没有意义。
        PendingPlayback item = std::move(incoming.back());
        ++playbackRevision_;

        if (!item.result.ok)
        {
            c::OnlinePlaybackDto next;
            next.revision = playbackRevision_;
            next.error = item.result.error;
            next.startIndex = item.startIndex;
            playback_ = std::move(next);
            return;
        }

        c::OnlinePlaybackDto next;
        next.revision = playbackRevision_;
        next.startIndex = item.startIndex;
        next.entries.reserve(item.result.entries.size());
        for (const auto &entry : item.result.entries)
        {
            c::OnlinePlaybackEntry out;
            out.songId = entry.songId;
            out.url = entry.url;
            out.bitrateKbps = entry.bitrate;
            next.entries.push_back(std::move(out));
        }
        playback_ = std::move(next);
    }

    kernel::Result<c::SourceStatusDto> OnlineService::handle(const c::GetSourceStatus &)
    {
        drainAuth();
        return kernel::Result<c::SourceStatusDto>::ok(status_);
    }

    kernel::Result<c::QrLoginDto> OnlineService::handle(const c::GetQrLoginState &)
    {
        drainQrLogin();

        // 1.5 秒一次的轮询由这个查询驱动：界面本来就在 50ms 轮询在线状态，
        // 不用再为扫码单开一条线程或定时器（§4 的线程模型：core 不持计时器）。
        const bool polling = qr_.phase == c::AccountPhase::QrWaiting || qr_.phase == c::AccountPhase::QrScanned;
        if (polling && !qrKey_.empty() && !qrPollInFlight_ && nowMillis(clock_) >= qrNextPollMs_)
        {
            qrPollInFlight_ = true;
            qrNextPollMs_ = nowMillis(clock_) + 1500;
            provider_.startQrPoll(qrKey_, [this](core::codec::QrPollResult result) {
                const std::scoped_lock lock{qrMutex_};
                qrPollPending_.push_back({std::move(result)});
            });
        }
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

    void OnlineService::restoreSession()
    {
        auto stored = secrets_.load(kProvider);
        if (!stored)
        {
            // 从没登录过（或密文解不开）：安静地当游客，别在界面上吓人。
            return;
        }

        // 先拿昵称把界面填上，再异步问网易云"这还认吗"——网络慢时胶囊不是空白。
        status_.nickname = loadStoredNickname();
        const auto parts = core::auth::parseCookieHeader(stored.value());
        if (parts.musicU.empty())
        {
            return;
        }

        std::string cookie = core::auth::buildCookieHeader(parts);
        pendingCookie_ = cookie;
        authRequestId_ = provider_.startValidateAccount(
            cookie, [this, cookie = std::move(cookie)](core::codec::AccountResult result) {
                std::lock_guard<std::mutex> lock(authMutex_);
                authPending_.push_back(PendingAuth{cookie, std::move(result)});
            });
    }

    void OnlineService::drainQrLogin()
    {
        std::vector<PendingQrKey> keys;
        std::vector<PendingQrPoll> polls;
        {
            const std::scoped_lock lock{qrMutex_};
            keys.swap(qrKeyPending_);
            polls.swap(qrPollPending_);
        }

        for (const auto &item : keys)
        {
            applyQrKey(item.result);
        }
        for (const auto &item : polls)
        {
            applyQrPoll(item.result);
        }
    }

    void OnlineService::applyQrKey(const core::codec::QrKeyResult &result)
    {
        if (!result.ok)
        {
            qr_.phase = c::AccountPhase::Failed;
            qr_.message = result.error.message.empty() ? "拿不到扫码 key" : result.error.message;
            return;
        }
        qrKey_ = result.unikey;
        // 新的一张码 = 新会话：界面靠 session 变化决定重画。
        qr_.session = ++qrSession_;
        qr_.rows = core::auth::encodeQrRows(core::provider::qrPayloadOf(result.unikey));
        qr_.phase = c::AccountPhase::QrWaiting;
        qr_.message = qr_.rows.empty() ? "二维码生成失败，点刷新再试" : "用网易云音乐手机端扫码";
        qrNextPollMs_ = 0;
    }

    void OnlineService::applyQrPoll(const core::codec::QrPollResult &result)
    {
        qrPollInFlight_ = false;
        if (!result.ok)
        {
            // 网络抖动不算扫码失败：留在等待态，下一次轮询继续。
            qr_.message = result.error.retryable ? "网络不稳，正在重试…" : result.error.message;
            return;
        }

        switch (result.code)
        {
        case 800:
            qr_.phase = c::AccountPhase::QrExpired;
            qr_.message = "二维码已过期，点「刷新」再来一次";
            qrKey_.clear();
            break;
        case 801:
            qr_.phase = c::AccountPhase::QrWaiting;
            break;
        case 802:
            qr_.phase = c::AccountPhase::QrScanned;
            qr_.message = "已扫码，请在手机上确认登录";
            break;
        case 803:
        {
            const auto usable = core::auth::parseCookieHeader(result.cookie);
            if (!usable.usable())
            {
                qr_.phase = c::AccountPhase::Failed;
                qr_.message = "扫码成功，但没拿到 cookie（接口可能变了）";
                break;
            }
            // 拿到 cookie 之后走和粘贴式登录**同一条**落盘路径，不复制第二份逻辑。
            pendingCookie_ = result.cookie;
            qr_.phase = c::AccountPhase::LoggedIn;
            qr_.message = "已扫码登录";
            status_.accountPhase = c::AccountPhase::LoggedIn;
            const std::string cookie = result.cookie;
            provider_.startValidateAccount(cookie, [this, cookie](core::codec::AccountResult account) {
                const std::scoped_lock lock{authMutex_};
                authPending_.push_back({cookie, std::move(account)});
            });
            break;
        }
        default:
            qr_.phase = c::AccountPhase::Failed;
            qr_.message = result.message.empty() ? "扫码登录失败" : result.message;
            break;
        }
    }

    void OnlineService::drainAuth()
    {
        std::vector<PendingAuth> incoming;
        {
            std::lock_guard<std::mutex> lock(authMutex_);
            incoming.swap(authPending_);
        }
        if (incoming.empty())
        {
            return;
        }

        for (auto &item : incoming)
        {
            if (item.result.ok && item.result.loggedIn)
            {
                const auto saved = persistSession(item.cookie, item.result.account);
                if (!saved)
                {
                    status_.lastError.code = saved.error().code();
                    status_.lastError.message = saved.error().message();
                    continue;
                }
                provider_.setCookie(item.cookie);
                status_.signedIn = true;
                status_.nickname = item.result.account.nickname;
                status_.accountPhase = c::AccountPhase::LoggedIn;
                status_.nextRetryAtUnix = 0;
                status_.lastError = c::OnlineError{};
                continue;
            }

            if (item.result.ok)
            {
                // cookie 能发，但网易云说这不是登录态（在浏览器里退出了）。
                status_.signedIn = false;
                status_.accountPhase = c::AccountPhase::Guest;
                status_.lastError.code = kernel::ErrorCode::Unauthenticated;
                status_.lastError.message = "这个 cookie 已经失效，请重新复制一次";
            }
            else
            {
                status_.signedIn = false;
                status_.accountPhase = item.result.error.code == kernel::ErrorCode::Unauthenticated
                                           ? c::AccountPhase::Guest
                                           : c::AccountPhase::Failed;
                status_.lastError = item.result.error;
            }
            pendingCookie_.clear();
        }
    }

    kernel::Result<void> OnlineService::persistSession(const std::string &cookie,
                                                       const core::codec::AccountInfo &account)
    {
        // 顺序：先密文、再会话行。反过来的话，行先落地而密文写失败，
        // 下次启动就会读到一个"有行没密文"的半截状态（R4-6）。
        auto saved = secrets_.save(kProvider, cookie);
        if (!saved)
        {
            return saved;
        }

        auto insert = db_.prepare(
            "INSERT OR REPLACE INTO online_session(provider, secret_ref, nickname, updated_at) "
            "VALUES(?1, ?2, ?3, ?4)");
        if (!insert)
        {
            secrets_.erase(kProvider);
            return kernel::Error{kernel::ErrorCode::Internal, "准备会话语句失败"};
        }

        auto &statement = insert.value();
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(clock_.now().time_since_epoch())
                               .count();
        statement.bind(1, std::string_view{kProvider});
        statement.bind(2, std::string_view{kProvider});
        statement.bind(3, std::string_view{account.nickname});
        statement.bind(4, static_cast<std::int64_t>(nowMs));
        auto stepped = statement.step();
        if (!stepped || stepped.value())
        {
            secrets_.erase(kProvider);
            return kernel::Error{kernel::ErrorCode::Internal, "写会话行失败"};
        }
        return {};
    }

    void OnlineService::clearSession()
    {
        // 登出：先删密文，再删行（R4-6）。密文删不掉也无所谓，行删了就不会再用它。
        secrets_.erase(kProvider);

        auto statement = db_.prepare("DELETE FROM online_session WHERE provider = ?1");
        if (!statement)
        {
            return;
        }
        statement.value().bind(1, std::string_view{kProvider});
        (void)statement.value().step();
    }

    std::string OnlineService::loadStoredNickname() const
    {
        auto statement = db_.prepare("SELECT nickname FROM online_session WHERE provider = ?1");
        if (!statement)
        {
            return {};
        }
        statement.value().bind(1, std::string_view{kProvider});
        auto stepped = statement.value().step();
        if (!stepped || !stepped.value())
        {
            return {};
        }
        return statement.value().columnString(0);
    }

} // namespace sonora::online::adapters
