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
        constexpr std::array<platform::storage::Migration, 3> kMigrations{{
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
            // v2（R5-3）：淘汰改成 LRU。`fetched_at` 只能回答"什么时候抓的"，
            // 回答不了"还有没有人用"——一个天天被搜的老词会被当成最旧的行删掉。
            // 存量行按 fetched_at 回填，老库升级后第一次 trim 的行为与以前一致。
            // v2（R5-3）：淘汰改成 LRU。`fetched_at` 只能回答"什么时候抓的"，
            // 回答不了"还有没有人用"——一个天天被搜的老词会被当成最旧的行删掉。
            //
            // 只加列、不回填：存量行的 last_used_at 保持 0，排序时用
            // `CASE WHEN last_used_at = 0 THEN fetched_at ELSE last_used_at END`
            // 兜底（见 SqliteOnlineStore::trimSearch），语义与"回填成 fetched_at"
            // 完全一样，但迁移只改 schema 不动行。
            //
            // 这不是洁癖：200 条大 payload 的 UPDATE 在 3.54 的 amalgamation 里
            // 会因语句日志落盘失败报 `unable to open database file`，把整个迁移
            // 卡死（实测复现）。ALTER 是 O(1) 的 schema 变更，永远不受数据量影响。
            {2,
             "online: 缓存加 last_used_at（按最近使用淘汰）",
             R"SQL(
ALTER TABLE online_search_cache ADD COLUMN last_used_at INTEGER NOT NULL DEFAULT 0;
ALTER TABLE online_detail_cache ADD COLUMN last_used_at INTEGER NOT NULL DEFAULT 0;
)SQL"},
            // v3（N2-1）：会话行要记 uid。"我的歌单"必须带 uid 才能问
            // `/api/user/playlist`，而从 cookie 里解不出 uid（MUSIC_U 是一坨密文）。
            // 只加列、不回填：老库升级后 uid 是空串，"我的"页显示"重新登录一次"。
            {3,
             "online: 会话行加 user_id",
             R"SQL(
ALTER TABLE online_session ADD COLUMN user_id TEXT NOT NULL DEFAULT '';
)SQL"},
        }};
    } // namespace

    std::span<const platform::storage::Migration> migrations()
    {
        return std::span<const platform::storage::Migration>{kMigrations};
    }

} // namespace sonora::online::adapters
