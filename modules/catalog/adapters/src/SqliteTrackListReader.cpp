#include "sonora/catalog/adapters/SqliteTrackListReader.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "sonora/catalog/adapters/SearchText.h"

namespace sonora::catalog::adapters {

using kernel::Result;
using platform::storage::SqliteStatement;

namespace {

// 筛选条件只有一个来源：全文索引。
//
// 为什么不用 LIKE：LIKE '%…%' 走不了索引，一万首就是一万次全表扫描，
// 而且中文子串在 LIKE 下命中不了"逐字拆分"的语义（'夜航' 能命中 '夜航西飞'
// 纯属巧合——对 '夜 航西飞' 这种带空格的标题就不成立）。
// FTS5 那边存的是逐字拆开的 body，查询侧写短语，正好等价于子串匹配。
constexpr std::string_view kSearchWhere =
    " WHERE id IN (SELECT rowid FROM track_fts WHERE track_fts MATCH ?)";

}  // namespace

Result<contracts::TrackListResult> SqliteTrackListReader::list(
    const contracts::ListTracks &query) const
{
    // matchQuery 对空串、全空白都返回空串：那种"筛选"等于没筛，直接走无 WHERE 的分支，
    // 免得给 FTS5 一个语法为空的 MATCH 让它报错。
    const std::string match = matchQuery(query.searchText);
    const bool filtered = !match.empty();
    const std::string where{kSearchWhere};

    // 总数先查：它是界面上"共 N 首"的唯一来源，而且和行数据是两条独立查询，
    // 不会因为分页而变。
    auto countStatement = connection_.prepare(
        std::string{"SELECT COUNT(*) FROM v_track_list"} + (filtered ? where : std::string{}));
    if (!countStatement) {
        return Result<contracts::TrackListResult>::fail(countStatement.error());
    }
    if (filtered) {
        if (auto bound = countStatement.value().bind(1, std::string_view{match}); !bound) {
            return Result<contracts::TrackListResult>::fail(bound.error());
        }
    }
    auto countRow = countStatement.value().step();
    if (!countRow) {
        return Result<contracts::TrackListResult>::fail(countRow.error());
    }
    if (!countRow.value()) {
        return Result<contracts::TrackListResult>::fail(
            kernel::Error{kernel::ErrorCode::Internal, "COUNT 没有返回行"});
    }

    contracts::TrackListResult result;
    result.totalCount = countStatement.value().columnInt(0);

    auto statement = connection_.prepare(
        std::string{"SELECT id, title, artist_name, album_title, duration_ms, file_path, cover_key"
                    " FROM v_track_list"} +
        (filtered ? where : std::string{}) + " ORDER BY id LIMIT ? OFFSET ?");
    if (!statement) {
        return Result<contracts::TrackListResult>::fail(statement.error());
    }
    SqliteStatement &s = statement.value();
    int slot = 1;
    if (filtered) {
        if (auto bound = s.bind(slot++, std::string_view{match}); !bound) {
            return Result<contracts::TrackListResult>::fail(bound.error());
        }
    }
    if (auto bound = s.bind(slot++, static_cast<std::int64_t>(query.limit)); !bound) {
        return Result<contracts::TrackListResult>::fail(bound.error());
    }
    if (auto bound = s.bind(slot, static_cast<std::int64_t>(query.offset)); !bound) {
        return Result<contracts::TrackListResult>::fail(bound.error());
    }

    result.rows.reserve(static_cast<std::size_t>(query.limit));
    for (;;) {
        auto row = s.step();
        if (!row) {
            return Result<contracts::TrackListResult>::fail(row.error());
        }
        if (!row.value()) {
            break;
        }
        contracts::TrackDto dto;
        dto.id = static_cast<std::uint64_t>(s.columnInt(0));
        dto.title = s.columnString(1);
        dto.artistName = s.columnString(2);
        dto.albumTitle = s.columnString(3);
        dto.durationMs = s.columnInt(4);
        dto.filePath = s.columnString(5);
        // 库里只存键，绝对路径在这里拼。键为空 → 空路径 → 界面走占位图分支。
        const std::string coverKey = s.columnString(6);
        if (!coverKey.empty() && !coverRoot_.empty()) {
            dto.coverPath = coverRoot_ + "/" + coverKey;
        }
        result.rows.push_back(std::move(dto));
    }

    return Result<contracts::TrackListResult>::ok(std::move(result));
}

}  // namespace sonora::catalog::adapters
