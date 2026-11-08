// 设置服务：内存副本 + 立即落库。
//
// 为什么不让查询直接回库：音量滑杆拖动时每帧都可能读一次设置，
// 每次开一条 SELECT 既慢又把"读"变成了可能失败的操作。启动时读一遍、
// 之后读内存，写的瞬间才落库。
//
// 为什么没有"脏了再批量刷"：那需要定时器、需要处理退出时的最后一次刷、
// 还要面对"进程被强杀"——设置的写入量以每分钟个位数计，不值得。
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <sonora/kernel/ILogger.h>
#include <sonora/kernel/Result.h>

#include "sonora/preferences/contracts/Settings.h"
#include "sonora/preferences/core/ports/SettingsStore.h"

namespace sonora::preferences::core {

class PreferencesService {
public:
    // store 的生命周期必须长于本对象。
    explicit PreferencesService(ISettingsStore &store, kernel::ILogger *logger = nullptr);

    PreferencesService(const PreferencesService &) = delete;
    PreferencesService &operator=(const PreferencesService &) = delete;

    // 一个键。没写过就 found=false。
    [[nodiscard]] contracts::SettingValue get(std::string_view key) const;

    // 一批键，顺序与入参一致。
    [[nodiscard]] contracts::SettingList getMany(const std::vector<std::string> &keys) const;

    // 带默认值的读取：调用方总有一个"没设置过时该是什么"的答案，
    // 让它自己写 `if (!found)` 是重复劳动。
    [[nodiscard]] std::string getOrDefault(std::string_view key, std::string_view fallback) const;

    // 写一个键：先落库，成功才更新内存副本。反过来会造出"内存说有、库里没有"
    // 的分裂状态——重启之后设置悄悄回退，而日志里一片安静。
    [[nodiscard]] kernel::Result<void> set(std::string_view key, std::string_view value);

    // 删一个键。库里没有也算成功。
    [[nodiscard]] kernel::Result<void> remove(std::string_view key);

    // 库里已有的键数量，给"设置页显示存储是否可用"这类探活用。
    [[nodiscard]] std::size_t size() const noexcept { return cache_.size(); }

private:
    ISettingsStore &store_;
    kernel::ILogger *logger_ = nullptr;
    // std::less<> 让 string_view 也能直接查，不必先构造 std::string。
    std::map<std::string, std::string, std::less<>> cache_;
};

}  // namespace sonora::preferences::core
