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

} // namespace sonora::online::contracts
