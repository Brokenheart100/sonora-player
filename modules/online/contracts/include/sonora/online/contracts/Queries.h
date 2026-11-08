// 在线音源的查询：只读内存里的状态表，全部 < 1ms。
//
// 查询不碰网络、不碰 SQLite 写路径——网络结果先落到服务自己的状态表里，
// 查询只做一次投影。这样 20Hz 的界面轮询不会变成 20Hz 的 HTTP 客户端。
#pragma once

#include <cstdint>
#include <string>

#include "sonora/online/contracts/Types.h"

namespace sonora::online::contracts
{

    // → OnlineJobState
    struct GetOnlineJob
    {
        std::uint64_t token{0};
    };

    // → OnlinePageDto；offset/limit 允许界面只取自己缺的那一段
    struct FetchOnlineRows
    {
        std::uint64_t token{0};
        std::int32_t offset{0};
        std::int32_t limit{30};
    };

    // → SourceStatusDto
    struct GetSourceStatus
    {
        std::string provider{"ncm"};
    };

    // → QrLoginDto
    struct GetQrLoginState
    {
        std::string provider{"ncm"};
    };

    // → std::vector<PlaylistDto>
    struct GetMyPlaylists
    {
        std::string provider{"ncm"};
    };

    // → OnlinePlaybackDto
    // 表现层 50ms 轮询里顺带问一次；revision 变了才真的重建播放队列。
    struct GetOnlinePlayback
    {
    };

    // → CollectionPageDto
    // 只读服务里已经拉回来的那批卡片；真正发起请求的是 LoadCollections 命令。
    struct FetchCollections
    {
        SourceKind kind{SourceKind::Playlist};
    };

    // → CollectionDetailDto
    // 歌单/榜单/专辑/歌手的头部。没拉过就返回 ok=false，界面显示"加载中"而不是报错。
    struct FetchCollectionDetail
    {
        SourceKind kind{SourceKind::Playlist};
        std::string collectionId;
    };

    // → OnlineDiagnosticsDto
    // 一次性快照（连续失败、令牌余量、缓存条数、最近 10 条错误）。刻意不进
    // 50ms 轮询：它要查两次 COUNT，只在用户点开诊断面板时问。
    struct GetOnlineDiagnostics
    {
        std::string provider{"ncm"};
    };

} // namespace sonora::online::contracts
