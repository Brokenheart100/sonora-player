#include "sonora/playlists/core/app/PlaylistService.h"

#include <chrono>
#include <utility>

namespace sonora::playlists::core {
namespace {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

// 歌单 id 从命令里进来时是裸 int64（契约层不暴露强类型 id）。
// 0/负数一律当"没有指定"，而不是当成一个合法的空号去查库。
[[nodiscard]] Result<PlaylistId> toId(std::int64_t raw)
{
    if (raw <= 0) {
        return Error{ErrorCode::InvalidArgument, "没有指定歌单"};
    }
    return Result<PlaylistId>::ok(PlaylistId::fromValue(raw));
}

}  // namespace

PlaylistService::PlaylistService(IPlaylistRepository &repository, const kernel::IClock *clock)
    : repository_(repository), clock_(clock)
{
}

std::int64_t PlaylistService::nowUnix() const
{
    if (clock_ != nullptr) {
        const auto now = clock_->now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::seconds>(now).count();
    }
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

Result<PlaylistId> PlaylistService::create(std::string name)
{
    auto playlist = Playlist::create(std::move(name), nowUnix());
    if (!playlist) {
        return playlist.error();
    }
    return repository_.insert(playlist.value());
}

Result<void> PlaylistService::rename(PlaylistId id, std::string name)
{
    auto loaded = repository_.findById(id);
    if (!loaded) {
        return loaded.error();
    }
    auto renamed = loaded.value().rename(std::move(name));
    if (!renamed) {
        return renamed;
    }
    return repository_.save(loaded.value());
}

Result<void> PlaylistService::remove(PlaylistId id) { return repository_.remove(id); }

Result<void> PlaylistService::addTrack(PlaylistId id, std::uint64_t trackId)
{
    auto loaded = repository_.findById(id);
    if (!loaded) {
        return loaded.error();
    }
    auto added = loaded.value().addTrack(trackId);
    if (!added) {
        return added;
    }
    return repository_.save(loaded.value());
}

Result<void> PlaylistService::removeTrack(PlaylistId id, std::uint64_t trackId)
{
    auto loaded = repository_.findById(id);
    if (!loaded) {
        return loaded.error();
    }
    auto removed = loaded.value().removeTrack(trackId);
    if (!removed) {
        return removed;
    }
    return repository_.save(loaded.value());
}

Result<void> PlaylistService::moveTrack(PlaylistId id, std::uint64_t trackId, std::int32_t toIndex)
{
    auto loaded = repository_.findById(id);
    if (!loaded) {
        return loaded.error();
    }
    auto moved = loaded.value().moveTrack(trackId, toIndex);
    if (!moved) {
        return moved;
    }
    return repository_.save(loaded.value());
}

Result<std::int64_t> PlaylistService::markTrackUnavailable(std::uint64_t trackId)
{
    if (trackId == 0) {
        return Error{ErrorCode::InvalidArgument, "曲目 id 无效"};
    }
    return repository_.markTrackUnavailable(trackId);
}

Result<std::vector<contracts::PlaylistDto>> PlaylistService::list() const
{
    return repository_.list();
}

Result<std::vector<contracts::PlaylistEntryDto>> PlaylistService::tracks(PlaylistId id) const
{
    auto loaded = repository_.findById(id);
    if (!loaded) {
        return loaded.error();
    }

    std::vector<contracts::PlaylistEntryDto> entries;
    entries.reserve(loaded.value().items().size());
    for (std::size_t index = 0; index < loaded.value().items().size(); ++index) {
        const auto &item = loaded.value().items()[index];
        contracts::PlaylistEntryDto dto;
        dto.trackId = item.trackId;
        dto.pos = static_cast<std::int32_t>(index);
        dto.available = item.available;
        entries.push_back(std::move(dto));
    }
    return Result<std::vector<contracts::PlaylistEntryDto>>::ok(std::move(entries));
}

}  // namespace sonora::playlists::core
