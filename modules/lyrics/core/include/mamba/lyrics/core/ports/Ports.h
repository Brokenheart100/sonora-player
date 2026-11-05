// Lyrics 模块的端口。
//
// 只有一个：**去哪儿找歌词文本**。它被声明在 core 里、由 adapters 实现，
// 因为"歌词从哪来"是技术细节（同名 .lrc？内嵌标签？将来的在线歌词？），
// 而"拿到文本之后怎么解释"才是这个模块的业务。
//
// 判据（docs/ARCHITECTURE.md §2.5）：把 adapters/ 删掉，Lyrics.Core 还得编得过。
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "mamba/kernel/Result.h"

namespace mamba::lyrics::core {

// 找到的一份歌词原文（还没解析）。
struct LyricsDocument {
    // 人读的来源说明，例如 `同名 .lrc` / `内嵌标签`。会原样显示在界面上，
    // 用户看到"歌词来自内嵌标签"就知道为什么改 .lrc 文件没反应。
    std::string source;
    std::string text;
};

class ILyricsSource {
public:
    ILyricsSource() = default;
    ILyricsSource(const ILyricsSource &) = default;
    ILyricsSource &operator=(const ILyricsSource &) = default;
    ILyricsSource(ILyricsSource &&) = default;
    ILyricsSource &operator=(ILyricsSource &&) = default;
    virtual ~ILyricsSource() = default;

    // 找不到歌词返回 `std::nullopt`——**不是错误**，"这首歌没有歌词"是常态。
    // 只有"本该能读却读坏了"（编码不对、权限不足）才返回 Error。
    [[nodiscard]] virtual kernel::Result<std::optional<LyricsDocument>>
    load(std::string_view audioFilePath) const = 0;
};

}  // namespace mamba::lyrics::core
