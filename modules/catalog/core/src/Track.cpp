#include "mamba/catalog/core/domain/Track.h"

#include <string_view>
#include <utility>

namespace mamba::catalog::core {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

namespace {

bool isBlank(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// 全角空格在中文标题里很常见（从网页复制来的），只 trim ASCII 会把
// "　夜航西飞" 当成非空标题存进去，然后列表里出现一行看起来是空的记录。
bool isFullWidthSpaceAt(std::string_view text, std::size_t index) noexcept
{
    // U+3000 的 UTF-8 是 E3 80 80
    return index + 2 < text.size() && static_cast<unsigned char>(text[index]) == 0xE3
           && static_cast<unsigned char>(text[index + 1]) == 0x80
           && static_cast<unsigned char>(text[index + 2]) == 0x80;
}

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
    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end) {
        if (isBlank(text[begin])) {
            ++begin;
        } else if (isFullWidthSpaceAt(text, begin)) {
            begin += 3;
        } else {
            break;
        }
    }

    while (end > begin) {
        // 从后往前找，多字节只能整段退，所以先在 [begin, end) 里定位最后一个
        // 非空白字节，再判断它是不是全角空格的结尾。
        const std::size_t last = end - 1;
        if (isBlank(text[last])) {
            end = last;
            continue;
        }
        if (end >= begin + 3 && isFullWidthSpaceAt(text, end - 3)) {
            end -= 3;
            continue;
        }
        break;
    }

    return std::string{text.substr(begin, end - begin)};
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

}  // namespace mamba::catalog::core
