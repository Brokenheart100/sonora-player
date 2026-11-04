// Catalog 的查询（读侧意图）。
//
// 查询刻意**没有**"返回 Track 列表"这种形态：读侧返回的是 TrackDto，
// 由投影 SQL 一次性拼好。想拿聚合去填列表，先准备好为万级曲库等上几百毫秒。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mamba/catalog/contracts/TrackDto.h"

namespace mamba::catalog::contracts {

// 曲目列表的一页。offset/limit 而不是"全部"：列表页永远分页，
// 哪怕第一版一页塞两百行——接口一旦允许"全都要"，迟早就有人真的全都要。
struct ListTracks {
    std::int32_t offset = 0;
    std::int32_t limit = 200;
    // 空 = 不排序（按 id 升序）。M5 会加 sortBy/descending。
    std::string searchText;
};

// 列表页需要的一个 DTO 集合：行 + 总数。总数单独给，UI 才能显示"共 1234 首"
// 而不必自己数一遍（也不必为了数而把所有行都拉下来）。
struct TrackListResult {
    std::vector<TrackDto> rows;
    std::int64_t totalCount = 0;
};

}  // namespace mamba::catalog::contracts
