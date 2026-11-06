// 把 PlaybackService 挂到两条总线上。
//
// 每个 Handler 只是一层薄壳：校验 → 转调服务。薄到什么程度算够？
// 薄到"如果哪天换掉总线，删掉这个文件不影响任何业务逻辑"。
#pragma once

#include <memory>

#include <sonora/kernel/CommandBus.h>
#include <sonora/kernel/QueryBus.h>
#include <sonora/playback/contracts/Commands.h>
#include <sonora/playback/contracts/Queries.h>
#include <sonora/playback/core/app/PlaybackService.h>

namespace sonora::playback::core {

class PlayTracksHandler {
public:
    explicit PlayTracksHandler(PlaybackService& service) : service_(service) {}
    kernel::Result<void> handle(const contracts::PlayTracks& command) {
        return service_.playTracks(command);
    }

private:
    PlaybackService& service_;
};

class TogglePlayPauseHandler {
public:
    explicit TogglePlayPauseHandler(PlaybackService& service) : service_(service) {}
    kernel::Result<void> handle(const contracts::TogglePlayPause&) {
        return service_.togglePlayPause();
    }

private:
    PlaybackService& service_;
};

class SeekToHandler {
public:
    explicit SeekToHandler(PlaybackService& service) : service_(service) {}
    kernel::Result<void> handle(const contracts::SeekTo& command) {
        return service_.seekTo(command.positionMs);
    }

private:
    PlaybackService& service_;
};

class SetVolumeHandler {
public:
    explicit SetVolumeHandler(PlaybackService& service) : service_(service) {}
    kernel::Result<void> handle(const contracts::SetVolume& command) {
        return service_.setVolume(command.value);
    }

private:
    PlaybackService& service_;
};

class PlayNextHandler {
public:
    explicit PlayNextHandler(PlaybackService& service) : service_(service) {}
    kernel::Result<void> handle(const contracts::PlayNext&) { return service_.next(); }

private:
    PlaybackService& service_;
};

class PlayPreviousHandler {
public:
    explicit PlayPreviousHandler(PlaybackService& service) : service_(service) {}
    kernel::Result<void> handle(const contracts::PlayPrevious&) { return service_.previous(); }

private:
    PlaybackService& service_;
};

class SetPlaybackModeHandler {
public:
    explicit SetPlaybackModeHandler(PlaybackService& service) : service_(service) {}
    kernel::Result<void> handle(const contracts::SetPlaybackMode& command) {
        return service_.setMode(command.mode);
    }

private:
    PlaybackService& service_;
};

class JumpToTrackHandler {
public:
    explicit JumpToTrackHandler(PlaybackService& service) : service_(service) {}
    kernel::Result<void> handle(const contracts::JumpToTrack& command) {
        return service_.jumpTo(command.trackId);
    }

private:
    PlaybackService& service_;
};

class GetPlayQueueHandler {
public:
    explicit GetPlayQueueHandler(const PlaybackService& service) : service_(service) {}
    kernel::Result<contracts::QueueView> handle(const contracts::GetPlayQueue&) const {
        return kernel::Result<contracts::QueueView>::ok(service_.queueView());
    }

private:
    const PlaybackService& service_;
};

class GetPlayerSnapshotHandler {
public:
    explicit GetPlayerSnapshotHandler(const PlaybackService& service) : service_(service) {}
    kernel::Result<contracts::PlayerSnapshot> handle(const contracts::GetPlayerSnapshot&) const {
        return kernel::Result<contracts::PlayerSnapshot>::ok(service_.snapshot());
    }

private:
    const PlaybackService& service_;
};

/// 组合根调用一次即可。放在这里而不是 host：注册表属于模块，host 只负责"叫一声"。
///
/// 参数是**具体**的 CommandBus / QueryBus 而不是接口：`on()` 是注册能力，
/// 只有实现类有；上层拿到的 ICommandBus 只该有 dispatch。
inline void registerPlayback(kernel::CommandBus& commands, kernel::QueryBus& queries,
                             PlaybackService& service) {
    // 用 shared_ptr 而不是值：总线的注册接口收的是"带 handle() 的对象"，
    // 值语义的 handler 会走另一个只认可调用对象的 overload。
    auto tracks = std::make_shared<PlayTracksHandler>(service);
    auto toggle = std::make_shared<TogglePlayPauseHandler>(service);
    auto seek = std::make_shared<SeekToHandler>(service);
    auto volume = std::make_shared<SetVolumeHandler>(service);
    auto next = std::make_shared<PlayNextHandler>(service);
    auto previous = std::make_shared<PlayPreviousHandler>(service);
    auto mode = std::make_shared<SetPlaybackModeHandler>(service);
    auto jump = std::make_shared<JumpToTrackHandler>(service);
    auto snapshot = std::make_shared<GetPlayerSnapshotHandler>(service);
    auto queue = std::make_shared<GetPlayQueueHandler>(service);

    commands.on<contracts::PlayTracks>(tracks);
    commands.on<contracts::TogglePlayPause>(toggle);
    commands.on<contracts::SeekTo>(seek);
    commands.on<contracts::SetVolume>(volume);
    commands.on<contracts::PlayNext>(next);
    commands.on<contracts::PlayPrevious>(previous);
    commands.on<contracts::SetPlaybackMode>(mode);
    commands.on<contracts::JumpToTrack>(jump);

    // QueryBus 只收可调用对象（没有 shared_ptr 的 handler overload），所以这里包一层 lambda。
    queries.on<contracts::GetPlayerSnapshot, contracts::PlayerSnapshot>(
        [snapshot](const contracts::GetPlayerSnapshot& query) { return snapshot->handle(query); });
    queries.on<contracts::GetPlayQueue, contracts::QueueView>(
        [queue](const contracts::GetPlayQueue& query) { return queue->handle(query); });
}

}  // namespace sonora::playback::core
