// 工作单元（事务边界）。
//
// 一个命令 = 一个事务。这条规矩让「导入 500 首歌」要么全成、要么全不成，
// 不会留下半批数据需要人来修。
//
// 注意 commit() 之后才允许发事件（架构 §5.4）：先落盘再广播，否则订阅者可能
// 读到还没提交的数据，然后投影表就永久性地比聚合表多一行。
#pragma once

#include "sonora/kernel/Result.h"

namespace sonora::kernel {

class IUnitOfWork {
public:
    virtual ~IUnitOfWork() = default;

    [[nodiscard]] virtual Result<void> begin() = 0;
    [[nodiscard]] virtual Result<void> commit() = 0;

    // 回滚不能失败到需要上报：真回滚不了，连接已经废了。所以它返回 void。
    virtual void rollback() noexcept = 0;

    [[nodiscard]] virtual bool active() const noexcept = 0;
};

// RAII 保护：没 commit 就析构 → 自动回滚。
// 用它之后，「异常/提前 return 导致事务悬挂」这件事不可能发生。
class UnitOfWorkScope {
public:
    explicit UnitOfWorkScope(IUnitOfWork &unit) : unit_(unit) {}

    UnitOfWorkScope(const UnitOfWorkScope &) = delete;
    UnitOfWorkScope &operator=(const UnitOfWorkScope &) = delete;

    ~UnitOfWorkScope()
    {
        if (!settled_) {
            unit_.rollback();
        }
    }

    [[nodiscard]] Result<void> begin() { return unit_.begin(); }

    [[nodiscard]] Result<void> commit()
    {
        auto result = unit_.commit();
        settled_ = true;
        return result;
    }

private:
    IUnitOfWork &unit_;
    bool settled_ = false;
};

}  // namespace sonora::kernel
