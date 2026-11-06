// 测试用的音频输出替身。
//
// 把"装载/播放/暂停/seek/播完"这些事件变成可以直接调用的方法，于是
// "播完自动下一首""单曲循环重放本曲"这种在真机上要等三分钟的路径，
// 单元测试里一行就能走到。
//
// 单独成文件是为了让多个测试 TU 共用同一份替身：它的行为就是
// IAudioOutput 的参考实现，散在两处迟早会不一致。
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "sonora/kernel/Error.h"
#include "sonora/playback/contracts/AudioOutput.h"

namespace sonoratest {

using sonora::kernel::ErrorCode;
using sonora::playback::contracts::ErrorListener;
using sonora::playback::contracts::FinishedListener;
using sonora::playback::contracts::IAudioOutput;
using sonora::playback::contracts::IPositionStream;
using sonora::playback::contracts::PlaybackState;
using sonora::playback::contracts::StateListener;

class FakeAudioOutput final : public IAudioOutput, public IPositionStream {
public:
    // ── IAudioOutput ────────────────────────────────────────────────────────
    sonora::kernel::Result<void> load(std::string_view filePathUtf8) override {
        // 按路径失败：模拟"文件在播放前排被删掉了"。
        if (failLoad || unloadable.count(std::string{filePathUtf8}) > 0) {
            currentState = PlaybackState::Failed;
            return sonora::kernel::Error{ErrorCode::Unavailable, "假装解不开"};
        }
        loadedPath = std::string{filePathUtf8};
        posMs = 0;
        currentState = PlaybackState::Stopped;
        if (stateListener) {
            stateListener(currentState);
        }
        return {};
    }

    sonora::kernel::Result<void> play() override {
        ++playCount;
        currentState = PlaybackState::Playing;
        if (stateListener) {
            stateListener(currentState);
        }
        return {};
    }

    sonora::kernel::Result<void> pause() override {
        currentState = PlaybackState::Paused;
        if (stateListener) {
            stateListener(currentState);
        }
        return {};
    }

    sonora::kernel::Result<void> stop() override {
        ++stopCount;
        posMs = 0;
        currentState = PlaybackState::Stopped;
        if (stateListener) {
            stateListener(currentState);
        }
        return {};
    }

    sonora::kernel::Result<void> seekMs(std::int64_t target) override {
        ++seekCount;
        posMs = target < 0 ? 0 : (target > duration ? duration : target);
        return {};
    }

    sonora::kernel::Result<void> setVolume(std::int32_t target) override {
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
    std::set<std::string> unloadable;
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

} // namespace sonoratest
