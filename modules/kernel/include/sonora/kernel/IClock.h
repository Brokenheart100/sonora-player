// 时间来源。
//
// 领域层不许直接调 std::chrono::system_clock::now()：那样「播放时长」「扫描耗时」
// 就没法在测试里被固定，一个跨越午夜的断言会间歇性失败。注入时钟是最便宜的
// 可测试性改造，所以从 M1 就做。
#pragma once

#include <chrono>

namespace sonora::kernel {

class IClock {
public:
    virtual ~IClock() = default;

    // 墙钟：给「什么时候导入的」这类要显示给用户看的时间戳用。
    [[nodiscard]] virtual std::chrono::system_clock::time_point now() const = 0;

    // 单调钟：给测耗时用。它不会被系统时间调整影响。
    [[nodiscard]] virtual std::chrono::steady_clock::time_point steadyNow() const = 0;
};

class SystemClock final : public IClock {
public:
    [[nodiscard]] std::chrono::system_clock::time_point now() const override
    {
        return std::chrono::system_clock::now();
    }

    [[nodiscard]] std::chrono::steady_clock::time_point steadyNow() const override
    {
        return std::chrono::steady_clock::now();
    }
};

}  // namespace sonora::kernel
