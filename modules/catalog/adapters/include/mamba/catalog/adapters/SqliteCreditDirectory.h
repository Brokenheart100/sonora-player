// 艺术家/专辑的「查或建」。
//
// 它不属于任何聚合：Artist 和 Album 在 M1 里只是"能被曲目引用的名字"，
// 没有自己的不变量。等到 M5 给专辑加曲目排序、给艺术家加别名时，它们才会
// 各自变成聚合，那时这个类是唯一需要改的落点。
#pragma once

#include <cstdint>
#include <string_view>

#include "mamba/catalog/core/ports/Ports.h"
#include "mamba/platform/storage/SqliteConnection.h"

namespace mamba::catalog::adapters {

class SqliteCreditDirectory final : public core::ICreditDirectory {
public:
    explicit SqliteCreditDirectory(platform::storage::SqliteConnection &connection)
        : connection_(connection)
    {
    }

    [[nodiscard]] kernel::Result<core::ArtistId> resolveArtist(std::string_view name) override;
    [[nodiscard]] kernel::Result<core::AlbumId> resolveAlbum(std::string_view title) override;

private:
    // table/column 只接受本文件里的常量，用户输入永远走绑定参数。
    [[nodiscard]] kernel::Result<std::int64_t> findOrCreate(std::string_view table,
                                                            std::string_view column,
                                                            std::string_view value);

    platform::storage::SqliteConnection &connection_;
};

}  // namespace mamba::catalog::adapters
