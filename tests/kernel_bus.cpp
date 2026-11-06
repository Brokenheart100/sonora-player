// 命令/查询总线的测试。
//
// 这组测试要证明三件事，而这三件事正是「总线值得存在」的理由：
//   1. 类型擦除是安全的——查不到 handler 得到 NotFound，而不是崩溃；
//   2. 失败会沿 Result 原路返回，调用方不会漏掉（[[nodiscard]] 也在帮忙）；
//   3. 慢命令会被记下来——这是 5ms 预算唯一能自动执行的地方。
#include "doctest_support.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <sonora/kernel/CommandBus.h>
#include <sonora/kernel/ILogger.h>
#include <sonora/kernel/QueryBus.h>


using sonora::kernel::CommandBus;
using sonora::kernel::ErrorCode;
using sonora::kernel::LogLevel;
using sonora::kernel::QueryBus;

namespace {

// 把日志收进 vector 的 logger：测试里断言「该 warn 的确实 warn 了」。
class RecordingLogger final : public sonora::kernel::ILogger {
public:
    [[nodiscard]] bool enabled(LogLevel level) const noexcept override
    {
        return level >= LogLevel::Info;
    }

    void log(LogLevel level, std::string_view message) override
    {
        entries.emplace_back(level, std::string(message));
    }

    std::vector<std::pair<LogLevel, std::string>> entries;
};

struct ImportFolder {
    std::string path;
};

struct RenameTrack {
    std::uint64_t id = 0;
    std::string title;
};

struct GetTrackCount {
    std::uint64_t albumId = 0;
};

// 标准的应用层 handler 形态：构造时注入依赖，handle() 返回 Result。
class RenameTrackHandler {
public:
    explicit RenameTrackHandler(bool shouldFail) : shouldFail_(shouldFail) {}

    sonora::kernel::Result<void> handle(const RenameTrack &command) const
    {
        lastId = command.id;
        lastTitle = command.title;
        if (shouldFail_) {
            return sonora::kernel::Result<void>::fail(
                sonora::kernel::Error{sonora::kernel::ErrorCode::NotFound, "没有这首曲子"});
        }
        return sonora::kernel::Result<void>::ok();
    }

    bool shouldFail_ = false;
    mutable std::uint64_t lastId = 0;
    mutable std::string lastTitle;
};

}  // namespace

TEST_CASE("CommandBus：注册了 handler 就能拿到命令")
{
    auto handler = std::make_shared<RenameTrackHandler>(false);
    CommandBus bus;

    bus.on<RenameTrack>(handler);
    CHECK(bus.handlerCount() == 1);

    const auto result = bus.dispatch(RenameTrack{.id = 7, .title = "夜航"});
    REQUIRE(result.hasValue());
    CHECK(handler->lastId == 7);
    CHECK(handler->lastTitle == "夜航");
}

TEST_CASE("CommandBus：没注册的命令返回 NotFound，而不是崩溃")
{
    CommandBus bus;
    const auto result = bus.dispatch(ImportFolder{.path = R"(D:\Music)"});

    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().code() == ErrorCode::NotFound);
}

TEST_CASE("CommandBus：handler 的失败原样返回给调用方")
{
    auto handler = std::make_shared<RenameTrackHandler>(true);
    CommandBus bus;
    bus.on<RenameTrack>(handler);

    const auto result = bus.dispatch(RenameTrack{.id = 1, .title = "x"});
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().code() == ErrorCode::NotFound);
    CHECK(result.error().message() == "没有这首曲子");
}

TEST_CASE("CommandBus：慢命令会被记一条 warn（5ms 预算的唯一自动执法点）")
{
    RecordingLogger logger;
    CommandBus bus(&logger, /*slowThresholdMs=*/1.0);

    bus.on<ImportFolder>([](const ImportFolder &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        return sonora::kernel::Result<void>::ok();
    });

    const auto result = bus.dispatch(ImportFolder{.path = R"(C:\慢)"});
    CHECK(result.hasValue());

    REQUIRE(logger.entries.size() == 1);
    CHECK(logger.entries.front().first == LogLevel::Warn);
    CHECK(logger.entries.front().second.find("ITaskQueue") != std::string::npos);
}

TEST_CASE("CommandBus：同一条命令再注册一次会覆盖（而不是重复执行）")
{
    int calls = 0;
    CommandBus bus;

    bus.on<ImportFolder>([&calls](const ImportFolder &) {
        ++calls;
        return sonora::kernel::Result<void>::ok();
    });
    bus.on<ImportFolder>([&calls](const ImportFolder &) {
        calls += 100;
        return sonora::kernel::Result<void>::ok();
    });

    const auto result = bus.dispatch(ImportFolder{.path = "x"});
    CHECK(result.hasValue());
    CHECK(bus.handlerCount() == 1);
    CHECK(calls == 100);
}

TEST_CASE("QueryBus：拿得到查询结果")
{
    QueryBus bus;
    bus.on<GetTrackCount, int>([](const GetTrackCount &query) {
        return sonora::kernel::Result<int>::ok(static_cast<int>(query.albumId) * 3);
    });

    const auto result = bus.ask<int>(GetTrackCount{.albumId = 5});
    REQUIRE(result.hasValue());
    CHECK(result.value() == 15);
}

TEST_CASE("QueryBus：未注册的查询返回 NotFound")
{
    QueryBus bus;
    const auto result = bus.ask<int>(GetTrackCount{.albumId = 1});

    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().code() == ErrorCode::NotFound);
}

TEST_CASE("QueryBus：handler 返回类型与 ask<T> 不一致时给 Internal")
{
    QueryBus bus;
    bus.on<GetTrackCount, int>([](const GetTrackCount &) {
        return sonora::kernel::Result<int>::ok(1);
    });

    // 注册的是 Result<int>，这里却按 std::string 去取。
    const auto result = bus.ask<std::string>(GetTrackCount{.albumId = 1});
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().code() == ErrorCode::Internal);
}

TEST_CASE("QueryBus：handler 的业务失败也能回去")
{
    QueryBus bus;
    bus.on<GetTrackCount, int>([](const GetTrackCount &) {
        return sonora::kernel::Result<int>::fail(
            sonora::kernel::Error{sonora::kernel::ErrorCode::Unavailable, "数据库被锁"});
    });

    const auto result = bus.ask<int>(GetTrackCount{});
    REQUIRE_FALSE(result.hasValue());
    CHECK(result.error().message() == "数据库被锁");
}
