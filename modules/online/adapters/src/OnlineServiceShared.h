#pragma once

// OnlineService 拆分后的共用小工具：只给 adapters/src 下的几个 .cpp 用。

#include <chrono>
#include <cstdint>

#include "sonora/kernel/IClock.h"
#include "sonora/online/contracts/Types.h"

namespace sonora::online::adapters::detail
{
    namespace c = online::contracts;

    inline std::int64_t nowMillis(const kernel::IClock &clock)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock.now().time_since_epoch()).count();
    }

    inline constexpr const char *kProvider = "ncm";

    inline c::OnlineError notWired(const char *what)
    {
        c::OnlineError error;
        error.code = kernel::ErrorCode::Unavailable;
        error.message = what;
        error.retryable = false;
        return error;
    }
} // namespace sonora::online::adapters::detail
