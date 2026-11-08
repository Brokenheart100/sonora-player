// 跨模块的集成事件。
//
// 放在 Sonora.Contracts 而不是某个模块的 Core 里：事件是**两个模块之间**的
// 事实通知，任何一方私有它都会让另一方依赖自己不该看见的东西。
//
// M5 的五条偏差里记过"TrackImported 至今没有发布者"。M6 真正需要跨模块
// 通信了（播放一次 → Catalog 记一次播放次数），才把事件总线建起来：
// 一条集成事件 + 一个订阅者，不预先造一堆没人听的事件。
#pragma once

#include <cstdint>
#include <string_view>

#include <sonora/kernel/IDomainEvent.h>

namespace sonora::contracts {

/// 一首曲子放完了（自然播完，不是被切歌）。
///
/// Playback 发布，Catalog 订阅并累计 play_count。
/// **跨模块只能用事件**：Playback 不许直接调 Catalog 的仓储。
struct TrackFinished final : kernel::IDomainEvent {
    static constexpr std::string_view kName = "playback.track_finished";

    TrackFinished() = default;
    TrackFinished(std::uint64_t id, std::int64_t playedAt) : trackId(id), playedAtUnix(playedAt) {}

    std::uint64_t trackId = 0;
    std::int64_t playedAtUnix = 0;

    [[nodiscard]] std::string_view eventName() const noexcept override { return kName; }
};


/// 追加式队列播到末尾了：还要不要下一批？
///
/// Playback 发布，**组合根**订阅。为什么不是 Playback 自己去问在线模块——
/// 播放模块不知道"FM 是什么"，它只知道"我的队列还能接东西"。
/// 由 host 去取下一批、再发 `AppendTracks`，边界才没有被打穿。
///
/// 只在"追加式队列"上发布：普通列表播完了就播完了，不该有人去要新歌。
struct QueueNeedsMore final : kernel::IDomainEvent {
    static constexpr std::string_view kName = "playback.queue_needs_more";

    QueueNeedsMore() = default;
    explicit QueueNeedsMore(std::int64_t at) : atUnix(at) {}

    std::int64_t atUnix = 0;

    [[nodiscard]] std::string_view eventName() const noexcept override { return kName; }
};

} // namespace sonora::contracts
