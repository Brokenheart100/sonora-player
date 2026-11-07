// 唯一的错误翻译：传输失败 + HTTP 状态码 → OnlineError。
//
// 之所以只有这里一处，是因为界面文案、重试按钮、日志三处都读同一个结论；
// 散成两份的后果是"日志说超时、界面说网络错误"（docs/ONLINE.md §5.2）。
#pragma once

#include <string>

#include "sonora/kernel/Error.h"
#include "sonora/online/contracts/Types.h"

namespace sonora::online::core::policy
{

    [[nodiscard]] inline contracts::OnlineError fromTransport(const kernel::Error &error)
    {
        contracts::OnlineError out;
        out.code = error.code();
        switch (error.code())
        {
        case kernel::ErrorCode::Timeout:
            out.message = "连接超时";
            out.retryable = true;
            break;
        case kernel::ErrorCode::Unavailable:
            out.message = "网络不可用";
            out.retryable = true;
            break;
        case kernel::ErrorCode::Internal:
            // 取消：不是故障，也不该被当成可重试
            out.message = error.message().empty() ? "已取消" : error.message();
            out.retryable = false;
            break;
        default:
            out.message = error.message().empty() ? "请求失败" : error.message();
            out.retryable = false;
            break;
        }
        return out;
    }

    [[nodiscard]] inline contracts::OnlineError fromHttpStatus(int statusCode, std::int32_t retryAfterMs = 0)
    {
        contracts::OnlineError out;
        out.retryAfterMs = retryAfterMs;
        if (statusCode == 401 || statusCode == 403)
        {
            out.code = kernel::ErrorCode::Unauthenticated;
            out.message = "登录状态已失效";
            out.retryable = false;
        }
        else if (statusCode == 429)
        {
            out.code = kernel::ErrorCode::RateLimited;
            out.message = "请求太频繁";
            out.retryable = true;
        }
        else if (statusCode >= 500)
        {
            out.code = kernel::ErrorCode::Upstream;
            out.message = "服务暂时不可用";
            out.retryable = true;
        }
        else if (statusCode == 404)
        {
            out.code = kernel::ErrorCode::NotFound;
            out.message = "接口不存在";
            out.retryable = false;
        }
        else
        {
            out.code = kernel::ErrorCode::Upstream;
            out.message = "接口返回异常";
            out.retryable = false;
        }
        return out;
    }

} // namespace sonora::online::core::policy
