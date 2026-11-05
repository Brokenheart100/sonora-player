-- MambaMusic target 图 —— 依赖关系的唯一真相源（docs/ARCHITECTURE.md §9.4）
--
-- 这张图不是"文档化的依赖"，它**就是**边界强制机制本身。
-- C++ 的 #include 搜索路径由构建系统授予，所以下面两件事不靠 code review：
--
--   1) 纯 C++ target（kernel / contracts / */core）不加 qt 规则、不 add_frameworks
--      → 领域层里写 #include <QString> 直接编译失败（找不到头文件）
--   2) Mamba.Presentation 不 add_deps 任何 *.Core
--      → ViewModel 想 include 聚合头，路径根本不存在
--
-- 机器管不到的那部分（语法痕迹、产物污染）交给 xmake/checks/boundaries.py。
-- 改动本文件前先读 §9.4：加一条 dep 就是给某个层开一扇门。
--
-- 写法约定：xmake 的 `target("x")` 是"设定当前 target"，之后的 API 调用都作用于它，
-- 所以下面每个 target 都是"先 target(...)，随后一串裸调用"。

local M = "modules/"
local P = "platform/"

-- 重要：本文件是被 includes() 进来的，xmake 解析相对路径时以**本文件所在目录**
-- （xmake/）为基准，不是仓库根。所有路径必须自己拼到项目根上，否则会变成
-- xmake/modules/... 然后静默匹配不到文件。
local ROOT = os.projectdir()

local function rel(...)
    return path.join(ROOT, ...)
end

-- 所有 target 共同的 Windows / MSVC 约束。集中一处，避免某个 target 漏规则。
local function msvc()
    set_languages("c++20")
    set_encodings("utf-8")   -- /utf-8：中文注释与 QML 字面量都指望它
    -- 运行库**故意不写死**：xmake 会按模式给 debug=/MDd、release=/MD。
    -- 有 Qt 的 target 必须按这个配对走——Qt 的 debug DLL（Qt6Cored.dll）是
    -- 用 /MDd 编的，我们若强写 /MD，进程里会同时存在两套 CRT，跨 DLL 的
    -- new/free 落到不同堆上，症状是启动几秒后静默退出、退出码 0xC0000374
    -- （STATUS_HEAP_CORRUPTION），而且不打任何日志。
    set_warnings("all")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_cxflags("/Zc:__cplusplus", "/permissive-", "/bigobj", {tools = "cl"})
end

-- spec = { name, dir, deps?, qt? }
--   qt 缺省 → 纯 C++ target（不许碰 Qt）。给了 qt 则显式列出需要的 Qt 模块。
local function lib(spec)
    target(spec.name)
    set_kind("static")
    msvc()
    add_files(rel(spec.dir, "src") .. "/**.cpp")
    add_includedirs(rel(spec.dir, "include"), {public = true})
    if spec.deps then
        add_deps(spec.deps)
    end
    if spec.qt then
        add_rules("qt.static")
        -- 有 Qt 的 target 必须把**头文件**也交给 qt.moc 规则：moc 只处理
        -- add_files 里扩展名为 .h/.hpp 的文件，光有 .cpp 是找不到 Q_OBJECT 的，
        -- 症状是链接期一串 LNK2001 找不到 staticMetaObject / qt_metacall。
        add_files(rel(spec.dir, "include") .. "/**.h")
        for _, framework in ipairs(spec.qt) do
            add_frameworks(framework)
        end
    end
end

local function capitalize(word)
    return word:sub(1, 1):upper() .. word:sub(2)
end

local BUSINESS = {"catalog", "playback", "playlists", "lyrics", "preferences"}

-- ── 共享内核 ────────────────────────────────────────────────────────────────
-- 零外部依赖。所有模块都可以依赖它。
lib({name = "Mamba.Kernel", dir = M .. "kernel"})
lib({name = "Mamba.Contracts", dir = M .. "contracts"})

