#include "sonora/lyrics/adapters/LyricsSourceChain.h"

namespace sonora::lyrics::adapters {

kernel::Result<std::optional<core::LyricsDocument>>
LyricsSourceChain::load(const core::LyricsRequest &request) const
{
    for (const core::ILyricsSource *source : sources_) {
        auto document = source->load(request);
        if (!document) {
            return kernel::Result<std::optional<core::LyricsDocument>>::fail(document.error());
        }
        if (document.value().has_value()) {
            // "还没到"也是确定的答案：别再往下问，让界面等那一秒。
            return document;
        }
    }
    return kernel::Result<std::optional<core::LyricsDocument>>::ok(std::nullopt);
}

}  // namespace sonora::lyrics::adapters
