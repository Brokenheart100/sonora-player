// 在线模块的四条策略 + 任务裁决表的测试。
//
// 这四件事都靠"数字"活着（退避多少毫秒、桶里有几个令牌、哪个状态码算什么错、
// 旧 token 的数据还要不要），而它们全都写成了纯函数/纯表，所以可以不用网络、
// 不用 Qt、不用 sleep 就钉死。改数值以前先看这里：改了就要同步改断言。
#include "doctest_support.h"

#include <cstdint>
#include <string>
#include <vector>

#include <sonora/online/contracts/Types.h>
#include <sonora/online/core/app/JobRegistry.h>
#include <sonora/online/core/policy/Backoff.h>
#include <sonora/online/core/policy/ErrorMap.h>
#include <sonora/online/core/policy/FailureCounter.h>
#include <sonora/online/core/policy/TokenBucket.h>

using namespace sonora::online::contracts;
using namespace sonora::online::core::policy;

namespace {

// 抖动是确定性的（xorshift(seed)），所以同一组输入的毫秒数可以断言。
// 这里不写死"307"这种魔法数，改成断言"在 base 与 base+jitter 之间"，
// 改 jitter 上限时不用逐条改断言。
std::int64_t delayMs(int attempt, std::uint32_t seed = 0x9E3779B9u)
{
    return delayFor(BackoffPolicy{}, attempt, seed).count();
}

OnlineTrackDto makeTrack(std::int64_t songId)
{
    OnlineTrackDto track;
    track.songId = songId;
    track.title = "t" + std::to_string(songId);
    return track;
}

} // namespace

TEST_CASE("online/backoff 指数增长并封顶")
{
    const BackoffPolicy policy;

    // attempt 0 = 第一次重试：base 起，加不超过 jitter 的抖动
    const auto first = delayMs(0);
    CHECK(first >= policy.base.count());
    CHECK(first <= policy.base.count() + policy.jitter.count());

    // 一次比一次长，直到 cap
    const auto second = delayMs(1);
    const auto third = delayMs(2);
    CHECK(second > first);
    CHECK(third > second);
    CHECK(third <= policy.cap.count() + policy.jitter.count());

    // 第 20 次不会溢出成天文数字，只会贴在 cap 上
    const auto late = delayMs(20);
    CHECK(late >= policy.cap.count());
    CHECK(late <= policy.cap.count() + policy.jitter.count());

    // 同一个 (attempt, seed) 必须给出同一个毫秒数——否则"重试风暴"没法复现
    CHECK(delayMs(3) == delayMs(3));

    // 负数 attempt 当 0 处理，不能像 -1 那样被当成"比第一次还短"
    CHECK(delayMs(-1) == delayMs(0));
}

TEST_CASE("online/failure counter 是退避闸门不是熔断器")
{
    const BackoffPolicy policy;
    FailureCounter counter;
    CHECK_FALSE(counter.blocked(1000));

    // 第一次失败：现在开始挡 300+jitter，之后自动放开
    counter.recordFailure(1000, policy);
    CHECK(counter.consecutive == 1);
    CHECK(counter.blocked(1001));
    CHECK_FALSE(counter.blocked(1000 + 300 + policy.jitter.count() + 1));

    // 再连续失败一次，等待时间变长（指数）
    counter.recordFailure(2000, policy);
    CHECK(counter.consecutive == 2);
    const auto waitAfterTwo = counter.nextRetryAtMs - 2000;
    CHECK(waitAfterTwo > 300 + policy.jitter.count());

    // 一次成功就归零：这里没有"半开"状态，也不需要
    counter.recordSuccess();
    CHECK(counter.consecutive == 0);
    CHECK_FALSE(counter.blocked(1));
}

