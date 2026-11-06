// 日志端口。
//
// 领域层只认这个接口。真正的实现（spdlog）在 Infrastructure 里，且只在
// 组合根处被创建一次——领域层连 <spdlog/spdlog.h> 的路径都拿不到（构建系统
// 不给），所以「不小心在聚合里打了条日志」会直接编译失败。
//
// 参数用 std::format 而不是 fmt：项目零 fmt 依赖（见 docs/DEPENDENCIES.md §3）。
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace sonora::kernel
{

    enum class LogLevel
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error
    };

    // 名字故意不叫 toString()：doctest 的 DOCTEST_STRINGIFY 会用无限定调用 toString(v)，
    // 而 ADL 会把本函数拉进候选集——非模板的精确匹配会赢过 doctest 的模板，
    // 结果是断言失败时两个操作数都变成 const char*，MSVC 报 C2110（加两个指针）。
    [[nodiscard]] constexpr const char *toName(LogLevel level) noexcept
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "trace";
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
        }
        return "unknown";
    }

    class ILogger
    {
    public:
        virtual ~ILogger() = default;

        // 先问再格式化：一条被丢弃的日志不应该付出 format 的开销。
        [[nodiscard]] virtual bool enabled(LogLevel level) const noexcept = 0;

        virtual void log(LogLevel level, std::string_view message) = 0;

        template <class... Args>
        void trace(std::format_string<Args...> fmt, Args &&...args)
        {
            write(LogLevel::Trace, fmt, std::forward<Args>(args)...);
        }

        template <class... Args>
        void debug(std::format_string<Args...> fmt, Args &&...args)
        {
            write(LogLevel::Debug, fmt, std::forward<Args>(args)...);
        }

        template <class... Args>
        void info(std::format_string<Args...> fmt, Args &&...args)
        {
            write(LogLevel::Info, fmt, std::forward<Args>(args)...);
        }

        template <class... Args>
        void warn(std::format_string<Args...> fmt, Args &&...args)
        {
            write(LogLevel::Warn, fmt, std::forward<Args>(args)...);
        }

        template <class... Args>
        void error(std::format_string<Args...> fmt, Args &&...args)
        {
            write(LogLevel::Error, fmt, std::forward<Args>(args)...);
        }

    private:
        template <class... Args>
        void write(LogLevel level, std::format_string<Args...> fmt, Args &&...args)
        {
            if (!enabled(level))
            {
                return;
            }
            log(level, std::format(fmt, std::forward<Args>(args)...));
        }
    };

    // 默认实现：什么都不做。测试和工具代码用，生产代码换 spdlog。
    class NullLogger final : public ILogger
    {
    public:
        [[nodiscard]] bool enabled(LogLevel) const noexcept override { return false; }
        void log(LogLevel, std::string_view) override {}
    };

} // namespace sonora::kernel
