// 网易云这一家的"发请求 + 收响应"，HTTP 之上再没有别的东西。
//
// 职责边界：拼 URL/头（core/provider）、解析 JSON（core/codec）、发请求（transport）。
// 这里只负责把它们串起来，并决定**在哪个线程串**——所有实际的发送动作都投给
// `SerialWorker`，因为 `QNetworkAccessManager` 只能在自己那条线程上用。
//
// 回调也在 worker 线程执行。调用方（OnlineService）必须把它当成跨线程回调，
// 只许往队列里塞，不许直接改状态表或数据库。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sonora/online/contracts/Executor.h"
#include "sonora/online/contracts/Transport.h"
#include "sonora/online/core/codec/NeteaseJson.h"
#include "sonora/online/core/provider/NeteaseUrls.h"

namespace sonora::online::adapters
{

    class NeteaseProvider final
    {
    public:
        using Callback = std::function<void(core::codec::DecodeResult)>;
        // 取播放地址：一批最多 kMaxSongIds 首，返回的是"能播的那些"。
        using UrlCallback = std::function<void(core::codec::UrlResult)>;
        // 校验一个 cookie 是否有登录态。R4 的"粘贴 cookie"与"启动时恢复会话"
        // 都走它：唯一权威判定就是网易云自己怎么说。
        using AccountCallback = std::function<void(core::codec::AccountResult)>;

        NeteaseProvider(contracts::IHttpTransport &transport, contracts::IExecutor &executor,
                        std::string baseUrl = core::provider::kNeteaseBaseUrl);

        // 登录态。R2 恒为空串；R4 才会真的写进来。
        void setCookie(std::string cookie) { cookie_ = std::move(cookie); }

        // 返回 requestId，取消时原样交回 `cancel()`。
        std::uint64_t startSearch(core::provider::SearchParams params, Callback onDone);

        // 取一批曲目的直链。调用方负责分批（≤ kMaxSongIds）与串行化。
        std::uint64_t startResolveUrls(const std::vector<std::string> &songIds, UrlCallback onDone);

        // 用**传入的** cookie 校验登录态（不改 this->cookie_）：粘贴流程要先用
        // 它验一遍，验过了才值得落盘。
        std::uint64_t startValidateAccount(std::string cookie, AccountCallback onDone);

        using QrKeyCallback = std::function<void(core::codec::QrKeyResult)>;
        using QrPollCallback = std::function<void(core::codec::QrPollResult)>;

        // 扫码登录：先要 unikey，再拿 unikey 轮询。两次都不带 cookie。
        std::uint64_t startQrKey(QrKeyCallback onDone);
        std::uint64_t startQrPoll(std::string unikey, QrPollCallback onDone);

        void cancel(std::uint64_t requestId);

    private:
        contracts::IHttpTransport &transport_;
        contracts::IExecutor &executor_;
        std::string baseUrl_;
        std::string cookie_;
        std::atomic<std::uint64_t> nextId_{1};
    };

} // namespace sonora::online::adapters
