#include "mamba/catalog/adapters/TagLibMetadataReader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tbytevector.h>
#include <taglib/tfile.h>
#include <taglib/tmap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>
#include <taglib/tvariant.h>

namespace mamba::catalog::adapters {
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

std::string trim(std::string text)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

// TagLib::String 到 std::string 一律走 UTF-8 —— 库里存的是 UTF-8，
// 中间任何一次 toCString() 都会在中文标题上按本地代码页截断。
std::string toUtf8(const TagLib::String &text)
{
    return text.isEmpty() ? std::string{} : text.to8Bit(true);
}

// 内嵌封面。TagLib 2 把这块统一成"复杂属性"，PICTURE 是其中固定的一项。
//
// 不直接用 ID3v2::AttachedPictureFrame 之类：那是格式专属接口，
// FLAC / MP4 / APE 都会漏。complexProperty 是跨格式的。
std::vector<std::uint8_t> readCover(TagLib::File *file)
{
    if (file == nullptr) {
        return {};
    }
    const TagLib::StringList keys = file->complexPropertyKeys();
    if (!keys.contains("PICTURE")) {
        return {};
    }
    const TagLib::List<TagLib::VariantMap> pictures = file->complexProperties("PICTURE");
    for (const auto &picture : pictures) {
        if (!picture.contains("data")) {
            continue;
        }
        bool ok = false;
        const TagLib::ByteVector bytes = picture.value("data").toByteVector(&ok);
        if (!ok || bytes.isEmpty()) {
            continue;
        }
        std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes.size()));
        std::copy(bytes.data(), bytes.data() + bytes.size(), data.begin());
        return data;
    }
    return {};
}

}  // namespace

kernel::Result<core::TrackMetadata> TagLibMetadataReader::read(std::string_view filePath) const
{
    const auto path = toPath(filePath);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return kernel::Result<core::TrackMetadata>::fail(
            kernel::Error{kernel::ErrorCode::NotFound, "文件不存在：" + std::string(filePath)});
    }

    core::TrackMetadata metadata;
    const auto size = fs::file_size(path, ec);
    metadata.fileSizeBytes = ec ? 0 : static_cast<std::int64_t>(size);

    // Windows 上 TagLib::FileName 不是 const char*，而是一个能把 wchar_t* 和 char* 都收下的类。
    // 它的 char* 构造按**本地 8 位编码**（中文机器上是 GBK）解释，直接喂 UTF-8 字节，
    // 文件名里有汉字就打不开文件——症状是"标签读不出来、时长 0"，而且不报错。
    // std::filesystem::path 在 Windows 内部就是宽字符，path.c_str() 正好是 wchar_t*。
#if defined(_WIN32)
    const TagLib::FileRef ref(TagLib::FileName{path.c_str()}, true, TagLib::AudioProperties::Fast);
#else
    const std::string nativePath = toUtf8(path);
    const TagLib::FileRef ref(TagLib::FileName{nativePath.c_str()}, true,
                              TagLib::AudioProperties::Fast);
#endif
    if (!ref.isNull()) {
        if (const TagLib::Tag *tag = ref.tag(); tag != nullptr) {
            metadata.title = trim(toUtf8(tag->title()));
            metadata.artist = trim(toUtf8(tag->artist()));
            metadata.album = trim(toUtf8(tag->album()));
        }
        if (const TagLib::AudioProperties *audio = ref.audioProperties(); audio != nullptr) {
            metadata.durationMs = audio->lengthInMilliseconds();
        }
        // 封面单独再开一次属性表，但用的是同一个已打开的文件对象，不额外 IO。
        metadata.coverData = readCover(ref.file());
    }

    if (metadata.title.empty()) {
        // 没有标题的文件比"一行空白"更常见（尤其是抓轨和流媒体缓存）。
        const auto stem = toUtf8(path.stem());
        metadata.title = stem.empty() ? toUtf8(path.filename()) : stem;
    }
    return kernel::Result<core::TrackMetadata>::ok(std::move(metadata));
}

}  // namespace mamba::catalog::adapters
