// 歌单的 DTO。
//
// id 用裸 int64 而不是领域层那个强类型 id：契约层会被别的模块和界面层看见，
// 一个模板类型在那里只会变成四处 fromValue/value() 的噪音。转换只发生在
// handler 那一层，且只有两行。
//
// 条目里**故意没有**标题/作者/封面：歌单模块不认识曲库，而"显示这首歌叫什么"
// 属于表现层的拼装工作（它本来就有曲库的投影）。为了少一次前端拼装而让
// playlists 去 JOIN catalog 的表，等于把两个模块的边界换成一句注释。
#pragma once

#include <cstdint>
#include <string>

namespace sonora::playlists::contracts {

struct PlaylistDto {
    std::int64_t playlistId = 0;
    std::string name;
    // 条目总数（含暂时不可用的）。列表页要显示"12 首"，不能只数可播的。
    std::int32_t trackCount = 0;
    std::int64_t createdAt = 0;
};

struct PlaylistEntryDto {
    std::uint64_t trackId = 0;
    // 在歌单里的顺序，从 0 开始。
    std::int32_t pos = 0;
    // 曲目被移出曲库后，条目还在（用户加过的痕迹不因为他删了文件就消失），
    // 但不能再播——界面显示成灰色占位。
    bool available = true;
};

}  // namespace sonora::playlists::contracts
