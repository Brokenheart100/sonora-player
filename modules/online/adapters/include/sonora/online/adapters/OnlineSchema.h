// 在线音源自己的表。
//
// 建表理由只有一条：**搜索与直链都是会过期的东西，而它们不该每次启动都重打一遍**。
// 不建"在线曲目表"是刻意的——在线曲目不入库（docs/ONLINE.md §1.3），
// 这里存的只有"响应原文 + 取回时刻"，用完即可丢，删掉整库不影响曲库。
#pragma once

#include <span>
#include <string_view>

#include "sonora/platform/storage/Migrator.h"

namespace sonora::online::adapters
{

    inline constexpr std::string_view kModuleName{"online"};

    // 迁移只许追加（见 platform/storage/Migrator.h 顶部的注释）。
    [[nodiscard]] std::span<const platform::storage::Migration> migrations();

} // namespace sonora::online::adapters
