// 曲目列表查询的 handler。
//
// 它薄得几乎不像一层——这正是 CQRS 读侧该有的样子：不加载聚合、不碰领域，
// 把参数校验完直接问投影。真正的逻辑在 SQL 里（join artists/albums），
// 而 SQL 的复杂度是可以用 EXPLAIN 看、可以用索引量的，比在内存里翻对象强。
#pragma once

#include <cstdint>

#include "sonora/catalog/contracts/Queries.h"
#include "sonora/catalog/core/ports/Ports.h"
#include "sonora/kernel/Result.h"

namespace sonora::catalog::core {

class ListTracksHandler {
public:
    explicit ListTracksHandler(const ITrackListReader &reader) : reader_(reader) {}

    [[nodiscard]] kernel::Result<contracts::TrackListResult> handle(
        const contracts::ListTracks &query) const
    {
        // 上限 10000 是**故意的**：给调用方一个"你不能一次要一百万行"的明确边界。
        // 没有这个上限，"分页"这件事迟早会退化成"一次全取然后前端分页"。
        if (query.offset < 0) {
            return kernel::Result<contracts::TrackListResult>::fail(
                kernel::Error{kernel::ErrorCode::InvalidArgument, "offset 不能为负"});
        }
        if (query.limit <= 0 || query.limit > 10000) {
            return kernel::Result<contracts::TrackListResult>::fail(
                kernel::Error{kernel::ErrorCode::InvalidArgument, "limit 必须在 1..10000 之间"});
        }
        return reader_.list(query);
    }

private:
    const ITrackListReader &reader_;
};

}  // namespace sonora::catalog::core
