// PlaybackService 的单元测试。
//
// 全程不碰真实音频：FakeAudioOutput 把"装载/播放/暂停/seek/播完"这些事件
// 变成可以直接调用的方法，于是"播完自动下一首""next 到队尾返回 NotFound"
// 这种在真机上要等 3 分钟的路径，这里一行就能走到。
#include "doctest_support.h"

#include <cstdint>
#include <string>
#include <vector>

#include "mamba/contracts/PlaybackEvents.h"
#include "mamba/kernel/Error.h"
#include "mamba/kernel/EventBus.h"
#include "mamba/playback/contracts/AudioOutput.h"
#include "mamba/playback/contracts/Commands.h"
#include "mamba/playback/core/app/PlaybackService.h"

#include "fake_audio_output.h"

namespace {

using mamba::kernel::ErrorCode;
using mamba::playback::contracts::PlaybackState;
using mamba::playback::contracts::PlayTracks;
using mamba::playback::contracts::QueueEntry;
using mambatest::FakeAudioOutput;

QueueEntry entry(std::uint64_t id, std::string path, std::string title) {
    QueueEntry e;
    e.trackId = id;
    e.filePath = std::move(path);
    e.title = std::move(title);
    return e;
}

PlayTracks playCommand(std::vector<QueueEntry> entries, std::int32_t startIndex = 0) {
    PlayTracks command;
    command.entries = std::move(entries);
    command.startIndex = startIndex;
    return command;
}

std::vector<QueueEntry> threeTracks() {
    return {entry(1, "C:/m/a.flac", "甲"), entry(2, "C:/m/b.mp3", "乙"),
            entry(3, "C:/m/c.wav", "丙")};
}

}  // namespace

TEST_CASE("播放第一条：装载 + play + 快照") {
    FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};

    const auto result = service.playTracks(playCommand(threeTracks(), 1));
    REQUIRE(result.hasValue());

    CHECK(output.loadedPath == "C:/m/b.mp3");
    CHECK(output.playCount == 1);
    CHECK(output.state() == PlaybackState::Playing);

    const auto snapshot = service.snapshot();
    CHECK(snapshot.hasTrack);
    CHECK(snapshot.trackId == 2U);
    CHECK(snapshot.title == "乙");
    CHECK(snapshot.queueIndex == 1);
    CHECK(snapshot.queueSize == 3);
    CHECK(snapshot.durationMs == 180000);
    CHECK(snapshot.state == PlaybackState::Playing);
}

TEST_CASE("空队列是参数错误，不是崩溃") {
    FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};

    const auto result = service.playTracks(playCommand({}));
    REQUIRE(!result.hasValue());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
    CHECK(output.playCount == 0);
    CHECK(service.snapshot().hasTrack == false);
}

TEST_CASE("toggle 在播放与暂停之间切换") {
    FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};
    REQUIRE(service.playTracks(playCommand(threeTracks())).hasValue());

    CHECK(service.togglePlayPause().hasValue());
    CHECK(output.state() == PlaybackState::Paused);

    CHECK(service.togglePlayPause().hasValue());
    CHECK(output.state() == PlaybackState::Playing);
}

TEST_CASE("seek 会被夹到 [0, duration]") {
    FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};
    REQUIRE(service.playTracks(playCommand(threeTracks())).hasValue());

    CHECK(service.seekTo(-5000).hasValue());
    CHECK(output.posMs == 0);

    CHECK(service.seekTo(999999).hasValue());
    CHECK(output.posMs == output.duration);
}

TEST_CASE("next 走到队尾返回 NotFound 并停下") {
    FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};
    REQUIRE(service.playTracks(playCommand(threeTracks(), 2)).hasValue());

    const auto result = service.next();
    REQUIRE(!result.hasValue());
    CHECK(result.error().code() == ErrorCode::NotFound);
    CHECK(output.stopCount >= 1);
}

TEST_CASE("previous 在第一首时回到开头") {
    FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};
    REQUIRE(service.playTracks(playCommand(threeTracks(), 0)).hasValue());

    const auto before = output.seekCount;
    CHECK(service.previous().hasValue());
    CHECK(output.seekCount == before + 1);
    CHECK(output.posMs == 0);
}

