// SQLite 存储层的测试。
//
// 这个文件最重要的作用不是"测 CRUD"，而是**证明 FTS5 真的编进去了**。
// 中文子串搜索（M5）全靠它；如果 amalgamation 的编译开关漏了 SQLITE_ENABLE_FTS5，
// 直到 M5 写完搜索才会发现，那时改的是构建系统而不是代码——所以现在就用
// 一个 CREATE VIRTUAL TABLE ... fts5 把它钉死。
#include "doctest_support.h"

#include <string>
#include <vector>


#include <mamba/kernel/Error.h>
#include <mamba/platform/storage/Migrator.h>
#include <mamba/platform/storage/SqliteConnection.h>
#include <mamba/platform/storage/SqliteStatement.h>
#include <mamba/platform/storage/SqliteTransaction.h>
#include <mamba/platform/storage/SqliteUnitOfWork.h>

using mamba::kernel::ErrorCode;
using mamba::platform::storage::Migration;
using mamba::platform::storage::Migrator;
using mamba::platform::storage::SqliteConnection;
using mamba::platform::storage::SqliteOpenMode;
using mamba::platform::storage::SqliteTransaction;
using mamba::platform::storage::SqliteUnitOfWork;

namespace {

SqliteConnection openMemory()
{
    auto opened = SqliteConnection::open(":memory:", SqliteOpenMode::ReadWriteCreate);
    REQUIRE(opened.hasValue());
    return std::move(opened).take();
}

}  // namespace

TEST_CASE("SQLite：能打开内存库并建表")
{
    auto connection = openMemory();
    CHECK(connection.isOpen());

    CHECK(connection.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT NOT NULL);").hasValue());
    CHECK(connection.queryInt("SELECT COUNT(*) FROM t;").valueOr(-1) == 0);
}

TEST_CASE("SQLite：FTS5 编译开关真的生效了（中文搜索的地基）")
{
    auto connection = openMemory();

    auto created = connection.exec(
        "CREATE VIRTUAL TABLE search USING fts5(title, tokenize = 'unicode61');");
    REQUIRE_MESSAGE(created.hasValue(),
                    "FTS5 不可用：检查 Mamba.ThirdParty.Sqlite 是否定义了 SQLITE_ENABLE_FTS5");

    REQUIRE(connection.exec("INSERT INTO search(title) VALUES('夜航西飞');").hasValue());
    REQUIRE(connection.exec("INSERT INTO search(title) VALUES('Night Flight');").hasValue());

    // 英文：unicode61 按空白分词，整个词能精确命中。
    auto english = connection.queryInt("SELECT COUNT(*) FROM search WHERE search MATCH 'flight';");
    REQUIRE(english.hasValue());
    CHECK(english.value() == 1);

    // 中文：unicode61 把连续的汉字当成**一个**词（"夜航西飞"），所以子串 '夜航'
    // 匹配不到，只有前缀查询能命中。这正是 M5 必须上 bigram 分词的原因——
    // 这一行不是妥协，是把「已知限制」写成可执行的断言。
    auto prefix = connection.queryInt("SELECT COUNT(*) FROM search WHERE search MATCH '夜航*';");
    REQUIRE(prefix.hasValue());
    CHECK(prefix.value() == 1);
}

TEST_CASE("SQLite：参数绑定与取值")
{
    auto connection = openMemory();
    REQUIRE(connection.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, score REAL);")
                .hasValue());

    auto prepared = connection.prepare("INSERT INTO t(id, name, score) VALUES(?1, ?2, ?3);");
    REQUIRE(prepared.hasValue());

    auto &insert = prepared.value();
    REQUIRE(insert.bind(1, static_cast<std::int64_t>(1)).hasValue());
    REQUIRE(insert.bind(2, std::string_view{"第一首"}).hasValue());
    REQUIRE(insert.bind(3, 0.75).hasValue());

    auto done = insert.step();
    REQUIRE(done.hasValue());
    CHECK_FALSE(done.value());          // INSERT 没有结果行
    CHECK(connection.lastInsertRowId() == 1);
    CHECK(connection.changes() == 1);

    auto query = connection.prepare("SELECT name, score FROM t WHERE id = ?1;");
    REQUIRE(query.hasValue());
    REQUIRE(query.value().bind(1, static_cast<std::int64_t>(1)).hasValue());

    auto row = query.value().step();
    REQUIRE(row.hasValue());
    REQUIRE(row.value());
    CHECK(query.value().columnString(0) == "第一首");
    CHECK(query.value().columnDouble(1) == doctest::Approx(0.75));
}

TEST_CASE("SQLite：唯一键冲突翻译成 Conflict，而不是一个裸返回码")
{
    auto connection = openMemory();
    REQUIRE(connection.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT UNIQUE);").hasValue());
    REQUIRE(connection.exec("INSERT INTO t(name) VALUES('同名');").hasValue());

    auto second = connection.exec("INSERT INTO t(name) VALUES('同名');");
    REQUIRE_FALSE(second.hasValue());
    CHECK(second.error().code() == ErrorCode::Conflict);
}

