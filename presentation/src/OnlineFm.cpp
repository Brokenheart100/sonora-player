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

    // —— 私人 FM ——

    void OnlineViewModel::startPersonalFm()
    {
        fmMode_ = true;
        emit fmChanged();
        errorText_.clear();
        statusText_ = tr("私人 FM：正在取第一批");
        emit statusChanged();

        // 喜欢要落到"我喜欢的音乐"里；顺手拉一次歌单，拿到 id 就能直接收藏。
        if (signedIn() && myPlaylists_.isEmpty())
        {
            loadMyPlaylists();
        }
        const auto result = commands_.dispatch(online::contracts::StartPersonalFm{3});
        if (!result)
        {
            errorText_ = fromUtf8(result.error().message());
            emit statusChanged();
        }
    }

    void OnlineViewModel::leaveFm()
    {
        if (!fmMode_)
        {
            return;
        }
        fmMode_ = false;
        emit fmChanged();
    }

    void OnlineViewModel::dislikeFmSong(const QString &songId)
    {
        if (songId.isEmpty())
        {
            return;
        }
        online::contracts::DislikeFmSong command;
        command.songId = songId.toStdString();
        (void)commands_.dispatch(command);
        statusText_ = tr("已跳过这首（以后不再出现）");
        emit statusChanged();
    }

    void OnlineViewModel::likeFmSong(const QString &songId)
    {
        if (songId.isEmpty())
        {
            return;
        }
        if (!signedIn())
        {
            errorText_ = tr("登录后才能收藏");
            emit statusChanged();
            return;
        }

        if (fmPlaylistId_.empty())
        {
            for (const auto &item : myPlaylists_)
            {
                const auto map = item.toMap();
                if (map.value(QStringLiteral("name")).toString() == tr("我喜欢的音乐"))
                {
                    fmPlaylistId_ = map.value(QStringLiteral("playlistId")).toString().toStdString();
                    break;
                }
            }
        }
        if (fmPlaylistId_.empty())
        {
            // 还没有歌单列表：记住这首，等 publishMyPlaylists 回来再补。
            pendingLikeSongId_ = songId.toStdString();
            loadMyPlaylists();
            statusText_ = tr("正在取「我喜欢的音乐」");
            emit statusChanged();
            return;
        }

        online::contracts::ToggleFavorite command;
        command.songId = songId.toStdString();
        command.playlistId = fmPlaylistId_;
        command.add = true;
        const auto result = commands_.dispatch(command);
        if (!result)
        {
            errorText_ = fromUtf8(result.error().message());
            emit statusChanged();
            return;
        }
        errorText_.clear();
        statusText_ = tr("已收藏");
        emit statusChanged();
    }


} // namespace sonora::presentation
