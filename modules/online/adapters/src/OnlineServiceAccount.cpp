#include "sonora/online/adapters/OnlineService.h"
#include "OnlineServiceShared.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "sonora/online/core/app/Hint.h"
#include "sonora/online/core/auth/CookieHeader.h"
#include "sonora/online/core/auth/QrCode.h"
#include "sonora/online/core/provider/NeteaseUrls.h"
#include "sonora/platform/storage/SqliteStatement.h"

namespace sonora::online::adapters
{
    using namespace detail;

    namespace c = online::contracts;

    // —— 登录、登出、扫码、会话持久化 ——

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
        sessionCookie_.clear();
        userId_.clear();
        myPlaylists_.clear();
        myPlaylistsLoaded_ = false;
        ++myPlaylistsRevision_;
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
            noteFailure(result.error, "扫码");
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
                sessionCookie_ = item.cookie;
                userId_ = item.result.account.userId;
                status_.signedIn = true;
                status_.nickname = item.result.account.nickname;
                status_.accountPhase = c::AccountPhase::LoggedIn;
                status_.nextRetryAtUnix = 0;
                status_.lastError = c::OnlineError{};
                noteSuccess();
                continue;
            }

            if (item.result.ok)
            {
                // cookie 能发，但网易云说这不是登录态（在浏览器里退出了）。
                noteFailure(c::OnlineError{kernel::ErrorCode::Unauthenticated,
                                           "这个 cookie 已经失效，请重新复制一次", false, 0},
                            "登录校验");
                status_.signedIn = false;
                status_.accountPhase = c::AccountPhase::Guest;
                status_.lastError.code = kernel::ErrorCode::Unauthenticated;
                status_.lastError.message = "这个 cookie 已经失效，请重新复制一次";
            }
            else
            {
                noteFailure(item.result.error, "登录校验");
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
            "INSERT OR REPLACE INTO online_session(provider, secret_ref, nickname, updated_at, user_id) "
            "VALUES(?1, ?2, ?3, ?4, ?5)");
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
        statement.bind(5, std::string_view{account.userId});
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
