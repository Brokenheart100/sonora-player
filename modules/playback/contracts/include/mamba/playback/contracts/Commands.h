// 播放器对外暴露的两种东西：命令（改状态）与快照（读状态）。
//
// 注意快照里**没有位置**。位置走 IPositionStream 旁路，不进这条链路——
// 一旦进来，30Hz × 每次一个 DTO 会让总线变成计时器。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mamba/kernel/TypedId.h>
#include <mamba/playback/contracts/AudioOutput.h>
#include <mamba/playback/contracts/PlaybackMode.h>

namespace mamba::playback::contracts {

/// 队列里的一项。字段是**扁平拷贝**，不是 Catalog 的聚合——
/// Playback 不需要知道 Track 有什么不变量，它只需要"放哪首、叫什么"。
struct QueueEntry {
    std::uint64_t trackId = 0;
    std::string filePath;   // UTF-8
    std::string title;
    std::string artistName;
};

/// 一整个列表交给播放器：点第 3 行，就是把整个列表从第 3 行开始播。
/// （只发这一首会让"下一首"无处可去。）
struct PlayTracks {
    std::vector<QueueEntry> entries;
    std::int32_t startIndex = 0;
};

struct TogglePlayPause {};
struct SeekTo {
    std::int64_t positionMs = 0;
};
struct SetVolume {
    std::int32_t value = 100;
};
struct PlayNext {};
struct PlayPrevious {};

/// 换播放模式。没给 mode 的字段不要——界面只发"换成哪个"，
/// "下一个是什么"这种顺序规则属于领域（contracts::nextMode）。
struct SetPlaybackMode {
    PlaybackMode mode = PlaybackMode::Sequential;
};

/// 在队列面板里点某一项（用 trackId 而不是下标：下标是播放顺序里的位置，
/// 随机模式下和用户在面板里看到的行号不是一回事）。
struct JumpToTrack {
    std::uint64_t trackId = 0;
};

/// 只读快照。ViewModel 拿它画界面，拿不到聚合。
struct PlayerSnapshot {
    bool hasTrack = false;
    std::uint64_t trackId = 0;
    std::string title;
    std::string artistName;
    std::int64_t durationMs = 0;
    std::int32_t volume = 100;
    PlaybackState state = PlaybackState::Idle;
    PlaybackMode mode = PlaybackMode::Sequential;
    std::int32_t queueIndex = -1;
    std::int32_t queueSize = 0;
    // 当前曲目在磁盘上的位置。歌词要靠它找同名 .lrc —— 歌词模块不认识曲库，
    // 表现层也不能为了一个路径去加载曲目聚合，所以让快照把它带出来。
    std::string filePath;
};

}  // namespace mamba::playback::contracts
