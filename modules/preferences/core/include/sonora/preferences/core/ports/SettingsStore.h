// 键值存储的端口。
//
// 存储层只负责"把这两列搬进搬出"，不认识任何具体设置项，
// 也不做缓存——缓存是应用层的决定（见 PreferencesService）。
#pragma once

#include <string_view>
#include <vector>

#include <sonora/kernel/Result.h>

#include "sonora/preferences/contracts/Settings.h"

namespace sonora::preferences::core {

class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;

    // 一次把全部键读出来。启动时调一次，之后都走内存副本。
    [[nodiscard]] virtual kernel::Result<std::vector<contracts::SettingValue>> loadAll() const = 0;

    // 有则更新、无则插入。value 可以很长（将来放 JSON 也不会坏）。
    [[nodiscard]] virtual kernel::Result<void> upsert(std::string_view key, std::string_view value) = 0;

    // 删一个键。键不存在不算失败。
    [[nodiscard]] virtual kernel::Result<void> remove(std::string_view key) = 0;
};

}  // namespace sonora::preferences::core
