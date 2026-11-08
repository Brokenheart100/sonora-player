#include "sonora/presentation/PlaylistsViewModel.h"

#include <QUrl>
#include <QVariantMap>

#include <utility>

#include "sonora/catalog/contracts/Queries.h"
#include "sonora/playback/contracts/Commands.h"
#include "sonora/playlists/contracts/Commands.h"
#include "sonora/playlists/contracts/Queries.h"

namespace sonora::presentation
{
    namespace
    {
        namespace pc = playlists::contracts;
        namespace cc = catalog::contracts;
        namespace bc = playback::contracts;

        QString fromUtf8(const std::string &text)
        {
            return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
        }

        // 列表里的时长：mm:ss。DTO 只给毫秒，格式化是表现层的事。
        QString formatDuration(std::int64_t ms)
        {
            if (ms <= 0)
            {
                return QString();
            }
            const auto total = ms / 1000;
            return QStringLiteral("%1:%2")
                .arg(total / 60)
                .arg(total % 60, 2, 10, QLatin1Char('0'));
        }
    } // namespace

    PlaylistsViewModel::PlaylistsViewModel(kernel::ICommandBus &commands,
                                           kernel::IQueryBus &queries, QObject *parent)
        : QObject(parent), commands_(commands), queries_(queries)
    {
        refresh();
    }

    void PlaylistsViewModel::setStatus(QString text)
    {
        if (status_ == text)
        {
            return;
        }
        status_ = std::move(text);
        emit statusChanged();
    }

    void PlaylistsViewModel::refresh()
    {
        auto result = queries_.ask<pc::PlaylistList>(pc::ListPlaylists{});
        if (!result)
        {
            setStatus(tr("取不到歌单：%1").arg(fromUtf8(result.error().message())));
            return;
        }

        QVariantList rows;
        rows.reserve(static_cast<qsizetype>(result.value().size()));
        for (const auto &item : result.value())
        {
            QVariantMap row;
            row.insert(QStringLiteral("playlistId"), QVariant::fromValue(item.playlistId));
            row.insert(QStringLiteral("name"), fromUtf8(item.name));
            row.insert(QStringLiteral("trackCount"), item.trackCount);
            rows.append(row);
        }
        playlists_ = std::move(rows);
        setStatus(tr("%1 个歌单").arg(count()));
        emit playlistsChanged();
    }

    void PlaylistsViewModel::createPlaylist(const QString &name)
    {
        pc::CreatePlaylist command;
        command.name = name.toStdString();
        if (auto created = commands_.dispatch(command); !created)
        {
            // 名字为空/太长都在这里：把领域的话原样给人看，比自己再拼一句更准。
            setStatus(fromUtf8(created.error().message()));
            return;
        }
        refresh();
    }

    void PlaylistsViewModel::removePlaylist(qint64 playlistId)
    {
        pc::DeletePlaylist command;
        command.playlistId = playlistId;
        if (auto removed = commands_.dispatch(command); !removed)
        {
            setStatus(fromUtf8(removed.error().message()));
            return;
        }
        if (current_id_ == playlistId)
        {
            closePlaylist();
        }
        refresh();
    }

    void PlaylistsViewModel::openPlaylist(qint64 playlistId, const QString &name)
    {
        current_id_ = playlistId;
        current_name_ = name;
        ensureTrackIndex();
        reloadEntries();
        emit openedChanged();
    }

    void PlaylistsViewModel::closePlaylist()
    {
        current_id_ = 0;
        current_name_.clear();
        raw_entries_.clear();
        entries_.clear();
        emit entriesChanged();
        emit openedChanged();
        refresh();
    }

    void PlaylistsViewModel::reloadEntries()
    {
        if (current_id_ == 0)
        {
            raw_entries_.clear();
            publishEntries();
            return;
        }
        pc::GetPlaylistTracks query;
        query.playlistId = current_id_;
        auto result = queries_.ask<pc::PlaylistTrackList>(query);
        if (!result)
        {
            raw_entries_.clear();
            setStatus(fromUtf8(result.error().message()));
            publishEntries();
            return;
        }
        raw_entries_ = result.value();
        publishEntries();
    }

