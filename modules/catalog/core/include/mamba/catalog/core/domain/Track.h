// Track 聚合根。
//
// 它保护的唯一不变量是「一条曲目记录说得出口」：
//   * 有非空标题（去掉首尾空白之后）
//   * 有非空文件路径（否则下次启动必定扫描失败）
//   * 时长、轨号、碟号不为负（负数说明读元数据时算错了，不是合法状态）
//
// 为什么 Track 是聚合根而不是 Album：导入是按文件来的，一次导入会新增一批
// Track、顺手补几个未知的 Artist/Album。如果 Track 在 Album 里面，这条最常见的
// 路径就要先加载专辑、再往里塞曲子——而"这张专辑还有哪些曲子"根本不需要一致性
// 保证（多一首少一首不会让数据自相矛盾）。反过来，Album 的曲目列表是**查询**，
// 由投影表回答，不来自聚合。
//
// Track 刻意不持 Album 对象引用，只持 AlbumId：跨聚合持对象引用，等于把两个
// 聚合的一致性绑在一起，那正是聚合边界要切断的东西。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "mamba/catalog/core/domain/CatalogIds.h"
#include "mamba/kernel/Result.h"

namespace mamba::catalog::core {

// 曲目的可写字段。和聚合分开，是为了让"构造"和"重建"走同一个校验入口，
// 也为了仓储层不用 twenty 个参数。
struct TrackDetails {
    std::string title;
    ArtistId artistId{};
    AlbumId albumId{};
    int trackNumber = 0;
    int discNumber = 0;
    std::int64_t durationMs = 0;
    std::string filePath;        // UTF-8，绝对路径
    std::int64_t fileSizeBytes = 0;
    std::int64_t addedAtUnix = 0;
};

class Track {
public:
    // 新导入：id 由仓储在落库时分配，所以这里拿到的是一个无效 id，
    // 这是有意的——"还没被持久化的实体"就是一个合法状态。
    [[nodiscard]] static kernel::Result<Track> add(TrackDetails details);

    // 从库里读回来：数据和 id 都是可信的，但**仍然校验**。
    // 不校验的理由只有一种（"数据是我们自己写的"），而那种理由会在第一次
    // 手改数据库、第一次格式迁移之后消失。
    [[nodiscard]] static kernel::Result<Track> rehydrate(TrackId id, TrackDetails details);

    [[nodiscard]] TrackId id() const noexcept { return id_; }
    [[nodiscard]] bool isPersisted() const noexcept { return id_.isValid(); }

    [[nodiscard]] const std::string &title() const noexcept { return details_.title; }
    [[nodiscard]] ArtistId artistId() const noexcept { return details_.artistId; }
    [[nodiscard]] AlbumId albumId() const noexcept { return details_.albumId; }
    [[nodiscard]] int trackNumber() const noexcept { return details_.trackNumber; }
    [[nodiscard]] int discNumber() const noexcept { return details_.discNumber; }
    [[nodiscard]] std::int64_t durationMs() const noexcept { return details_.durationMs; }
    [[nodiscard]] const std::string &filePath() const noexcept { return details_.filePath; }
    [[nodiscard]] std::int64_t fileSizeBytes() const noexcept { return details_.fileSizeBytes; }
    [[nodiscard]] std::int64_t addedAtUnix() const noexcept { return details_.addedAtUnix; }

    [[nodiscard]] const TrackDetails &details() const noexcept { return details_; }

    // 行为而不是 setter：改名要经过去空白与判空，setTitle 会把这条规则漏出去。
    [[nodiscard]] kernel::Result<void> rename(std::string_view newTitle);

private:
    Track(TrackId id, TrackDetails details);

    TrackId id_;
    TrackDetails details_;
};

// 去掉首尾空白（含全角空格 U+3000）。返回新串，不改入参。
[[nodiscard]] std::string trimTitle(std::string_view text);

}  // namespace mamba::catalog::core
