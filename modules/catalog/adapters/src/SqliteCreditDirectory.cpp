#include "sonora/catalog/adapters/SqliteCreditDirectory.h"

#include <string>

namespace sonora::catalog::adapters {

using kernel::Error;
using kernel::ErrorCode;
using kernel::Result;

Result<std::int64_t> SqliteCreditDirectory::findOrCreate(std::string_view table,
                                                          std::string_view column,
                                                          std::string_view value)
{
    if (value.empty()) {
        return Result<std::int64_t>::fail(Error{ErrorCode::InvalidArgument, "名字为空"});
    }

    // ON CONFLICT DO NOTHING 让"已存在"和"刚创建"走同一条代码路径：
    // 否则先 SELECT 再 INSERT 会有竞态，而 SELECT+INSERT 的写法在并发导入时
    // 会撞 UNIQUE 然后让整批事务回滚——那是最难复现的一类 bug。
    std::string insert = "INSERT INTO ";
    insert.append(table).append("(").append(column).append(") VALUES(?) ON CONFLICT(");
    insert.append(column).append(") DO NOTHING");

    auto statement = connection_.prepare(insert);
    if (!statement) {
        return Result<std::int64_t>::fail(statement.error());
    }
    if (auto bound = statement.value().bind(1, value); !bound) {
        return Result<std::int64_t>::fail(bound.error());
    }
    if (auto stepped = statement.value().step(); !stepped) {
        return Result<std::int64_t>::fail(stepped.error());
    }

    std::string select = "SELECT id FROM ";
    select.append(table).append(" WHERE ").append(column).append(" = ?");

    auto lookup = connection_.prepare(select);
    if (!lookup) {
        return Result<std::int64_t>::fail(lookup.error());
    }
    if (auto bound = lookup.value().bind(1, value); !bound) {
        return Result<std::int64_t>::fail(bound.error());
    }
    auto row = lookup.value().step();
    if (!row) {
        return Result<std::int64_t>::fail(row.error());
    }
    if (!row.value()) {
        return Result<std::int64_t>::fail(
            Error{ErrorCode::Internal, "刚写入的名字查不到——事务可见性出了问题"});
    }
    return Result<std::int64_t>::ok(lookup.value().columnInt(0));
}

Result<core::ArtistId> SqliteCreditDirectory::resolveArtist(std::string_view name)
{
    auto id = findOrCreate("artists", "name", name);
    if (!id) {
        return Result<core::ArtistId>::fail(id.error());
    }
    return Result<core::ArtistId>::ok(
        core::ArtistId::fromValue(static_cast<std::uint64_t>(id.value())));
}

Result<core::AlbumId> SqliteCreditDirectory::resolveAlbum(std::string_view title)
{
    auto id = findOrCreate("albums", "title", title);
    if (!id) {
        return Result<core::AlbumId>::fail(id.error());
    }
    return Result<core::AlbumId>::ok(
        core::AlbumId::fromValue(static_cast<std::uint64_t>(id.value())));
}

}  // namespace sonora::catalog::adapters
