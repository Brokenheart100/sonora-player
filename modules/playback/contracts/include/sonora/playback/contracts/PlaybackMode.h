// 播放模式。
//
// 为什么在 Contracts 而不是 Core：界面要显示它、将来队列持久化要存它，
// 而这两边都只允许看见契约；把它放进 Core 就等于让表现层看领域。
#pragma once

#include <cstdint>
#include <string_view>

namespace sonora::playback::contracts {

enum class PlaybackMode : std::uint8_t {
    Sequential, // 顺序：最后一首播完就停
    RepeatAll,  // 列表循环
    RepeatOne,  // 单曲循环：**播完**重放本曲；手动点"下一首"仍然前进
    Shuffle,    // 随机：预生成排列 + 游标，"上一首"能精确回到刚才那首
};

// 叫 toName 不叫 toString：doctest 的无限定 toString 会通过 ADL 抢候选集
// （kernel 里踩过一次，见 Error.h 的注释）。
constexpr std::string_view toName(PlaybackMode mode) noexcept {
    switch (mode) {
        case PlaybackMode::Sequential: return "sequential";
        case PlaybackMode::RepeatAll: return "repeat-all";
        case PlaybackMode::RepeatOne: return "repeat-one";
        case PlaybackMode::Shuffle: return "shuffle";
    }
    return "unknown";
}

/// 点一下模式按钮的循环顺序：顺序 → 列表循环 → 单曲循环 → 随机 → 顺序。
constexpr PlaybackMode nextMode(PlaybackMode mode) noexcept {
    switch (mode) {
        case PlaybackMode::Sequential: return PlaybackMode::RepeatAll;
        case PlaybackMode::RepeatAll: return PlaybackMode::RepeatOne;
        case PlaybackMode::RepeatOne: return PlaybackMode::Shuffle;
        case PlaybackMode::Shuffle: return PlaybackMode::Sequential;
    }
    return PlaybackMode::Sequential;
}

/// 按钮上那个词。够短才塞得进播放条。
constexpr std::string_view modeLabel(PlaybackMode mode) noexcept {
    switch (mode) {
        case PlaybackMode::Sequential: return "顺序";
        case PlaybackMode::RepeatAll: return "循环";
        case PlaybackMode::RepeatOne: return "单曲";
        case PlaybackMode::Shuffle: return "随机";
    }
    return "顺序";
}

} // namespace sonora::playback::contracts
