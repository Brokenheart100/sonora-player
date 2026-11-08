// R5 的两块"脏活"：缓存上限与降级留痕。
//
// 这两件事都没法用假端口证明——淘汰规则写在 SQL 里，降级分类写在服务层的
// 失败分支里。所以这里用**真的 SQLite**（`.test_tmp/` 下）跑淘汰，用真的
// `Diagnostics` 跑分类；服务层那几个分支的接线由 R5-1 的真机矩阵覆盖。
//
// 为什么不断言"删掉的是哪一行"而断言"活下来的是哪一行"：淘汰顺序是 before/after
// 语义，断言"k149 被删"比断言"删了 50 行"更能挡住"排序写反了"这种错。
#include "doctest_support.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <sonora/kernel/IClock.h>
#include <sonora/online/adapters/OnlineSchema.h>
#include <sonora/online/adapters/SqliteOnlineStore.h>
#include <sonora/online/core/app/Diagnostics.h>
#include <sonora/online/core/app/Hint.h>
#include <sonora/platform/storage/Migrator.h>
#include <sonora/platform/storage/SqliteConnection.h>

namespace {

namespace fs = std::filesystem;
using namespace sonora;
using namespace sonora::online;

// 时间由测试给：TTL 与 LRU 都靠时间比较，用真时钟就要 sleep 十分钟。
struct TestClock final : kernel::IClock
{
    std::chrono::system_clock::time_point now_{std::chrono::system_clock::from_time_t(1'700'000'000)};

    [[nodiscard]] std::chrono::system_clock::time_point now() const override { return now_; }
    [[nodiscard]] std::chrono::steady_clock::time_point steadyNow() const override { return {}; }

    void advance(std::int64_t ms) { now_ += std::chrono::milliseconds{ms}; }
};

struct Sandbox
{
    Sandbox()
    {
        std::error_code ec;
        root_ = fs::current_path(ec) / ".test_tmp" / "sonora_r5_online_store";
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
        dbPath_ = (root_ / "online.sqlite").string();
    }

    ~Sandbox()
    {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    Sandbox(const Sandbox &) = delete;
    Sandbox &operator=(const Sandbox &) = delete;

    [[nodiscard]] const std::string &dbPath() const { return dbPath_; }

private:
    fs::path root_;
    std::string dbPath_;
};

struct Stack
{
    platform::storage::SqliteConnection connection;
    adapters::SqliteOnlineStore store;

    Stack(platform::storage::SqliteConnection conn, const kernel::IClock &clock)
        : connection(std::move(conn)), store(connection, clock)
    {
    }
};

std::unique_ptr<Stack> openStack(const Sandbox &sandbox, const kernel::IClock &clock)
{
    auto opened = platform::storage::SqliteConnection::open(sandbox.dbPath());
    REQUIRE_MESSAGE(opened.hasValue(), "打开失败：" << opened.error().message());
    auto stack = std::make_unique<Stack>(std::move(opened).take(), clock);
    platform::storage::Migrator migrator{stack->connection, adapters::kModuleName};
    const auto version = migrator.apply(adapters::migrations());
    REQUIRE(version.hasValue());
    CHECK(version.value() == 2);
    return stack;
}

void fill(Stack &stack, TestClock &clock, int from, int count)
{
    for (int index = from; index < from + count; ++index)
    {
        stack.store.saveSearch("ncm", "k" + std::to_string(index), 0, "payload-" + std::to_string(index));
        clock.advance(1); // 每行一个不同的 last_used_at，淘汰顺序才是确定的
    }
}

} // namespace

TEST_CASE("在线缓存：超出行数上限时淘汰最久没被用的，命中过的留下")
{
    Sandbox sandbox;
    TestClock clock;
    auto stack = openStack(sandbox, clock);

    fill(*stack, clock, 0, 300);
    stack->store.trimSearch();
    // 300 行 → 200 行：先写的那 100 行被淘汰。
    CHECK(stack->store.stats().searchRows == 200);

    // 再塞 50 行，然后"用过"最旧的那条（k100）。若 LRU 排序没写反，
    // k100 因为刚被读走而活下来，k149（没被读过）被删。
    fill(*stack, clock, 300, 50);
    CHECK(stack->store.stats().searchRows == 250);
    const auto touched = stack->store.loadSearch("ncm", "k100", 0);
    REQUIRE(touched.has_value());
    CHECK(touched.value() == "payload-100");

    stack->store.trimSearch();
    CHECK(stack->store.stats().searchRows == 200);
    CHECK(stack->store.loadSearch("ncm", "k100", 0).has_value());
    CHECK_FALSE(stack->store.loadSearch("ncm", "k149", 0).has_value());
}

TEST_CASE("在线缓存：老行的 last_used_at 为 0 时按 fetched_at 排队")
{
    // 迁移只加列不回填（大 payload 的 UPDATE 会把迁移卡死，见 OnlineSchema.cpp），
    // 所以升级上来的老行 last_used_at 全是 0。它们必须按 fetched_at 排序淘汰，
    // 而不是被当成"最久没用过"一股脑删光——否则升级当天缓存就空了。
    Sandbox sandbox;
    TestClock clock;
    auto stack = openStack(sandbox, clock);

    fill(*stack, clock, 0, 250);
    const auto zeroed = stack->connection.exec("UPDATE online_search_cache SET last_used_at = 0;");
    REQUIRE(zeroed.hasValue());

    stack->store.trimSearch();
    CHECK(stack->store.stats().searchRows == 200);
    // 最新的 200 行（k50…k249）该活着，最早的 50 行被淘汰。
    CHECK(stack->store.loadSearch("ncm", "k249", 0).has_value());
    CHECK(stack->store.loadSearch("ncm", "k50", 0).has_value());
    CHECK_FALSE(stack->store.loadSearch("ncm", "k49", 0).has_value());
    CHECK_FALSE(stack->store.loadSearch("ncm", "k0", 0).has_value());
}

TEST_CASE("在线缓存：字节上限同样触发淘汰")
{
    Sandbox sandbox;
    TestClock clock;
    auto stack = openStack(sandbox, clock);

    // 4 MiB 上限，每行 64 KiB：70 行 = 4.375 MiB，必然超。
    const std::string big(64 * 1024, 'x');
    for (int index = 0; index < 70; ++index)
    {
        stack->store.saveSearch("ncm", "big" + std::to_string(index), 0, big);
        clock.advance(1);
    }
    stack->store.trimSearch();

    const auto stats = stack->store.stats();
    CHECK(stats.searchBytes <= adapters::SqliteOnlineStore::kMaxSearchBytes);
    CHECK(stats.searchRows < 70);
    CHECK(stats.searchRows > 0);
}

TEST_CASE("在线缓存：超过 TTL 的行不再命中，但仍然占着位置直到被淘汰")
{
    Sandbox sandbox;
    TestClock clock;
    auto stack = openStack(sandbox, clock);

    stack->store.saveSearch("ncm", "old", 0, "stale");
    clock.advance(adapters::SqliteOnlineStore::kSearchTtlMs + 1);
    CHECK_FALSE(stack->store.loadSearch("ncm", "old", 0).has_value());
    // 过期不等于删除：清理是 trim 的事，读路径不做写。
    CHECK(stack->store.stats().searchRows == 1);
}

TEST_CASE("降级留痕：按用户可见形态分类，取消不算降级")
{
    core::app::Diagnostics diag;

    contracts::OnlineError error;
    error.code = kernel::ErrorCode::Timeout;
    error.message = "超时";
    diag.recordError(error, 1000, "搜索");

    error.code = kernel::ErrorCode::RateLimited;
    diag.recordError(error, 1100, "搜索");

    error.code = kernel::ErrorCode::Unauthenticated;
    diag.recordError(error, 1200, "登录校验");

    error.code = kernel::ErrorCode::Upstream;
    diag.recordError(error, 1300, "搜索");

    error.code = kernel::ErrorCode::NotFound;
    diag.recordError(error, 1400, "取地址");

    error.code = kernel::ErrorCode::Unavailable;
    diag.recordError(error, 1500, "搜索");

    // 用户自己按的取消：既不加分类，也不加 totalErrors。
    error.code = kernel::ErrorCode::Internal;
    error.message = "已取消";
    diag.recordError(error, 1600, "搜索");

    diag.recordCacheHit();
    diag.recordCacheHit();
    diag.recordUrlExpired();

    const auto &counters = diag.counters();
    CHECK(counters.timeout == 1);
    CHECK(counters.rateLimited == 1);
    CHECK(counters.unauthenticated == 1);
    // Upstream 与 NotFound 在矩阵里是同一行（"接口可能已变更"那类）。
    CHECK(counters.upstream == 2);
    CHECK(counters.offline == 1);
    CHECK(counters.totalErrors == 6);
    CHECK(counters.cacheHits == 2);
    CHECK(counters.urlExpired == 1);
}

TEST_CASE("降级留痕：环形缓冲只留最近 10 条，最新在前")
{
    core::app::Diagnostics diag;

    contracts::OnlineError error;
    error.code = kernel::ErrorCode::Upstream;
    for (int index = 0; index < 12; ++index)
    {
        error.message = "e" + std::to_string(index);
        diag.recordError(error, 1000 + index, "搜索");
    }

    const auto recent = diag.recent();
    REQUIRE(recent.size() == 10);
    CHECK(recent.front().error.message == "e11");
    CHECK(recent.back().error.message == "e2");
    // 时间换算成 Unix 秒：1011 ms → 1 s。
    CHECK(recent.front().atUnix == 1);
}

TEST_CASE("接口可能已变更：只有上游类错误、且连续失败两次以上才改写消息")
{
    contracts::OnlineError upstream;
    upstream.code = kernel::ErrorCode::Upstream;
    upstream.message = "服务器返回 HTTP 404";

    // 第一次失败：还可能是抖了一下，原样给用户。
    const auto first = core::app::withInterfaceHint(upstream, 1);
    CHECK(first.message == "服务器返回 HTTP 404");
    CHECK_FALSE(first.retryable);

    // 第二次：同一类错误连着来，多半是接口本身变了，把原文留在括号里方便报 bug。
    const auto second = core::app::withInterfaceHint(upstream, 2);
    CHECK(second.message == "接口可能已变更，稍后再试（服务器返回 HTTP 404）");
    CHECK(second.retryable);

    // 超时/限流/未登录各有各的说法，不能被这条提示盖掉。
    contracts::OnlineError timeout;
    timeout.code = kernel::ErrorCode::Timeout;
    timeout.message = "请求超时";
    const auto slow = core::app::withInterfaceHint(timeout, 9);
    CHECK(slow.message == "请求超时");
    CHECK_FALSE(slow.retryable);
}
