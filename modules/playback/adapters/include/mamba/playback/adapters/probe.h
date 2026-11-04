// M0 构建探针。存在的唯一目的：让本 target 拥有一个真实翻译单元与一个导出符号，
// 从而证明「include 路径由 target 图授予」这条边界机制确实生效。
// 落地真实代码的里程碑应删除本文件（同时从 targets.lua 的 src 列表里消失）。
#pragma once

namespace mamba::playback::adapters {

/// 本 target 的探针编号。跨 target 链接成功时，各模块返回值互不相同。
int probe() noexcept;

}  // namespace mamba::playback::adapters
