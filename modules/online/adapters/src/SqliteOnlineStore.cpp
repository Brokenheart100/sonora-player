#include "sonora/online/adapters/SqliteOnlineStore.h"

#include <algorithm>
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
                                                             std::int32_t offset)
    {
        auto statement = db_.prepare("SELECT payload, fetched_at FROM online_search_cache"
                                     " WHERE provider = ?1 AND keywords = ?2 AND [offset] = ?3");
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
        const std::string payload = query.columnString(0);

        // 命中即"最近使用"。写失败不影响本次命中——下次淘汰时它会被当成旧行，
        // 代价只是可能多删一条还能用的缓存。
        auto touch = db_.prepare("UPDATE online_search_cache SET last_used_at = ?4"
                                 " WHERE provider = ?1 AND keywords = ?2 AND [offset] = ?3");
        if (touch)
        {
            auto &update = touch.value();
            (void)update.bind(1, provider);
            (void)update.bind(2, keywords);
            (void)update.bind(3, std::int64_t{offset});
            (void)update.bind(4, nowMillis(clock_));
            (void)update.step();
        }
        return payload;
    }

    void SqliteOnlineStore::saveSearch(const std::string &provider, const std::string &keywords,
                                       std::int32_t offset, const std::string &payload)
    {
        const std::int64_t now = nowMillis(clock_);
        auto statement = db_.prepare("INSERT OR REPLACE INTO online_search_cache"
                                     "(provider, keywords, [offset], payload, fetched_at, last_used_at)"
                                     " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
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
        (void)write.bind(5, now);
        (void)write.bind(6, now);
        (void)write.step();
    }

    void SqliteOnlineStore::trimSearch() const
    {
        Stats current = stats();
        // 每次最多删 50 行：一次删太多会把 WAL 撑大，而这些行本来也只是缓存。
        constexpr std::int32_t kBatch = 50;
        while (current.searchRows > kMaxSearchRows || current.searchBytes > kMaxSearchBytes)
        {
            const std::int32_t excessRows = current.searchRows > kMaxSearchRows ? current.searchRows - kMaxSearchRows : 1;
            const std::int32_t batch = std::clamp(excessRows, 1, kBatch);
            const auto removed = db_.exec(
                "DELETE FROM online_search_cache WHERE rowid IN ("
                "  SELECT rowid FROM online_search_cache"
                "  ORDER BY CASE WHEN last_used_at = 0 THEN fetched_at ELSE last_used_at END ASC,"
                "           fetched_at ASC LIMIT " +
                std::to_string(batch) + ")");
            if (!removed)
            {
                return;
            }
            const Stats next = stats();
            if (next.searchRows >= current.searchRows)
            {
                return; // 没有进展就不再转，避免在坏库上死循环
            }
            current = next;
        }
    }

    SqliteOnlineStore::Stats SqliteOnlineStore::stats() const
    {
        Stats out;
        auto statement = db_.prepare(
            "SELECT (SELECT COUNT(*) FROM online_search_cache),"
            "       (SELECT COALESCE(SUM(length(payload)), 0) FROM online_search_cache),"
            "       (SELECT COUNT(*) FROM online_song_cache),"
            "       (SELECT COUNT(*) FROM online_detail_cache)");
        if (!statement)
        {
            return out;
        }
        auto &query = statement.value();
        auto row = query.step();
        if (!row || !row.value())
        {
            return out;
        }
        out.searchRows = static_cast<std::int32_t>(query.columnInt(0));
        out.searchBytes = query.columnInt(1);
        out.songRows = static_cast<std::int32_t>(query.columnInt(2));
        out.detailRows = static_cast<std::int32_t>(query.columnInt(3));
        return out;
    }

} // namespace sonora::online::adapters
