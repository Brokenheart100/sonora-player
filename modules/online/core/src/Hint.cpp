#include "sonora/online/core/app/Hint.h"

namespace sonora::online::core::app
{
    contracts::OnlineError withInterfaceHint(contracts::OnlineError error, int consecutiveFailures)
    {
        const bool upstream = error.code == kernel::ErrorCode::Upstream ||
                              error.code == kernel::ErrorCode::NotFound;
        if (!upstream || consecutiveFailures < 2)
        {
            return error;
        }

        // 原文保留在括号里：用户要报 bug 时得能看见服务端到底回了什么。
        error.message = "接口可能已变更，稍后再试（" + error.message + "）";
        error.retryable = true;
        return error;
    }
} // namespace sonora::online::core::app
