// 连续失败计数 + "什么时候允许再试"。
//
// 这不是熔断器，是**退避闸门**：连续失败时把下一次允许请求的时刻往后推，
// 一次成功就归零。个人播放器只有"断网"和"登录过期"两种真实失败，
// 多一个状态机换不到任何行为，只换来一条测试路径（docs/ONLINE.md §5.1）。
#pragma once

#include <cstdint>

#include "sonora/online/core/policy/Backoff.h"

namespace sonora::online::core::policy
{

    struct FailureCounter
    {
        int consecutive{0};
        std::int64_t nextRetryAtMs{0};

        void recordSuccess() noexcept
        {
            consecutive = 0;
            nextRetryAtMs = 0;
        }

        void recordFailure(std::int64_t nowMs, const BackoffPolicy &policy, std::uint32_t seed = 0x9E3779B9u) noexcept
        {
            ++consecutive;
            nextRetryAtMs = nowMs + delayFor(policy, consecutive - 1, seed).count();
        }

        [[nodiscard]] bool blocked(std::int64_t nowMs) const noexcept { return nextRetryAtMs > nowMs; }
    };

} // namespace sonora::online::core::policy
