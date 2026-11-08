#include "sonora/online/core/app/Diagnostics.h"

namespace sonora::online::core::app
{
    namespace
    {
        [[nodiscard]] bool isCancellation(const contracts::OnlineError &error) noexcept
        {
            return error.code == kernel::ErrorCode::Internal && error.message == "已取消";
        }
    } // namespace

    void Diagnostics::recordError(const contracts::OnlineError &error, std::int64_t nowMs,
                                  std::string where) noexcept
    {
        if (isCancellation(error))
        {
            return;
        }

        using kernel::ErrorCode;
        switch (error.code)
        {
        case ErrorCode::Timeout:
            ++counters_.timeout;
            break;
        case ErrorCode::RateLimited:
            ++counters_.rateLimited;
            break;
        case ErrorCode::Unauthenticated:
            ++counters_.unauthenticated;
            break;
        case ErrorCode::Upstream:
        case ErrorCode::NotFound: // 接口不存在，在矩阵里与"结构不符"同一行
            ++counters_.upstream;
            break;
        case ErrorCode::Unavailable:
            ++counters_.offline;
            break;
        default:
            // Internal / Failed / None：不是可归类的降级，但仍是错误
            break;
        }
        ++counters_.totalErrors;

        contracts::OnlineErrorRecord record;
        record.atUnix = nowMs / 1000;
        record.where = std::move(where);
        record.error = error;
        ring_[next_] = std::move(record);
        next_ = (next_ + 1) % kRecentLimit;
        if (filled_ < kRecentLimit)
        {
            ++filled_;
        }
    }

    std::vector<contracts::OnlineErrorRecord> Diagnostics::recent() const
    {
        std::vector<contracts::OnlineErrorRecord> out;
        out.reserve(filled_);
        // 从最新往回走：next_-1 是最新那条，绕一圈取 filled_ 条。
        for (std::size_t i = 0; i < filled_; ++i)
        {
            const std::size_t index = (next_ + kRecentLimit - 1 - i) % kRecentLimit;
            out.push_back(ring_[index]);
        }
        return out;
    }

} // namespace sonora::online::core::app
