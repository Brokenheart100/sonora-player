-- Sonora · 顶层构建入口
--
-- 这里只管「项目级策略」。所有 target 集中定义在 xmake/targets.lua 里，
-- 因为 target 图本身就是架构边界（见 docs/ARCHITECTURE.md §9）。
--
-- 第一次使用：
--     xmake f --qt=<QT_PREFIX> -m debug -a x64
--     xmake -j
--     xmake run

set_xmakever("2.9.5")

set_project("Sonora")

-- 版本号的**唯一出处**。改这一行，`QCoreApplication::applicationVersion()`、
-- 侧栏底部的 `v0.1.0`、打包产物名都会跟着变；只有 `assets/app.rc` 的
-- VERSIONINFO 要手动同步（资源编译器不读 Lua）。
SONORA_VERSION = "0.1.0"
set_version(SONORA_VERSION)

-- 同一份版本号进 C++：host/main.cpp 用它调 setApplicationVersion()；
-- QML 侧读 `Qt.application.version`，两侧永远一致。
add_defines('SONORA_VERSION="' .. SONORA_VERSION .. '"')

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
-- TagLib（M5）：真时长 + 内嵌封面。走 xmake-repo 源码构建，与项目同一个 /MD、
-- 同一个 C++20，不会引进第二套 CRT（这是不用 vcpkg 装它的理由，见
-- docs/DEPENDENCIES.md §4）。
--
-- 其余运行时依赖仍然没有：SQLite 走自带 amalgamation（third_party/），
-- FFmpeg 走 vcpkg（xmake/targets.lua 顶部的 VCPKG_ROOT）。
add_requires("doctest 2.5.3", {configs = {cmake = false}})
add_requires("taglib 2.3.2")
-- JSON 只给 online/core 用（解析网易云的响应）。header-only，走 cmake=false
-- 是为了让 xmake 直接解压模板头文件，不额外拉一个 CMake 构建步骤。
add_requires("nlohmann_json 3.12.0", {configs = {cmake = false}})
-- 扫码登录的二维码要本地编码：网易云的明文接口只给 unikey，不返回二维码图。
-- nayuki 的实现是单文件、MIT、零依赖，正好挂在 core 上（测试也不用 Qt）。
add_requires("qr-code-generator-cpp v1.8.0", {configs = {cmake = false}})

-- TagLib 是 LGPL-2.1 / MPL-1.1 双授权。我们走 MPL-1.1：它只要求"改动过的
-- TagLib 源文件"继续开源（我们一行没改），静态链接是允许的。xmake 只看包声明的
-- 第一个许可证（LGPL-2.1），拿不准就每次构建都警告一句。这里关掉那条检查，
-- 理由记在 docs/DEPENDENCIES.md §3，而不是让它变成一条人人无视的噪音。
set_policy("check.target_package_licenses", false)

-- 22 个库 target + 1 个可执行 + 1 个测试二进制
includes("xmake/targets.lua")

-- 自定义 task：`xmake qsb` 把 assets/shaders/*.frag 编成 .qsb
-- （Qt 的 qt_add_shaders() 在 xmake 里没有等价物，见 docs/ARCHITECTURE.md §9.5）
includes("xmake/rules/qsb.lua")
