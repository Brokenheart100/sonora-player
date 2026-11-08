#include "sonora/presentation/OnlineViewModel.h"

#include <QVariantList>


#include <algorithm>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

#include "sonora/kernel/Error.h"
#include "sonora/online/contracts/Commands.h"
#include "sonora/online/contracts/Queries.h"
#include "sonora/playback/contracts/Commands.h"
#include "sonora/playback/contracts/Queries.h"

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

        // QML 里传的是字符串（锚点跟着标签页走），到这里换成枚举；
        // 认不出来就退回搜索——总比静默什么都不做要好查。
        online::contracts::SourceKind kindFromName(const QString &name)
        {
            using online::contracts::SourceKind;
            if (name == QLatin1String("chart"))
            {
                return SourceKind::Chart;
            }
            if (name == QLatin1String("album"))
            {
                return SourceKind::Album;
            }
            if (name == QLatin1String("artist"))
            {
                return SourceKind::Artist;
            }
            if (name == QLatin1String("newsongs"))
            {
                return SourceKind::NewSongs;
            }
            if (name == QLatin1String("playlist"))
            {
                return SourceKind::Playlist;
            }
            return SourceKind::Search;
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

    // ---- OnlineCollectionModel ----

    OnlineCollectionModel::OnlineCollectionModel(QObject *parent) : RowsModel{parent} {}

    QHash<int, QByteArray> OnlineCollectionModel::roleNames() const
    {
        return {
            {IdRole, "collectionId"}, {NameRole, "name"}, {CoverRole, "cover"},
            {SubtitleRole, "subtitle"}, {CountRole, "trackCount"},
        };
    }

    QVariant OnlineCollectionModel::data(const QModelIndex &index, int role) const
    {
        if (!index.isValid())
        {
            return {};
        }
        const auto row = static_cast<std::size_t>(index.row());
        if (row >= items_.size())
        {
            return {};
        }
        const auto &item = items_[row];
        switch (role)
        {
        case IdRole:
            return fromUtf8(item.id);
        case NameRole:
            return fromUtf8(item.name);
        case CoverRole:
            return fromUtf8(item.coverUrl);
        case SubtitleRole:
            return fromUtf8(item.subtitle);
        case CountRole:
            return item.count;
        default:
            return {};
        }
    }

    void OnlineCollectionModel::setItems(const std::vector<online::contracts::CollectionDto> &items)
    {
        beginResetModel();
        items_ = items;
        assignCount(static_cast<int>(items_.size()));
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

        playbackSongId_ = rows[static_cast<std::size_t>(index)].songId;

        auto songIds = std::vector<std::string>{};
        songIds.reserve(rows.size());
        for (const auto &row : rows)
        {
            songIds.push_back(row.songId);
        }

        const auto token = token_;
        const auto origin = pageKind_;
        dispatchQuietly(
            [this, token, origin, songIds = std::move(songIds), index]
            {
                online::contracts::ResolveAndPlayOnline command;
                command.token = token;
                command.songIds = songIds;
                command.startIndex = index;
                command.origin = origin;
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

    void OnlineViewModel::signIn(const QString &cookie)
    {
        loginError_.clear();
        signingIn_ = true;
        online::contracts::SignInWithCookie command;
        command.provider = "ncm";
        command.cookie = cookie.toStdString();
        const auto result = commands_.dispatch(command);
        if (!result)
        {
            signingIn_ = false;
            loginError_ = fromUtf8(result.error().message());
        }
        emit statusChanged();
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
                                          ? QStringLiteral("%1 首").arg(detail.value().trackCount)
                                          : QString{};
                if (!detail.value().creator.empty())
                {
                    collectionSubtitle_ += QStringLiteral(" · ") + fromUtf8(detail.value().creator);
                }
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

        rescanFailedPlayback();
    }

    void OnlineViewModel::rescanFailedPlayback()
    {
        // R3-9：在线直链 20 分钟就过期（R0 实测 expi=1200）。过期后的症状是
        // 播放器进 Failed，而用户只看到一个"出错"——这里悄悄重取一次地址，
        // 连续两次还失败就停手，交给界面去报错。
        if (lastPlaybackSongId_.empty() || reResolveAttempts_ >= 2)
        {
            return;
        }
        const auto snapshot = queries_.ask<playback::contracts::PlayerSnapshot>(
            playback::contracts::GetPlayerSnapshot{});
        if (!snapshot || snapshot.value().state != playback::contracts::PlaybackState::Failed)
        {
            return;
        }

        ++reResolveAttempts_;
        const auto songId = lastPlaybackSongId_;
        dispatchQuietly(
            [this, songId]
            {
                online::contracts::ResolveAndPlayOnline command;
                command.token = token_;
                command.songIds = {songId};
                command.startIndex = 0;
                command.origin = pageKind_;
                return commands_.dispatch(command);
            });
    }

    void OnlineViewModel::applyPlayback(const online::contracts::OnlinePlaybackDto &playback)
    {
        if (playback.error.code != kernel::ErrorCode::None)
        {
            errorText_ = fromUtf8(playback.error.message);
            retryable_ = playback.error.retryable;
            emit statusChanged();
            return;
        }
        if (playback.entries.empty())
        {
            // 整批都取不到地址（版权下架 / 会员曲）：这是"点了放不了"，不是列表坏了。
            errorText_ = QStringLiteral("这些歌都没有可播放的地址");
            retryable_ = false;
            emit statusChanged();
            return;
        }

        const auto &rows = model_.rows();

        // 按**列表顺序**重建队列，而不是按取地址接口返回的顺序：ids=[...] 传过去
        // 以后，响应里的 data[] 未必保持同样的顺序（实测会重排），照着它排会让
        // "点第 2 行"变成"从第 27 行开始"。
        std::unordered_map<std::string, const online::contracts::OnlinePlaybackEntry *> urlById;
        urlById.reserve(playback.entries.size());
        for (const auto &item : playback.entries)
        {
            urlById.emplace(item.songId, &item);
        }

        auto entries = std::vector<playback::contracts::QueueEntry>{};
        entries.reserve(playback.entries.size());
        std::int32_t startIndex = 0;
        for (const auto &row : rows)
        {
            const auto found = urlById.find(row.songId);
            if (found == urlById.end())
            {
                // 取不到地址的行不进队列；于是它的下标在队列里不存在，
                // 下面用"当前队列长度"算起点正好跳过它。
                continue;
            }
            if (!playbackSongId_.empty() && row.songId == playbackSongId_)
            {
                startIndex = static_cast<std::int32_t>(entries.size());
            }

            playback::contracts::QueueEntry entry;
            // 在线曲目没有本地行：trackId 用 0，播放记录据此跳过落库。
            entry.trackId = 0;
            entry.filePath = found->second->url;
            entry.title = row.title;
            entry.artistName = row.artists;
            entry.coverPath = row.coverUrl;
            entries.push_back(std::move(entry));
        }

        const auto result = commands_.dispatch(playback::contracts::PlayTracks{std::move(entries), startIndex});
        if (!result)
        {
            errorText_ = fromUtf8(result.error().message());
            retryable_ = false;
            emit statusChanged();
            return;
        }
        errorText_.clear();
        retryable_ = false;
        lastPlaybackSongId_ = playbackSongId_;
        reResolveAttempts_ = 0;
        emit statusChanged();
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

    void OnlineViewModel::loadCollections(const QString &kind)
    {
        const auto source = kindFromName(kind);
        if (source != online::contracts::SourceKind::Playlist && source != online::contracts::SourceKind::Chart)
        {
            return;
        }
        collectionsWanted_ = static_cast<int>(source);
        online::contracts::LoadCollections command;
        command.kind = source;
        command.limit = source == online::contracts::SourceKind::Chart ? 65 : 30;
        dispatchQuietly([this, command] { return commands_.dispatch(command); });
    }

    void OnlineViewModel::openCollection(const QString &kind, const QString &id, const QString &title)
    {
        const auto source = kindFromName(kind);
        if (source == online::contracts::SourceKind::Search)
        {
            return;
        }

        ++token_;
        seenRevision_ = 0;
        pageKind_ = source;
        pageCollectionId_ = id.toStdString();
        collectionTitle_ = title;
        collectionSubtitle_.clear();
        keywords_.clear();
        model_.clearRows();
        busy_ = true;
        hasMore_ = false;
        retryable_ = false;
        errorText_.clear();
        statusText_ = QStringLiteral("正在打开：%1").arg(title);
        emit statusChanged();
        emit collectionChanged();

        const auto token = token_;
        const auto collectionId = pageCollectionId_;
        dispatchQuietly(
            [this, token, source, collectionId]
            {
                online::contracts::RequestOnlinePage command;
                command.token = token;
                command.kind = source;
                command.collectionId = collectionId;
                command.offset = 0;
                command.limit = 100;
                return commands_.dispatch(command);
            });
    }

    void OnlineViewModel::closeCollection()
    {
        pageKind_ = online::contracts::SourceKind::Search;
        pageCollectionId_.clear();
        collectionTitle_.clear();
        collectionSubtitle_.clear();
        model_.clearRows();
        token_ = 0;
        seenRevision_ = 0;
        busy_ = false;
        hasMore_ = false;
        retryable_ = false;
        errorText_.clear();
        statusText_ = QStringLiteral("输入关键词，回车搜索");
        emit statusChanged();
        emit collectionChanged();
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

    bool OnlineViewModel::qrCell(int row, int col) const
    {
        if (row < 0 || col < 0 || row >= qrSize_ || col >= qrSize_)
        {
            return false;
        }
        const std::string &line = qrRows_[static_cast<std::size_t>(row)];
        if (static_cast<std::size_t>(col) >= line.size())
        {
            return false;
        }
        return line[static_cast<std::size_t>(col)] == '1';
    }

    void OnlineViewModel::startQrLogin()
    {
        dispatchQuietly([this] { return commands_.dispatch(online::contracts::StartQrLogin{}); });
        qrActive_ = true;
        qrRows_.clear();
        qrSize_ = 0;
        qrSession_ = 0;
        qrMessage_ = QStringLiteral("正在生成二维码…");
        loginError_.clear();
        emit qrChanged();
    }

    void OnlineViewModel::cancelQrLogin()
    {
        dispatchQuietly([this] { return commands_.dispatch(online::contracts::CancelQrLogin{}); });
        qrActive_ = false;
        emit qrChanged();
    }

    void OnlineViewModel::refreshDiagnostics()
    {
        const auto snapshot =
            queries_.ask<online::contracts::OnlineDiagnosticsDto>(online::contracts::GetOnlineDiagnostics{});
        if (!snapshot)
        {
            return;
        }
        const auto &value = snapshot.value();

        QVariantMap out;
        out.insert(QStringLiteral("provider"), fromUtf8(value.provider));
        out.insert(QStringLiteral("consecutiveFailures"), value.consecutiveFailures);
        out.insert(QStringLiteral("nextRetryAtUnix"), static_cast<qlonglong>(value.nextRetryAtUnix));
        out.insert(QStringLiteral("tokensLeft"), value.tokensLeft);
        out.insert(QStringLiteral("tokenBurst"), value.tokenBurst);
        out.insert(QStringLiteral("cacheRows"), value.cache.searchRows);
        out.insert(QStringLiteral("cacheBytes"), static_cast<qlonglong>(value.cache.searchBytes));
        out.insert(QStringLiteral("capRows"), value.cache.capRows);
        out.insert(QStringLiteral("capBytes"), static_cast<qlonglong>(value.cache.capBytes));
        out.insert(QStringLiteral("songRows"), value.cache.songRows);
        out.insert(QStringLiteral("detailRows"), value.cache.detailRows);

        const auto &d = value.degradations;
        QVariantMap degradation;
        degradation.insert(QStringLiteral("offline"), d.offline);
        degradation.insert(QStringLiteral("timeout"), d.timeout);
        degradation.insert(QStringLiteral("rateLimited"), d.rateLimited);
        degradation.insert(QStringLiteral("unauthenticated"), d.unauthenticated);
        degradation.insert(QStringLiteral("upstream"), d.upstream);
        degradation.insert(QStringLiteral("urlExpired"), d.urlExpired);
        degradation.insert(QStringLiteral("cacheHits"), d.cacheHits);
        degradation.insert(QStringLiteral("totalErrors"), d.totalErrors);
        out.insert(QStringLiteral("degradations"), degradation);

        // 时间只给 Unix 秒，界面自己写"3 分钟前"——ViewModel 不持时钟。
        QVariantList recent;
        recent.reserve(static_cast<qsizetype>(value.recentErrors.size()));
        for (const auto &record : value.recentErrors)
        {
            QVariantMap item;
            item.insert(QStringLiteral("at"), static_cast<qlonglong>(record.atUnix));
            item.insert(QStringLiteral("where"), fromUtf8(record.where));
            item.insert(QStringLiteral("message"), fromUtf8(record.error.message));
            item.insert(QStringLiteral("retryable"), record.error.retryable);
            recent.append(item);
        }
        out.insert(QStringLiteral("recentErrors"), recent);

        diagnostics_ = std::move(out);
        emit diagnosticsChanged();
    }

} // namespace sonora::presentation
