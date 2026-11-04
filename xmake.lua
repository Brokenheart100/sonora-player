-- MambaMusic · 顶层构建入口
--
-- 这里只管「项目级策略」。所有 target 集中定义在 xmake/targets.lua 里，
-- 因为 target 图本身就是架构边界（见 docs/ARCHITECTURE.md §9）。
--
-- 第一次使用：
--     xmake f --qt=<QT_PREFIX> -m debug -a x64
--     xmake -j
--     xmake run

set_xmakever("2.9.5")

set_project("MambaMusic")
set_version("0.1.0")

-- 平台与架构是架构决定，不是可选项。
-- 放开跨平台会立刻把 #ifdef _WIN32 漏进 core/，那正是要防的东西。
set_allowedplats("windows")
set_allowedarchs("x64")
set_defaultplat("windows")
set_defaultarchs("x64")

add_rules("mode.debug", "mode.release")

-- ── 依赖 ────────────────────────────────────────────────────────────────────
-- 只有测试用 doctest。它 headeronly、cmake=false 时只是把 doctest/ 目录拷到
-- include 下，不需要装 cmake（默认 true 会去跑 cmake，白白多一个工具依赖）。
--
-- 运行时依赖这里一个都没有：SQLite 走自带的 amalgamation（third_party/，
-- 见 docs/DEPENDENCIES.md §3），spdlog/TagLib/FFmpeg 都在 M5/M4 才进来。
add_requires("doctest 2.5.3", {configs = {cmake = false}})

-- 22 个库 target + 1 个可执行 + 1 个测试二进制
includes("xmake/targets.lua")
