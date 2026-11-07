#include "sonora/online/adapters/NeteaseProvider.h"

#include <chrono>
#include <utility>

namespace sonora::online::adapters
{
    namespace c = online::contracts;
    namespace codec = online::core::codec;
    namespace provider = online::core::provider;

    namespace
    {
        // 响应本身拿到了，但状态码不是 200：这不是"网络失败"，是"对面拒绝了"。
        // 5xx 与 429 值得重试，其余不值得（4xx 重试只是把错误再问一遍）。
        codec::DecodeResult fromStatus(int statusCode)
        {
            codec::DecodeResult out;
            out.error.code = statusCode >= 500 || statusCode == 429 ? kernel::ErrorCode::Unavailable
                                                                   : kernel::ErrorCode::Upstream;
            out.error.message = "网易云返回 HTTP " + std::to_string(statusCode);
            out.error.retryable = statusCode >= 500 || statusCode == 429;
            out.error.retryAfterMs = statusCode == 429 ? 2000 : 0;
            return out;
        }
    } // namespace

    NeteaseProvider::NeteaseProvider(c::IHttpTransport &transport, c::IExecutor &executor, std::string baseUrl)
        : transport_(transport), executor_(executor), baseUrl_(std::move(baseUrl))
    {
    }

    std::uint64_t NeteaseProvider::startSearch(provider::SearchParams params, Callback onDone)
    {
        const std::uint64_t requestId = nextId_.fetch_add(1, std::memory_order_relaxed);

        // cookie/baseUrl 按值捕获：命令在主线程调用，任务在 worker 线程执行，
        // 中间这段时间里 setCookie() 可能已经把成员改掉了。
        const std::string baseUrl = baseUrl_;
        const std::string cookie = cookie_;
        const std::string keywords = params.keywords;

        executor_.post([this, requestId, params, keywords, baseUrl, cookie, onDone = std::move(onDone)] {
            auto request = provider::makeSearchRequest(baseUrl, params, std::chrono::milliseconds{8000});
            request.headers = provider::defaultHeaders(cookie);

            transport_.send(requestId, request,
                            [onDone](kernel::Result<c::HttpResponse> response) {
                                if (!response)
                                {
                                    codec::DecodeResult failed;
                                    const kernel::Error &error = response.error();
                                    failed.error.code = error.code();
                                    failed.error.message = error.message();
                                    // 超时与"连不上"值得重试；被取消不值得（用户已经不要了）。
                                    failed.error.retryable = error.code() == kernel::ErrorCode::Timeout ||
                                                             error.code() == kernel::ErrorCode::Unavailable;
                                    if (onDone)
                                    {
                                        onDone(std::move(failed));
                                    }
                                    return;
                                }

                                const c::HttpResponse &http = response.value();
                                codec::DecodeResult decoded;
                                if (http.statusCode != 200)
                                {
                                    decoded = fromStatus(http.statusCode);
                                }
                                else
                                {
                                    decoded = codec::decodeSearch(http.body);
                                    // 原样留一份给缓存：缓存存原文，解析器升级后旧条目自动按新规则解析。
                                    decoded.raw = http.body;
                                }
                                if (onDone)
                                {
                                    onDone(std::move(decoded));
                                }
                            });
        });

        (void)keywords;
        return requestId;
    }

    std::uint64_t NeteaseProvider::startQrKey(QrKeyCallback onDone)
    {
        const std::uint64_t requestId = nextId_.fetch_add(1);

        executor_.post([this, requestId, onDone = std::move(onDone)] {
            auto request = provider::makeQrKeyRequest(baseUrl_, std::chrono::milliseconds{8000});
            transport_.send(requestId, request,
                            [onDone = std::move(onDone)](kernel::Result<c::HttpResponse> response) {
                                codec::QrKeyResult out;
                                if (!response)
                                {
                                    const auto &failed = response.error();
                                    out.error.code = failed.code();
                                    out.error.message = failed.message();
                                    out.error.retryable = failed.code() == kernel::ErrorCode::Timeout ||
                                                          failed.code() == kernel::ErrorCode::Unavailable;
                                }
                                else
                                {
                                    const c::HttpResponse &http = response.value();
                                    if (http.statusCode != 200)
                                    {
                                        out.error.code = http.statusCode >= 500 || http.statusCode == 429
                                                             ? kernel::ErrorCode::Unavailable
                                                             : kernel::ErrorCode::Upstream;
                                        out.error.message = "网易云返回 HTTP " + std::to_string(http.statusCode);
                                        out.error.retryable = out.error.code == kernel::ErrorCode::Unavailable;
                                    }
                                    else
                                    {
                                        out = codec::decodeQrKey(http.body);
                                    }
                                }
                                onDone(out);
                            });
        });
        return requestId;
    }

