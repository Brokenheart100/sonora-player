// Lyrics 的查询。
#pragma once

#include <string>

namespace sonora::lyrics::contracts {

// 给一首音频文件，要它的歌词。
//
// 参数是**音频文件的绝对路径**，不是 trackId：歌词模块不认识曲库，
// 曲库也不认识歌词。谁想显示歌词，就得知道那一行在磁盘上的位置——
// 这件事本来就该由表现层从 Player 快照里拿。
struct GetLyrics {
    std::string filePath;
    // 在线曲目才有：网易云 songId。本地曲目留空。
    //
    // 歌词模块同样不认识在线服务——它只是把"这首歌的 id"原样转给在线歌词来源，
    // 由那个适配器去调 Online.Contracts 的查询。
    std::string onlineSongId;
};

}  // namespace sonora::lyrics::contracts
