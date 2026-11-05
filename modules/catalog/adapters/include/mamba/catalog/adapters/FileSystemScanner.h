// 文件系统扫描：目录 → 音频文件路径列表。
//
// 不带 Qt 是有意的：扫目录这件事没有任何 UI 语义，用 Qt 只会让这个模块
// 在测试里必须拖着 Qt DLL。真需要 QFileSystemWatcher 的是"监听目录变化"，
// 那是 M5 的另一个端口。
#pragma once

#include <string>
#include <vector>

#include "mamba/catalog/core/ports/Ports.h"

namespace mamba::catalog::adapters {

class FileSystemScanner final : public core::ILibraryScanner {
public:
    // 传空则用 defaultExtensions()。大小写不敏感（Windows 上 ".FLAC" 也是 flac）。
    explicit FileSystemScanner(std::vector<std::string> extensions = {});

    [[nodiscard]] kernel::Result<std::vector<core::ScannedFile>> scan(
        std::string_view folderPath) const override;

    // 支持的扩展名，全部小写、带点。
    [[nodiscard]] static std::vector<std::string> defaultExtensions();

private:
    std::vector<std::string> extensions_;
};

}  // namespace mamba::catalog::adapters
