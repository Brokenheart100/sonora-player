// 把 PreferencesService 挂到两条总线上。
//
// 与 PlaybackHandlers 同一套做法：每个 Handler 只是一层薄壳，
// 校验在服务里，这里只做"命令 → 服务方法"的转调。
#pragma once

#include <sonora/kernel/CommandBus.h>
#include <sonora/kernel/QueryBus.h>
#include <sonora/preferences/contracts/Commands.h>
#include <sonora/preferences/contracts/Queries.h>
#include <sonora/preferences/core/app/PreferencesService.h>

namespace sonora::preferences::core {

class GetSettingHandler {
public:
    explicit GetSettingHandler(const PreferencesService &service) : service_(service) {}
    [[nodiscard]] kernel::Result<contracts::SettingValue> handle(
        const contracts::GetSetting &query) const
    {
        return kernel::Result<contracts::SettingValue>::ok(service_.get(query.key));
    }

private:
    const PreferencesService &service_;
};

class GetSettingsHandler {
public:
    explicit GetSettingsHandler(const PreferencesService &service) : service_(service) {}
    [[nodiscard]] kernel::Result<contracts::SettingList> handle(
        const contracts::GetSettings &query) const
    {
        return kernel::Result<contracts::SettingList>::ok(service_.getMany(query.keys));
    }

private:
    const PreferencesService &service_;
};

class SetSettingHandler {
public:
    explicit SetSettingHandler(PreferencesService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::SetSetting &command)
    {
        return service_.set(command.key, command.value);
    }

private:
    PreferencesService &service_;
};

class RemoveSettingHandler {
public:
    explicit RemoveSettingHandler(PreferencesService &service) : service_(service) {}
    kernel::Result<void> handle(const contracts::RemoveSetting &command)
    {
        return service_.remove(command.key);
    }

private:
    PreferencesService &service_;
};

// 注册全部四个 handler。返回类型是 void：注册不该失败。
inline void registerPreferences(kernel::CommandBus &commands, kernel::QueryBus &queries,
                                PreferencesService &service)
{
    commands.on<contracts::SetSetting>(
        std::make_shared<SetSettingHandler>(service));
    commands.on<contracts::RemoveSetting>(
        std::make_shared<RemoveSettingHandler>(service));
    // 查询总线只收可调用对象（不像命令总线还收 shared_ptr）：handler 是无状态
    // 薄壳，就地构造一个再调用即可。捕获的是 service 的引用，它比总线活得久。
    queries.on<contracts::GetSetting, contracts::SettingValue>(
        [&service](const contracts::GetSetting &query) {
            return GetSettingHandler{service}.handle(query);
        });
    queries.on<contracts::GetSettings, contracts::SettingList>(
        [&service](const contracts::GetSettings &query) {
            return GetSettingsHandler{service}.handle(query);
        });
}

}  // namespace sonora::preferences::core