-- ── 业务模块：contracts（纯数据，薄）/ core（全部业务）/ adapters（全部技术细节）
for _, module in ipairs(BUSINESS) do
    local name = "Mamba." .. capitalize(module) .. "."
    local dir = M .. module .. "/"

    -- 契约：跨模块集成事件与共享 DTO。只许依赖 Kernel + 共享 Contracts。
    lib({
        name = name .. "Contracts",
        dir = dir .. "contracts",
        deps = {"Mamba.Kernel", "Mamba.Contracts"},
    })

    -- 领域层与应用层。**故意没有 qt**：这里凡是出现 Q 开头的东西都是设计错误。
    -- 之所以能"故意没有"，是因为 include 路径不给就真拿不到。
    lib({
        name = name .. "Core",
        dir = dir .. "core",
        deps = {"Mamba.Kernel", "Mamba.Contracts", name .. "Contracts"},
    })

    -- 适配器：仓储实现、元数据读取、平台桥接。唯一允许看见基础设施的层。
    --
    -- 这里暂时**不给 qt**（M0 的计划里是给的）。理由：M1 的适配器一个 Qt 类型都
    -- 没用上，而一旦链上 QtCore，所有想测它们的二进制都得拖着 Qt DLL 跑——
    -- 那会让"领域+存储+适配器是可以纯 C++ 单测的"这条性质当场失效。
    -- 加回来的时机很明确：某个适配器第一次出现 Q_OBJECT 或 QString 的时候。
    lib({
        name = name .. "Adapters",
        dir = dir .. "adapters",
        deps = {
            name .. "Core",
            name .. "Contracts",
            "Mamba.Platform.Storage",
            "Mamba.Kernel",
            "Mamba.Contracts",
        },
    })
end

-- ── 平台层（横切）────────────────────────────────────────────────────────────
-- 具体技术：SQLite、音频输出、文件系统、Win32。领域层永远看不见这四个。
-- 里程碑推进时逐步加依赖（M0 刻意只留 QtCore，缩小首次编译的失败面）：
--   M3 → Platform.Audio   加 QtMultimedia（QAudioSink / QMediaPlayer 过渡）
--   M4 → Platform.Audio   加 ffmpeg（vcpkg 构建，见 §9.5）
--   M5 → Catalog.Adapters 加 taglib
--
-- Storage 已经落地（M1）：它**不链 Qt**。一个 SQLite 封装没有任何理由依赖 QtCore，
-- 而一旦链上，所有要测它的二进制都得拖着 Qt DLL 跑——那会把"领域层测试纯 C++"
-- 这个性质弄丢。现在 mamba_tests 可以直接把它链进来。

-- ── 第三方：SQLite amalgamation ──────────────────────────────────────────────
-- 为什么不用 add_requires("sqlite3")：xmake-repo 的 sqlite3 包**没有任何 FTS5 开关**
-- （它只有 rtree / math_functions / dbstat_vtab 之类），而 FTS5 是编译期特性，
-- 装完才发现搜不了中文就只能重来（见 docs/DEPENDENCIES.md §3）。
-- 自带 amalgamation 之后：开关归我们、没有第二套 runtime、版本随仓库走。
target("Mamba.ThirdParty.Sqlite")
set_kind("static")
-- 故意不调 msvc()：这是 C 文件，/Zc:__cplusplus、/permissive-、set_languages("c++20")
-- 都是 C++ 专用；把 sqlite3.c 当 C++ 编会直接爆掉。
set_encodings("utf-8")
-- 运行库跟随模式（同 msvc() 里的说明）：sqlite3.obj 必须与链接它的库同一套 CRT
add_defines(
    "SQLITE_ENABLE_FTS5",              -- 中文子串搜索的地基（M5 用）
    "SQLITE_ENABLE_MATH_FUNCTIONS",    -- 核对采样率/时长时顺手
    "SQLITE_THREADSAFE=1",             -- 串行化模式；我们只有一条写线程，够用
    "SQLITE_DEFAULT_WAL_SYNCHRONOUS=1",-- WAL + NORMAL：崩溃不丢库，只丢最后一次提交
    "SQLITE_DQS=0",                    -- 禁止用双引号当字符串，早点暴露 SQL 写错
    "SQLITE_OMIT_DEPRECATED"
)
add_files(rel("third_party", "sqlite3", "sqlite3.c"))
add_includedirs(rel("third_party", "sqlite3"), {public = true})

