#include "mamba/catalog/adapters/SqliteTrackListReader.h"

#include <cstdint>
#include <string>

namespace mamba::catalog::adapters {

using kernel::Result;
using platform::storage::SqliteStatement;

namespace {

bool hasSearch(const contracts::ListTracks &query)
{
    return !query.searchText.empty();
}

// LIKE 的通配符里 % 和 _ 是元字符。用户搜 "100%" 时把 % 当通配符会让结果多出
// 一大截，而且没人会想到是这里。这里用 ESCAPE 把它们降级成普通字符。
std::string likePattern(const std::string &text)
{
    std::string pattern;
    pattern.reserve(text.size() + 2);
    pattern.push_back('%');
    for (const char c : text) {
        if (c == '%' || c == '_' || c == '\\') {
            pattern.push_back('\\');
        }
        pattern.push_back(c);
    }
    pattern.push_back('%');
    return pattern;
}

}  // namespace

Result<contracts::TrackListResult> SqliteTrackListReader::list(
    const contracts::ListTracks &query) const
{
    const bool filtered = hasSearch(query);
    const std::string where = filtered
                                  ? " WHERE (title LIKE ? ESCAPE '\\' OR artist_name LIKE ? ESCAPE '\\'"
                                    " OR album_title LIKE ? ESCAPE '\\')"
                                  : "";

    // 总数先查：它是界面上"共 N 首"的唯一来源，而且和行数据是两条独立查询，
    // 不会因为分页而变。
    auto countStatement = connection_.prepare("SELECT COUNT(*) FROM v_track_list" + where);
    if (!countStatement) {
        return Result<contracts::TrackListResult>::fail(countStatement.error());
    }
    if (filtered) {
        const auto pattern = likePattern(query.searchText);
        for (int index = 1; index <= 3; ++index) {
            if (auto bound = countStatement.value().bind(index, std::string_view{pattern}); !bound) {
                return Result<contracts::TrackListResult>::fail(bound.error());
            }
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

    auto statement = connection_.prepare("SELECT id, title, artist_name, album_title, duration_ms,"
                                         " file_path FROM v_track_list" + where +
                                         " ORDER BY id LIMIT ? OFFSET ?");
    if (!statement) {
        return Result<contracts::TrackListResult>::fail(statement.error());
    }
    SqliteStatement &s = statement.value();
    int slot = 1;
    if (filtered) {
        const auto pattern = likePattern(query.searchText);
        for (; slot <= 3; ++slot) {
            if (auto bound = s.bind(slot, std::string_view{pattern}); !bound) {
                return Result<contracts::TrackListResult>::fail(bound.error());
            }
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
        result.rows.push_back(std::move(dto));
    }

    return Result<contracts::TrackListResult>::ok(std::move(result));
}

}  // namespace mamba::catalog::adapters
