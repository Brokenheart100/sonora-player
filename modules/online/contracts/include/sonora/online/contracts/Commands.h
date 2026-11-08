// 在线音源的命令。
//
// 全部**同步返回、单个 < 5ms**：网络活在 `platform/net` 的串行 worker 线程上，
// 命令只负责记账（登记 token、重置状态、投一个任务），然后立刻返回。
// 结果由界面轮询 `GetOnlineJob` 拿——这条链上没有回调穿过线程边界，
// 也就不存在"回调在哪个线程执行"的问题（见 docs/ONLINE.md §4.1）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sonora/online/contracts/Types.h"

namespace sonora::online::contracts
{

    // 请求一页结果。`token` 由**界面**递增生成：旧 token 的迟到结果直接丢弃，
    // 这是"连打 5 个字只发 1 次请求"的实现基础（另一半是 280ms 防抖）。
    //
    // `collectionId` 是歌单 / 榜单 / 专辑 / 歌手的 id：**共用一个字段**，
    // 不为每种来源加一条命令——它们要的都是"一页曲目"。
    struct RequestOnlinePage
    {
        std::uint64_t token{0};
        SourceKind kind{SourceKind::Search};
        std::string keywords;
        std::string playlistId;
        std::string collectionId;
        std::int32_t offset{0};
        std::int32_t limit{30};
    };

    // 拉一页"卡片"（推荐歌单 / 榜单列表）。与 RequestOnlinePage 分开：
    // 卡片列表没有分页游标之外的输入，也不需要用户可见的 job 状态机。
    struct LoadCollections
    {
        SourceKind kind{SourceKind::Playlist};
        std::int32_t limit{30};
    };

    struct CancelOnlineJob
    {
        std::uint64_t token{0};
    };

    struct RetryOnlineJob
    {
        std::uint64_t token{0};
    };

    // 解析整队直链后交给表现层拼 PlayTracks。
    // **不在网络线程里发播放命令**：播放是主线程的事，网络线程只填地址表。
    struct ResolveAndPlayOnline
    {
        std::uint64_t token{0};
        std::vector<std::string> songIds;
        std::int32_t startIndex{0};
        SourceKind origin{SourceKind::Search};
    };

    // 含秘密：cookie 只许出现在这一个字段里，不许进日志、事件、DTO。
    struct SignInWithCookie
    {
        std::string provider{"ncm"};
        std::string cookie;
    };

    struct SignOut
    {
        std::string provider{"ncm"};
    };

    struct StartQrLogin
    {
        std::string provider{"ncm"};
    };

    struct CancelQrLogin
    {
        std::string provider{"ncm"};
    };

} // namespace sonora::online::contracts
