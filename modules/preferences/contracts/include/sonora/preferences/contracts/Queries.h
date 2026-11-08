// Preferences 的查询（读侧意图）。
//
// 读设置是高频动作（音量滑杆一拖就是几十次），所以服务层持内存副本；
// 这里的查询命中缓存，不回库。
#pragma once

#include <string>
#include <vector>

#include "sonora/preferences/contracts/Settings.h"

namespace sonora::preferences::contracts {

// 一个键。没有就返回 found=false，不是错误。
struct GetSetting {
    std::string key;
};

// 一批键。用来让界面一次性取回"需要在启动时恢复的一组设置"。
struct GetSettings {
    std::vector<std::string> keys;
};

}  // namespace sonora::preferences::contracts