TEST_CASE("SQLite：事务回滚后数据不在，提交后在")
{
    auto connection = openMemory();
    REQUIRE(connection.exec("CREATE TABLE t(id INTEGER PRIMARY KEY);").hasValue());

    {
        SqliteTransaction transaction(connection);
        REQUIRE(transaction.active());
        REQUIRE(connection.exec("INSERT INTO t(id) VALUES(1);").hasValue());
        transaction.rollback();
    }
    CHECK(connection.queryInt("SELECT COUNT(*) FROM t;").valueOr(-1) == 0);

    {
        SqliteTransaction transaction(connection);
        REQUIRE(transaction.active());
        REQUIRE(connection.exec("INSERT INTO t(id) VALUES(2);").hasValue());
        REQUIRE(transaction.commit().hasValue());
    }
    CHECK(connection.queryInt("SELECT COUNT(*) FROM t;").valueOr(-1) == 1);
}

TEST_CASE("SQLite：没 commit 就析构 → 自动回滚")
{
    auto connection = openMemory();
    REQUIRE(connection.exec("CREATE TABLE t(id INTEGER PRIMARY KEY);").hasValue());

    {
        SqliteTransaction transaction(connection);
        REQUIRE(connection.exec("INSERT INTO t(id) VALUES(9);").hasValue());
        // 故意不 commit
    }

    CHECK(connection.queryInt("SELECT COUNT(*) FROM t;").valueOr(-1) == 0);
}

TEST_CASE("UnitOfWork：begin/commit 正常，重复 begin 被拒绝")
{
    auto connection = openMemory();
    REQUIRE(connection.exec("CREATE TABLE t(id INTEGER PRIMARY KEY);").hasValue());

    SqliteUnitOfWork unit(connection);
    REQUIRE(unit.begin().hasValue());
    CHECK(unit.active());
    CHECK_FALSE(unit.begin().hasValue());   // 不支持嵌套
    REQUIRE(connection.exec("INSERT INTO t(id) VALUES(1);").hasValue());
    REQUIRE(unit.commit().hasValue());
    CHECK_FALSE(unit.active());

    CHECK(connection.queryInt("SELECT COUNT(*) FROM t;").valueOr(-1) == 1);
}

TEST_CASE("UnitOfWork：rollback 之后 active() 归位")
{
    auto connection = openMemory();
    REQUIRE(connection.exec("CREATE TABLE t(id INTEGER PRIMARY KEY);").hasValue());

    SqliteUnitOfWork unit(connection);
    REQUIRE(unit.begin().hasValue());
    REQUIRE(connection.exec("INSERT INTO t(id) VALUES(1);").hasValue());
    unit.rollback();

    CHECK_FALSE(unit.active());
    CHECK(connection.queryInt("SELECT COUNT(*) FROM t;").valueOr(-1) == 0);
}

TEST_CASE("Migrator：新库从 0 开始，按顺序跑完，重复跑不重复执行")
{
    auto connection = openMemory();

    const std::vector<Migration> migrations{
        Migration{1, "建 tracks 表",
                  "CREATE TABLE tracks(id INTEGER PRIMARY KEY, title TEXT NOT NULL);"},
        Migration{2, "加 duration_ms 列",
                  "ALTER TABLE tracks ADD COLUMN duration_ms INTEGER NOT NULL DEFAULT 0;"},
    };

    Migrator migrator(connection, "catalog");
    CHECK(migrator.currentVersion().valueOr(-1) == 0);

    auto applied = migrator.apply(migrations);
    REQUIRE(applied.hasValue());
    CHECK(applied.value() == 2);
    CHECK(migrator.currentVersion().valueOr(-1) == 2);

    // 再跑一次：版本没变，也不会因为 CREATE TABLE 撞车而失败。
    auto again = migrator.apply(migrations);
    REQUIRE(again.hasValue());
    CHECK(again.value() == 2);

    REQUIRE(connection.exec("INSERT INTO tracks(title) VALUES('测试');").hasValue());
    CHECK(connection.queryInt("SELECT duration_ms FROM tracks;").valueOr(-1) == 0);
}

TEST_CASE("Migrator：两个模块的版本号互不干扰")
{
    auto connection = openMemory();

    Migrator catalog(connection, "catalog");
    Migrator lyrics(connection, "lyrics");

    const std::vector<Migration> catalogMigrations{
        Migration{1, "catalog v1", "CREATE TABLE t_catalog(id INTEGER PRIMARY KEY);"}};
    const std::vector<Migration> lyricsMigrations{
        Migration{1, "lyrics v1", "CREATE TABLE t_lyrics(id INTEGER PRIMARY KEY);"},
        Migration{2, "lyrics v2", "ALTER TABLE t_lyrics ADD COLUMN body TEXT;"}};

    CHECK(catalog.apply(catalogMigrations).valueOr(-1) == 1);
    CHECK(lyrics.apply(lyricsMigrations).valueOr(-1) == 2);

    CHECK(catalog.currentVersion().valueOr(-1) == 1);
    CHECK(lyrics.currentVersion().valueOr(-1) == 2);
}

TEST_CASE("Migrator：失败的迁移整体回滚，版本号不动")
{
    auto connection = openMemory();

    const std::vector<Migration> migrations{
        Migration{1, "建表", "CREATE TABLE t(id INTEGER PRIMARY KEY);"},
        Migration{2, "写错的 SQL", "ALTER TABLE t ADD COLUMN nope INTEGER;"},
        Migration{3, "不存在的表", "ALTER TABLE missing ADD COLUMN x INTEGER;"},
    };

    Migrator migrator(connection, "catalog");
    auto applied = migrator.apply(migrations);
    REQUIRE_FALSE(applied.hasValue());

    // 关键：第 1 条也不该留下——整个 apply 是一个事务。
    CHECK(migrator.currentVersion().valueOr(-1) == 0);
    CHECK_FALSE(connection.queryInt("SELECT COUNT(*) FROM t;").hasValue());
}
