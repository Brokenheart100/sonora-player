// TypedId 的测试。
//
// 重点不是「能不能取值」，而是**两个 id 类型不能互相赋值**。那条由编译器保证，
// 写不出反例，所以这里测的是运行时语义：默认无效、0 无效、比较与哈希一致。
#include "doctest_support.h"

#include <string>
#include <unordered_set>

#include <mamba/kernel/TypedId.h>

namespace {

struct TrackTag;
struct AlbumTag;

using TrackId = mamba::kernel::TypedId<TrackTag>;
using AlbumTagId = mamba::kernel::TypedId<AlbumTag>;

}  // namespace

TEST_CASE("TypedId：默认构造是无效 id，不是 id=0 的合法值")
{
    const TrackId empty;
    CHECK_FALSE(empty.isValid());
    CHECK(empty.value() == 0);

    const auto real = TrackId::fromValue(42);
    CHECK(real.isValid());
    CHECK(real.value() == 42);
}

TEST_CASE("TypedId：相等与排序按底盘值来")
{
    CHECK(TrackId::fromValue(7) == TrackId::fromValue(7));
    CHECK(TrackId::fromValue(7) != TrackId::fromValue(8));
    CHECK(TrackId::fromValue(7) < TrackId::fromValue(8));
}

TEST_CASE("TypedId：能放进 unordered_set，且不同值不冲突")
{
    std::unordered_set<TrackId> ids;
    ids.insert(TrackId::fromValue(1));
    ids.insert(TrackId::fromValue(2));
    ids.insert(TrackId::fromValue(1));  // 重复，不该增加

    CHECK(ids.size() == 2);
    CHECK(ids.contains(TrackId::fromValue(2)));
    CHECK_FALSE(ids.contains(TrackId::fromValue(3)));
}

TEST_CASE("TypedId：不同 Tag 的 id 在类型上就是两回事")
{
    // 这一条不是运行时断言，而是「写不出来就是通过」：
    //   TrackId a = TrackId::fromValue(1);
    //   AlbumTagId b = a;            // 编译错误，这正是要的
    // 下面用 static_assert 把这条意图钉住，避免以后有人加一个隐式转换构造函数。
    static_assert(!std::is_convertible_v<TrackId, AlbumTagId>);
    static_assert(!std::is_same_v<TrackId, AlbumTagId>);
    // 顺带钉住：没有从整数隐式转换。
    static_assert(!std::is_convertible_v<std::uint64_t, TrackId>);
    CHECK(true);
}
