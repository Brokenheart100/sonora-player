#include "sonora/preferences/core/app/PreferencesService.h"

#include <utility>

namespace sonora::preferences::core {
namespace {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

// 空键名是调用方的 bug，不是用户的输入问题——提前挡住，
// 否则 SQLite 会老老实实插一行空字符串键，然后谁也说不清那是什么。
[[nodiscard]] Result<void> validateKey(std::string_view key)
{
    if (key.empty()) {
        return Error{ErrorCode::InvalidArgument, "设置项的键不能为空"};
    }
    return {};
}

}  // namespace

PreferencesService::PreferencesService(ISettingsStore &store, kernel::ILogger *logger)
    : store_(store), logger_(logger)
{
    auto loaded = store_.loadAll();
    if (!loaded) {
        // 读不出来不是致命错误：用空副本继续，用户改一条写一条，
        // 下一次启动就能读到。要是这里 return 错误，整个应用就起不来了。
        if (logger_ != nullptr) {
            logger_->warn("设置读取失败，本次运行使用空设置：{}", loaded.error().message());
        }
        return;
    }
    for (const auto &entry : loaded.value()) {
        cache_.insert_or_assign(entry.key, entry.value);
    }
}

contracts::SettingValue PreferencesService::get(std::string_view key) const
{
    contracts::SettingValue value;
    value.key = std::string{key};
    if (const auto it = cache_.find(key); it != cache_.end()) {
        value.value = it->second;
        value.found = true;
    }
    return value;
}

contracts::SettingList PreferencesService::getMany(const std::vector<std::string> &keys) const
{
    contracts::SettingList list;
    list.entries.reserve(keys.size());
    for (const auto &key : keys) {
        list.entries.push_back(get(key));
    }
    return list;
}

std::string PreferencesService::getOrDefault(std::string_view key, std::string_view fallback) const
{
    if (const auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }
    return std::string{fallback};
}

Result<void> PreferencesService::set(std::string_view key, std::string_view value)
{
    auto valid = validateKey(key);
    if (!valid) {
        return valid;
    }
    auto written = store_.upsert(key, value);
    if (!written) {
        return written;
    }
    cache_.insert_or_assign(std::string{key}, std::string{value});
    return {};
}

Result<void> PreferencesService::remove(std::string_view key)
{
    auto valid = validateKey(key);
    if (!valid) {
        return valid;
    }
    auto removed = store_.remove(key);
    if (!removed) {
        return removed;
    }
    cache_.erase(std::string{key});
    return {};
}

}  // namespace sonora::preferences::core
