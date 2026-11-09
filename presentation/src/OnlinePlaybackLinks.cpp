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

    // —— 播放/排队/查看歌手与直链回填 ——

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

    void OnlineViewModel::playAll()
    {
        play(0);
    }

    void OnlineViewModel::enqueue(int index)
    {
        const auto &rows = model_.rows();
        if (index < 0 || static_cast<std::size_t>(index) >= rows.size())
        {
            return;
        }
        enqueueIds_.clear();
        enqueueIds_.push_back(rows[static_cast<std::size_t>(index)].songId);
        enqueueOnly_ = true;
        playbackSongId_.clear();

        const auto token = token_;
        const auto origin = pageKind_;
        auto songIds = idsOfRows(rows);
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

    void OnlineViewModel::enqueueAll()
    {
        const auto &rows = model_.rows();
        if (rows.empty())
        {
            return;
        }
        enqueueIds_ = idsOfRows(rows);
        enqueueOnly_ = true;
        playbackSongId_.clear();

        const auto token = token_;
        const auto origin = pageKind_;
        auto songIds = idsOfRows(rows);
        dispatchQuietly(
            [this, token, origin, songIds = std::move(songIds)]
            {
                online::contracts::ResolveAndPlayOnline command;
                command.token = token;
                command.songIds = songIds;
                command.startIndex = 0;
                command.origin = origin;
                return commands_.dispatch(command);
            });
    }

    void OnlineViewModel::openArtist(int index)
    {
        const auto &rows = model_.rows();
        if (index < 0 || static_cast<std::size_t>(index) >= rows.size())
        {
            return;
        }
        const auto &row = rows[static_cast<std::size_t>(index)];
        if (row.artistId.empty())
        {
            errorText_ = tr("这首歌没有歌手信息");
            emit statusChanged();
            return;
        }
        openCollection(QStringLiteral("artist"), fromUtf8(row.artistId), fromUtf8(row.artists));
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
            errorText_ = tr("这些歌都没有可播放的地址");
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
        auto entryIds = std::vector<std::string>{};
        entries.reserve(playback.entries.size());
        entryIds.reserve(playback.entries.size());
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
            // 歌词要靠 songId 找词：filePath 里是 CDN 直链，看不出是哪首歌。
            entry.onlineSongId = row.songId;
            entryIds.push_back(row.songId);
            entries.push_back(std::move(entry));
        }

        if (enqueueOnly_)
        {
            // 排队：只挑这次要加的那几首，追加到队尾；不打断正在播的那首
            // （playWhenIdle = false 时，播放服务只在队列空着时才起播）。
            auto picked = std::vector<playback::contracts::QueueEntry>{};
            picked.reserve(enqueueIds_.size());
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                const auto wanted = std::find(enqueueIds_.begin(), enqueueIds_.end(), entryIds[i]);
                if (wanted != enqueueIds_.end())
                {
                    picked.push_back(std::move(entries[i]));
                }
            }
            const auto appended = picked.size();
            playback::contracts::AppendTracks command;
            command.entries = std::move(picked);
            command.playWhenIdle = false;
            const auto queued = commands_.dispatch(command);
            enqueueOnly_ = false;
            enqueueIds_.clear();
            playbackSongId_.clear();
            if (!queued)
            {
                errorText_ = fromUtf8(queued.error().message());
                retryable_ = false;
            }
            else
            {
                errorText_.clear();
                retryable_ = false;
                statusText_ = tr("已加入队列：%n 首", "", static_cast<int>(appended));
                statusHoldTicks_ = 100;
            }
            emit statusChanged();
            return;
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


} // namespace sonora::presentation
