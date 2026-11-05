// 「给我这首歌的歌词」——一个查询 handler。
//
// 它做的事只有三件：问端口要文本、交给解析器、把结果翻成契约形态。
// 时间轴、二分、高亮全在下游（Lyrics 聚合 + 表现层的模型）。
#pragma once

#include <string>

#include "mamba/lyrics/contracts/Queries.h"
#include "mamba/lyrics/contracts/LyricsDto.h"
#include "mamba/lyrics/core/ports/Ports.h"
#include "mamba/kernel/Result.h"

namespace mamba::lyrics::core {

class GetLyricsHandler {
public:
    explicit GetLyricsHandler(const ILyricsSource &source) : source_(source) {}

    [[nodiscard]] kernel::Result<contracts::LyricsView> handle(
        const contracts::GetLyrics &query) const;

private:
    const ILyricsSource &source_;
};

}  // namespace mamba::lyrics::core
