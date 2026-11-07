#include "sonora/presentation/OnlineViewModel.h"


#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "sonora/kernel/Error.h"
#include "sonora/online/contracts/Commands.h"
#include "sonora/online/contracts/Queries.h"

namespace sonora::presentation
{

    namespace
    {
        using online::contracts::JobPhase;

        // 毫秒 → "3:45"。界面不自己算：列表里两千行各自算一遍没有意义，
        // 而且一旦两处算法不同，就会看到"表头和行对不上"。
        QString formatDurationMs(std::int64_t ms)
        {
            if (ms <= 0)
            {
                return QStringLiteral("--:--");
            }
            const auto total = ms / 1000;
            const auto seconds = total % 60;
            const auto minutes = total / 60;
            return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
        }

        QString fromUtf8(const std::string &text) { return QString::fromStdString(text); }

        // 与服务端的 "loaded / total" 对齐：total 为 0 时说明接口没给总数，
        // 那就只报"已加载 N 条"，不要编一个 0/0。
        QString countText(const online::contracts::OnlineJobState &state)
        {
            if (state.total > 0)
            {
                return QStringLiteral("%1 / %2 首").arg(state.loaded).arg(state.total);
            }
            return QStringLiteral("%1 首").arg(state.loaded);
        }

        bool isBusy(JobPhase phase)
        {
            return phase == JobPhase::Loading || phase == JobPhase::LoadingMore;
        }
    } // namespace

    // ---- OnlineListModel ----

    OnlineListModel::OnlineListModel(QObject *parent) : RowsModel{parent} {}

    QHash<int, QByteArray> OnlineListModel::roleNames() const
    {
        return {
            {SongIdRole, "songId"},  {TitleRole, "title"},     {ArtistRole, "artist"},
            {AlbumRole, "album"},    {DurationRole, "duration"}, {CoverRole, "cover"},
            {QualityRole, "quality"}, {PlayableRole, "playable"},
        };
    }

    QVariant OnlineListModel::data(const QModelIndex &index, int role) const
    {
        if (!index.isValid())
        {
            return {};
        }
        const auto row = static_cast<std::size_t>(index.row());
        if (row >= rows_.size())
        {
            return {};
        }
        const auto &track = rows_[row];

        switch (role)
        {
        case SongIdRole:
            return fromUtf8(track.songId);
        case TitleRole:
            return fromUtf8(track.title);
        case ArtistRole:
            return fromUtf8(track.artists);
        case AlbumRole:
            return fromUtf8(track.album);
        case DurationRole:
            return formatDurationMs(track.durationMs);
        case CoverRole:
            return fromUtf8(track.coverUrl);
        case QualityRole:
            return fromUtf8(track.qualityLabel);
        case PlayableRole:
            return track.playable;
        default:
            return {};
        }
    }

    void OnlineListModel::setRows(const std::vector<online::contracts::OnlineTrackDto> &rows)
    {
        beginResetModel();
        rows_ = rows;
        // 行数必须在 endResetModel() 之前落到 count_：视图在 reset 结束时会向
        // rowCount() 要行数，而 RowsModel::rowCount() 读的就是 count_。先 end 后
        // assignCount 会让视图拿到旧行数（0），列表就永远是空的——这个 bug 的表现
        // 是"侧栏计数 30、列表一片空白"，很难从代码上直接看出来。
        assignCount(static_cast<int>(rows_.size()));
        endResetModel();
    }

    void OnlineListModel::clearRows()
    {
        if (rows_.empty())
        {
            return;
        }
        beginResetModel();
        rows_.clear();
        assignCount(0);
        endResetModel();
    }

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
            statusText_ = QStringLiteral("先输入关键词");
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
        model_.clearRows();
        busy_ = true;
        hasMore_ = false;
        retryable_ = false;
        errorText_.clear();
        statusText_ = QStringLiteral("正在搜索：%1").arg(trimmed);
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
        statusText_ = QStringLiteral("正在加载第 %1 页…").arg(offset / 30 + 2);
        emit statusChanged();

        const auto token = token_;
        const auto text = keywords_.toStdString();
        dispatchQuietly(
            [this, token, text, offset]
            {
                online::contracts::RequestOnlinePage command;
                command.token = token;
                command.kind = online::contracts::SourceKind::Search;
                command.keywords = text;
                command.offset = offset;
                command.limit = 30;
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
        statusText_ = QStringLiteral("正在重试…");
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

    void OnlineViewModel::play(int index)
    {
        const auto &rows = model_.rows();
        if (index < 0 || static_cast<std::size_t>(index) >= rows.size())
        {
            return;
        }

        auto songIds = std::vector<std::string>{};
        songIds.reserve(rows.size());
        for (const auto &row : rows)
        {
            songIds.push_back(row.songId);
        }

        const auto token = token_;
        dispatchQuietly(
            [this, token, songIds = std::move(songIds), index]
            {
                online::contracts::ResolveAndPlayOnline command;
                command.token = token;
                command.songIds = songIds;
                command.startIndex = index;
                command.origin = online::contracts::SourceKind::Search;
                return commands_.dispatch(command);
            });
    }

    void OnlineViewModel::signOut()
    {
        dispatchQuietly(
            [this]
            {
                online::contracts::SignOut command;
                return commands_.dispatch(command);
            });
        refreshStatus();
    }

    void OnlineViewModel::signInHint()
    {
        errorText_ = QStringLiteral("登录还没接通（下一步 R4）");
        retryable_ = false;
        emit statusChanged();
    }

    void OnlineViewModel::onTick()
    {
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
        auto nextStatus = statusText_;
        auto nextError = errorText_;
        auto nextRetryable = retryable_;

        switch (state.phase)
        {
        case JobPhase::Idle:
            nextStatus = QStringLiteral("输入关键词，回车搜索");
            break;
        case JobPhase::Loading:
            nextStatus = QStringLiteral("正在搜索…");
            break;
        case JobPhase::LoadingMore:
            nextStatus = QStringLiteral("正在加载下一页…");
            break;
        case JobPhase::Ready:
            nextStatus = countText(state);
            nextError.clear();
            break;
        case JobPhase::Empty:
            nextStatus = QStringLiteral("没有找到结果");
            nextError.clear();
            break;
        case JobPhase::Error:
            nextStatus = QStringLiteral("出错了");
            nextError = fromUtf8(state.error.message);
            nextRetryable = state.error.retryable;
            break;
        case JobPhase::Offline:
            nextStatus = QStringLiteral("当前网络不可用");
            nextError = fromUtf8(state.error.message);
            nextRetryable = true;
            break;
        }

        const auto nextBusy = isBusy(state.phase);
        const bool changed = nextStatus != statusText_ || nextError != errorText_ ||
                             nextBusy != busy_ || nextRetryable != retryable_;
        statusText_ = nextStatus;
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
                               : QStringLiteral("未登录");
        if (nextSignedIn == signedIn_ && nextAccount == accountText_)
        {
            return;
        }
        signedIn_ = nextSignedIn;
        accountText_ = nextAccount;
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

} // namespace sonora::presentation
