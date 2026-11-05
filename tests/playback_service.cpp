// PlaybackService 的单元测试。
//
// 全程不碰真实音频：FakeAudioOutput 把"装载/播放/暂停/seek/播完"这些事件
// 变成可以直接调用的方法，于是"播完自动下一首""next 到队尾返回 NotFound"
// 这种在真机上要等 3 分钟的路径，这里一行就能走到。
#include "doctest_support.h"

#include <cstdint>
#include <string>
#include <vector>

#include "mamba/kernel/Error.h"
#include "mamba/playback/contracts/AudioOutput.h"
#include "mamba/playback/contracts/Commands.h"
#include "mamba/playback/core/app/PlaybackService.h"

namespace {

using mamba::kernel::ErrorCode;
using mamba::playback::contracts::ErrorListener;
using mamba::playback::contracts::FinishedListener;
using mamba::playback::contracts::IAudioOutput;
using mamba::playback::contracts::IPositionStream;
using mamba::playback::contracts::PlaybackState;
using mamba::playback::contracts::PlayTracks;
using mamba::playback::contracts::QueueEntry;
using mamba::playback::contracts::StateListener;

class FakeAudioOutput final : public IAudioOutput, public IPositionStream {
public:
    // ── IAudioOutput ────────────────────────────────────────────────────────
    mamba::kernel::Result<void> load(std::string_view filePathUtf8) override {
        if (failLoad) {
            currentState = PlaybackState::Failed;
            return mamba::kernel::Error{ErrorCode::Unavailable, "假装解不开"};
        }
        loadedPath = std::string{filePathUtf8};
        posMs = 0;
        currentState = PlaybackState::Stopped;
        if (stateListener) {
            stateListener(currentState);
        }
        return {};
    }

    mamba::kernel::Result<void> play() override {
        ++playCount;
        currentState = PlaybackState::Playing;
        if (stateListener) {
            stateListener(currentState);
        }
        return {};
    }

    mamba::kernel::Result<void> pause() override {
        currentState = PlaybackState::Paused;
        if (stateListener) {
            stateListener(currentState);
        }
        return {};
    }

    mamba::kernel::Result<void> stop() override {
        ++stopCount;
        posMs = 0;
        currentState = PlaybackState::Stopped;
        if (stateListener) {
            stateListener(currentState);
        }
        return {};
    }

    mamba::kernel::Result<void> seekMs(std::int64_t target) override {
        ++seekCount;
        posMs = target < 0 ? 0 : (target > duration ? duration : target);
        return {};
    }

    mamba::kernel::Result<void> setVolume(std::int32_t target) override {
        volume_ = target < 0 ? 0 : (target > 100 ? 100 : target);
        return {};
    }

    [[nodiscard]] std::int32_t volume() const noexcept override { return volume_; }
    [[nodiscard]] std::int64_t durationMs() const noexcept override { return duration; }
    [[nodiscard]] PlaybackState state() const noexcept override { return currentState; }

    void setStateListener(StateListener listener) override { stateListener = std::move(listener); }
    void setFinishedListener(FinishedListener listener) override {
        finishedListener = std::move(listener);
    }
    void setErrorListener(ErrorListener listener) override { errorListener = std::move(listener); }

    // ── IPositionStream ─────────────────────────────────────────────────────
    [[nodiscard]] std::int64_t positionMs() const noexcept override { return posMs; }

    // ── 测试用的旋钮 ────────────────────────────────────────────────────────
    void finishCurrent() {
        if (finishedListener) {
            finishedListener();
        }
    }

    std::string loadedPath;
    std::int64_t posMs = 0;
    std::int64_t duration = 180000;
    bool failLoad = false;
    int playCount = 0;
    int stopCount = 0;
    int seekCount = 0;

private:
    PlaybackState currentState = PlaybackState::Idle;
    std::int32_t volume_ = 100;
    StateListener stateListener;
    FinishedListener finishedListener;
    ErrorListener errorListener;
};

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
