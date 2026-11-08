// 在线歌词：链子的第三来源（N3）。
//
// 为什么它住在这里而不是 Online 模块：**"歌词从哪来"是歌词模块的事**，
// 在线只是一个新来源。反过来让 online 去实现 ILyricsSource，就得让
// Online 依赖 Lyrics.Core——那是把两个业务模块焊在一起，方向还错了。
//
// 依赖的是 `Online.Contracts` 的查询（模块 → 契约合法，见 AGENTS.md 规矩 3/4），
// 不是 Online.Core / Online.Adapters：这条边只要求"有个东西能回答
// FetchOnlineLyrics"，至于它是真接口、假 transport 还是录好的 fixture，
// 歌词模块不需要知道。
//
// 三态语义（对应 contracts::OnlineLyricDto 的 ready）：
//   - 还没取回来 → 交一份 `pending` 的文档，链子停在原地，界面一秒后再问；
//   - 取回了有词 → 原样交出去，`source = "在线歌词"`；
//   - 取回了但没词（纯音乐/未收录/查询失败）→ 交 `nullopt`，链子正常结束。
//     这里**不返回 Error**：本地没有词、在线也没有，就是"这首歌没词"。
#pragma once

#include <optional>
#include <string_view>

#include "sonora/kernel/IQueryBus.h"
#include "sonora/kernel/Result.h"
#include "sonora/lyrics/core/ports/Ports.h"

namespace sonora::lyrics::adapters {

class OnlineLyricsSource final : public core::ILyricsSource {
public:
    explicit OnlineLyricsSource(kernel::IQueryBus &queries) : queries_(queries) {}

    // 只在路径里找词是没有意义的（在线曲目的"路径"是 CDN 直链），
    // 所以旧的单参数形态一律"没找到"，真正的入口是下面那个带 LyricsRequest 的。
    [[nodiscard]] kernel::Result<std::optional<core::LyricsDocument>>
    load(std::string_view audioFilePath) const override;

    [[nodiscard]] kernel::Result<std::optional<core::LyricsDocument>>
    load(const core::LyricsRequest &request) const override;

private:
    kernel::IQueryBus &queries_;
};

}  // namespace sonora::lyrics::adapters
