// 内嵌歌词：第二来源。
//
// 有些文件把歌词塞进标签里（`LYRICS` 是 ID3v2 的标准帧，`UNSYNCEDLYRICS` 是
// 旧名字，部分写作工具用 `SYNCEDLYRICS` 存带时间的）。这里只负责取出来，
// 编码由 TagLib 的 `String::to8Bit(true)` 负责——它返回 UTF-8，省掉一堆猜。
#pragma once

#include <string_view>
#include <optional>

#include "sonora/kernel/Result.h"
#include "sonora/lyrics/core/ports/Ports.h"

namespace sonora::lyrics::adapters {

class TagLibLyricsSource final : public core::ILyricsSource {
public:
    [[nodiscard]] kernel::Result<std::optional<core::LyricsDocument>>
    load(std::string_view audioFilePath) const override;
};

}  // namespace sonora::lyrics::adapters
