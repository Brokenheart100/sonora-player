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

#include "sonora/kernel/Result.h"

namespace sonora::lyrics::core {

// 找到的一份歌词原文（还没解析）。
struct LyricsDocument {
    // 人读的来源说明，例如 `同名 .lrc` / `内嵌标签`。会原样显示在界面上，
    // 用户看到"歌词来自内嵌标签"就知道为什么改 .lrc 文件没反应。
    std::string source;
    std::string text;
    // 在线那份还没取回来。链子见到它就停在原地（后面的来源本来也轮不到），
    // handler 据此告诉界面"再等一秒"，而不是"这首歌没词"。
    bool pending = false;
    // 这份词是不是在线来的（界面上标"在线歌词"）。
    bool online = false;
};

// 一次"找歌词"的全部线索。
//
// 只有在线来源用得到 `onlineSongId`：本地文件靠路径就能定到同名 .lrc 与内嵌标签，
// 而在线歌词只知道"这是哪首歌"。两个都存在的时候，谁先谁后是链子的事。
struct LyricsRequest {
    std::string_view audioFilePath;
    std::string_view onlineSongId;
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

    // 带全部线索的版本。默认丢掉 `onlineSongId`、退回只看路径的旧实现——
    // 这样本地两个来源不必各写一遍转发，只有在线来源覆写它。
    [[nodiscard]] virtual kernel::Result<std::optional<LyricsDocument>>
    load(const LyricsRequest &request) const
    {
        return load(request.audioFilePath);
    }
};

}  // namespace sonora::lyrics::core
