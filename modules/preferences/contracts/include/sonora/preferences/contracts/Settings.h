// 设置的 DTO。
//
// 这里刻意只有 key/value 两列：设置项的名字由使用者约定，本模块不认识
// "音量""主题"这类业务概念——一旦认识，加一个设置就要改一次模块，
// 而"设置"这种东西的条目数量是只增不减的。
#pragma once

#include <string>
#include <vector>

namespace sonora::preferences::contracts {

// 一个键的当前值。found=false 表示这个键从来没写过（不是错误：
// 调用方应当带着自己的默认值来问，而不是要求每个键都必须存在）。
struct SettingValue {
    std::string key;
    std::string value;
    bool found = false;
};

// 一批键的值。顺序与请求的 keys 一致，缺的键以 found=false 占位，
// 这样调用方不用再自己对齐一遍下标。
struct SettingList {
    std::vector<SettingValue> entries;
};

}  // namespace sonora::preferences::contracts
