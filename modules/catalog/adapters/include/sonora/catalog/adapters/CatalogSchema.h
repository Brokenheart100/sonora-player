// Catalog 模块的库结构。
//
// 迁移脚本只许**追加**：版本 1 一旦发布就冻结，哪怕它写得不好看。
// 改它等于让"用户库"和"新装的库"永久性地不一样，而且没有任何自动手段能发现。
#pragma once

#include <span>
#include <string_view>

#include "sonora/platform/storage/Migrator.h"

namespace sonora::catalog::adapters {

// 在 __module_version 表里的名字。每个模块一个，各自从 1 数版本号。
inline constexpr std::string_view kModuleName = "catalog";

[[nodiscard]] std::span<const platform::storage::Migration> migrations();

}  // namespace sonora::catalog::adapters
