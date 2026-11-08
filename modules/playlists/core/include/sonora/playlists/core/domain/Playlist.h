// Playlist 聚合。
//
// 不变量只有三条，但每条都值得一个方法而不是一句 UPDATE：
//   1. 名字不能为空（空白也算空）；
//   2. 同一首歌在同一个歌单里只出现一次；
//   3. 条目顺序是 pos 的唯一真相——增删之后必须重新连续编号。
//
// 为什么"曲目从库里删掉"只标 not available 而不是删条目：用户加进歌单的是
// "这件事"（我喜欢这首），文件暂时不在只说明现在播不了。删条目等于替用户
// 做了"我不再喜欢"的决定。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sonora/kernel/Result.h>
#include <sonora/kernel/TypedId.h>

namespace sonora::playlists::core {

struct PlaylistIdTag;
using PlaylistId = kernel::TypedId<PlaylistIdTag, std::int64_t>;

class Playlist {
public:
    // 一条条目：曲目 id + 现在还播不播得动。
    struct Item {
        std::uint64_t trackId = 0;
        bool available = true;

        friend bool operator==(const Item &, const Item &) = default;
    };

    // 新建（还没有 id，由仓储写入后回填）。
    [[nodiscard]] static kernel::Result<Playlist> create(std::string name, std::int64_t createdAt);

    // 从库里读回来。id 必须有效。
    [[nodiscard]] static kernel::Result<Playlist> rehydrate(PlaylistId id, std::string name,
                                                           std::int64_t createdAt,
                                                           std::vector<Item> items);

    [[nodiscard]] PlaylistId id() const noexcept { return id_; }
    [[nodiscard]] const std::string &name() const noexcept { return name_; }
    [[nodiscard]] std::int64_t createdAt() const noexcept { return createdAt_; }
    [[nodiscard]] const std::vector<Item> &items() const noexcept { return items_; }
    [[nodiscard]] std::int32_t trackCount() const noexcept
    {
        return static_cast<std::int32_t>(items_.size());
    }

    // 写入后由仓储回填真实 id（新建路径专用）。
    void assignId(PlaylistId id) noexcept { id_ = id; }

    [[nodiscard]] kernel::Result<void> rename(std::string name);

    // 加一首：重复 → Conflict。
    [[nodiscard]] kernel::Result<void> addTrack(std::uint64_t trackId);

    // 移除一条：不存在 → NotFound。
    [[nodiscard]] kernel::Result<void> removeTrack(std::uint64_t trackId);

    // 把条目挪到 toIndex。toIndex 越界会被夹到 [0, size-1]；不存在 → NotFound。
    [[nodiscard]] kernel::Result<void> moveTrack(std::uint64_t trackId, std::int32_t toIndex);

    // 曲库那边删掉了这首歌：条目在、不可播。不存在就什么也不发生（幂等）。
    void markTrackUnavailable(std::uint64_t trackId) noexcept;

    [[nodiscard]] bool hasTrack(std::uint64_t trackId) const noexcept;

private:
    Playlist(PlaylistId id, std::string name, std::int64_t createdAt, std::vector<Item> items);

    [[nodiscard]] static kernel::Result<void> validateName(std::string_view name);

    PlaylistId id_{};
    std::string name_;
    std::int64_t createdAt_ = 0;
    std::vector<Item> items_;
};

}  // namespace sonora::playlists::core
