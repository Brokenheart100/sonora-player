#include "sonora/lyrics/adapters/OnlineLyricsSource.h"

#include <utility>
#include <optional>

#include "sonora/online/contracts/Queries.h"
#include "sonora/online/contracts/Types.h"

namespace sonora::lyrics::adapters {
namespace {

using DocumentResult = kernel::Result<std::optional<core::LyricsDocument>>;

// 界面右上角那句来源说明。写死在这里而不是让调用方传：它是"这份词的出处"，
// 与 .lrc / 内嵌标签那两句并列，属于同一个层次的东西。
constexpr std::string_view kSource = "在线歌词";

}  // namespace

kernel::Result<std::optional<core::LyricsDocument>>
OnlineLyricsSource::load(std::string_view audioFilePath) const
{
    (void)audioFilePath;
    return DocumentResult::ok(std::nullopt);
}

kernel::Result<std::optional<core::LyricsDocument>>
OnlineLyricsSource::load(const core::LyricsRequest &request) const
{
    if (request.onlineSongId.empty()) {
        // 本地曲目：这一来源无事可做，交给链子的下一个（也没有下一个了）。
        return DocumentResult::ok(std::nullopt);
    }

    online::contracts::FetchOnlineLyrics query;
    query.songId = std::string(request.onlineSongId);
    // `ask` 在总线上不是 const（它要写审计日志、给 handler 记耗时）。这里的
    // 语义确实是只读的，所以转掉 const：接口本身没有副作用。
    const auto result =
        const_cast<kernel::IQueryBus &>(queries_).ask<online::contracts::OnlineLyricDto>(query);
    if (!result) {
        // 在线模块自己出问题不该让"看歌词"变成报错：当作没有，界面照常。
        return DocumentResult::ok(std::nullopt);
    }

    const online::contracts::OnlineLyricDto &dto = result.value();
    if (!dto.ready) {
        core::LyricsDocument waiting;
        waiting.source = std::string(kSource);
        waiting.pending = true;
        waiting.online = true;
        return DocumentResult::ok(std::move(waiting));
    }
    if (dto.lrc.empty()) {
        // 纯音乐 / 未收录 / 取词失败：三种都按"没有在线词"处理。
        return DocumentResult::ok(std::nullopt);
    }

    core::LyricsDocument document;
    document.source = std::string(kSource);
    document.online = true;
    document.text = dto.lrc;
    if (!dto.translated.empty()) {
        // 翻译按"同一时间戳再来一行"的形态接在后面：LrcParser 会把它并成
        // 上一行的 translation（与本地双语 LRC 完全同一条路径）。
        document.text += '\n';
        document.text += dto.translated;
    }
    return DocumentResult::ok(std::move(document));
}

}  // namespace sonora::lyrics::adapters