TEST_CASE("播完自动下一首") {
    FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};
    REQUIRE(service.playTracks(playCommand(threeTracks())).hasValue());

    output.finishCurrent();

    CHECK(output.loadedPath == "C:/m/b.mp3");
    CHECK(service.snapshot().queueIndex == 1);

    // 最后一首播完就停在原地，不再往后跳
    output.finishCurrent();
    output.finishCurrent();
    CHECK(service.snapshot().queueIndex == 2);
}

TEST_CASE("音量夹到 0..100") {
    FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};

    CHECK(service.setVolume(-20).hasValue());
    CHECK(output.volume() == 0);
    CHECK(service.snapshot().volume == 0);

    CHECK(service.setVolume(500).hasValue());
    CHECK(output.volume() == 100);
    CHECK(service.snapshot().volume == 100);
}

TEST_CASE("装载失败会冒泡成错误，而不是假装在播") {
    FakeAudioOutput output;
    output.failLoad = true;
    mamba::playback::core::PlaybackService service{output};

    const auto result = service.playTracks(playCommand(threeTracks()));
    REQUIRE(!result.hasValue());
    CHECK(result.error().code() == ErrorCode::Unavailable);
    CHECK(output.playCount == 0);
}

TEST_CASE("播完发出 TrackFinished：trackId 是被放完的那一首")
{
    mambatest::FakeAudioOutput output;
    mamba::kernel::EventBus events;

    std::vector<std::uint64_t> finishedTracks;
    events.subscribe<mamba::contracts::TrackFinished>(
        [&finishedTracks](const mamba::contracts::TrackFinished &event)
        { finishedTracks.push_back(event.trackId); });

    // 事件指针放最后一位；queue_ 之外什么都不用改。
    mamba::playback::core::PlaybackService service{output, nullptr, nullptr, &events};

    mamba::playback::contracts::PlayTracks command;
    command.entries = {
        {1, "C:/m/a.flac", "A", "X"},
        {2, "C:/m/b.flac", "B", "X"},
    };
    REQUIRE(service.playTracks(command));

    output.finishCurrent();
    // 事件在推进队列之前发，所以记的是"刚放完的这首"，不是下一首。
    REQUIRE(finishedTracks.size() == 1);
    CHECK(finishedTracks.front() == 1);

    output.finishCurrent();
    REQUIRE(finishedTracks.size() == 2);
    CHECK(finishedTracks.back() == 2);

    // 队列播完（最后一首放完）也要发事件：它确实被听完了。
    output.finishCurrent();
    CHECK(finishedTracks.size() == 3);
    CHECK(finishedTracks.back() == 2);
}

TEST_CASE("文件被删：自动推进跳过装不上的那首，不崩也不卡住")
{
    mambatest::FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};

    mamba::playback::contracts::PlayTracks command;
    command.entries = {
        {1, "C:/m/a.flac", "A", "X"},
        {2, "C:/m/gone.flac", "B", "X"},   // 播到它之前被删了
        {3, "C:/m/c.flac", "C", "X"},
    };
    REQUIRE(service.playTracks(command));

    output.unloadable.insert("C:/m/gone.flac");
    output.finishCurrent();   // a.flac 放完 → 下一首 gone.flac 装不上 → 应该落到 c.flac

    CHECK(output.loadedPath == "C:/m/c.flac");
    CHECK(service.snapshot().queueIndex == 2);
    CHECK(service.snapshot().state == mamba::playback::contracts::PlaybackState::Playing);
}

TEST_CASE("整个队列都坏掉：停下来，而不是无限重试")
{
    mambatest::FakeAudioOutput output;
    mamba::playback::core::PlaybackService service{output};

    mamba::playback::contracts::PlayTracks command;
    command.entries = {
        {1, "C:/m/a.flac", "A", "X"},
        {2, "C:/m/b.flac", "B", "X"},
    };
    REQUIRE(service.playTracks(command));

    output.unloadable.insert("C:/m/b.flac");
    output.unloadable.insert("C:/m/a.flac");
    output.finishCurrent();

    // 两首都装不上：停掉输出，state 不该停在 Playing。
    CHECK(output.loadedPath == "C:/m/a.flac");   // 第一首是开始播放前就装好的
    CHECK(service.snapshot().state != mamba::playback::contracts::PlaybackState::Playing);
    CHECK(output.stopCount > 0);
}
