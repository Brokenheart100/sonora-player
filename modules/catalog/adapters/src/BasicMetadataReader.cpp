#include "sonora/catalog/adapters/BasicMetadataReader.h"

#include "sonora/kernel/Text.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace sonora::catalog::adapters {
namespace {

namespace fs = std::filesystem;

fs::path toPath(std::string_view utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string toUtf8(const fs::path &path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

}  // namespace

kernel::Result<core::TrackMetadata> BasicMetadataReader::read(std::string_view filePath) const
{
    const auto path = toPath(filePath);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return kernel::Result<core::TrackMetadata>::fail(
            kernel::Error{kernel::ErrorCode::NotFound, "文件不存在：" + std::string(filePath)});
    }

    const auto size = fs::file_size(path, ec);
    const auto stem = toUtf8(path.stem());

    core::TrackMetadata metadata;
    metadata.fileSizeBytes = ec ? 0 : static_cast<std::int64_t>(size);

    // "艺术家 - 标题"：分隔符有三种（半角、全角、破折号），中文歌名里都常见。
    const auto separator = stem.find(" - ");
    if (separator != std::string::npos) {
        metadata.artist = kernel::trimSpaceCopy(stem.substr(0, separator));
        metadata.title = kernel::trimSpaceCopy(stem.substr(separator + 3));
    }
    if (metadata.title.empty()) {
        // 一首叫 "01.flac" 的歌比列表里一行空白强，也比"导入失败"强。
        metadata.title = stem.empty() ? toUtf8(path.filename()) : stem;
    }
    // 时长暂时读不出来（要解码才准）。给 0 而不是猜一个数：界面上显示 "--:--"
    // 是诚实的，显示一个错的时长会让用户以为文件坏了。
    metadata.durationMs = 0;
    return kernel::Result<core::TrackMetadata>::ok(std::move(metadata));
}

}  // namespace sonora::catalog::adapters
