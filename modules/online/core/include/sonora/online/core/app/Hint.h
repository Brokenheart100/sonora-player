// 「接口可能已变更」这条提示从服务层搬到了 core：纯粹是"错误 + 连续失败次数"到
// "给人看的错误"的映射，没有 I/O、没有状态，放在 adapter 里既难测也没道理。
#pragma once

#include "sonora/kernel/Result.h"
#include "sonora/online/contracts/Types.h"

namespace sonora::online::core::app
{
    // 上游连续失败两次以上时，错误多半不是"网不好"而是"接口变了"。
    // 只改 Upstream / NotFound 这两类，其余原样返回（超时、限流、未登录各有各的说法）。
    [[nodiscard]] contracts::OnlineError withInterfaceHint(contracts::OnlineError error,
                                                           int consecutiveFailures);
} // namespace sonora::online::core::app