lib({
    name = "Mamba.Platform.Storage",
    dir = P .. "storage",
    deps = {"Mamba.Kernel", "Mamba.ThirdParty.Sqlite"},
})
lib({
    name = "Mamba.Platform.FileSystem",
    dir = P .. "filesystem",
    deps = {"Mamba.Kernel"},
    qt = {"QtCore"},
})
lib({
    name = "Mamba.Platform.Audio",
    dir = P .. "audio",
    -- 只依赖播放模块的**契约**：音频输出是实现 IAudioOutput 端口的适配器，
    -- 它不该知道 PlayQueue 的存在。
    -- M3：加 QtMultimedia（QMediaPlayer 过渡引擎，见 docs/IMPLEMENTATION.md §M3）。
    deps = {"Mamba.Kernel", "Mamba.Playback.Contracts"},
    qt = {"QtCore", "QtMultimedia"},
})
lib({
    name = "Mamba.Platform.Win",
    dir = P .. "win",
    deps = {"Mamba.Kernel"},
    qt = {"QtCore"},
})

-- ── 表现层（MVVM）───────────────────────────────────────────────────────────
-- 只依赖各模块的 Contracts。**这一行下面不许出现任何 *.Core。**
local presentation_deps = {"Mamba.Kernel", "Mamba.Contracts"}
for _, module in ipairs(BUSINESS) do
    table.insert(presentation_deps, "Mamba." .. capitalize(module) .. ".Contracts")
end
lib({
    name = "Mamba.Presentation",
    dir = "presentation",
    deps = presentation_deps,
    qt = {"QtCore", "QtGui", "QtQuick"},
})

-- ── 组合根：唯一同时看得见 Adapters 与 Presentation 的地方 ───────────────────
target("MambaMusic")
set_kind("binary")
msvc()
add_rules("qt.quickapp")
add_rules("qt.qrc")
add_files(rel("host", "*.cpp"))
add_files(rel("resources.qrc"))
add_deps({
    "Mamba.Presentation",
    "Mamba.Catalog.Adapters",
    "Mamba.Playback.Core",
    "Mamba.Playback.Adapters",
    "Mamba.Playlists.Adapters",
    "Mamba.Lyrics.Adapters",
    "Mamba.Preferences.Adapters",
    "Mamba.Platform.Storage",
    "Mamba.Platform.Audio",
    "Mamba.Platform.FileSystem",
    "Mamba.Platform.Win",
})

-- ── 测试二进制 ──────────────────────────────────────────────────────────────
-- 只链纯 C++ target，所以它不初始化 Qt、不加载 Qt DLL、能当纯单测跑。
-- 领域层测不干净，说明领域层已经被污染了。
target("mamba_tests")
set_kind("binary")
set_default(false)   -- 不参与默认构建：xmake build mamba_tests 显式构建
set_languages("c++20")
set_encodings("utf-8")
add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
add_cxflags("/Zc:__cplusplus", "/permissive-", "/bigobj", {tools = "cl"})
add_files(rel("tests", "*.cpp"))
add_includedirs(rel("tests"))
add_packages("doctest")
add_deps({
    "Mamba.Kernel",
    "Mamba.Contracts",
    "Mamba.Catalog.Contracts",
    "Mamba.Catalog.Core",
    "Mamba.Playback.Contracts",
    "Mamba.Playback.Core",
    "Mamba.Playlists.Contracts",
    "Mamba.Playlists.Core",
    "Mamba.Lyrics.Contracts",
    "Mamba.Lyrics.Core",
    "Mamba.Preferences.Contracts",
    "Mamba.Preferences.Core",
    -- Storage 与 Catalog.Adapters 都不链 Qt，所以可以进这个二进制。
    -- 有它们才能测真实的 SQL、迁移与导入链路——M1 的验收（导入 20 首 → 列表 20 行）
    -- 就是在这里被证明的，不是靠假端口。
    "Mamba.Platform.Storage",
    "Mamba.Catalog.Adapters",
})
