#include "sonora/presentation/OnlineViewModel.h"
#include "OnlineViewModelShared.h"

#include <QCoreApplication>
#include <QVariantList>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sonora/kernel/Error.h"
#include "sonora/online/contracts/Commands.h"
#include "sonora/online/contracts/Queries.h"
#include "sonora/playback/contracts/Commands.h"
#include "sonora/playback/contracts/Queries.h"
#include "sonora/presentation/Format.h"

namespace sonora::presentation
{
    using namespace detail;

    // —— 构造、搜索与分页、每拍轮询、状态刷新 ——

    // ---- OnlineViewModel ----

    OnlineViewModel::OnlineViewModel(kernel::ICommandBus &commands, kernel::IQueryBus &queries,
                                     QObject *parent)
        : QObject{parent}, commands_{commands}, queries_{queries}
    {
        // 50ms：比一帧还慢，但足够让"刚点下去"到"状态变了"看起来是瞬间的。
        // 20Hz 也只读一张内存表，没有请求。
        timer_.setInterval(50);
        timer_.setTimerType(Qt::CoarseTimer);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { onTick(); });
        timer_.start();

        // 防抖：280ms。这个数字是手感与请求量的折中——低于 150ms 等于没有防抖
        // （中文输入法连续上屏的间隔就这么短），高于 500ms 又会觉得"卡住了"。
        debounce_.setSingleShot(true);
        debounce_.setInterval(280);
        debounce_.setTimerType(Qt::CoarseTimer);
        QObject::connect(&debounce_, &QTimer::timeout, this, [this] { commitSearch(pendingKeywords_); });

