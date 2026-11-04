// 列表投影的读取。
//
// 一条 SQL 直接产出 DTO：join 在库里做，字符串在库里拼，一次往返。
// 换成"加载一万个 Track 再逐个查艺术家名"大约慢两个数量级——而这两条路在
// 二十首歌的开发期表现完全一样，所以必须从接口层面就只留下这一条。
#pragma once

#include <string_view>

#include "mamba/catalog/core/ports/Ports.h"
#include "mamba/platform/storage/SqliteConnection.h"

namespace mamba::catalog::adapters {

class SqliteTrackListReader final : public core::ITrackListReader {
public:
    explicit SqliteTrackListReader(platform::storage::SqliteConnection &connection)
        : connection_(connection)
    {
    }

    [[nodiscard]] kernel::Result<contracts::TrackListResult> list(
        const contracts::ListTracks &query) const override;

private:
    platform::storage::SqliteConnection &connection_;
};

}  // namespace mamba::catalog::adapters
