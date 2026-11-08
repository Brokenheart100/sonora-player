#include "sonora/catalog/core/domain/Track.h"

#include "sonora/kernel/Text.h"

#include <string_view>
#include <utility>

namespace sonora::catalog::core {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

namespace {

// 统一校验：新增与重建都走这里，避免两条路径的不变量慢慢分叉。
Result<void> validate(const TrackDetails &details)
{
    if (details.title.empty()) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "曲目标题不能为空"});
    }
    if (details.filePath.empty()) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "曲目文件路径不能为空"});
    }
    if (details.durationMs < 0) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "曲目时长不能为负"});
    }
    if (details.trackNumber < 0) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "轨号不能为负"});
    }
    if (details.discNumber < 0) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "碟号不能为负"});
    }
    if (details.fileSizeBytes < 0) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "文件大小不能为负"});
    }
    if (details.fileMtimeUnix < 0) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "文件时间戳不能为负"});
    }
    return Result<void>::ok();
}

TrackDetails normalized(TrackDetails details)
{
    details.title = trimTitle(details.title);
    return details;
}

}  // namespace

std::string trimTitle(std::string_view text)
{
    // 规则住在 Kernel：导入侧（adapters）与领域侧（这里）必须用同一套空白定义，
    // 否则会出现"标签里带全角空格的标题被存成非空"这种只在数据里才看得见的错。
    return kernel::trimSpaceCopy(text);
}

Track::Track(TrackId id, TrackDetails details) : id_(id), details_(std::move(details)) {}

Result<Track> Track::add(TrackDetails details)
{
    details = normalized(std::move(details));
    if (auto result = validate(details); !result) {
        return Result<Track>::fail(result.error());
    }
    return Result<Track>::ok(Track{TrackId{}, std::move(details)});
}

Result<Track> Track::rehydrate(TrackId id, TrackDetails details)
{
    if (!id.isValid()) {
        return Result<Track>::fail(Error{ErrorCode::InvalidArgument, "已持久化的曲目必须有有效 id"});
    }

    details = normalized(std::move(details));
    if (auto result = validate(details); !result) {
        return Result<Track>::fail(result.error());
    }
    return Result<Track>::ok(Track{id, std::move(details)});
}

Result<void> Track::rename(std::string_view newTitle)
{
    const std::string trimmed = trimTitle(newTitle);
    if (trimmed.empty()) {
        return Result<void>::fail(Error{ErrorCode::InvalidArgument, "曲目标题不能为空"});
    }
    details_.title = trimmed;
    return Result<void>::ok();
}

}  // namespace sonora::catalog::core
