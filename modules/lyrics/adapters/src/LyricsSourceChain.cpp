#include "sonora/lyrics/adapters/LyricsSourceChain.h"

namespace sonora::lyrics::adapters {

kernel::Result<std::optional<core::LyricsDocument>>
LyricsSourceChain::load(std::string_view audioFilePath) const
{
    for (const core::ILyricsSource *source : sources_) {
        auto document = source->load(audioFilePath);
        if (!document) {
            return kernel::Result<std::optional<core::LyricsDocument>>::fail(document.error());
        }
        if (document.value().has_value()) {
            return document;
        }
    }
    return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
}

}  // namespace sonora::lyrics::adapters
