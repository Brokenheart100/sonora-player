// 按优先级串起多个来源。
//
// 为什么要有它：来源不止一个（同名 .lrc、内嵌标签、将来的在线歌词），
// 而"先看哪个"是**产品决定**，不该硬编码在 GetLyricsHandler 里——
// handler 只该认识一个 ILyricsSource。串接本身也是端口实现，所以它住在适配器层。
//
// 语义：按加入顺序试，第一个给出文本的赢；"找不到"继续试下一个；
// **出错立刻停**（编码坏了就别假装没有歌词，用户需要看到那句话）。
#pragma once

#include <string_view>
#include <vector>

#include "mamba/kernel/Result.h"
#include "mamba/lyrics/core/ports/Ports.h"

namespace mamba::lyrics::adapters {

class LyricsSourceChain final : public core::ILyricsSource {
public:
    // 不接管所有权：这些 source 由 host 持有，活得比链长。
    void add(const core::ILyricsSource &source) { sources_.push_back(&source); }

    [[nodiscard]] std::size_t size() const noexcept { return sources_.size(); }

    [[nodiscard]] kernel::Result<std::optional<core::LyricsDocument>>
    load(std::string_view audioFilePath) const override;

private:
    std::vector<const core::ILyricsSource *> sources_;
};

}  // namespace mamba::lyrics::adapters
