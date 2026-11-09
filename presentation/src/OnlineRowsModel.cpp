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

    // —— 两个行模型（结果行、卡片行） ——

    // ---- OnlineListModel ----

    OnlineListModel::OnlineListModel(QObject *parent) : RowsModel{parent} {}

    QHash<int, QByteArray> OnlineListModel::roleNames() const
    {
        return {
            {SongIdRole, "songId"},  {TitleRole, "title"},     {ArtistRole, "artist"},
            {ArtistIdRole, "artistId"},
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
        case ArtistIdRole:
            return fromUtf8(track.artistId);
        case AlbumRole:
            return fromUtf8(track.album);
        case DurationRole:
            return formatDuration(track.durationMs);
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


} // namespace sonora::presentation
