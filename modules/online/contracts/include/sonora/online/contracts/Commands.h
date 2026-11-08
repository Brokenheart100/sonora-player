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

    // 取一批私人 FM 曲目（≈3 首），由组合根接到播放队列尾部。
    //
    // 为什么不是"点一下播放"：FM 是无限流，一次只取一批；播到队尾时
    // Playback 发 QueueNeedsMore，组合根再发这个命令续一批。
    struct StartPersonalFm
    {
        std::int32_t count{3};
    };

    /// "不感兴趣"：歌曲不再出现（本地黑名单，不发请求）。
    struct DislikeFmSong
    {
        std::string songId;
    };

    // 拉一次"我的歌单"。结果缓存进服务里，界面用 `GetMyPlaylists` 取。
    // 未登录时**不把任务发出去**：游客点"我的"看到的应该是"登录后可见"，
    // 而不是一条"未登录"错误（AGENTS：降级不消耗令牌）。
    struct LoadMyPlaylists
    {
    };

    // 收藏 / 取消收藏。写操作只有这一个，参数就是"哪张歌单、哪首歌、加还是删"。
    // `playlistId` 在"我喜欢"场景下是用户的喜欢歌单 id（由界面传入）。
    struct ToggleFavorite
    {
        std::string songId;
        std::string playlistId;
        bool add{true};
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
