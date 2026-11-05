// 列表投影的读取。
//
// 一条 SQL 直接产出 DTO：join 在库里做，字符串在库里拼，一次往返。
// 换成"加载一万个 Track 再逐个查艺术家名"大约慢两个数量级——而这两条路在
// 二十首歌的开发期表现完全一样，所以必须从接口层面就只留下这一条。
#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "mamba/catalog/core/ports/Ports.h"
#include "mamba/platform/storage/SqliteConnection.h"

namespace mamba::catalog::adapters {

class SqliteTrackListReader final : public core::ITrackListReader {
public:
    // coverRoot：封面文件的存放目录（与 FileCoverStore 同一个）。给空串就是
    // "不提供封面"——DTO 里 coverPath 一律为空，界面显示占位图。
    // 把目录交给读侧而不是把字节塞进 DTO：列表滚动的每一帧都在读这些行，
    // 每行带几十 KB 的图片字节是不可接受的。
    explicit SqliteTrackListReader(platform::storage::SqliteConnection &connection,
                                  std::string coverRoot = {})
        : connection_(connection), coverRoot_(std::move(coverRoot))
    {
    }

    [[nodiscard]] kernel::Result<contracts::TrackListResult> list(
        const contracts::ListTracks &query) const override;

private:
    platform::storage::SqliteConnection &connection_;
    std::string coverRoot_;
};

}  // namespace mamba::catalog::adapters
