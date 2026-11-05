#include <mamba/playback/core/domain/ShufflePlanner.h>

#include <algorithm>
#include <numeric>

namespace mamba::playback::core {

std::vector<std::int32_t> ShufflePlanner::plan(std::int32_t size, std::int32_t first,
                                               std::uint64_t seed) {
    if (size <= 0) {
        return {};
    }

    std::vector<std::int32_t> order(static_cast<std::size_t>(size));
    std::iota(order.begin(), order.end(), 0);

    // 先把 first 提到最前面，再对**后面那段**做 Fisher–Yates。
    const auto head = std::clamp<std::int32_t>(first, 0, size - 1);
    std::swap(order[0], order[static_cast<std::size_t>(head)]);

    // 只洗 [1, size)：循环写到 i > 1 为止。写成 i > 0 会把 order[0]
    // 也当成候选换走——那样"第一首固定是用户点的那首"就不成立了。
    std::mt19937_64 rng{seed};
    for (std::size_t i = order.size() - 1; i > 1; --i) {
        std::uniform_int_distribution<std::size_t> dist{1, i};
        std::swap(order[i], order[dist(rng)]);
    }
    return order;
}

std::uint64_t ShufflePlanner::freshSeed() {
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32) ^ static_cast<std::uint64_t>(device());
}

} // namespace mamba::playback::core
