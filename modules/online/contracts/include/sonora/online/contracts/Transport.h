// HTTP 传输端口 —— 全项目唯一一道"网络可替换"的缝。
//
// 为什么值得单独一个端口（其余 provider/store/session 都直接写成具体类）：
//   * 测试里注入假 transport，`online/core` 的编排逻辑不需要网络也能跑；
//   * `online/adapters` 因此不必链 QtNetwork —— 整个工程只有 platform/net 有它。
//
// 接口是**异步回调**，不是阻塞 send()。Qt 的原生形态就是"事件循环 + 信号"，
// 阻塞版要靠局部事件循环把异步 API 掰回同步，多一层循环、多一处"谁在等谁"。
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "sonora/kernel/Result.h"

namespace sonora::online::contracts
{

    enum class HttpMethod : std::uint8_t
    {
        Get,
        Post,
    };

    struct HttpRequest
    {
        HttpMethod method{HttpMethod::Get};
        // 必须是 https://（明文直接拒绝）。播放用的直链是 http://，
        // 但那些地址不经过这里——它们直接交给 FFmpeg（见 docs/ONLINE.md §7.1）。
        std::string url;
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        // 一次设定覆盖 DNS + 连接 + TLS + 等首字节（传输层负责落实）。
        std::chrono::milliseconds transferTimeout{8000};
        std::int64_t maxBodyBytes{8 * 1024 * 1024};
    };

    struct HttpResponse
    {
        int statusCode{0};
        std::string body;
        std::string contentType;
        std::chrono::milliseconds elapsed{};
        // 响应头，键统一小写。扫码登录成功时 cookie 只在 Set-Cookie 里，
        // body 里一句 `{"code":803}` 什么都不带——所以这一层必须透出响应头。
        std::vector<std::pair<std::string, std::string>> headers;
    };

    using HttpCallback = std::function<void(kernel::Result<HttpResponse>)>;

    class IHttpTransport
    {
    public:
        virtual ~IHttpTransport() = default;

        // 立刻返回，回调在 transport 自己的线程（worker 线程）上触发。
        // HTTP 4xx/5xx **也是 Ok(HttpResponse)**：状态码 → 语义的翻译只有一处
        // （online/core/policy/ErrorMap.h），传输层不做业务判断。
        virtual void send(std::uint64_t requestId, const HttpRequest &request, HttpCallback callback) = 0;

        // 取消一个在飞的请求；已完成或未知 id 是空操作（幂等）。
        virtual void cancel(std::uint64_t requestId) = 0;
    };

} // namespace sonora::online::contracts
