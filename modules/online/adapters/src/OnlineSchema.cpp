#include "sonora/online/adapters/OnlineSchema.h"

#include <array>

namespace sonora::online::adapters
{

    namespace
    {
        // 四张表各管一件事：
        //   online_search_cache —— 关键词+offset → 原始 JSON（TTL 10 分钟）
        //   online_detail_cache —— songId → 歌曲详情 JSON（TTL 7 天，字段几乎不变）
        //   online_song_cache   —— songId → 直链 + 过期时刻（expi 给的是 1200 秒）
        //   online_session      —— provider → 凭据文件名（**从不存明文 cookie**）
        //
        // 存原始 JSON 而不是拆成列：解析规则会随接口变，拆了列就得每次跟着迁移；
        // 存原文则"解析器升级"不需要动库，而且夹具测试能直接复用它。
        constexpr std::array<platform::storage::Migration, 1> kMigrations{{
            {1,
             "online: 搜索/详情/直链缓存与会话指针",
             R"SQL(
CREATE TABLE online_search_cache (
    provider   TEXT    NOT NULL,
    keywords   TEXT    NOT NULL,
    offset     INTEGER NOT NULL,
    payload    TEXT    NOT NULL,
    fetched_at INTEGER NOT NULL,
    PRIMARY KEY (provider, keywords, offset)
);

CREATE TABLE online_detail_cache (
    song_id    TEXT PRIMARY KEY,
    payload    TEXT NOT NULL,
    fetched_at INTEGER NOT NULL
);

CREATE TABLE online_song_cache (
    song_id    TEXT PRIMARY KEY,
    url        TEXT    NOT NULL,
    expires_at INTEGER NOT NULL,
    level      TEXT    NOT NULL,
    br         INTEGER NOT NULL
);

CREATE TABLE online_session (
    provider   TEXT PRIMARY KEY,
    secret_ref TEXT NOT NULL,
    nickname   TEXT NOT NULL DEFAULT '',
    updated_at INTEGER NOT NULL DEFAULT 0
);
)SQL"},
        }};
    } // namespace

    std::span<const platform::storage::Migration> migrations()
    {
        return std::span<const platform::storage::Migration>{kMigrations};
    }

} // namespace sonora::online::adapters
