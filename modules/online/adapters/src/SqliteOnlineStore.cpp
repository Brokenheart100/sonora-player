#include "sonora/online/adapters/SqliteOnlineStore.h"

#include <string>

namespace sonora::online::adapters
{
    using platform::storage::SqliteStatement;

    namespace
    {
        // 表里存的是 unix 毫秒（整数），时钟给的是 time_point，只在这里转一次。
        std::int64_t nowMillis(const kernel::IClock &clock)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(clock.now().time_since_epoch()).count();
        }
    } // namespace

    SqliteOnlineStore::SqliteOnlineStore(platform::storage::SqliteConnection &db, const kernel::IClock &clock) noexcept
        : db_(db), clock_(clock)
    {
    }

    std::optional<std::string> SqliteOnlineStore::loadSearch(const std::string &provider,
                                                             const std::string &keywords,
                                                             std::int32_t offset) const
    {
        auto statement = db_.prepare("SELECT payload, fetched_at FROM online_search_cache"
                                     " WHERE provider = ?1 AND keywords = ?2 AND \"offset\" = ?3");
        if (!statement)
        {
            return std::nullopt;
        }
        auto &query = statement.value();
        if (!query.bind(1, provider) || !query.bind(2, keywords) || !query.bind(3, std::int64_t{offset}))
        {
            return std::nullopt;
        }

        auto row = query.step();
        if (!row || !row.value())
        {
            return std::nullopt;
        }

        const std::int64_t fetchedAt = query.columnInt(1);
        if (nowMillis(clock_) - fetchedAt > kSearchTtlMs)
        {
            return std::nullopt;
        }
        return query.columnString(0);
    }

    void SqliteOnlineStore::saveSearch(const std::string &provider, const std::string &keywords,
                                       std::int32_t offset, const std::string &payload)
    {
        auto statement = db_.prepare("INSERT OR REPLACE INTO online_search_cache"
                                     "(provider, keywords, \"offset\", payload, fetched_at)"
                                     " VALUES(?1, ?2, ?3, ?4, ?5)");
        if (!statement)
        {
            return;
        }
        auto &write = statement.value();
        // 写失败就算了：缓存写不进去不该让搜索失败。
        (void)write.bind(1, provider);
        (void)write.bind(2, keywords);
        (void)write.bind(3, std::int64_t{offset});
        (void)write.bind(4, payload);
        (void)write.bind(5, nowMillis(clock_));
        (void)write.step();
    }

    void SqliteOnlineStore::trimSearch() const
    {
        // rowid 是 SQLite 的隐式主键：四张表都是复合主键，没有自增列可用。
        (void)db_.exec("DELETE FROM online_search_cache WHERE rowid NOT IN ("
                       "  SELECT rowid FROM online_search_cache ORDER BY fetched_at DESC LIMIT " +
                       std::to_string(kMaxSearchRows) + ")");
    }

} // namespace sonora::online::adapters
