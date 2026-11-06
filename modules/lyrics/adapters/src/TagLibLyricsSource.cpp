#include "sonora/lyrics/adapters/TagLibLyricsSource.h"

#include <array>
#include <optional>
#include <string>
#include <system_error>

#include <taglib/fileref.h>
#include <taglib/tfile.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>

#include "PathText.h"

namespace sonora::lyrics::adapters {
namespace {

// 优先 `LYRICS`（ID3v2 标准），其次 `UNSYNCEDLYRICS`（老名字），
// 最后 `SYNCEDLYRICS`（带时间戳的那类）。顺序就是"信息量从高到低"。
constexpr std::array<std::string_view, 3> kKeys = {"LYRICS", "UNSYNCEDLYRICS", "SYNCEDLYRICS"};

}  // namespace

kernel::Result<std::optional<core::LyricsDocument>>
TagLibLyricsSource::load(std::string_view audioFilePath) const
{
    if (audioFilePath.empty()) {
        return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
    }

    const std::filesystem::path path = detail::u8Path(std::string(audioFilePath));
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
    }

    // FileName 在 Windows 上是个类：传 path.c_str()（wchar_t*）才按宽字符打开。
    // 传 UTF-8 的 char* 会被当本地代码页解释，中文文件名直接打不开且不报错。
    const TagLib::FileRef ref(TagLib::FileName{path.c_str()}, true, TagLib::AudioProperties::Fast);
    if (ref.isNull() || ref.file() == nullptr) {
        return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
    }

    const TagLib::PropertyMap properties = ref.file()->properties();
    for (const std::string_view key : kKeys) {
        const TagLib::String tagKey{std::string(key), TagLib::String::UTF8};
        if (!properties.contains(tagKey)) {
            continue;
        }
        const TagLib::StringList values = properties.value(tagKey);
        if (values.isEmpty()) {
            continue;
        }
        const std::string text = values.front().to8Bit(true);
        if (text.empty()) {
            continue;
        }
        core::LyricsDocument document;
        document.text = text;
        document.source = "内嵌标签 " + std::string(key);
        return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::move(document));
    }

    return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
}

}  // namespace sonora::lyrics::adapters
