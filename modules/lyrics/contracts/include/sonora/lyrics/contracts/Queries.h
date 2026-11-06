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
};

}  // namespace sonora::lyrics::contracts