        refreshStatus();

    }

    void OnlineViewModel::queueSearch(const QString &keywords)
    {
        pendingKeywords_ = keywords;
        // 重启而不是追加：连续输入只会剩下最后一次输入之后的那个计时器。
        debounce_.start();
    }

    void OnlineViewModel::search(const QString &keywords)
    {
        // 回车是"我已经决定了"：不等防抖，直接发。
        pendingKeywords_ = keywords;
        debounce_.stop();
        commitSearch(keywords);
    }

    void OnlineViewModel::commitSearch(const QString &keywords)
    {
        const auto trimmed = keywords.trimmed();
        if (trimmed.isEmpty())
        {
            statusText_ = tr("先输入关键词");
            errorText_.clear();
            emit statusChanged();
            return;
        }

        // 同一个词再来一次（回车重复按、或防抖与回车撞在一起）：不重发。
        if (trimmed == keywords_ && token_ != 0)
        {
            return;
        }

        // 换词时把还在飞的那次请求掐掉：留着只是让它白跑一趟，回来也会被 token 丢弃。
        if (busy_ && token_ != 0)
        {
            const auto stale = token_;
            dispatchQuietly(
                [this, stale]
                {
                    online::contracts::CancelOnlineJob command;
                    command.token = stale;
                    return commands_.dispatch(command);
                });
        }

        // token 由界面递增：旧 token 的迟到结果会被服务端丢弃（JobRegistry 里唯一裁决）。
        ++token_;
        seenRevision_ = 0;
        keywords_ = trimmed;
        pageKind_ = online::contracts::SourceKind::Search;
        pageCollectionId_.clear();
        collectionTitle_.clear();
        collectionSubtitle_.clear();
        emit collectionChanged();
        model_.clearRows();
        busy_ = true;
        hasMore_ = false;
        retryable_ = false;
        errorText_.clear();
        statusText_ = tr("正在搜索：%1").arg(trimmed);
        emit statusChanged();

        const auto token = token_;
        const auto text = trimmed.toStdString();
        dispatchQuietly(
            [this, token, text]
            {
                online::contracts::RequestOnlinePage command;
                command.token = token;
                command.kind = online::contracts::SourceKind::Search;
                command.keywords = text;
                command.offset = 0;
                command.limit = 30;
                return commands_.dispatch(command);
            });
    }

    void OnlineViewModel::loadMore()
    {
        if (token_ == 0 || busy_)
        {
            return;
        }

        const auto offset = model_.count();
        busy_ = true;
        statusText_ = tr("正在加载第 %1 页…").arg(offset / 30 + 2);
        emit statusChanged();

        const auto token = token_;
        const auto text = keywords_.toStdString();
        const auto kind = pageKind_;
        const auto collectionId = pageCollectionId_;
        dispatchQuietly(
            [this, token, text, kind, collectionId, offset]
            {
                online::contracts::RequestOnlinePage command;
                command.token = token;
                command.kind = kind;
                command.keywords = text;
                command.collectionId = collectionId;
                command.offset = offset;
                command.limit = kind == online::contracts::SourceKind::Search ? 30 : 100;
                return commands_.dispatch(command);
            });
    }

    void OnlineViewModel::retry()
    {
        if (token_ == 0)
        {
            return;
        }
        busy_ = true;
        errorText_.clear();
        statusText_ = tr("正在重试…");
        emit statusChanged();

        const auto token = token_;
        dispatchQuietly(
            [this, token]
            {
                online::contracts::RetryOnlineJob command;
                command.token = token;
                return commands_.dispatch(command);
            });
    }

    void OnlineViewModel::onTick()
    {
        // 登录态与播放回包与"有没有搜索过"无关：没搜过也要能登录、也要能
        // 知道直链过期了。所以先跑这两块，token_ 只挡搜索那条路。
        refreshStatus();

        const auto playbackNow = queries_.ask<online::contracts::OnlinePlaybackDto>(
            online::contracts::GetOnlinePlayback{});
        if (playbackNow && playbackNow.value().revision != seenPlaybackRevision_)
        {
            seenPlaybackRevision_ = playbackNow.value().revision;
            applyPlayback(playbackNow.value());
        }

        // 扫码登录：面板开着时才问（问一次就会按 1.5s 节奏推一次网络轮询）。
        if (qrActive_)
        {
            const auto qr = queries_.ask<online::contracts::QrLoginDto>(online::contracts::GetQrLoginState{});
            if (qr)
            {
                const auto &state = qr.value();
                if (state.session != qrSession_)
                {
                    qrSession_ = state.session;
                    qrRows_ = state.rows;
                    qrSize_ = static_cast<int>(qrRows_.size());
                    emit qrChanged();
                }
                const QString message = QString::fromStdString(state.message);
                if (qrMessage_ != message)
                {
                    qrMessage_ = message;
                    emit qrChanged();
                }
                if (state.phase == online::contracts::AccountPhase::LoggedIn)
                {
                    qrActive_ = false;
                    emit qrChanged();
                }
            }
        }

        // 卡片列表（推荐歌单 / 榜单）：界面切到哪个标签页就只轮询哪一类。
        if (myPlaylistsWanted_ && signedIn())
        {
            // 每 4 帧问一次服务层（约 200ms）：服务层在排水，这里只读缓存。
            if (++myPlaylistsTick_ % 4 == 0)
            {
                const auto playlists = queries_.ask<std::vector<online::contracts::PlaylistDto>>(
                    online::contracts::GetMyPlaylists{});
                if (playlists)
                {
                    publishMyPlaylists(playlists.value());
                }
            }
        }

        if (collectionsWanted_ >= 0)
        {
            const auto wanted = static_cast<online::contracts::SourceKind>(collectionsWanted_);
            const auto page =
                queries_.ask<online::contracts::CollectionPageDto>(online::contracts::FetchCollections{wanted});
            if (page)
            {
                const auto revision = page.value().revision;
                if (revision != seenCollectionRevisions_.value(collectionsWanted_, 0))
                {
                    seenCollectionRevisions_[collectionsWanted_] = revision;
                    if (page.value().loaded)
                    {
                        collections_.setItems(page.value().items);
                        collections_.setStatus(QString{});
                    }
                    else if (!page.value().error.message.empty())
                    {
                        collections_.setStatus(fromUtf8(page.value().error.message));
                    }
                }
            }
        }

        // 详情页的副标题（"播放量 · 创建者"）只用一条查询补，页面自己不去猜。
        if (pageKind_ != online::contracts::SourceKind::Search && collectionSubtitle_.isEmpty() && token_ != 0)
        {
            const auto detail = queries_.ask<online::contracts::CollectionDetailDto>(
                online::contracts::FetchCollectionDetail{pageKind_, pageCollectionId_});
            if (detail && detail.value().ok)
            {
                collectionSubtitle_ = detail.value().trackCount > 0
                                          ? tr("%n 首", "", detail.value().trackCount)
                                          : QString{};
                if (!detail.value().creator.empty())
                {
                    collectionSubtitle_ += QStringLiteral(" · ") + fromUtf8(detail.value().creator);
                }
                collectionCover_ = fromUtf8(detail.value().coverUrl);
                emit collectionChanged();
            }
        }

        if (token_ == 0)
        {
            return;
        }

        const auto job = queries_.ask<online::contracts::OnlineJobState>(
            online::contracts::GetOnlineJob{token_});
        if (!job)
        {
            return;
        }

        const auto &state = job.value();
        if (cachedResult_ != state.fromCache)
        {
            cachedResult_ = state.fromCache;
            emit statusChanged();
        }
        auto nextStatus = statusText_;
        auto nextError = errorText_;
        auto nextRetryable = retryable_;

        switch (state.phase)
        {
        case JobPhase::Idle:
            nextStatus = tr("输入关键词，回车搜索");
            break;
        case JobPhase::Loading:
            nextStatus = tr("正在搜索…");
            break;
        case JobPhase::LoadingMore:
            nextStatus = tr("正在加载下一页…");
            break;
        case JobPhase::Ready:
            nextStatus = countText(state);
            nextError.clear();
            break;
        case JobPhase::Empty:
            nextStatus = tr("没有找到结果");
            nextError.clear();
            break;
        case JobPhase::Error:
            nextStatus = tr("出错了");
            nextError = fromUtf8(state.error.message);
            nextRetryable = state.error.retryable;
            break;
        case JobPhase::Offline:
            nextStatus = tr("当前网络不可用");
            nextError = fromUtf8(state.error.message);
            nextRetryable = true;
            break;
        }

        const auto nextBusy = isBusy(state.phase);
        // 排队 / 收藏成功这类一次性提示不该被每拍的"100 首"立刻盖掉。
        const bool holdStatus = statusHoldTicks_ > 0;
        if (holdStatus)
        {
            --statusHoldTicks_;
        }
        const bool changed = (!holdStatus && nextStatus != statusText_) ||
                             nextError != errorText_ || nextBusy != busy_ ||
                             nextRetryable != retryable_;
        if (!holdStatus)
        {
            statusText_ = nextStatus;
        }
        errorText_ = nextError;
        busy_ = nextBusy;
        retryable_ = nextRetryable;

        if (state.revision != seenRevision_)
        {
            seenRevision_ = state.revision;
            refreshRows();
        }

        if (changed)
        {
            emit statusChanged();
        }

        rescanFailedPlayback();
    }

    void OnlineViewModel::refreshStatus()
    {
        const auto status = queries_.ask<online::contracts::SourceStatusDto>(
            online::contracts::GetSourceStatus{});
        if (!status)
        {
            return;
        }

        const auto &value = status.value();
        const auto nextSignedIn = value.signedIn;
        auto nextAccount = value.signedIn && !value.nickname.empty()
                               ? fromUtf8(value.nickname)
                               : tr("未登录");
        const auto nextLastError = fromUtf8(value.lastError.message);

        // 校验中的状态只由这里收敛：成功 → 清错误；给了 lastError → 显示它。
        // 这样"点登录 → 输错 cookie → 看到原因"不需要额外的信号。
        auto nextSigning = signingIn_;
        auto nextLoginError = loginError_;
        if (signingIn_ && (nextSignedIn || !nextLastError.isEmpty()))
        {
            nextSigning = false;
            nextLoginError = nextSignedIn ? QString() : nextLastError;
        }

        if (nextSignedIn == signedIn_ && nextAccount == accountText_ && nextSigning == signingIn_ &&
            nextLoginError == loginError_)
        {
            return;
        }
        signedIn_ = nextSignedIn;
        accountText_ = nextAccount;
        signingIn_ = nextSigning;
        loginError_ = nextLoginError;
        emit statusChanged();
    }

    void OnlineViewModel::refreshRows()
    {
        const auto page = queries_.ask<online::contracts::OnlinePageDto>(
            online::contracts::FetchOnlineRows{token_, 0, 200});
        if (!page)
        {
            return;
        }
        model_.setRows(page.value().rows);
        hasMore_ = page.value().hasMore;
    }

    void OnlineViewModel::dispatchQuietly(
        const std::function<kernel::Result<void>()> &action)
    {
        const auto result = action();
        if (result)
        {
            return;
        }
        // 命令失败是"这一下没做成"，不是"整页坏了"：只换错误行，不清列表。
        errorText_ = fromUtf8(result.error().message());
        retryable_ = false;
        emit statusChanged();
    }


    void OnlineViewModel::retranslate()
    {
        // 账号区（未登录 / 昵称）与空闲态状态行是缓存的文案；其它状态会在下一次
        // 轮询里用新语言重算，所以这里只补这两处，再让 QML 重读一遍。
        refreshStatus();
        if (token_ == 0)
        {
            if (statusText_ != tr("输入关键词，回车搜索"))
            {
                statusText_ = tr("输入关键词，回车搜索");
                emit statusChanged();
            }
        }
        emit collectionChanged();
    }

} // namespace sonora::presentation
