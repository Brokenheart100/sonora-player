#include "sonora/lyrics/core/app/GetLyricsHandler.h"

#include <utility>
#include <vector>

#include "sonora/lyrics/core/domain/LrcParser.h"
#include "sonora/lyrics/core/domain/Lyrics.h"

namespace sonora::lyrics::core {
namespace {

using contracts::LyricLine;
using contracts::LyricsStatus;
using contracts::LyricsView;

// 纯音乐的占位。放在 core 而不是表现层，是因为"没有歌词时显示什么"
// 属于产品语义，不属于渲染细节——将来要做"这首歌是纯音乐版"的着色，
// 判断依据也在这里，别在界面里再写一遍字符串。
constexpr std::string_view kInstrumentalText = "纯音乐，请欣赏";

LyricsView instrumentalView(std::string source)
{
    LyricsView view;
    view.status = LyricsStatus::Instrumental;
    view.source = std::move(source);
    LyricLine placeholder;
    placeholder.timeMs = 0;
    placeholder.text = std::string(kInstrumentalText);
    view.lines.push_back(std::move(placeholder));
    return view;
}

}  // namespace

kernel::Result<contracts::LyricsView> GetLyricsHandler::handle(
    const contracts::GetLyrics &query) const
{
    if (query.filePath.empty()) {
        return kernel::Result<LyricsView>::fail(
            kernel::Error{kernel::ErrorCode::InvalidArgument, "没有给音频路径，找不了歌词"});
    }

    auto document = source_.load(query.filePath);
    if (!document) {
        return kernel::Result<LyricsView>::fail(document.error());
    }
    if (!document.value().has_value()) {
        LyricsView view;
        view.status = LyricsStatus::NotFound;
        return kernel::Result<LyricsView>::ok(std::move(view));
    }

    const LyricsDocument &found = *document.value();
    const ParsedLyrics parsed = LrcParser::parse(found.text);
    const Lyrics lyrics = Lyrics::create(parsed.entries);

    if (lyrics.empty()) {
        // 文件读到了、但一条时间标签都没有——可能是纯音乐，也可能是不认识的格式。
        // 两种都按"纯音乐"呈现：给用户一句可读的话，比一个空盒子有用。
        return kernel::Result<LyricsView>::ok(instrumentalView(found.source));
    }

    LyricsView view;
    view.status = LyricsStatus::Found;
    view.source = found.source;
    view.lines.reserve(lyrics.size());
    for (const LyricEntry &entry : lyrics.entries()) {
        LyricLine line;
        line.timeMs = entry.timeMs;
        line.text = entry.text;
        line.translation = entry.translation;
        line.words.reserve(entry.words.size());
        for (const LyricWord &word : entry.words) {
            line.words.push_back(contracts::LyricWord{word.timeMs, word.text});
        }
        view.lines.push_back(std::move(line));
    }
    return kernel::Result<LyricsView>::ok(std::move(view));
}

}  // namespace sonora::lyrics::core
