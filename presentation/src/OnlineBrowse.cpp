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

    // —— 推荐/榜单/新歌的分类页与歌单详情 ——

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
        collectionCover_.clear();
        keywords_.clear();
        model_.clearRows();
        busy_ = true;
        hasMore_ = false;
        retryable_ = false;
        errorText_.clear();
        statusText_ = tr("正在打开：%1").arg(title);
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
        collectionCover_.clear();
        enqueueOnly_ = false;
        enqueueIds_.clear();
        model_.clearRows();
        token_ = 0;
        seenRevision_ = 0;
        busy_ = false;
        hasMore_ = false;
        retryable_ = false;
        errorText_.clear();
        statusText_ = tr("输入关键词，回车搜索");
        emit statusChanged();
        emit collectionChanged();
    }

    void OnlineViewModel::openDailyRecommend()
    {
        // 每日推荐走详情页，但没有具体歌单 id：服务层按 kind 自己发请求。
        pageKind_ = online::contracts::SourceKind::DailyRecommend;
        pageCollectionId_.clear();
        collectionTitle_ = tr("每日推荐");
        collectionSubtitle_.clear();
        model_.clearRows();
        ++token_;
        seenRevision_ = 0;
        busy_ = true;
        hasMore_ = false;
        retryable_ = false;
        errorText_.clear();
        statusText_ = tr("正在取每日推荐…");
        emit statusChanged();
        emit collectionChanged();

        online::contracts::RequestOnlinePage command;
        command.token = token_;
        command.kind = online::contracts::SourceKind::DailyRecommend;
        command.offset = 0;
        command.limit = 100;
        dispatchQuietly([this, command] { return commands_.dispatch(command); });
    }


} // namespace sonora::presentation