    std::uint64_t NeteaseProvider::startQrPoll(std::string unikey, QrPollCallback onDone)
    {
        const std::uint64_t requestId = nextId_.fetch_add(1);

        executor_.post([this, requestId, unikey = std::move(unikey), onDone = std::move(onDone)] {
            auto request = provider::makeQrPollRequest(baseUrl_, unikey, std::chrono::milliseconds{6000});
            transport_.send(requestId, request,
                            [onDone = std::move(onDone)](kernel::Result<c::HttpResponse> response) {
                                codec::QrPollResult out;
                                if (!response)
                                {
                                    const auto &failed = response.error();
                                    out.error.code = failed.code();
                                    out.error.message = failed.message();
                                    out.error.retryable = failed.code() == kernel::ErrorCode::Timeout ||
                                                          failed.code() == kernel::ErrorCode::Unavailable;
                                }
                                else
                                {
                                    const c::HttpResponse &http = response.value();
                                    if (http.statusCode != 200)
                                    {
                                        out.error.code = http.statusCode >= 500 || http.statusCode == 429
                                                             ? kernel::ErrorCode::Unavailable
                                                             : kernel::ErrorCode::Upstream;
                                        out.error.message = "网易云返回 HTTP " + std::to_string(http.statusCode);
                                        out.error.retryable = out.error.code == kernel::ErrorCode::Unavailable;
                                    }
                                    else
                                    {
                                        out = codec::decodeQrPoll(http.body, http.headers);
                                    }
                                }
                                onDone(out);
                            });
        });
        return requestId;
    }

    std::uint64_t NeteaseProvider::startResolveUrls(const std::vector<std::string> &songIds,
                                                    UrlCallback onDone)
    {
        const std::uint64_t requestId = nextId_.fetch_add(1, std::memory_order_relaxed);
        const std::string baseUrl = baseUrl_;
        const std::string cookie = cookie_;

        executor_.post([this, requestId, songIds, baseUrl, cookie, onDone = std::move(onDone)] {
            auto request = provider::makeSongUrlRequest(baseUrl, songIds, "exhigh",
                                                        std::chrono::milliseconds{6000});
            request.headers = provider::defaultHeaders(cookie);

            transport_.send(requestId, request,
                            [onDone](kernel::Result<c::HttpResponse> response) {
                                codec::UrlResult out;
                                if (!response)
                                {
                                    const kernel::Error &error = response.error();
                                    out.error.code = error.code();
                                    out.error.message = error.message();
                                    out.error.retryable = error.code() == kernel::ErrorCode::Timeout ||
                                                          error.code() == kernel::ErrorCode::Unavailable;
                                    if (onDone)
                                    {
                                        onDone(std::move(out));
                                    }
                                    return;
                                }

                                const c::HttpResponse &http = response.value();
                                if (http.statusCode != 200)
                                {
                                    out.error.code = http.statusCode >= 500 || http.statusCode == 429
                                                         ? kernel::ErrorCode::Unavailable
                                                         : kernel::ErrorCode::Upstream;
                                    out.error.message = "网易云返回 HTTP " + std::to_string(http.statusCode);
                                    out.error.retryable = out.error.code == kernel::ErrorCode::Unavailable;
                                }
                                else
                                {
                                    out = codec::decodeSongUrls(http.body);
                                }
                                if (onDone)
                                {
                                    onDone(std::move(out));
                                }
                            });
        });

        return requestId;
    }

    std::uint64_t NeteaseProvider::startValidateAccount(std::string cookie, AccountCallback onDone)
    {
        const std::uint64_t requestId = nextId_.fetch_add(1);

        executor_.post([this, requestId, cookie = std::move(cookie), onDone = std::move(onDone)] {
            auto request = provider::makeAccountRequest(baseUrl_, cookie, std::chrono::milliseconds{8000});
            transport_.send(requestId, request,
                            [onDone = std::move(onDone)](kernel::Result<contracts::HttpResponse> response) {
                                codec::AccountResult out;
                                if (!response)
                                {
                                    const auto &failed = response.error();
                                    out.error.code = failed.code();
                                    out.error.message = failed.message();
                                    out.error.retryable = failed.code() == kernel::ErrorCode::Timeout ||
                                                          failed.code() == kernel::ErrorCode::Unavailable;
                                }
                                else
                                {
                                    const c::HttpResponse &http = response.value();
                                    if (http.statusCode != 200)
                                    {
                                        out.error.code = http.statusCode >= 500 || http.statusCode == 429
                                                             ? kernel::ErrorCode::Unavailable
                                                             : kernel::ErrorCode::Upstream;
                                        out.error.message =
                                            "网易云返回 HTTP " + std::to_string(http.statusCode);
                                        out.error.retryable =
                                            out.error.code == kernel::ErrorCode::Unavailable;
                                    }
                                    else
                                    {
                                        out = codec::decodeAccount(http.body);
                                    }
                                }
                                if (onDone)
                                {
                                    onDone(std::move(out));
                                }
                            });
        });

        return requestId;
    }

    void NeteaseProvider::cancel(std::uint64_t requestId)
    {
        // 取消也必须回到 worker 线程做：`abort()` 作用在 QNetworkReply 上，
        // 从别的线程调它等于跨线程碰一个没有加锁的对象。
        executor_.post([this, requestId] { transport_.cancel(requestId); });
    }

} // namespace sonora::online::adapters
