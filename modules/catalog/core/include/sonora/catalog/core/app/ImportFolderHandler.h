// 「导入一个文件夹」这个用例。
//
// 它是 M1 的验收对象：一条命令穿过总线 → 应用层 → 五个端口 → 落库。
// 在这条路径上你能看到整套架构的成本：一个 handler、一次 port 定义、
// 两个适配器实现、一条 SQL 迁移。如果连这条最简单的路都要改十几个文件，
// 那分层就是过重了——docs/IMPLEMENTATION.md §M1 把「改动文件 ≤ 8」写成硬指标，
// 就是让这个判断不靠感觉。
//
// M5 之后它多了三件事，都是"一万首"逼出来的：
//
//   * **增量**：文件 mtime 与 size 都没变就跳过，连标签都不读（一次重扫从
//     "读一万个文件"变成"读一万个目录项"）。
//   * **分批**：每 500 个文件一个事务。一个事务覆盖一万首意味着要么全成要么全废，
//     而且那一次提交会锁住数据库几秒钟。
//   * **进度**：处理到第几个了，用原子变量记着，界面轮询。它不参与业务，
//     所以不进返回值也不进事件。
//   * **封面**：TagLib 读标签时封面字节已经在内存里了，顺手交给 ICoverStore
//     落盘并只留一个内容哈希键。为封面再开一次文件，就是一万次多余 IO。
//
// 但**单个文件失败仍然不算失败**：坏文件的结局是"跳过并计数"，不是让整次导入
// 回滚。这两种"失败"的区别，是导入功能能不能用的分界线。
#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "sonora/catalog/contracts/Commands.h"
#include "sonora/catalog/core/ports/Ports.h"
#include "sonora/kernel/IClock.h"
#include "sonora/kernel/ILogger.h"
#include "sonora/kernel/IUnitOfWork.h"
#include "sonora/kernel/Result.h"

namespace sonora::catalog::core {

// 给界面看的导入结果（"扫描 24 个文件，导入 20 首，跳过 3 首，失败 1 个"）。
// 它是**值**不是异常：一次导入里有几个文件读不出来是完全正常的。
struct ImportSummary {
    std::int32_t filesFound = 0;   // 扫描到的文件数
    std::int32_t imported = 0;     // 新入库
    std::int32_t updated = 0;      // 已在库里但文件变了（重读了标签）
    std::int32_t unchanged = 0;    // 连标签都没读：mtime 与 size 都没动
    std::int32_t duplicates = 0;   // 落库撞 UNIQUE（并发导入时才会出现）
    std::int32_t failed = 0;
};

// 导入过程中的快照，给界面做进度显示用。全程无锁读（每个字段各自一个原子量）。
struct ImportProgress {
    bool running = false;
    std::int32_t filesFound = 0;
    std::int32_t processed = 0;
    std::int32_t imported = 0;
    std::int32_t updated = 0;
};

class ImportFolderHandler {
public:
    // batchSize：多少个文件一个事务。500 是量出来的折中——再小提交次数多，
    // 再大一次提交的锁时间开始能被界面感觉到。
    ImportFolderHandler(ILibraryScanner &scanner, IMetadataReader &reader, ICreditDirectory &credits,
                        ITrackRepository &tracks, kernel::IUnitOfWork &unit,
                        const kernel::IClock &clock, kernel::ILogger *logger = nullptr,
                        ICoverStore *covers = nullptr, std::int32_t batchSize = 500)
        : scanner_(scanner),
          reader_(reader),
          credits_(credits),
          tracks_(tracks),
          unit_(unit),
          clock_(clock),
          logger_(logger),
          covers_(covers),
          batchSize_(batchSize > 0 ? batchSize : 1)
    {
    }

    // 真正的用例实现。返回汇总，让调用方（或测试）能看清发生了什么。
    [[nodiscard]] kernel::Result<ImportSummary> import(const contracts::ImportFolder &command);

    // 总线入口：把"有汇总"折成"只关心成败"。
    [[nodiscard]] kernel::Result<void> handle(const contracts::ImportFolder &command);

    // 线程安全的进度快照。异步导入时界面每隔几百毫秒读一次。
    [[nodiscard]] ImportProgress progress() const noexcept;

private:
    // 处理单个文件。返回 true 表示这一首真的写库了（新增或更新）。
    // 错误在内部就被吃掉并计数——这就是"坏文件不阻塞整批"的落点。
    bool importOne(const ScannedFile &file, std::int64_t addedAtUnix, ImportSummary &summary);

    // 读标签 → 解析艺术家/专辑 → 造 Track。新增与更新共用这一段。
    [[nodiscard]] kernel::Result<TrackDetails> buildDetails(const ScannedFile &file,
                                                            std::int64_t addedAtUnix,
                                                            TrackId existingId);

    ILibraryScanner &scanner_;
    IMetadataReader &reader_;
    ICreditDirectory &credits_;
    ITrackRepository &tracks_;
    kernel::IUnitOfWork &unit_;
    const kernel::IClock &clock_;
    kernel::ILogger *logger_ = nullptr;
    // 可空：没有封面缓存时导入照常跑，只是 cover_key 一直是空串。
    // 封面是"锦上添花"这一段的前提——没有它不该让任何一首歌进不来。
    ICoverStore *covers_ = nullptr;
    std::int32_t batchSize_ = 500;

    // 进度：只给界面看，跨线程读数，所以每个字段一个原子量。
    std::atomic<bool> running_{false};
    std::atomic<std::int32_t> filesFound_{0};
    std::atomic<std::int32_t> processed_{0};
    std::atomic<std::int32_t> imported_{0};
    std::atomic<std::int32_t> updated_{0};
};

}  // namespace sonora::catalog::core
