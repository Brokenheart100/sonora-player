#include "sonora/online/adapters/NeteaseProvider.h"

#include <algorithm>
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
    {        const std::uint64_t requestId = nextId_.fetch_add(1);

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

    // ------------------------------------------------------------------ N1

    namespace
    {
        // 三种返回类型（DecodeResult / CollectionListResult / PlaylistDetailResult）
        // 都有 `error` 字段，所以"HTTP 层失败怎么填"可以只有这一处。
        template <typename T>
        void fillTransportError(T &out, const kernel::Error &error)
        {
            out.error.code = error.code();
            out.error.message = error.message();
            // 超时与"连不上"值得重试；被取消不值得（用户已经不要了）。
            out.error.retryable =
                error.code() == kernel::ErrorCode::Timeout || error.code() == kernel::ErrorCode::Unavailable;
        }

        template <typename T>
        void fillStatusError(T &out, int statusCode)
        {
            out.error.code =
                statusCode >= 500 || statusCode == 429 ? kernel::ErrorCode::Unavailable : kernel::ErrorCode::Upstream;
            out.error.message = "网易云返回 HTTP " + std::to_string(statusCode);
            out.error.retryable = statusCode >= 500 || statusCode == 429;
            out.error.retryAfterMs = statusCode == 429 ? 2000 : 0;
        }
    } // namespace

    template <typename T, typename Decode>
    std::uint64_t NeteaseProvider::startGet(std::string path, Decode decode, std::function<void(T)> onDone)
    {
        const std::uint64_t requestId = nextId_.fetch_add(1, std::memory_order_relaxed);

        // 与 startSearch 一致：baseUrl/cookie 按值捕获，因为投递到 worker 线程的
        // 这段时间里 setCookie() 可能已经把成员改掉。
        const std::string baseUrl = baseUrl_;
        const std::string cookie = cookie_;

        executor_.post([this, requestId, path = std::move(path), baseUrl, cookie, decode = std::move(decode),
                        onDone = std::move(onDone)]() mutable {
            c::HttpRequest request;
            request.method = c::HttpMethod::Get;
            request.url = baseUrl + path;
            request.headers = provider::defaultHeaders(cookie);
            request.transferTimeout = std::chrono::milliseconds{8000};

            transport_.send(
                requestId, request,
                [decode = std::move(decode), onDone = std::move(onDone)](kernel::Result<c::HttpResponse> response) {
                    T out{};
                    if (!response)
                    {
                        fillTransportError(out, response.error());
                    }
                    else
                    {
                        const c::HttpResponse &http = response.value();
                        if (http.statusCode != 200)
                        {
                            fillStatusError(out, http.statusCode);
                        }
                        else
                        {
                            out = decode(http.body);
                            // 原样留一份给缓存：解析器升级后旧条目自动按新规则解析。
                            out.raw = http.body;
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

    std::uint64_t NeteaseProvider::startPlaylistDetail(std::string playlistId, PlaylistDetailCallback onDone)
    {
        return startGet<codec::PlaylistDetailResult>(
            "/api/v6/playlist/detail?id=" + provider::encodeUrlComponent(playlistId),
            [](const std::string &body) { return codec::decodePlaylistDetail(body, c::SourceKind::Playlist); },
            std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startSongDetails(std::vector<std::string> songIds, PageCallback onDone)
    {
        std::string ids = "[";
        const std::size_t limit = std::min<std::size_t>(
            songIds.size(), static_cast<std::size_t>(provider::kMaxSongDetailsPerRequest));
        for (std::size_t index = 0; index < limit; ++index)
        {
            if (index != 0)
            {
                ids.push_back(',');
            }
            ids += songIds[index];
        }
        ids.push_back(']');
        // 方括号是 gen-delims，不编码会被服务端当坏参数（实测回 code 400，界面表现为"空结果"）。
        return startGet<codec::DecodeResult>("/api/song/detail?ids=" + provider::encodeUrlComponent(ids),
                                             [](const std::string &body) { return codec::decodeSongDetails(body); },
                                             std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startAlbum(std::string albumId, PageCallback onDone)
    {
        return startGet<codec::DecodeResult>(
            "/api/v1/album/" + provider::encodeUrlComponent(albumId),
            [](const std::string &body) { return codec::decodeAlbum(body); }, std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startArtist(std::string artistId, PageCallback onDone)
    {
        return startGet<codec::DecodeResult>(
            "/api/v1/artist/" + provider::encodeUrlComponent(artistId),
            [](const std::string &body) { return codec::decodeArtist(body); }, std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startNewSongs(int limit, PageCallback onDone)
    {
        const int clamped = std::clamp(limit, 1, provider::kMaxCollectionLimit);
        return startGet<codec::DecodeResult>(
            "/api/personalized/newsong?limit=" + std::to_string(clamped),
            [](const std::string &body) { return codec::decodeNewSongs(body); }, std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startToplist(CollectionCallback onDone)
    {
        return startGet<codec::CollectionListResult>(
            "/api/toplist", [](const std::string &body) { return codec::decodeToplist(body); }, std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startPersonalizedPlaylists(int limit, CollectionCallback onDone)
    {
        const int clamped = std::clamp(limit, 1, provider::kMaxCollectionLimit);
        return startGet<codec::CollectionListResult>(
            "/api/personalized/playlist?limit=" + std::to_string(clamped),
            [](const std::string &body) { return codec::decodePersonalizedPlaylists(body); },
            std::move(onDone));
    }

    // ------------------------------------------------------------------ N2

    std::uint64_t NeteaseProvider::startUserPlaylists(const std::string &userId, PlaylistsCallback onDone)
    {
        return startGet<codec::PlaylistsResult>(
            provider::makeUserPlaylistsRequest(baseUrl_, userId, cookie_, provider::kMaxUserPlaylistsLimit,
                                               std::chrono::milliseconds{8000})
                .url.substr(baseUrl_.size()),
            [](const std::string &body) { return codec::decodeUserPlaylists(body); }, std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startDailySongs(PageCallback onDone)
    {
        return startGet<codec::DecodeResult>(
            "/api/v3/discovery/recommend/songs",
            [](const std::string &body) { return codec::decodeDailySongs(body); }, std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startLyric(std::string songId, LyricCallback onDone)
    {
        // 歌词接口不需要 cookie，但走同一条 GET 骨架（超时、状态码映射、解码器
        // 的错误填法都复用）——它自己那几个 maker 是给"要 POST/要特殊头"的接口用的。
        return startGet<codec::LyricResult>(
            "/api/song/lyric?id=" + provider::encodeUrlComponent(songId) + "&lv=-1&tv=-1",
            [](const std::string &body) { return codec::decodeLyric(body); }, std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startRadio(PageCallback onDone)
    {
        return startGet<codec::DecodeResult>("/api/v1/radio/get",
                                             [](const std::string &body) { return codec::decodeRadio(body); },
                                             std::move(onDone));
    }

    std::uint64_t NeteaseProvider::startToggleFavorite(const std::string &playlistId,
                                                      const std::string &songId, bool add,
                                                      const std::string &csrf, FavoriteCallback onDone)
    {
        const std::uint64_t requestId = nextId_.fetch_add(1, std::memory_order_relaxed);
        const std::string baseUrl = baseUrl_;
        const std::string cookie = cookie_;

        // 写操作不能走 startGet 的 GET 骨架：它是表单 POST，而且要求 cookie。
        executor_.post([this, requestId, playlistId, songId, add, csrf, baseUrl, cookie,
                        onDone = std::move(onDone)]() mutable {
            const auto request = provider::makeFavoriteToggleRequest(
                baseUrl, playlistId, songId, add, csrf, cookie, std::chrono::milliseconds{8000});
            transport_.send(
                requestId, request,
                [onDone = std::move(onDone)](kernel::Result<c::HttpResponse> response) {
                    codec::FavoriteResult out{};
                    if (!response)
                    {
                        fillTransportError(out, response.error());
                    }
                    else
                    {
                        const c::HttpResponse &http = response.value();
                        if (http.statusCode != 200)
                        {
                            fillStatusError(out, http.statusCode);
                        }
                        else
                        {
                            out = codec::decodeFavoriteToggle(http.body);
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

} // namespace sonora::online::adapters
