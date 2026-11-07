// `IHttpTransport` 的 Qt 实现：QNetworkAccessManager 的薄包装。
//
// 三条规矩（都由 docs/ONLINE.md §4.3 定下）：
//   * 只在 worker 线程上构造与使用（构造函数里有断言）；
//   * `send()` 立刻返回，回调在 worker 线程上触发；
//   * 只接受 `https://`。播放用的直链是 `http://`，但那些地址直接交给 FFmpeg，
//     不经过这里。
#pragma once

#include <cstdint>
#include <memory>

#include "sonora/online/contracts/Transport.h"

namespace sonora::platform::net
{

    class QtHttpTransport final : public online::contracts::IHttpTransport
    {
    public:
        QtHttpTransport();
        ~QtHttpTransport() override;

        QtHttpTransport(const QtHttpTransport &) = delete;
        QtHttpTransport &operator=(const QtHttpTransport &) = delete;

        void send(std::uint64_t requestId, const online::contracts::HttpRequest &request,
                  online::contracts::HttpCallback callback) override;

        void cancel(std::uint64_t requestId) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace sonora::platform::net