TEST_CASE("online/token bucket 按时间补桶")
{
    TokenBucket bucket{3.0, 6.0};

    // 第一次 take 只记时间、不补桶：瞬间能拿满 burst
    for (int i = 0; i < 6; ++i) {
        CHECK(bucket.take(0));
    }
    CHECK_FALSE(bucket.take(0));

    // 1 秒补 3 个
    CHECK(bucket.take(1000));
    CHECK(bucket.take(1000));
    CHECK(bucket.take(1000));
    CHECK_FALSE(bucket.take(1000));

    // 10 秒后补满，但不会超过 burst
    CHECK(bucket.tokens(11000) == 6.0);

    // prime 是唯一的重置口（登录态变化 / 手动重试）
    for (int i = 0; i < 6; ++i) {
        CHECK(bucket.take(11000));
    }
    bucket.prime(11000);
    CHECK(bucket.take(11000));
}

TEST_CASE("online/error map 只翻译一次")
{
    // 传输层失败
    const auto timeout = fromTransport(sonora::kernel::Error{sonora::kernel::ErrorCode::Timeout, "t"});
    CHECK(timeout.code == sonora::kernel::ErrorCode::Timeout);
    CHECK(timeout.message == "连接超时");
    CHECK(timeout.retryable);

    const auto offline = fromTransport(sonora::kernel::Error{sonora::kernel::ErrorCode::Unavailable, "u"});
    CHECK(offline.retryable);

    // 取消落在 Internal，且不可重试
    const auto cancelled = fromTransport(sonora::kernel::Error{sonora::kernel::ErrorCode::Internal, ""});
    CHECK(cancelled.message == "已取消");
    CHECK_FALSE(cancelled.retryable);

    // HTTP 状态码
    CHECK(fromHttpStatus(401).code == sonora::kernel::ErrorCode::Unauthenticated);
    CHECK(fromHttpStatus(403).code == sonora::kernel::ErrorCode::Unauthenticated);
    CHECK(fromHttpStatus(429).code == sonora::kernel::ErrorCode::RateLimited);
    CHECK(fromHttpStatus(429).retryable);
    CHECK(fromHttpStatus(500).code == sonora::kernel::ErrorCode::Upstream);
    CHECK(fromHttpStatus(503).retryable);
    CHECK(fromHttpStatus(404).code == sonora::kernel::ErrorCode::NotFound);
    CHECK_FALSE(fromHttpStatus(404).retryable);

    // 其余 4xx 归类成 Upstream 但不重试；Retry-After 原样透传
    const auto weird = fromHttpStatus(418, 1500);
    CHECK(weird.code == sonora::kernel::ErrorCode::Upstream);
    CHECK_FALSE(weird.retryable);
    CHECK(weird.retryAfterMs == 1500);
}

TEST_CASE("online/job registry 用 token 裁决迟到结果")
{
    sonora::online::core::app::JobRegistry jobs;
    CHECK(jobs.currentToken() == 0);
    CHECK(jobs.size() == 0);

    jobs.begin(7, SourceKind::Search, "jay");
    CHECK(jobs.currentToken() == 7);
    CHECK(jobs.size() == 1);
    CHECK(jobs.state(7).phase == JobPhase::Loading);

    jobs.appendRows(7, {makeTrack(1), makeTrack(2)}, true);
    auto state = jobs.state(7);
    CHECK(state.phase == JobPhase::Ready);
    CHECK(state.loaded == 2);
    CHECK(jobs.page(7, 0, 10).hasMore);
    CHECK(state.revision == 1);

    // offset/limit 越界由 page() 夹住，界面传什么都不该抛
    const auto page = jobs.page(7, -5, 1000);
    CHECK(page.rows.size() == 2);
    CHECK(page.token == 7);

    // 迟到：新任务开始后，旧 token 的数据会被丢弃
    jobs.begin(8, SourceKind::Search, "jay chou");
    jobs.appendRows(7, {makeTrack(3)}, false);
    CHECK(jobs.state(7).loaded == 2);

    jobs.fail(8, OnlineError{sonora::kernel::ErrorCode::Unavailable, "断网", true, 300});
    CHECK(jobs.state(8).phase == JobPhase::Offline);
    CHECK(jobs.state(8).error.message == "断网");
    CHECK(jobs.state(8).error.retryable);

    // 未知 token 不该凭空造出一条记录
    CHECK(jobs.state(999).phase == JobPhase::Idle);

    jobs.forget(7);
    CHECK(jobs.size() == 1);
}
