// 同名 .lrc：歌词的第一来源，也是现实中最常见的一类。
//
// 只做一件事——"音频文件旁边有没有一个同名 .lrc"。解码规则见 PathText.h 顶部。
#pragma once

#include <string_view>

#include "mamba/kernel/Result.h"
#include "mamba/lyrics/core/ports/Ports.h"

namespace mamba::lyrics::adapters {

class SidecarLyricsSource final : public core::ILyricsSource {
public:
    [[nodiscard]] kernel::Result<std::optional<core::LyricsDocument>>
    load(std::string_view audioFilePath) const override;
};

}  // namespace mamba::lyrics::adapters
