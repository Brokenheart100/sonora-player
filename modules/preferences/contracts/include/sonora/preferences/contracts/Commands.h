// Preferences 的命令（写侧意图）。
//
// 一条设置一次写入，没有"批量事务"：一行写坏不该拖累别的行，
// 而设置本来就是一条一条改的。
#pragma once

#include <string>

namespace sonora::preferences::contracts {

// 写一个键。已存在就覆盖。
struct SetSetting {
    std::string key;
    std::string value;
};

// 删一个键。不存在也算成功（幂等：重复卸载、双开退出都别报错）。
struct RemoveSetting {
    std::string key;
};

}  // namespace sonora::preferences::contracts
