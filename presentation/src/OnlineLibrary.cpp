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

    // —— 我的歌单、每日推荐、收藏 ——

    void OnlineViewModel::publishMyPlaylists(const std::vector<online::contracts::PlaylistDto> &items)
    {
        QVariantList next;
        next.reserve(static_cast<qsizetype>(items.size()));
        for (const auto &item : items)
        {
            QVariantMap row;
            row.insert(QStringLiteral("playlistId"), QString::fromStdString(item.playlistId));
            row.insert(QStringLiteral("name"), QString::fromStdString(item.name));
            row.insert(QStringLiteral("cover"), QString::fromStdString(item.coverUrl));
            row.insert(QStringLiteral("trackCount"), item.trackCount);
            next.append(row);
        }
        if (next.size() == myPlaylists_.size())
        {
            return;
        }
        myPlaylists_ = std::move(next);
        emit myPlaylistsChanged();

        // 进来时点过"喜欢"但那时还不知道歌单 id：现在补上。
        if (!pendingLikeSongId_.empty())
        {
            const QString wanted = QString::fromStdString(pendingLikeSongId_);
            pendingLikeSongId_.clear();
            likeFmSong(wanted);
        }
    }

    void OnlineViewModel::loadMyPlaylists()
    {
        // 只发一次"拉"的命令；真正的回包由 onTick 的 50ms 轮询搬进来。
        myPlaylistsWanted_ = true;
        myPlaylistsTick_ = 0;
        commands_.dispatch(online::contracts::LoadMyPlaylists{});
    }

    void OnlineViewModel::requestFavorite(int rowIndex)
    {
        const auto &rows = model_.rows();
        if (rowIndex < 0 || static_cast<std::size_t>(rowIndex) >= rows.size())
        {
            return;
        }
        if (!signedIn())
        {
            errorText_ = tr("登录后才能收藏");
            emit statusChanged();
            return;
        }
        if (myPlaylists_.isEmpty())
        {
            // 喜欢列表的 id 只能从"我的歌单"里拿：没有任何歌单时先拉一次。
            loadMyPlaylists();
            errorText_ = tr("正在取「我喜欢的音乐」，稍后再试");
            emit statusChanged();
            return;
        }

        const auto &row = rows[static_cast<std::size_t>(rowIndex)];
        favoriteSongId_ = row.songId;
        favoritePlaylistId_.clear();
        for (const auto &item : myPlaylists_)
        {
            const auto map = item.toMap();
            if (map.value(QStringLiteral("name")).toString() == tr("我喜欢的音乐"))
            {
                favoritePlaylistId_ = map.value(QStringLiteral("playlistId")).toString().toStdString();
                break;
            }
        }
        if (favoritePlaylistId_.empty())
        {
            const auto first = myPlaylists_.first().toMap();
            favoritePlaylistId_ = first.value(QStringLiteral("playlistId")).toString().toStdString();
        }

        favoritePromptText_ = tr("收藏《%1》到「我喜欢的音乐」？").arg(fromUtf8(row.title));
        favoritePromptVisible_ = true;
        emit favoriteChanged();
    }

    void OnlineViewModel::confirmFavorite()
    {
        favoritePromptVisible_ = false;
        emit favoriteChanged();
        if (favoriteSongId_.empty() || favoritePlaylistId_.empty())
        {
            return;
        }

        online::contracts::ToggleFavorite command;
        command.songId = favoriteSongId_;
        command.playlistId = favoritePlaylistId_;
        command.add = true;
        const auto result = commands_.dispatch(command);
        if (!result)
        {
            // 写失败：不做回滚（本来就没改过界面），只把原因摆出来。
            errorText_ = fromUtf8(result.error().message());
            emit statusChanged();
            return;
        }
        errorText_.clear();
        statusText_ = tr("已收藏");
        emit statusChanged();
    }


    void OnlineViewModel::cancelFavorite()
    {
        favoritePromptVisible_ = false;
        emit favoriteChanged();
    }


} // namespace sonora::presentation
