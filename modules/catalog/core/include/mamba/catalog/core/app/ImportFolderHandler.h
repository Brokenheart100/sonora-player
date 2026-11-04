// 「导入一个文件夹」这个用例。
//
// 它是 M1 的验收对象：一条命令穿过总线 → 应用层 → 五个端口 → 落库。
// 在这条路径上你能看到整套架构的成本：一个 handler、一次 port 定义、
// 两个适配器实现、一条 SQL 迁移。如果连这条最简单的路都要改十几个文件，
// 那分层就是过重了——docs/IMPLEMENTATION.md §M1 把「改动文件 ≤ 8」写成硬指标，
// 就是让这个判断不靠感觉。
//
// 一个命令 = 一个事务：20 首也好、2000 首也好，中途出错不能留下半批数据。
// 但**单个文件失败不算失败**：坏文件的结局是"跳过并计数"，不是让整次导入回滚。
// 这两种"失败"的区别，是导入功能能不能用的分界线。
#pragma once

#include <cstdint>
#include <string_view>

#include "mamba/catalog/contracts/Commands.h"
#include "mamba/catalog/core/ports/Ports.h"
#include "mamba/kernel/IClock.h"
#include "mamba/kernel/ILogger.h"
#include "mamba/kernel/IUnitOfWork.h"
#include "mamba/kernel/Result.h"

namespace mamba::catalog::core {

// 给界面看的导入结果（"扫描 24 个文件，导入 20 首，跳过 3 首，失败 1 个"）。
// 它是**值**不是异常：一次导入里有几个文件读不出来是完全正常的。
struct ImportSummary {
    std::int32_t filesFound = 0;
    std::int32_t imported = 0;
    std::int32_t duplicates = 0;
    std::int32_t failed = 0;
};

class ImportFolderHandler {
public:
    ImportFolderHandler(ILibraryScanner &scanner, IMetadataReader &reader, ICreditDirectory &credits,
                        ITrackRepository &tracks, kernel::IUnitOfWork &unit,
                        const kernel::IClock &clock, kernel::ILogger *logger = nullptr)
        : scanner_(scanner),
          reader_(reader),
          credits_(credits),
          tracks_(tracks),
          unit_(unit),
          clock_(clock),
          logger_(logger)
    {
    }

    // 真正的用例实现。返回汇总，让调用方（或测试）能看清发生了什么。
    [[nodiscard]] kernel::Result<ImportSummary> import(const contracts::ImportFolder &command);

    // 总线入口：把"有汇总"折成"只关心成败"。
    [[nodiscard]] kernel::Result<void> handle(const contracts::ImportFolder &command);

private:
    // 处理单个文件。返回 true 表示这一首成功入库。
    // 错误在内部就被吃掉并计数——这就是"坏文件不阻塞整批"的落点。
    bool importOne(std::string_view filePath, std::int64_t addedAtUnix, ImportSummary &summary);

    ILibraryScanner &scanner_;
    IMetadataReader &reader_;
    ICreditDirectory &credits_;
    ITrackRepository &tracks_;
    kernel::IUnitOfWork &unit_;
    const kernel::IClock &clock_;
    kernel::ILogger *logger_ = nullptr;
};

}  // namespace mamba::catalog::core
