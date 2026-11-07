// 搜索结果的磁盘缓存。
//
// 为什么缓存原始 JSON 而不是拆成列：解析器还会变（R3 要加 privilege、R4 要加
// 登录态字段），拆列意味着每次改动都写一次迁移；存原文则一段 SQL 都不用动，
// 解析器升级后旧缓存自动按新规则解析，实在解析不动就当没缓存。
//
// 缓存是**纯优化**：读写失败一律吞掉。让一次查询因为缓存写盘失败而报错，
// 是把可用性押在一个可以不存在的表上。
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
        SqliteOnlineStore(platform::storage::SqliteConnection &db, const kernel::IClock &clock) noexcept;

        // 命中且未过期才返回；过期也算未命中（下面 save 时会覆盖）。
        [[nodiscard]] std::optional<std::string> loadSearch(const std::string &provider,
                                                           const std::string &keywords, std::int32_t offset) const;

        void saveSearch(const std::string &provider, const std::string &keywords, std::int32_t offset,
                        const std::string &payload);

        // 只留最新 `kMaxSearchRows` 条：缓存是给"改一个字再改回来"用的，
        // 不是历史归档，没人指望它能省下整天的请求量。
        void trimSearch() const;

        static constexpr std::int64_t kSearchTtlMs = 10 * 60 * 1000;
        static constexpr std::int64_t kMaxSearchRows = 200;

    private:
        platform::storage::SqliteConnection &db_;
        const kernel::IClock &clock_;
    };

} // namespace sonora::online::adapters
