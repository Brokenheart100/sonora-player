// Catalog 的共享契约：模块对外的数据形状与用户意图。
//
// 这个 target **只依赖 Kernel**，而且被 Sonora.Presentation 直接依赖——也就是说
// 表现层与仓储层看到的是同一份 DTO/命令定义，但它们谁都不知道领域模型长什么样。
// 这就是"加个命令要不要改 12 个文件"那个问题的答案：改契约是跨层事件，改实现不是。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sonora::catalog::contracts {

// 读侧的行。字段是**扁平**的、直接能画到界面上的：
// 没有 artistId 让 UI 自己再查一次名字，没有 duration（只有毫秒）让 UI 自己算格式。
// 把"显示需要什么"留在写侧，是所有列表页卡顿的起源。
struct TrackDto {
    std::uint64_t id = 0;
    std::string title;
    std::string artistName;
    std::string albumTitle;
    std::int64_t durationMs = 0;
    std::string filePath;

    // 封面图片的绝对路径（可能为空）。这里给**路径**而不是字节：
    // 字节走一遍总线的代价是每行几十 KB 穿过类型擦除的返回值，
    // 而界面真正需要的是"去哪读"。去重、落盘、编码都是导入时的事。
    std::string coverPath;
};

}  // namespace sonora::catalog::contracts
