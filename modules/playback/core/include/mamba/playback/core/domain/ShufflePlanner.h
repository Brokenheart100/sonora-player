// 随机播放的排列生成。
//
// 为什么不"每次抽一首"：那样"上一首"只能靠历史栈近似，而历史栈在
// 用户手动跳转、循环回头之后就不准了。预生成一个完整排列 + 游标，
// "上一首"就是游标减一，永远精确。
//
// 排列用固定种子生成，因此**可序列化、可复原**：把 seed + 排列存进库，
// 下次启动还能接着放（M6 的队列持久化用它）。
#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace mamba::playback::core {

class ShufflePlanner {
public:
    /// 生成 [0, size) 的一个排列，且**第一项固定是 first**。
    ///
    /// first 固定的理由：用户点了第 7 行才开始随机播放，那第 7 行必须
    /// 是现在放的这首，而不是"随机到 7 之后再说"。
    /// size <= 0 返回空；first 越界时按 0 处理。
    [[nodiscard]] static std::vector<std::int32_t> plan(std::int32_t size, std::int32_t first,
                                                        std::uint64_t seed);

    /// 进程内取一个种子。单独一个函数是为了让测试能换成固定值。
    [[nodiscard]] static std::uint64_t freshSeed();
};

} // namespace mamba::playback::core