    void PlaylistsViewModel::ensureTrackIndex()
    {
        if (index_ready_)
        {
            return;
        }
        // `ListTracks` 明确拒绝 limit > 10000（那是读侧故意设的边界），
        // 所以按 5000 一页翻到底。一万首也就两页，投影 SQL 是毫秒级的。
        constexpr std::int32_t kPage = 5000;
        index_.clear();
        std::int64_t offset = 0;
        bool complete = false;
        for (int guard = 0; guard < 200; ++guard)
        {
            cc::ListTracks query;
            query.offset = static_cast<std::int32_t>(offset);
            query.limit = kPage;
            auto result = queries_.ask<cc::TrackListResult>(query);
            if (!result)
            {
                // 拿不到曲库不是"歌单错了"：条目会全部显示成不可用占位，提示另行给出。
                setStatus(tr("曲库暂时读不出来：%1").arg(fromUtf8(result.error().message())));
                return;
            }
            const auto &page = result.value();
            for (const auto &track : page.rows)
            {
                index_.insert(static_cast<qint64>(track.id), track);
            }
            offset += static_cast<std::int64_t>(page.rows.size());
            if (page.rows.empty() || offset >= page.totalCount)
            {
                complete = true;
                break;
            }
        }
        index_ready_ = complete;
    }

    void PlaylistsViewModel::publishEntries()
    {
        QVariantList rows;
        rows.reserve(static_cast<qsizetype>(raw_entries_.size()));
        int available = 0;
        for (const auto &item : raw_entries_)
        {
            const auto key = static_cast<qint64>(item.trackId);
            const auto found = index_.constFind(key);
            QVariantMap row;
            row.insert(QStringLiteral("trackId"), item.trackId);
            row.insert(QStringLiteral("pos"), item.pos);
            row.insert(QStringLiteral("available"), item.available && found != index_.constEnd());

            if (found == index_.constEnd())
            {
                row.insert(QStringLiteral("title"), tr("曲目 %1").arg(item.trackId));
                row.insert(QStringLiteral("artist"), tr("曲库里没有这首"));
                row.insert(QStringLiteral("duration"), QString());
                row.insert(QStringLiteral("cover"), QString());
            }
            else
            {
                const auto &track = found.value();
                row.insert(QStringLiteral("title"), fromUtf8(track.title));
                row.insert(QStringLiteral("artist"), fromUtf8(track.artistName));
                row.insert(QStringLiteral("duration"), formatDuration(track.durationMs));
                row.insert(QStringLiteral("cover"),
                           track.coverPath.empty()
                               ? QString()
                               : QUrl::fromLocalFile(fromUtf8(track.coverPath)).toString());
                if (item.available)
                {
                    ++available;
                }
            }
            rows.append(row);
        }
        entries_ = std::move(rows);
        setStatus(tr("%1 首（可播 %2 首）").arg(static_cast<int>(raw_entries_.size())).arg(available));
        emit entriesChanged();
    }

    void PlaylistsViewModel::removeTrack(qint64 trackId)
    {
        if (current_id_ == 0)
        {
            return;
        }
        pc::RemoveTrackFromPlaylist command;
        command.playlistId = current_id_;
        command.trackId = static_cast<std::uint64_t>(trackId);
        if (auto removed = commands_.dispatch(command); !removed)
        {
            setStatus(fromUtf8(removed.error().message()));
            return;
        }
        reloadEntries();
        refresh();
    }

    void PlaylistsViewModel::playEntry(int index)
    {
        if (index < 0 || index >= static_cast<int>(raw_entries_.size()))
        {
            return;
        }
        bc::PlayTracks command;
        int row = 0;
        int start = -1;
        for (const auto &item : raw_entries_)
        {
            const auto found = index_.constFind(static_cast<qint64>(item.trackId));
            if (found != index_.constEnd())
            {
                const auto &track = found.value();
                bc::QueueEntry entry;
                entry.trackId = track.id;
                entry.filePath = track.filePath;
                entry.title = track.title;
                entry.artistName = track.artistName;
                entry.coverPath = track.coverPath;
                if (row == index)
                {
                    // 前面有不可用的行被跳过了，队列下标和界面行号不是一回事。
                    start = static_cast<int>(command.entries.size());
                }
                command.entries.push_back(std::move(entry));
            }
            ++row;
        }
        if (start < 0 || command.entries.empty())
        {
            return;
        }
        command.startIndex = start;
        (void)commands_.dispatch(command);
    }

    void PlaylistsViewModel::playAll() { playEntry(0); }

    void PlaylistsViewModel::addFromLibrary(int row)
    {
        if (current_id_ == 0 || row < 0)
        {
            return;
        }
        cc::ListTracks query;
        query.offset = row;
        query.limit = 1;
        auto result = queries_.ask<cc::TrackListResult>(query);
        if (!result)
        {
            setStatus(tr("读曲库失败：%1").arg(fromUtf8(result.error().message())));
            return;
        }
        if (result.value().rows.empty())
        {
            setStatus(tr("曲库里没有这一行"));
            return;
        }
        pc::AddTrackToPlaylist command;
        command.playlistId = current_id_;
        command.trackId = result.value().rows.front().id;
        if (auto added = commands_.dispatch(command); !added)
        {
            setStatus(fromUtf8(added.error().message()));
            return;
        }
        reloadEntries();
        refresh();
    }

} // namespace sonora::presentation
