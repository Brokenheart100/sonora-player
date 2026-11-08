#include "sonora/playlists/core/domain/Playlist.h"

#include <algorithm>
#include <utility>

namespace sonora::playlists::core {
namespace {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

// 名字里允许空格（"路上听 2" 是正常名字），但不允许整串都是空白。
[[nodiscard]] bool isBlank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    });
}

}  // namespace

Playlist::Playlist(PlaylistId id, std::string name, std::int64_t createdAt, std::vector<Item> items)
    : id_(id), name_(std::move(name)), createdAt_(createdAt), items_(std::move(items))
{
}

Result<void> Playlist::validateName(std::string_view name)
{
    if (name.empty() || isBlank(name)) {
        return Error{ErrorCode::InvalidArgument, "歌单名字不能为空"};
    }
    // 名字是给界面用的，不是文档：上限挡住的是"把整段文本粘进来"。
    if (name.size() > 120) {
        return Error{ErrorCode::InvalidArgument, "歌单名字太长（最多 120 个字符）"};
    }
    return {};
}

Result<Playlist> Playlist::create(std::string name, std::int64_t createdAt)
{
    auto valid = validateName(name);
    if (!valid) {
        return valid.error();
    }
    return Result<Playlist>::ok(Playlist{PlaylistId{}, std::move(name), createdAt, {}});
}

Result<Playlist> Playlist::rehydrate(PlaylistId id, std::string name, std::int64_t createdAt,
                                     std::vector<Item> items)
{
    if (!id.isValid()) {
        return Error{ErrorCode::Internal, "歌单 id 无效"};
    }
    auto valid = validateName(name);
    if (!valid) {
        return valid.error();
    }
    return Result<Playlist>::ok(Playlist{id, std::move(name), createdAt, std::move(items)});
}

Result<void> Playlist::rename(std::string name)
{
    auto valid = validateName(name);
    if (!valid) {
        return valid;
    }
    name_ = std::move(name);
    return {};
}

Result<void> Playlist::addTrack(std::uint64_t trackId)
{
    if (trackId == 0) {
        return Error{ErrorCode::InvalidArgument, "曲目 id 无效"};
    }
    if (hasTrack(trackId)) {
        return Error{ErrorCode::Conflict, "这首歌已经在歌单里了"};
    }
    items_.push_back(Item{trackId, true});
    return {};
}

Result<void> Playlist::removeTrack(std::uint64_t trackId)
{
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [trackId](const Item &item) { return item.trackId == trackId; });
    if (it == items_.end()) {
        return Error{ErrorCode::NotFound, "这首歌不在歌单里"};
    }
    items_.erase(it);
    return {};
}

Result<void> Playlist::moveTrack(std::uint64_t trackId, std::int32_t toIndex)
{
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [trackId](const Item &item) { return item.trackId == trackId; });
    if (it == items_.end()) {
        return Error{ErrorCode::NotFound, "这首歌不在歌单里"};
    }

    const auto size = static_cast<std::int32_t>(items_.size());
    const auto target = std::clamp<std::int32_t>(toIndex, 0, size - 1);
    const auto from = static_cast<std::int32_t>(std::distance(items_.begin(), it));
    if (from == target) {
        return {};
    }

    Item moved = std::move(*it);
    items_.erase(it);
    items_.insert(items_.begin() + target, std::move(moved));
    return {};
}

void Playlist::markTrackUnavailable(std::uint64_t trackId) noexcept
{
    for (auto &item : items_) {
        if (item.trackId == trackId) {
            item.available = false;
        }
    }
}

bool Playlist::hasTrack(std::uint64_t trackId) const noexcept
{
    return std::any_of(items_.begin(), items_.end(),
                       [trackId](const Item &item) { return item.trackId == trackId; });
}

}  // namespace sonora::playlists::core
