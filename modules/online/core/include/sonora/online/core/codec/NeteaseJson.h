// 网易云响应的解析：JSON 字符串 → 契约 DTO。只此一处。
//
// 三条规矩：
//   * **不用异常**。`json::parse(..., allow_exceptions=false)`，解析失败就是一条
//     `Internal`，不会把整个进程掀掉。
//   * **字段缺失/为 null 一律降级**，不抛也不猜：`name` 为 null 就当空串，
//     `duration`/`dt` 缺就当 0（界面显示 `--:--`），`id` 是数字还是字符串都收。
//   * **不认识的值原样保留**：超长标题不截断（截断是显示层的事），
//     `alias` 之类这次用不上的字段干脆不读。
//
// 放在 core 而不是 adapters：这里零 Qt、零 IO，夹具单测能直接跑进 sonora_tests。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sonora/online/contracts/Types.h"

namespace sonora::online::core::codec
{

    struct DecodeResult
    {
        // 解析成功且业务码是 200。false 时看 `error`。
        bool ok{false};
        contracts::OnlineError error;
        std::vector<contracts::OnlineTrackDto> rows;
        std::int32_t total{0};
        bool hasMore{false};

        // 原始响应体。只由 adapters 填（codec 自己不认识 HTTP），
        // 用来把"刚拿到的这一页"原样存进缓存。
        std::string raw;
    };

    // `/api/search/get` 与 `/api/cloudsearch/pc` 两种形状都认：
    // 前者是 `artists/album/duration`，后者是 `ar/al/dt`（R0 两种都抓了夹具）。
    [[nodiscard]] DecodeResult decodeSearch(const std::string &body);

} // namespace sonora::online::core::codec
