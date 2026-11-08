// Playlists 模块的库结构。
//
// 两张表，没有外键到 catalog（跨模块不建外键是硬规矩）：`playlist_item.track_id`
// 只是一个数字，指向谁由调用方解释。代价是"歌被删了条目还在"，收益是两个模块
// 可以各自迁移、各自删库而不互相绑架。
//
// `available` 列就是那条代价的答案：曲库发 TrackRemoved 时把它置 0，
// 条目不消失，界面显示成灰色占位。
#pragma once

#include <span>
#include <string_view>

#include "sonora/platform/storage/Migrator.h"

namespace sonora::playlists::adapters {

inline constexpr std::string_view kModuleName = "playlists";

[[nodiscard]] std::span<const platform::storage::Migration> migrations();

}  // namespace sonora::playlists::adapters
