// 令牌桶：3 req/s、burst 6（数值来源与理由见 docs/ONLINE.md §5.1）。
//
// 时间由调用方给（`nowMs`），所以测试不需要 sleep：喂 0、250、2500 三个时刻
// 就能把"补桶"与"限流"两件事都断言掉。
#pragma once

#include <algorithm>
#include <cstdint>

namespace sonora::online::core::policy
{

    class TokenBucket
    {
    public:
        TokenBucket(double ratePerSecond, double burst)
            : rate_(ratePerSecond), burst_(burst), tokens_(burst), lastMs_(0), primed_(false)
        {
        }

        // 取一个令牌。返回 false 表示"现在应该等"。
        bool take(std::int64_t nowMs) noexcept
        {
            refill(nowMs);
            if (tokens_ < 1.0)
            {
                return false;
            }
            tokens_ -= 1.0;
            return true;
        }

        [[nodiscard]] double tokens(std::int64_t nowMs) noexcept
        {
            refill(nowMs);
            return tokens_;
        }

        // 唯一一处"重置"：登录态变化或用户手动重试时把它加满。
        void prime(std::int64_t nowMs) noexcept
        {
            tokens_ = burst_;
            lastMs_ = nowMs;
            primed_ = true;
        }

    private:
        void refill(std::int64_t nowMs) noexcept
        {
            if (!primed_)
            {
                // 第一次调用只记时间，不补桶：否则"从 0 到 2026 年"会一次性补满。
                lastMs_ = nowMs;
                primed_ = true;
                return;
            }
            if (nowMs <= lastMs_)
            {
                lastMs_ = nowMs;
                return;
            }
            const double elapsedSec = static_cast<double>(nowMs - lastMs_) / 1000.0;
            tokens_ = std::min(burst_, tokens_ + elapsedSec * rate_);
            lastMs_ = nowMs;
        }

        double rate_;
        double burst_;
        double tokens_;
        std::int64_t lastMs_;
        bool primed_;
    };

} // namespace sonora::online::core::policy
