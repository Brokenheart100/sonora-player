// 在线任务的状态表：token → (状态 + 行数据)。
//
// 为什么要有它：界面每 50ms 轮询一次 `GetOnlineJob`，而"这次 token 的结果
// 到底还算不算数"必须有唯一裁决者。裁决规则就一条——**token 对不上就丢弃**，
// 这里是唯一实现它的地方（见 docs/ONLINE.md §4.1 的时序）。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sonora/online/contracts/Types.h"

namespace sonora::online::core::app
{

    class JobRegistry
    {
    public:
        // 开一个新任务。token 由界面递增生成；旧 token 的记录保留（界面可能还
        // 持着旧快照），但它们再也收不到新数据。
        void begin(std::uint64_t token, contracts::SourceKind kind, std::string keywords);

        void setPhase(std::uint64_t token, contracts::JobPhase phase);

        // 追加一页行。`revision` 自增，界面对着它判断要不要重新 Fetch。
        void appendRows(std::uint64_t token, std::vector<contracts::OnlineTrackDto> rows, bool hasMore);

        void fail(std::uint64_t token, contracts::OnlineError error);

        [[nodiscard]] contracts::OnlineJobState state(std::uint64_t token) const;

        [[nodiscard]] contracts::OnlinePageDto page(std::uint64_t token, std::int32_t offset,
                                                   std::int32_t limit) const;

        void forget(std::uint64_t token);

        [[nodiscard]] std::uint64_t currentToken() const noexcept { return current_; }
        [[nodiscard]] std::size_t size() const noexcept { return jobs_.size(); }

    private:
        struct Job
        {
            contracts::OnlineJobState state;
            std::vector<contracts::OnlineTrackDto> rows;
            bool hasMore{false};
            contracts::SourceKind kind{contracts::SourceKind::Search};
            std::string keywords;
        };

        [[nodiscard]] Job *find(std::uint64_t token);
        [[nodiscard]] Job *findWritable(std::uint64_t token);

        std::unordered_map<std::uint64_t, Job> jobs_;
        std::uint64_t current_{0};
    };

} // namespace sonora::online::core::app
