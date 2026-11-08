// 搜索结果的磁盘缓存。
//
// 为什么缓存原始 JSON 而不是拆成列：解析器还会变（R3 要加 privilege、R4 要加
// 登录态字段），拆列意味着每次改动都写一次迁移；存原文则一段 SQL 都不用动，
// 解析器升级后旧缓存自动按新规则解析，实在解析不动就当没缓存。
//
// 缓存是**纯优化**：读写失败一律吞掉。让一次查询因为缓存写盘失败而报错，
// 是把可用性押在一个可以不存在的表上。
//
// 淘汰策略（R5-3）：按 `last_used_at` 走 LRU，上限是**行数与字节数双闸**。
// 只按行数限流有个隐蔽的坏处：一页 payload 从 8KB 涨到 80KB（接口加了字段），
// 行数没超，磁盘却翻了十倍。只按字节限流则会为了几十条小行反复删大行，
// 命中率反而下降，所以两条都要。
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "sonora/kernel/IClock.h"
#include "sonora/platform/storage/SqliteConnection.h"

namespace sonora::online::adapters
{

    class SqliteOnlineStore final
    {
    public:
        // 缓存现状。`cap*` 一并给出，界面照实显示"用了多少 / 上限多少"。
        struct Stats
        {
            std::int32_t searchRows{0};
            std::int64_t searchBytes{0};
            std::int32_t songRows{0};
            std::int32_t detailRows{0};
        };

        SqliteOnlineStore(platform::storage::SqliteConnection &db, const kernel::IClock &clock) noexcept;

        // 命中且未过期才返回；过期也算未命中（下面 save 时会覆盖）。
        // 命中会顺手把 `last_used_at` 推到当下——这就是"最近使用"的来源。
        // 因为要写这一行，方法不是 const（写失败不影响返回值）。
        [[nodiscard]] std::optional<std::string> loadSearch(const std::string &provider,
                                                           const std::string &keywords, std::int32_t offset);

        void saveSearch(const std::string &provider, const std::string &keywords, std::int32_t offset,
                        const std::string &payload);

        // 超上限就按 `last_used_at` 从旧到新删，直到两条闸都过。
        // 缓存表不存在（迁移没跑）时静默返回：缓存是纯优化。
        void trimSearch() const;

        [[nodiscard]] Stats stats() const;

        static constexpr std::int64_t kSearchTtlMs = 10 * 60 * 1000;
        static constexpr std::int32_t kMaxSearchRows = 200;
        // 4 MiB：一页 30 条的中文搜索结果约 20–40 KB，200 条也就 4–8 MB，
        // 这个闸是为了挡住"接口某天开始回整张专辑"那种体积暴涨。
        static constexpr std::int64_t kMaxSearchBytes = 4 * 1024 * 1024;

    private:
        platform::storage::SqliteConnection &db_;
        const kernel::IClock &clock_;
    };

} // namespace sonora::online::adapters
