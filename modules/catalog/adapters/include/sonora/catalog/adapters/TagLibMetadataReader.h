// 用 TagLib 读真标签。
//
// 这是 M1 里那个 BasicMetadataReader 的正牌替身：同一个端口，换一个实现，
// 上层（ImportFolderHandler）一行都不用动。区别只有两处——它能给出真实的
// 播放时长，也能把内嵌封面读出来。
//
// 一条设计约束：**读不出标签不算失败**。网上下载来的文件常常没有 artist/album，
// 甚至没有 title；把这些文件判成"导入失败"会让用户面对一个满是指纹不符的
// 错误列表，而他要的只是"这首歌能出现在列表里"。
#pragma once

#include <string_view>

#include "sonora/catalog/core/ports/Ports.h"

namespace sonora::catalog::adapters {

class TagLibMetadataReader final : public core::IMetadataReader {
public:
    // 读标签 + 音频属性 + 内嵌封面。文件不存在才返回失败；
    // 文件存在但标签是空的，仍然成功，缺的字段用文件名兜底。
    [[nodiscard]] kernel::Result<core::TrackMetadata> read(
        std::string_view filePath) const override;
};

}  // namespace sonora::catalog::adapters
