// Preferences 的两层：库里那条 upsert，和内存副本与库的一致性。
//
// 这里用**真的 SQLite**（内存库）：这一层的价值全在 SQL 上（ON CONFLICT 的
// 语义、unixepoch() 的格式、空键名要不要挡），用假存储测不到。
#include "doctest_support.h"

#include <string>
#include <vector>

#include <sonora/platform/storage/Migrator.h>
#include <sonora/platform/storage/SqliteConnection.h>
#include <sonora/preferences/adapters/PreferencesSchema.h>
#include <sonora/preferences/adapters/SqliteSettingsStore.h>
#include <sonora/preferences/core/app/PreferencesService.h>

namespace {

using namespace sonora;
using sonora::preferences::core::PreferencesService;

struct Fixture {
    platform::storage::SqliteConnection connection;

    Fixture() : connection(open()) {}

    static platform::storage::SqliteConnection open()
    {
        auto opened = platform::storage::SqliteConnection::open(":memory:");
        REQUIRE_MESSAGE(opened.hasValue(), "打开内存库失败：" << opened.error().message());
        auto connection = std::move(opened).take();
        platform::storage::Migrator migrator{connection, preferences::adapters::kModuleName};
        const auto version = migrator.apply(preferences::adapters::migrations());
        REQUIRE(version.hasValue());
        CHECK(version.value() == static_cast<int>(preferences::adapters::migrations().size()));
        return connection;
    }
};

}  // namespace

TEST_CASE("设置：没写过的键是 found=false，不是错误")
{
    Fixture fixture;
    preferences::adapters::SqliteSettingsStore store{fixture.connection};
    PreferencesService service{store};

    const auto value = service.get("playback/volume");
    CHECK_FALSE(value.found);
    CHECK(value.key == "playback/volume");
    CHECK(value.value.empty());
    CHECK(service.size() == 0);

    // 没设置过时给默认值，是调用方唯一该做的事。
    CHECK(service.getOrDefault("playback/volume", "100") == "100");
}

TEST_CASE("设置：写进去能读出来，覆盖写不会留两份")
{
    Fixture fixture;
    preferences::adapters::SqliteSettingsStore store{fixture.connection};
    PreferencesService service{store};

    REQUIRE(service.set("playback/volume", "80").hasValue());
    REQUIRE(service.set("playback/mode", "shuffle").hasValue());
    CHECK(service.size() == 2);
    CHECK(service.get("playback/volume").value == "80");
    CHECK(service.get("playback/volume").found);

    REQUIRE(service.set("playback/volume", "55").hasValue());
    CHECK(service.size() == 2);
    CHECK(service.get("playback/volume").value == "55");

    // 库里也只有两行：覆盖写走的是 ON CONFLICT，不是"插一行新的"。
    CHECK(fixture.connection.queryInt("SELECT COUNT(*) FROM kv").value() == 2);
    CHECK(fixture.connection.queryText("SELECT value FROM kv WHERE key = 'playback/volume'").value() ==
          "55");
}

TEST_CASE("设置：内存副本与库同源——新实例读得到上一次写的值")
{
    Fixture fixture;
    preferences::adapters::SqliteSettingsStore store{fixture.connection};
    {
        PreferencesService first{store};
        REQUIRE(first.set("ui/language", "zh_CN").hasValue());
    }
    // 模拟重启：换一个服务实例，缓存是空的，必须从库里读回来。
    PreferencesService second{store};
    CHECK(second.size() == 1);
    CHECK(second.get("ui/language").value == "zh_CN");
}

TEST_CASE("设置：批量取回的条目顺序与请求一致，缺的键以 found=false 占位")
{
    Fixture fixture;
    preferences::adapters::SqliteSettingsStore store{fixture.connection};
    PreferencesService service{store};
    REQUIRE(service.set("a", "1").hasValue());
    REQUIRE(service.set("c", "3").hasValue());

    const auto list = service.getMany({"c", "b", "a"});
    REQUIRE(list.entries.size() == 3);
    CHECK(list.entries[0].key == "c");
    CHECK(list.entries[0].value == "3");
    CHECK_FALSE(list.entries[1].found);
    CHECK(list.entries[2].value == "1");
}

TEST_CASE("设置：删掉的键不再出现在库里，重复删不报错")
{
    Fixture fixture;
    preferences::adapters::SqliteSettingsStore store{fixture.connection};
    PreferencesService service{store};
    REQUIRE(service.set("temp/key", "x").hasValue());

    REQUIRE(service.remove("temp/key").hasValue());
    CHECK_FALSE(service.get("temp/key").found);
    CHECK(fixture.connection.queryInt("SELECT COUNT(*) FROM kv").value() == 0);

    // 幂等：卸载流程里可能删两次。
    CHECK(service.remove("temp/key").hasValue());
}

TEST_CASE("设置：空键名被挡在服务层，不会写进库")
{
    Fixture fixture;
    preferences::adapters::SqliteSettingsStore store{fixture.connection};
    PreferencesService service{store};

    const auto written = service.set("", "x");
    REQUIRE_FALSE(written.hasValue());
    CHECK_FALSE(service.set("", "x").hasValue());
    CHECK(fixture.connection.queryInt("SELECT COUNT(*) FROM kv").value() == 0);
}

TEST_CASE("设置：updated_at 由库填写（unixepoch），不是常量 0")
{
    Fixture fixture;
    preferences::adapters::SqliteSettingsStore store{fixture.connection};
    PreferencesService service{store};
    REQUIRE(service.set("k", "v").hasValue());

    // 这条断言真正防的是"忘了写 updated_at，列默认值 0 一直留着"——
    // 将来做"按修改时间清理"时才会发现，太晚。
    CHECK(fixture.connection.queryInt("SELECT updated_at FROM kv WHERE key = 'k'").value() > 0);
}
