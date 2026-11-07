// 退避策略：纯函数，零状态、零 Qt、可确定性测试。
//
// 单独一个文件而不是散在服务里，是为了让"第 n 次重试等多久"这件事能被一条
// 单测钉死——线上出现"重试风暴"时，改的应该是这里的数字，而不是读逻辑。
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace sonora::online::core::policy
{

    struct BackoffPolicy
    {
        std::chrono::milliseconds base{300};
        double factor{2.0};
        std::chrono::milliseconds cap{5000};
        // 抖动上限：真随机会让测试无法断言毫秒数，所以用 (attempt, seed) 的
        // xorshift 生成确定性抖动——线上换 seed 就够了，测试里 seed 固定。
        std::chrono::milliseconds jitter{200};
    };

    [[nodiscard]] constexpr std::uint32_t xorshift(std::uint32_t x) noexcept
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    }

    // attempt 从 0 开始：第 1 次重试传 0。
    [[nodiscard]] constexpr std::chrono::milliseconds delayFor(const BackoffPolicy &policy, int attempt,
                                                               std::uint32_t seed = 0x9E3779B9u) noexcept
    {
        const int n = attempt < 0 ? 0 : attempt;
        double raw = static_cast<double>(policy.base.count());
        const double cap = static_cast<double>(policy.cap.count());
        for (int i = 0; i < n && raw < cap; ++i)
        {
            raw *= policy.factor;
        }
        raw = std::min(raw, cap);

        std::int64_t jitterMs = 0;
        if (policy.jitter.count() > 0)
        {
            const auto span = static_cast<std::uint32_t>(policy.jitter.count()) + 1u;
            const std::uint32_t mixed = xorshift(seed ^ (static_cast<std::uint32_t>(n) * 2654435761u));
            jitterMs = static_cast<std::int64_t>(mixed % span);
        }
        return std::chrono::milliseconds{static_cast<std::int64_t>(raw) + jitterMs};
    }

} // namespace sonora::online::core::policy
