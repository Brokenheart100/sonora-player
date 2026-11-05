#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mamba/playback/contracts/Commands.h>

namespace mamba::playback::contracts {

/// 读播放器状态。返回类型是 PlayerSnapshot。
struct GetPlayerSnapshot {};

/// 队列面板用的一行。**按播放顺序**给出，不是按 entries 顺序——
/// 随机模式下用户看到的"接下来放什么"必须是真顺序。
struct QueueItemView {
    std::uint64_t trackId = 0;
    std::string title;
    std::string artistName;
    bool current = false;
};

struct QueueView {
    PlaybackMode mode = PlaybackMode::Sequential;
    std::int32_t currentIndex = -1; // 在 items 里的位置
    std::vector<QueueItemView> items;
};

struct GetPlayQueue {};

} // namespace mamba::playback::contracts
