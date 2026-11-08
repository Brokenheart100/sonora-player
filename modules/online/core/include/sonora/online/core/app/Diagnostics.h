// 降级留痕：计数器 + 最近失败环形缓冲。
//
// docs/ONLINE.md §5.3 的最后一句是"任何降级都必须留痕：一行日志 + 计数器 +1，
// 不允许静默为空"。计数器和环形缓冲都放在这里（零 Qt、可单测），
// 服务层只负责把每次失败喂进来，界面只负责把快照摆出来。
//
// 分类按**用户可见的降级形态**走，而不是按 ErrorCode 走：
// `Upstream` 既可能是"接口 5xx"也可能是"200 但字段没了"，两者在矩阵里是同一行，
// 但对用户的说法不同（前者可重试，后者"接口可能已变更"），所以 message 原样保留。
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sonora/online/contracts/Types.h"

namespace sonora::online::core::app
{
    class Diagnostics
    {
    public:
        static constexpr std::size_t kRecentLimit = 10;

        // 一次失败：分门别类 +1，并进环形缓冲。`nowMs` 由调用方给（服务里有 IClock）。
        // 取消（`Internal` + "已取消"）不算降级：用户自己按的取消，不该污染诊断。
        void recordError(const contracts::OnlineError &error, std::int64_t nowMs, std::string where) noexcept;

        void recordCacheHit() noexcept { ++counters_.cacheHits; }
        // 写操作单独计数：收藏/取消收藏是用户主动动作，成功与失败都要留痕，
        // 但不能混进读路径的降级（"收藏失败"不是"搜索失败"）。
        void recordFavoriteWrite() noexcept { ++favoriteWrites_; }
        void recordFavoriteFailure() noexcept { ++favoriteFailures_; }

        // 私人 FM 同理：它不是用户点出来的，失败时安静重试，单独计数。
        void recordFmFailure() noexcept { ++fmFailures_; }
        [[nodiscard]] std::int32_t favoriteWrites() const noexcept { return favoriteWrites_; }
        [[nodiscard]] std::int32_t favoriteFailures() const noexcept { return favoriteFailures_; }
        [[nodiscard]] std::int32_t fmFailures() const noexcept { return fmFailures_; }
        void recordUrlExpired() noexcept { ++counters_.urlExpired; }

        [[nodiscard]] const contracts::DegradationCounters &counters() const noexcept { return counters_; }

        // 最新在前，最多 10 条。时间换算成 Unix 秒，界面写"3 分钟前"。
        [[nodiscard]] std::vector<contracts::OnlineErrorRecord> recent() const;

        void clear() noexcept
        {
            counters_ = {};
            favoriteWrites_ = 0;
            favoriteFailures_ = 0;
            fmFailures_ = 0;
            next_ = 0;
            filled_ = 0;
        }

    private:
        contracts::DegradationCounters counters_{};
        std::int32_t favoriteWrites_{0};
        std::int32_t favoriteFailures_{0};
        std::int32_t fmFailures_{0};
        std::array<contracts::OnlineErrorRecord, kRecentLimit> ring_{};
        std::size_t next_{0}; // 下一条写哪
        std::size_t filled_{0};
    };

} // namespace sonora::online::core::app
