// 只用文件名和文件大小构造元数据。
//
// 这是**临时实现**，M5 换成 TagLib。之所以现在就写它：M1 的验收是"导入 20 首 →
// 列表 20 行"，这条链路上真正待验证的是分层与存储，不是 ID3 解析。
// 先让它跑通，再换一个实现替换掉这一个类——这正是端口存在的意义。
#pragma once

#include <string_view>

#include "mamba/catalog/core/ports/Ports.h"

namespace mamba::catalog::adapters {

class BasicMetadataReader final : public core::IMetadataReader {
public:
    // 认识的命名约定："艺术家 - 标题.flac"。不匹配就把整个主文件名当标题。
    [[nodiscard]] kernel::Result<core::TrackMetadata> read(
        std::string_view filePath) const override;
};

}  // namespace mamba::catalog::adapters
