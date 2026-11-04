// Catalog 的命令（写侧意图）。
//
// 命令是**祈使句**，事件是过去式。命令可以失败（所以总线返回 Result），
// 事件不能失败（它描述的事实已经发生了）。
//
// 命令必须放在 Contracts 而不是 Core：表现层要能构造它、投进总线，但它绝不能
// 看见执行它的 Handler。这条分工是 CQRS 里唯一真正硬的部分。
#pragma once

#include <string>

namespace mamba::catalog::contracts {

// 扫描一个文件夹并导入其中的音频文件。
struct ImportFolder {
    std::string folderPath;   // UTF-8，绝对路径
};

// 重新扫描：换到新位置后补回丢失的文件（M5 才实现，先占位让接口稳定）。
struct RescanLibrary {
    bool fullRescan = false;
};

}  // namespace mamba::catalog::contracts
