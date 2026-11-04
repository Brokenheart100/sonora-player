// M0 遗留的链接探针。
//
// 每个静态库的 probe() 返回一个互不相同的编号，验证 12 个库都被正确链接、
// 符号都能到达。M1 之后真实代码取代了大部分 probe，但这组断言留着——
// 它是「target 图没断」最便宜的证据。
//
// 编号不连续是有意的：5/8/11/14 留给了各模块的 Adapters（它们依赖 Qt，
// 本二进制不许链）。改 probe 编号时这里要同步改。
#include "doctest_support.h"

#include <mamba/catalog/contracts/probe.h>
#include <mamba/catalog/core/probe.h>
#include <mamba/contracts/probe.h>
#include <mamba/kernel/probe.h>
#include <mamba/lyrics/contracts/probe.h>
#include <mamba/lyrics/core/probe.h>
#include <mamba/playback/contracts/probe.h>
#include <mamba/playback/core/probe.h>
#include <mamba/playlists/contracts/probe.h>
#include <mamba/playlists/core/probe.h>
#include <mamba/preferences/contracts/probe.h>
#include <mamba/preferences/core/probe.h>

TEST_CASE("M0 链接探针：12 个纯 C++ 库都能到达")
{
    CHECK(mamba::kernel::probe() == 1);
    CHECK(mamba::contracts::probe() == 2);

    CHECK(mamba::catalog::contracts::probe() == 3);
    CHECK(mamba::catalog::core::probe() == 4);

    CHECK(mamba::playback::contracts::probe() == 6);
    CHECK(mamba::playback::core::probe() == 7);

    CHECK(mamba::playlists::contracts::probe() == 9);
    CHECK(mamba::playlists::core::probe() == 10);

    CHECK(mamba::lyrics::contracts::probe() == 12);
    CHECK(mamba::lyrics::core::probe() == 13);

    CHECK(mamba::preferences::contracts::probe() == 15);
    CHECK(mamba::preferences::core::probe() == 16);
}
