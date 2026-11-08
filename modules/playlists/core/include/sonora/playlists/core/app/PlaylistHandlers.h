// 把 PlaylistService 挂到两条总线上。
//
// 每个 Handler 只做一件事：把契约里的裸 id 换成强类型 id，然后转调。
// 校验、聚合规则、事务都不在这里——薄壳原则同上。
#pragma once

#include <memory>
#include <utility>

#include <sonora/kernel/CommandBus.h>
#include <sonora/kernel/QueryBus.h>
#include <sonora/playlists/contracts/Commands.h>
#include <sonora/playlists/contracts/Queries.h>
#include <sonora/playlists/core/app/PlaylistService.h>

namespace sonora::playlists::core {

class CreatePlaylistHandler {
public:
    explicit CreatePlaylistHandler(PlaylistService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::CreatePlaylist &command)
    {
        // 新建的 id 给界面用（建完直接进那个歌单），但命令总线的返回类型是
        // Result<void>。界面要走这条路就先建后查——多一次 SELECT，换一个
        // 不需要改总线的接口。
        auto created = service_.create(command.name);
        if (!created) {
            return created.error();
        }
        return {};
    }

private:
    PlaylistService &service_;
};

class RenamePlaylistHandler {
public:
    explicit RenamePlaylistHandler(PlaylistService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::RenamePlaylist &command)
    {
        const auto id = PlaylistId::fromValue(command.playlistId);
        if (!id.isValid()) {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌单"};
        }
        return service_.rename(id, command.name);
    }

private:
    PlaylistService &service_;
};

class DeletePlaylistHandler {
public:
    explicit DeletePlaylistHandler(PlaylistService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::DeletePlaylist &command)
    {
        const auto id = PlaylistId::fromValue(command.playlistId);
        if (!id.isValid()) {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌单"};
        }
        return service_.remove(id);
    }

private:
    PlaylistService &service_;
};

class AddTrackToPlaylistHandler {
public:
    explicit AddTrackToPlaylistHandler(PlaylistService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::AddTrackToPlaylist &command)
    {
        const auto id = PlaylistId::fromValue(command.playlistId);
        if (!id.isValid()) {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌单"};
        }
        return service_.addTrack(id, command.trackId);
    }

private:
    PlaylistService &service_;
};

class RemoveTrackFromPlaylistHandler {
public:
    explicit RemoveTrackFromPlaylistHandler(PlaylistService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::RemoveTrackFromPlaylist &command)
    {
        const auto id = PlaylistId::fromValue(command.playlistId);
        if (!id.isValid()) {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌单"};
        }
        return service_.removeTrack(id, command.trackId);
    }

private:
    PlaylistService &service_;
};

class MovePlaylistTrackHandler {
public:
    explicit MovePlaylistTrackHandler(PlaylistService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::MovePlaylistTrack &command)
    {
        const auto id = PlaylistId::fromValue(command.playlistId);
        if (!id.isValid()) {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌单"};
        }
        return service_.moveTrack(id, command.trackId, command.toIndex);
    }

private:
    PlaylistService &service_;
};

class MarkTrackUnavailableHandler {
public:
    explicit MarkTrackUnavailableHandler(PlaylistService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::MarkTrackUnavailable &command)
    {
        auto affected = service_.markTrackUnavailable(command.trackId);
        if (!affected) {
            return affected.error();
        }
        return {};
    }

private:
    PlaylistService &service_;
};

class ListPlaylistsHandler {
public:
    explicit ListPlaylistsHandler(const PlaylistService &service) : service_(service) {}
    [[nodiscard]] kernel::Result<contracts::PlaylistList> handle(const contracts::ListPlaylists &) const
    {
        return service_.list();
    }

private:
    const PlaylistService &service_;
};

class GetPlaylistTracksHandler {
public:
    explicit GetPlaylistTracksHandler(const PlaylistService &service) : service_(service) {}
    [[nodiscard]] kernel::Result<contracts::PlaylistTrackList> handle(
        const contracts::GetPlaylistTracks &query) const
    {
        const auto id = PlaylistId::fromValue(query.playlistId);
        if (!id.isValid()) {
            return kernel::Error{kernel::ErrorCode::InvalidArgument, "没有指定歌单"};
        }
        return service_.tracks(id);
    }

private:
    const PlaylistService &service_;
};

inline void registerPlaylists(kernel::CommandBus &commands, kernel::QueryBus &queries,
                              PlaylistService &service)
{
    commands.on<contracts::CreatePlaylist>(std::make_shared<CreatePlaylistHandler>(service));
    commands.on<contracts::RenamePlaylist>(std::make_shared<RenamePlaylistHandler>(service));
    commands.on<contracts::DeletePlaylist>(std::make_shared<DeletePlaylistHandler>(service));
    commands.on<contracts::AddTrackToPlaylist>(std::make_shared<AddTrackToPlaylistHandler>(service));
    commands.on<contracts::RemoveTrackFromPlaylist>(
        std::make_shared<RemoveTrackFromPlaylistHandler>(service));
    commands.on<contracts::MovePlaylistTrack>(std::make_shared<MovePlaylistTrackHandler>(service));
    commands.on<contracts::MarkTrackUnavailable>(
        std::make_shared<MarkTrackUnavailableHandler>(service));

    // 查询总线只收可调用对象；handler 无状态，就地构造即可。
    queries.on<contracts::ListPlaylists, contracts::PlaylistList>(
        [&service](const contracts::ListPlaylists &query) {
            return ListPlaylistsHandler{service}.handle(query);
        });
    queries.on<contracts::GetPlaylistTracks, contracts::PlaylistTrackList>(
        [&service](const contracts::GetPlaylistTracks &query) {
            return GetPlaylistTracksHandler{service}.handle(query);
        });
}

}  // namespace sonora::playlists::core
