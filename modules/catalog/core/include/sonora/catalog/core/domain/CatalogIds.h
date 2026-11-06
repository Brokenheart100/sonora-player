// Catalog 模块的强类型 id。
//
// 三个 id 的底盘都是 uint64，但它们是三个不同的类型：把 AlbumId 传给接收
// TrackId 的函数编译不过。这一层保护不花运行时成本，也不需要测试——
// 它靠的是"写不出来"，而不是"测出来"（kernel_typedid.cpp 里有 static_assert 钉住）。
#pragma once

#include "sonora/kernel/TypedId.h"

namespace sonora::catalog::core {

struct TrackTag;
struct AlbumTag;
struct ArtistTag;

using TrackId = kernel::TypedId<TrackTag>;
using AlbumId = kernel::TypedId<AlbumTag>;
using ArtistId = kernel::TypedId<ArtistTag>;

}  // namespace sonora::catalog::core
