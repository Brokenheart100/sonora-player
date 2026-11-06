// Album 与 Artist：两个"薄"实体。
//
// 它们没有行为，只有一个不变量：名字非空。之所以还要写成类而不是 struct，
// 是因为"名字非空"必须由构造路径保证——一旦允许 struct 聚合初始化，任何地方
// 都能造出一个没有名字的专辑，而它会在每一个列表页上显示成空行。
//
// 它们不是聚合根：专辑的曲目列表是查询结果（投影表），不是聚合内部集合。
// 这一点决定了 Album 不可能有 addTrack()——那会把 Track 拉进 Album 的一致性边界。
#pragma once

#include <string>
#include <string_view>

#include "sonora/catalog/core/domain/CatalogIds.h"
#include "sonora/kernel/Result.h"

namespace sonora::catalog::core {

class Album {
public:
    [[nodiscard]] static kernel::Result<Album> add(std::string title);
    [[nodiscard]] static kernel::Result<Album> rehydrate(AlbumId id, std::string title);

    [[nodiscard]] AlbumId id() const noexcept { return id_; }
    [[nodiscard]] bool isPersisted() const noexcept { return id_.isValid(); }
    [[nodiscard]] const std::string &title() const noexcept { return title_; }

    [[nodiscard]] kernel::Result<void> rename(std::string_view newTitle);

private:
    Album(AlbumId id, std::string title);

    AlbumId id_;
    std::string title_;
};

class Artist {
public:
    [[nodiscard]] static kernel::Result<Artist> add(std::string name);
    [[nodiscard]] static kernel::Result<Artist> rehydrate(ArtistId id, std::string name);

    [[nodiscard]] ArtistId id() const noexcept { return id_; }
    [[nodiscard]] bool isPersisted() const noexcept { return id_.isValid(); }
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    [[nodiscard]] kernel::Result<void> rename(std::string_view newName);

private:
    Artist(ArtistId id, std::string name);

    ArtistId id_;
    std::string name_;
};

}  // namespace sonora::catalog::core
