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

#include "sonora/kernel/Result.h"
#include "sonora/lyrics/core/ports/Ports.h"

namespace sonora::lyrics::adapters {

class LyricsSourceChain final : public core::ILyricsSource {
public:
    // 不接管所有权：这些 source 由 host 持有，活得比链长。
    void add(const core::ILyricsSource &source) { sources_.push_back(&source); }

    [[nodiscard]] std::size_t size() const noexcept { return sources_.size(); }

    [[nodiscard]] kernel::Result<std::optional<core::LyricsDocument>>
    load(const core::LyricsRequest &request) const override;

    // 只给路径的旧形态：测试与"只想验本地链子"的调用方用。
    [[nodiscard]] kernel::Result<std::optional<core::LyricsDocument>>
    load(std::string_view audioFilePath) const override
    {
        return load(core::LyricsRequest{audioFilePath, std::string_view{}});
    }

private:
    std::vector<const core::ILyricsSource *> sources_;
};

}  // namespace sonora::lyrics::adapters
