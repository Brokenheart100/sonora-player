#include "sonora/catalog/core/domain/Credit.h"

#include <utility>

#include "sonora/catalog/core/domain/Track.h"   // trimTitle

namespace sonora::catalog::core {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

namespace {

Result<void> requireName(const std::string &name, const char *what)
{
    if (name.empty()) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, what});
    }
    return Result<void>::ok();
}

}  // namespace

// ── Album ──────────────────────────────────────────────────────────────────
Album::Album(AlbumId id, std::string title) : id_(id), title_(std::move(title)) {}

Result<Album> Album::add(std::string title)
{
    title = trimTitle(title);
    if (auto result = requireName(title, "专辑名不能为空"); !result) {
        return Result<Album>::fail(result.error());
    }
    return Result<Album>::ok(Album{AlbumId{}, std::move(title)});
}

Result<Album> Album::rehydrate(AlbumId id, std::string title)
{
    if (!id.isValid()) {
        return Result<Album>::fail(Error{ErrorCode::InvalidArgument, "已持久化的专辑必须有有效 id"});
    }
    title = trimTitle(title);
    if (auto result = requireName(title, "专辑名不能为空"); !result) {
        return Result<Album>::fail(result.error());
    }
    return Result<Album>::ok(Album{id, std::move(title)});
}

Result<void> Album::rename(std::string_view newTitle)
{
    const std::string trimmed = trimTitle(newTitle);
    if (auto result = requireName(trimmed, "专辑名不能为空"); !result) {
        return result;
    }
    title_ = trimmed;
    return Result<void>::ok();
}

// ── Artist ─────────────────────────────────────────────────────────────────
Artist::Artist(ArtistId id, std::string name) : id_(id), name_(std::move(name)) {}

Result<Artist> Artist::add(std::string name)
{
    name = trimTitle(name);
    if (auto result = requireName(name, "艺术家名不能为空"); !result) {
        return Result<Artist>::fail(result.error());
    }
    return Result<Artist>::ok(Artist{ArtistId{}, std::move(name)});
}

Result<Artist> Artist::rehydrate(ArtistId id, std::string name)
{
    if (!id.isValid()) {
        return Result<Artist>::fail(
            Error{ErrorCode::InvalidArgument, "已持久化的艺术家必须有有效 id"});
    }
    name = trimTitle(name);
    if (auto result = requireName(name, "艺术家名不能为空"); !result) {
        return Result<Artist>::fail(result.error());
    }
    return Result<Artist>::ok(Artist{id, std::move(name)});
}

Result<void> Artist::rename(std::string_view newName)
{
    const std::string trimmed = trimTitle(newName);
    if (auto result = requireName(trimmed, "艺术家名不能为空"); !result) {
        return result;
    }
    name_ = trimmed;
    return Result<void>::ok();
}

}  // namespace sonora::catalog::core
