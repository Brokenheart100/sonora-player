// M0 遗留的链接探针。
//
// 每个静态库的 probe() 返回一个互不相同的编号，验证 12 个库都被正确链接、
// 符号都能到达。M1 之后真实代码取代了大部分 probe，但这组断言留着——
// 它是「target 图没断」最便宜的证据。
//
// 编号不连续是有意的：5/8/11/14 留给了各模块的 Adapters（它们依赖 Qt，
// 本二进制不许链）。改 probe 编号时这里要同步改。
#include "doctest_support.h"

#include <sonora/catalog/contracts/probe.h>
#include <sonora/catalog/core/probe.h>
#include <sonora/contracts/probe.h>
#include <sonora/kernel/probe.h>
#include <sonora/lyrics/contracts/probe.h>
#include <sonora/lyrics/core/probe.h>
#include <sonora/playback/contracts/probe.h>
#include <sonora/playback/core/probe.h>
#include <sonora/playlists/contracts/probe.h>
#include <sonora/playlists/core/probe.h>
#include <sonora/preferences/contracts/probe.h>
#include <sonora/preferences/core/probe.h>

TEST_CASE("M0 链接探针：12 个纯 C++ 库都能到达")
{
    CHECK(sonora::kernel::probe() == 1);
    CHECK(sonora::contracts::probe() == 2);

    CHECK(sonora::catalog::contracts::probe() == 3);
    CHECK(sonora::catalog::core::probe() == 4);

    CHECK(sonora::playback::contracts::probe() == 6);
    CHECK(sonora::playback::core::probe() == 7);

    CHECK(sonora::playlists::contracts::probe() == 9);
    CHECK(sonora::playlists::core::probe() == 10);

    CHECK(sonora::lyrics::contracts::probe() == 12);
    CHECK(sonora::lyrics::core::probe() == 13);

    CHECK(sonora::preferences::contracts::probe() == 15);
    CHECK(sonora::preferences::core::probe() == 16);
}
