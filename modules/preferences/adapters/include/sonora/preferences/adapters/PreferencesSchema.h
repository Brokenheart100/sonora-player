// Preferences 模块的库结构。
//
// 这个模块是支撑域里唯一"不套 DDD"的地方（docs/ARCHITECTURE.md §6.2 的例外条款）：
// 一张两列的键值表，没有聚合、没有不变量、没有领域事件。
// 硬给它造一个 Setting 聚合只会多出四层包装，而它要做的仍然是 SELECT/UPDATE。
#pragma once

#include <span>
#include <string_view>

#include "sonora/platform/storage/Migrator.h"

namespace sonora::preferences::adapters {

inline constexpr std::string_view kModuleName = "preferences";

[[nodiscard]] std::span<const platform::storage::Migration> migrations();

}  // namespace sonora::preferences::adapters
