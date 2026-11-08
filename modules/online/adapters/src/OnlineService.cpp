#include "sonora/online/adapters/OnlineService.h"
#include "sonora/online/core/app/Hint.h"

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
        bucket_.prime(nowMs());
    }

    // ---------------------------------------------------------------- 命令

    kernel::Result<void> OnlineService::handle(const c::RequestOnlinePage &command)
    {
        return startPage(command);
    }

    kernel::Result<void> OnlineService::startPage(const c::RequestOnlinePage &command)
    {
        if (command.kind != c::SourceKind::Search)
        {
            // 非搜索类先补 token、登记任务，再按资源类型走各自的取数路径。
            std::uint64_t token = command.token == 0 ? jobs_.currentToken() + 1 : command.token;
            auto request = command;
            request.token = token;
            // 只有"新开一页"才重置任务：翻页用的是同一个 token，再 begin 一次会把
            // 前一页的行清空（界面表现为"加载更多以后还是 100 首"）。
            if (jobs_.state(token).phase == c::JobPhase::Idle)
            {
                jobs_.begin(token, request.kind, request.keywords);
            }
            requests_[token] = request;
            if (!gate(token, request.kind, nowMs()))
            {
                return {};
            }

            const std::string key = pageKey(request.kind, request.collectionId);
            switch (request.kind)
            {
            case c::SourceKind::Playlist:
            case c::SourceKind::Chart:
            {
                if (request.collectionId.empty())
                {
                    jobs_.fail(token, c::OnlineError{kernel::ErrorCode::InvalidArgument, "没有指定歌单", false, 0});
                    return {};
                }
                // 第二页起 trackIds 已经在内存里，直接取下一批歌曲详情。
                if (const auto plan = playlists_.find(key); plan != playlists_.end())
                {
                    startPlaylistBatch(token, request.kind, request.collectionId, plan->second.trackIds, request.offset,
                                       request.limit);
                    return {};
                }
                provider_.startPlaylistDetail(
                    request.collectionId,
                    [this, token, kind = request.kind, id = request.collectionId](
                        core::codec::PlaylistDetailResult result) {
                        std::lock_guard<std::mutex> guard(pageMutex_);
                        playlistDetailPending_.push_back(
                            PendingPlaylistDetail{token, id, kind, std::move(result)});
                    });
                return {};
            }
            case c::SourceKind::Album:
            case c::SourceKind::Artist:
            case c::SourceKind::NewSongs:
            case c::SourceKind::DailyRecommend:
            {
                // 这三类一次取全（都 ≤ 100 行），分页在内存里切片；重进同一页不再发请求。
                if (const auto full = fullPages_.find(key); full != fullPages_.end())
                {
                    const auto &rows = full->second;
                    const std::size_t begin = std::min<std::size_t>(static_cast<std::size_t>(std::max(request.offset, 0)),
                                                                    rows.size());
                    const std::size_t end =
                        std::min<std::size_t>(begin + static_cast<std::size_t>(std::max(request.limit, 1)), rows.size());
                    std::vector<c::OnlineTrackDto> slice(rows.begin() + static_cast<std::ptrdiff_t>(begin),
                                                         rows.begin() + static_cast<std::ptrdiff_t>(end));
                    if (begin == 0 && slice.empty())
                    {
                        jobs_.setPhase(token, c::JobPhase::Empty);
                        return {};
                    }
                    const bool hasMore = end < rows.size();
                    jobs_.appendRows(token, std::move(slice), hasMore);
                    return {};
                }

                auto onPage = [this, token, kind = request.kind, key, offset = request.offset,
                              limit = request.limit](core::codec::DecodeResult result) {
                    std::lock_guard<std::mutex> guard(pageMutex_);
                    pagePending_.push_back(PendingPage{token, kind, key, offset, limit, std::move(result)});
                };
                switch (request.kind)
                {
                case c::SourceKind::Album:
                    provider_.startAlbum(request.collectionId, onPage);
                    return {};
                case c::SourceKind::Artist:
                    provider_.startArtist(request.collectionId, onPage);
                    return {};
                case c::SourceKind::DailyRecommend:
                    provider_.startDailySongs(onPage);
                    return {};
                default:
                    provider_.startNewSongs(request.limit, onPage);
                    return {};
                }
            }
            default:
                jobs_.fail(token, notWired("这一路资源还没接：只做了搜索/歌单/榜单/专辑/歌手/新歌"));
                return {};
            }
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
            diag_.recordCacheHit();
            cachedTokens_.insert(token);
            Pending pending;
            pending.token = token;
            pending.fromCache = true;
            pending.keywords = request.keywords;
            pending.offset = request.offset;
            pending.rawForDecode = *cached;
            {
                std::lock_guard<std::mutex> guard(pendingMutex_);
                pending_.push_back(std::move(pending));
            }
            return {};
        }

        // 两道闸门挡在真正发请求之前。顺序有讲究：先退避再限流——退避是"连续失败
        // 之后别急着再来"，限流是"连打太快"；被退避挡住时不该再去消耗令牌。
        const std::int64_t now = nowMs();
        if (failures_.blocked(now))
        {
            const std::int64_t waitMs = failures_.nextRetryAtMs - now;
            c::OnlineError error{kernel::ErrorCode::Unavailable,
                                 "连续失败，约 " + std::to_string((waitMs + 999) / 1000) + " 秒后自动重试", true, 0};
            // 只进降级计数，不动连续失败计数：被闸门挡下不是又失败了一次。
            diag_.recordError(error, now, "搜索");
            jobs_.fail(token, error);
            return {};
        }
        if (!bucket_.take(now))
        {
            c::OnlineError error{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            diag_.recordError(error, now, "搜索");
            jobs_.fail(token, error);
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
        if (it->second.kind == c::SourceKind::Search)
        {
            submit(it->second, /*bypassCache=*/true);
        }
        else
        {
            // 非搜索类的重试就是把同一条命令再走一遍（中间状态若还在，会跳过一次网络）。
            startPage(it->second);
        }
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

    // ---------------------------------------------------------------- 查询

    kernel::Result<c::OnlineJobState> OnlineService::handle(const c::GetOnlineJob &query)
    {
        drain();
        drainAuth();
        auto state = jobs_.state(query.token);
        state.fromCache = cachedTokens_.count(query.token) > 0;
        return kernel::Result<c::OnlineJobState>::ok(state);
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
            noteFailure(item.result.error, "取地址");
            // URL 过期（20 分钟 TTL）在矩阵里单独一行：它是唯一"重解析一次就好"的失败。
            if (item.result.error.code == kernel::ErrorCode::NotFound)
            {
                diag_.recordUrlExpired();
            }
            c::OnlinePlaybackDto next;
            next.revision = playbackRevision_;
            next.error = withHint(item.result.error);
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
        noteSuccess();
        playback_ = std::move(next);
    }

    kernel::Result<c::SourceStatusDto> OnlineService::handle(const c::GetSourceStatus &)
    {
        // 这个查询是界面每一拍都会问的那一条（登录态），把它当成心跳：
        // 私人 FM 的批次不走 JobRegistry，只靠这条心跳才不会被饿死。
        drain();
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

    kernel::Result<std::vector<c::PlaylistDto>> OnlineService::handle(const c::GetMyPlaylists &)
    {
        // 与 GetOnlineJob 同一个道理：界面 50ms 轮询这个查询，顺手把回包搬进缓存。
        drainFm();
        drainFmUrls();
        drainMyPlaylists();
        return kernel::Result<std::vector<c::PlaylistDto>>::ok(myPlaylists_);
    }

    kernel::Result<void> OnlineService::handle(const c::LoadMyPlaylists &)
    {
        if (!status_.signedIn || userId_.empty())
        {
            // 游客：不发请求、不取令牌、不记错误。界面上"我的"页显示"登录后可见"。
            myPlaylists_.clear();
            myPlaylistsError_ = {};
            myPlaylistsLoaded_ = true;
            ++myPlaylistsRevision_;
            return {};
        }
        if (myPlaylistsLoading_)
        {
            return {};
        }

        const std::int64_t now = nowMs();
        if (failures_.blocked(now))
        {
            const std::int64_t waitMs = failures_.nextRetryAtMs - now;
            myPlaylistsError_ = c::OnlineError{kernel::ErrorCode::Unavailable,
                                               "连续失败，约 " + std::to_string((waitMs + 999) / 1000) +
                                                   " 秒后自动重试",
                                               true, 0};
            ++myPlaylistsRevision_;
            return {};
        }
        if (!bucket_.take(now))
        {
            myPlaylistsError_ = c::OnlineError{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            ++myPlaylistsRevision_;
            return {};
        }

        myPlaylistsLoading_ = true;
        provider_.startUserPlaylists(userId_, [this](core::codec::PlaylistsResult result) {
            const std::scoped_lock lock{pageMutex_};
            userPlaylistsPending_.push_back({std::move(result)});
        });
        return {};
    }

    kernel::Result<void> OnlineService::handle(const c::ToggleFavorite &command)
    {
        if (!status_.signedIn)
        {
            return kernel::Error{kernel::ErrorCode::Unauthenticated, "登录后才能收藏"};
        }
        if (command.songId.empty() || command.playlistId.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌曲或歌单"};
        }

        diag_.recordFavoriteWrite();
        // `csrf` 只作为表单字段进请求；它不缓存、不进日志、不进任何 DTO。
        const std::string csrf = csrfToken();
        provider_.startToggleFavorite(
            command.playlistId, command.songId, command.add, csrf, [this](core::codec::FavoriteResult result) {
                const std::scoped_lock lock{pageMutex_};
                favoritePending_.push_back({std::move(result), true});
            });
        return {};
    }

    std::string OnlineService::csrfToken() const
    {
        // cookie 形如 `MUSIC_U=…; __csrf=abc123; ...`，取出 `__csrf` 的值。
        const std::string key = "__csrf=";
        const std::size_t at = sessionCookie_.find(key);
        if (at == std::string::npos)
        {
            return {};
        }
        const std::size_t begin = at + key.size();
        const std::size_t end = sessionCookie_.find(';', begin);
        return sessionCookie_.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }


    void OnlineService::setFmListener(FmListener listener)
    {
        fmListener_ = std::move(listener);
    }

    kernel::Result<void> OnlineService::handle(const contracts::StartPersonalFm &command)
    {
        if (fmActive_)
        {
            // 已经在取：拖住第二个请求，别让"队列喊饿"变成一串并行请求。
            return {};
        }

        const std::int32_t count = std::clamp(command.count, 1, kFmMaxBatch);
        const std::int64_t now = nowMs();
        if (failures_.blocked(now) || !bucket_.take(now))
        {
            // FM 是背景服务：被闸门挡住不报错、不弹窗，只留一个计数，
            // 下一首播完还会再来要。
            diag_.recordFmFailure();
            return {};
        }

        fmActive_ = true;
        fmExpected_ = count;
        fmSettled_ = 0;
        fmSongs_.clear();
        for (std::int32_t i = 0; i < count; ++i)
        {
            provider_.startRadio([this](core::codec::DecodeResult result) {
                const std::scoped_lock lock{fmMutex_};
                radioPending_.push_back({std::move(result)});
            });
        }
        return {};
    }

    kernel::Result<void> OnlineService::handle(const contracts::DislikeFmSong &command)
    {
        if (command.songId.empty())
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌曲"};
        }
        fmDisliked_.push_back(command.songId);
        if (fmDisliked_.size() > kFmSeenLimit)
        {
            fmDisliked_.erase(fmDisliked_.begin());
        }
        return {};
    }

    void OnlineService::drainFm()
    {
        std::vector<PendingRadio> incoming;
        {
            const std::scoped_lock lock{fmMutex_};
            incoming.swap(radioPending_);
        }
        if (incoming.empty())
        {
            return;
        }

        for (PendingRadio &item : incoming)
        {
            if (!item.result.ok)
            {
                // 单独计数：FM 的失败不该把搜索那条退避闸门按下去。
                diag_.recordFmFailure();
            }
            else
            {
                for (contracts::OnlineTrackDto &dto : item.result.rows)
                {
                    if (dto.songId.empty())
                    {
                        continue;
                    }
                    const bool disliked =
                        std::find(fmDisliked_.begin(), fmDisliked_.end(), dto.songId) != fmDisliked_.end();
                    const bool seen = std::find(fmSeen_.begin(), fmSeen_.end(), dto.songId) != fmSeen_.end();
                    const bool pending = std::any_of(fmSongs_.begin(), fmSongs_.end(),
                                                     [&dto](const contracts::OnlineTrackDto &other) {
                                                         return other.songId == dto.songId;
                                                     });
                    if (disliked || seen || pending)
                    {
                        continue;
                    }
                    fmSongs_.push_back(std::move(dto));
                }
            }

            ++fmSettled_;
            if (fmSettled_ < fmExpected_)
            {
                continue;
            }

            // 一批齐了：解析直链。没解析出来的直接丢（FM 不弹"无版权"）。
            fmExpected_ = 0;
            fmSettled_ = 0;
            if (fmSongs_.empty())
            {
                fmActive_ = false;
                return;
            }

            std::vector<std::string> ids;
            ids.reserve(fmSongs_.size());
            for (const contracts::OnlineTrackDto &dto : fmSongs_)
            {
                ids.push_back(dto.songId);
            }
            provider_.startResolveUrls(ids, [this](core::codec::UrlResult result) {
                const std::scoped_lock lock{fmMutex_};
                fmUrlPending_.push_back({std::move(result)});
            });
            return;
        }
    }

    void OnlineService::drainFmUrls()
    {
        std::vector<PendingFmUrls> incoming;
        {
            const std::scoped_lock lock{fmMutex_};
            incoming.swap(fmUrlPending_);
        }
        if (incoming.empty())
        {
            return;
        }

        PendingFmUrls item = std::move(incoming.back());
        fmActive_ = false;
        if (!item.result.ok)
        {
            diag_.recordFmFailure();
            fmSongs_.clear();
            return;
        }

        std::vector<contracts::OnlineTrackDto> ready;
        ready.reserve(fmSongs_.size());
        for (contracts::OnlineTrackDto &dto : fmSongs_)
        {
            const auto found = std::find_if(item.result.entries.begin(), item.result.entries.end(),
                                            [&dto](const core::codec::UrlEntry &entry) {
                                                return entry.songId == dto.songId;
                                            });
            if (found == item.result.entries.end() || found->url.empty())
            {
                continue;
            }
            dto.playUrl = found->url;
            dto.playable = true;
            fmSeen_.push_back(dto.songId);
            if (fmSeen_.size() > kFmSeenLimit)
            {
                fmSeen_.erase(fmSeen_.begin());
            }
            ready.push_back(std::move(dto));
        }
        fmSongs_.clear();

        if (ready.empty() || !fmListener_)
        {
            return;
        }
        fmListener_(ready);
    }

    void OnlineService::drainMyPlaylists()
    {
        std::vector<PendingUserPlaylists> incoming;
        {
            const std::scoped_lock lock{pageMutex_};
            incoming.swap(userPlaylistsPending_);
        }
        for (auto &item : incoming)
        {
            myPlaylistsLoading_ = false;
            if (item.result.ok)
            {
                myPlaylists_ = std::move(item.result.items);
                myPlaylistsError_ = {};
                myPlaylistsLoaded_ = true;
                noteSuccess();
            }
            else
            {
                noteFailure(item.result.error, "我的歌单");
                myPlaylistsError_ = withHint(item.result.error);
            }
            ++myPlaylistsRevision_;
        }
    }

    kernel::Result<contracts::OnlineLyricDto> OnlineService::handle(
        const contracts::FetchOnlineLyrics &query)
    {
        // 先排空：调用方是 1 秒一次的定时重试，回包可能刚好在这一拍之前到。
        drainLyrics();

        const std::string &songId = query.songId;
        if (songId.empty())
        {
            return kernel::Result<contracts::OnlineLyricDto>::ok(contracts::OnlineLyricDto{});
        }

        if (const auto it = lyrics_.find(songId); it != lyrics_.end())
        {
            return kernel::Result<contracts::OnlineLyricDto>::ok(it->second.dto);
        }

        // 退避闸门挡住时**不报错**，就当"还没到"：歌词是背景服务，
        // 把它变成一条红色错误只会打扰正在听歌的人。
        if (failures_.blocked(nowMs()))
        {
            return kernel::Result<contracts::OnlineLyricDto>::ok(contracts::OnlineLyricDto{});
        }

        // 内存上限：超了就把最早那首丢掉（它不是 LRU，但歌词的重取成本极低）。
        if (lyrics_.size() >= kLyricCacheSize && !lyricOrder_.empty())
        {
            lyrics_.erase(lyricOrder_.front());
            lyricOrder_.erase(lyricOrder_.begin());
        }

        LyricEntry entry;
        entry.loading = true;
        lyrics_.emplace(songId, entry);
        lyricOrder_.push_back(songId);

        provider_.startLyric(songId, [this, songId](core::codec::LyricResult result) {
            // worker 线程：只入队（与其它回包同一个约定）。
            const std::scoped_lock lock{lyricMutex_};
            lyricPending_.push_back(PendingLyric{songId, std::move(result)});
        });
        return kernel::Result<contracts::OnlineLyricDto>::ok(contracts::OnlineLyricDto{});
    }

    void OnlineService::drainLyrics()
    {
        std::vector<PendingLyric> incoming;
        {
            const std::scoped_lock lock{lyricMutex_};
            incoming.swap(lyricPending_);
        }
        for (auto &item : incoming)
        {
            const auto it = lyrics_.find(item.songId);
            if (it == lyrics_.end())
            {
                // 已经被内存上限淘汰掉了：结果没处放，也不算失败。
                continue;
            }
            contracts::OnlineLyricDto dto;
            dto.ready = true;
            if (item.result.ok)
            {
                dto.lrc = std::move(item.result.lrc);
                dto.translated = std::move(item.result.translated);
                dto.instrumental = item.result.instrumental;
                noteSuccess();
            }
            else
            {
                // 歌词取不到不影响听歌，所以只留痕、不进全局 lastError。
                noteFailure(item.result.error, "歌词");
                dto.error = withHint(item.result.error);
                dto.instrumental = true;
            }
            it->second.dto = std::move(dto);
            it->second.loading = false;
        }
    }

    void OnlineService::drainFavorite()
    {
        std::vector<PendingFavorite> incoming;
        {
            const std::scoped_lock lock{pageMutex_};
            incoming.swap(favoritePending_);
        }
        for (auto &item : incoming)
        {
            if (item.result.ok)
            {
                noteSuccess();
                continue;
            }
            // 写操作的失败**单独计数**，并且只留最后一条错误给界面回滚用。
            diag_.recordFavoriteFailure();
            status_.lastError = withHint(item.result.error);
        }
    }

    kernel::Result<void> OnlineService::handle(const c::LoadCollections &command)
    {
        const auto kind = command.kind;
        if (kind != c::SourceKind::Playlist && kind != c::SourceKind::Chart)
        {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "这一类没有卡片列表"};
        }

        // 卡片列表没有 token：失败信息直接落在页对象上，界面看到的是"重试"而不是"空"。
        auto &page = collections_[static_cast<int>(kind)];
        const std::int64_t now = nowMs();
        if (failures_.blocked(now))
        {
            const std::int64_t waitMs = failures_.nextRetryAtMs - now;
            c::OnlineError error{kernel::ErrorCode::Unavailable,
                                 "连续失败，约 " + std::to_string((waitMs + 999) / 1000) + " 秒后自动重试", true, 0};
            diag_.recordError(error, now, whereOf(kind));
            page.loaded = false;
            page.items.clear();
            page.error = error;
            ++page.revision;
            return {};
        }
        if (!bucket_.take(now))
        {
            c::OnlineError error{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            diag_.recordError(error, now, whereOf(kind));
            page.loaded = false;
            page.items.clear();
            page.error = error;
            ++page.revision;
            return {};
        }

        const auto limit = std::clamp<std::int32_t>(command.limit, 1, core::provider::kMaxCollectionLimit);
        auto onList = [this, kind](core::codec::CollectionListResult result) {
            std::lock_guard<std::mutex> guard(pageMutex_);
            collectionPending_.push_back(PendingCollection{kind, std::move(result)});
        };
        if (kind == c::SourceKind::Chart)
        {
            provider_.startToplist(std::move(onList));
        }
        else
        {
            provider_.startPersonalizedPlaylists(limit, std::move(onList));
        }
        return {};
    }

    kernel::Result<c::CollectionPageDto> OnlineService::handle(const c::FetchCollections &query)
    {
        // 卡片墙的轮询就是它自己的驱动源：网络回包先落在 collectionPending_，
        // 没有搜索任务时没人替我们调 drain()，所以这里顺手排干。
        drainPages();

        const auto it = collections_.find(static_cast<int>(query.kind));
        if (it == collections_.end())
        {
            return kernel::Result<c::CollectionPageDto>::ok(c::CollectionPageDto{});
        }
        return kernel::Result<c::CollectionPageDto>::ok(it->second);
    }

    kernel::Result<c::CollectionDetailDto> OnlineService::handle(const c::FetchCollectionDetail &query) const
    {
        const auto it = details_.find(pageKey(query.kind, query.collectionId));
        if (it == details_.end())
        {
            // 还没取到头部（或取失败）：交一个 ok=false 的空对象，界面据此显示占位标题。
            c::CollectionDetailDto empty;
            empty.id = query.collectionId;
            empty.kind = query.kind;
            return kernel::Result<c::CollectionDetailDto>::ok(std::move(empty));
        }
        return kernel::Result<c::CollectionDetailDto>::ok(it->second);
    }

    kernel::Result<c::OnlineDiagnosticsDto> OnlineService::handle(const c::GetOnlineDiagnostics &query)
    {
        const std::int64_t now = nowMs();
        c::OnlineDiagnosticsDto out;
        out.provider = query.provider.empty() ? std::string{kProvider} : query.provider;
        out.degradations = diag_.counters();
        out.consecutiveFailures = failures_.consecutive;
        out.nextRetryAtUnix = failures_.nextRetryAtMs > 0 ? (failures_.nextRetryAtMs + 999) / 1000 : 0;
        out.tokensLeft = bucket_.tokens(now);
        out.tokenBurst = 6.0;

        const auto stats = store_.stats();
        out.cache.searchRows = stats.searchRows;
        out.cache.searchBytes = stats.searchBytes;
        out.cache.songRows = stats.songRows;
        out.cache.detailRows = stats.detailRows;
        out.cache.capRows = static_cast<std::int32_t>(SqliteOnlineStore::kMaxSearchRows);
        out.cache.capBytes = SqliteOnlineStore::kMaxSearchBytes;

        out.favoriteWrites = diag_.favoriteWrites();
        out.favoriteFailures = diag_.favoriteFailures();
        out.recentErrors = diag_.recent();
        return kernel::Result<c::OnlineDiagnosticsDto>::ok(std::move(out));
    }

    // ---------------------------------------------------------------- 内部

    std::int64_t OnlineService::nowMs() const noexcept
    {
        return nowMillis(clock_);
    }

    void OnlineService::noteFailure(const c::OnlineError &error, std::string where)
    {
        const std::int64_t now = nowMs();
        diag_.recordError(error, now, std::move(where));

        // 取消不算失败：记录里已经跳过，这里也不能让退避闸门关起来——
        // 否则用户连打三个字又删掉，就会触发一次"连续失败 3 次"。
        if (error.code == kernel::ErrorCode::Internal && error.message == "已取消")
        {
            return;
        }
        failures_.recordFailure(now, backoff_);
    }

    c::OnlineError OnlineService::withHint(c::OnlineError error) const
    {
        return core::app::withInterfaceHint(std::move(error), failures_.consecutive);
    }

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
            noteFailure(result.error, "搜索");
            jobs_.fail(token, withHint(result.error));
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
        // 能解码出结果就算业务成功：空结果也是上游在正常说话，不该继续退避。
        noteSuccess();
    }

    std::string OnlineService::pageKey(c::SourceKind kind, const std::string &collectionId)
    {
        return std::string{c::toName(kind)} + ":" + collectionId;
    }

    std::string OnlineService::whereOf(c::SourceKind kind)
    {
        switch (kind)
        {
        case c::SourceKind::Search:
            return "搜索";
        case c::SourceKind::Playlist:
            return "歌单";
        case c::SourceKind::Chart:
            return "榜单";
        case c::SourceKind::Album:
            return "专辑";
        case c::SourceKind::Artist:
            return "歌手";
        case c::SourceKind::NewSongs:
            return "新歌";
        case c::SourceKind::DailyRecommend:
            return "每日推荐";
        default:
            return "在线";
        }
    }

    bool OnlineService::gate(std::uint64_t token, c::SourceKind kind, std::int64_t now)
    {
        const std::string where = whereOf(kind);
        if (failures_.blocked(now))
        {
            const std::int64_t waitMs = failures_.nextRetryAtMs - now;
            c::OnlineError error{kernel::ErrorCode::Unavailable,
                                 "连续失败，约 " + std::to_string((waitMs + 999) / 1000) + " 秒后自动重试", true, 0};
            // 只进降级计数，不动连续失败计数：被闸门挡下不是又失败了一次。
            diag_.recordError(error, now, where);
            jobs_.fail(token, error);
            return false;
        }
        if (!bucket_.take(now))
        {
            c::OnlineError error{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            diag_.recordError(error, now, where);
            jobs_.fail(token, error);
            return false;
        }
        return true;
    }

    void OnlineService::startPlaylistBatch(std::uint64_t token, c::SourceKind kind, const std::string &collectionId,
                                           const std::vector<std::string> &trackIds, std::int32_t offset,
                                           std::int32_t limit)
    {
        if (trackIds.empty())
        {
            jobs_.setPhase(token, c::JobPhase::Empty);
            return;
        }
        // 第二批起也要过一次令牌桶：一次翻页最多 500 首，不该绕过限流。
        const std::int64_t now = nowMs();
        if (!bucket_.take(now))
        {
            c::OnlineError error{kernel::ErrorCode::RateLimited, "请求太频繁，稍后再试", true, 300};
            diag_.recordError(error, now, whereOf(kind));
            jobs_.fail(token, error);
            return;
        }

        const std::size_t total = trackIds.size();
        const std::size_t begin = std::min<std::size_t>(static_cast<std::size_t>(std::max(offset, 0)), total);
        const std::size_t end = std::min<std::size_t>(begin + static_cast<std::size_t>(std::max(limit, 1)), total);
        std::vector<std::string> ids(trackIds.begin() + static_cast<std::ptrdiff_t>(begin),
                                     trackIds.begin() + static_cast<std::ptrdiff_t>(end));
        if (ids.empty())
        {
            jobs_.setPhase(token, c::JobPhase::Empty);
            return;
        }

        const std::string key = pageKey(kind, collectionId);
        provider_.startSongDetails(ids, [this, token, kind, key, offset, limit](core::codec::DecodeResult result) {
            std::lock_guard<std::mutex> guard(pageMutex_);
            pagePending_.push_back(PendingPage{token, kind, key, offset, limit, std::move(result)});
        });
    }

    void OnlineService::applyPlaylistDetail(const PendingPlaylistDetail &item)
    {
        if (!item.result.ok)
        {
            noteFailure(item.result.error, whereOf(item.kind));
            jobs_.fail(item.token, withHint(item.result.error));
            return;
        }

        const std::string key = pageKey(item.kind, item.collectionId);
        PlaylistPlan plan;
        plan.detail = item.result.detail;
        plan.trackIds = item.result.trackIds;
        details_[key] = plan.detail;

        const auto command = requests_.find(item.token);
        const std::int32_t offset = command != requests_.end() ? command->second.offset : 0;
        const std::int32_t limit = command != requests_.end() ? command->second.limit : 100;
        playlists_[key] = std::move(plan);
        startPlaylistBatch(item.token, item.kind, item.collectionId, playlists_[key].trackIds, offset, limit);
    }

    void OnlineService::applyPage(PendingPage &item)
    {
        if (!item.result.ok)
        {
            noteFailure(item.result.error, whereOf(item.kind));
            jobs_.fail(item.token, withHint(item.result.error));
            return;
        }

        if (item.kind == c::SourceKind::Playlist || item.kind == c::SourceKind::Chart)
        {
            const auto plan = playlists_.find(item.key);
            const std::size_t total = plan != playlists_.end() ? plan->second.trackIds.size() : item.result.rows.size();
            if (item.offset == 0 && item.result.rows.empty())
            {
                jobs_.setPhase(item.token, c::JobPhase::Empty);
                return;
            }
            const std::size_t count = item.result.rows.size();
            const bool hasMore = static_cast<std::size_t>(item.offset) + count < total;
            jobs_.appendRows(item.token, std::move(item.result.rows), hasMore);
            noteSuccess();
            return;
        }

        // 专辑/歌手/新歌：第一页的回包就是全量，后面几页都在内存里切。
        auto &full = fullPages_[item.key];
        if (item.result.detail.ok)
        {
            details_[item.key] = item.result.detail;
        }
        if (item.offset == 0 || full.empty())
        {
            full = item.result.rows;
        }
        const std::size_t begin =
            std::min<std::size_t>(static_cast<std::size_t>(std::max(item.offset, 0)), full.size());
        const std::size_t end =
            std::min<std::size_t>(begin + static_cast<std::size_t>(std::max(item.limit, 1)), full.size());
        std::vector<c::OnlineTrackDto> slice(full.begin() + static_cast<std::ptrdiff_t>(begin),
                                             full.begin() + static_cast<std::ptrdiff_t>(end));
        if (begin == 0 && slice.empty())
        {
            jobs_.setPhase(item.token, c::JobPhase::Empty);
            return;
        }
        const bool hasMore = end < full.size();
        jobs_.appendRows(item.token, std::move(slice), hasMore);
        noteSuccess();
    }

    void OnlineService::applyCollectionList(const PendingCollection &item)
    {
        auto &page = collections_[static_cast<int>(item.kind)];
        if (!item.result.ok)
        {
            noteFailure(item.result.error, whereOf(item.kind));
            page.loaded = false;
            page.items.clear();
            page.error = withHint(item.result.error);
            ++page.revision;
            return;
        }
        page.items = item.result.items;
        page.hasMore = item.result.hasMore;
        page.loaded = true;
        page.error = {};
        ++page.revision;
        noteSuccess();
    }

    void OnlineService::drainPages()
    {
        std::vector<PendingCollection> collections;
        std::vector<PendingPlaylistDetail> detailPages;
        std::vector<PendingPage> pages;
        {
            std::lock_guard<std::mutex> guard(pageMutex_);
            collections.swap(collectionPending_);
            detailPages.swap(playlistDetailPending_);
            pages.swap(pagePending_);
        }

        for (const auto &item : collections)
        {
            applyCollectionList(item);
        }
        for (const auto &item : detailPages)
        {
            applyPlaylistDetail(item);
        }
        for (auto &item : pages)
        {
            applyPage(item);
        }
        // 登录侧的两条回包通道：与页面结果同一个排水口。
        drainFm();
        drainFmUrls();
        drainMyPlaylists();
        drainFavorite();
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
            if (!item.fromCache)
            {
                // 真正的网络回包：这一页不再是"上次结果"。
                cachedTokens_.erase(item.token);
            }
            if (!item.rawForDecode.empty())
            {
                item.result = core::codec::decodeSearch(item.rawForDecode);
                item.result.raw = std::move(item.rawForDecode);
            }
            apply(item.token, std::move(item.result), item.keywords, item.offset);
        }
        // 非搜索类的回包走各自的队列；同一个 50ms 轮询里一起排空。
        drainPages();
        drainLyrics();
        drainFm();
        drainFmUrls();
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
