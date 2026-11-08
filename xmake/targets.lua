-- Sonora target 图 —— 依赖关系的唯一真相源（docs/ARCHITECTURE.md §9.4）
--
-- 这张图不是"文档化的依赖"，它**就是**边界强制机制本身。
-- C++ 的 #include 搜索路径由构建系统授予，所以下面两件事不靠 code review：
--
--   1) 纯 C++ target（kernel / contracts / */core）不加 qt 规则、不 add_frameworks
--      → 领域层里写 #include <QString> 直接编译失败（找不到头文件）
--   2) Sonora.Presentation 不 add_deps 任何 *.Core
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

-- FFmpeg 只从 vcpkg 拿。理由见 docs/DEPENDENCIES.md §3：gyan.dev / BtbN 的
-- Windows 预编译包都是 MinGW 构建（.dll.a），和 MSVC 链接会得到
-- `LNK2019: __imp_av_*` 那种看似"函数不存在"的错。
-- 路径可用环境变量 VCPKG_ROOT 覆盖，默认就是本机那份。
local VCPKG_ROOT = os.getenv("VCPKG_ROOT") or "D:/vcpkg"
local VCPKG_TRIPLET = "x64-windows"
local VCPKG_INSTALLED = path.join(VCPKG_ROOT, "installed", VCPKG_TRIPLET)

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

-- online 是 M10 的在线音源（网易云）：三个 target 的依赖形态与其它模块完全一样，
-- 唯一的额外一条是 Adapters 还要链 Sonora.Platform.Net（见下面 platform 段之后）。
local BUSINESS = {"catalog", "playback", "playlists", "lyrics", "preferences", "online"}

-- ── 共享内核 ────────────────────────────────────────────────────────────────
-- 零外部依赖。所有模块都可以依赖它。
lib({name = "Sonora.Kernel", dir = M .. "kernel"})
lib({name = "Sonora.Contracts", dir = M .. "contracts"})

-- ── 业务模块：contracts（纯数据，薄）/ core（全部业务）/ adapters（全部技术细节）
for _, module in ipairs(BUSINESS) do
    local name = "Sonora." .. capitalize(module) .. "."
    local dir = M .. module .. "/"

    -- 契约：跨模块集成事件与共享 DTO。只许依赖 Kernel + 共享 Contracts。
    lib({
        name = name .. "Contracts",
        dir = dir .. "contracts",
        deps = {"Sonora.Kernel", "Sonora.Contracts"},
    })

    -- 领域层与应用层。**故意没有 qt**：这里凡是出现 Q 开头的东西都是设计错误。
    -- 之所以能"故意没有"，是因为 include 路径不给就真拿不到。
    lib({
        name = name .. "Core",
        dir = dir .. "core",
        deps = {"Sonora.Kernel", "Sonora.Contracts", name .. "Contracts"},
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
            "Sonora.Platform.Storage",
            "Sonora.Kernel",
            "Sonora.Contracts",
        },
    })
end

-- online/core 是唯一用到第三方 JSON 的地方。把解析器留在"零 Qt"这一层，
-- 好处是 codec 的夹具单测能直接跑进 sonora_tests（那个二进制不许拖 Qt DLL）；
-- 代价只是给这一层加一个 header-only 包。
target("Sonora.Online.Core")
add_packages("nlohmann_json", "qr-code-generator-cpp")

-- ── 平台层（横切）────────────────────────────────────────────────────────────
-- 具体技术：SQLite、音频输出、文件系统、Win32。领域层永远看不见这四个。
-- 里程碑推进时逐步加依赖（M0 刻意只留 QtCore，缩小首次编译的失败面）：
--   M3 → Platform.Audio   加 QtMultimedia（QAudioSink / QMediaPlayer 过渡）
--   M4 → Platform.Audio   加 ffmpeg（vcpkg 构建，见 §9.5）
--   M5 → Catalog.Adapters 加 taglib
--
-- Storage 已经落地（M1）：它**不链 Qt**。一个 SQLite 封装没有任何理由依赖 QtCore，
-- 而一旦链上，所有要测它的二进制都得拖着 Qt DLL 跑——那会把"领域层测试纯 C++"
-- 这个性质弄丢。现在 sonora_tests 可以直接把它链进来。

-- ── 第三方：SQLite amalgamation ──────────────────────────────────────────────
-- 为什么不用 add_requires("sqlite3")：xmake-repo 的 sqlite3 包**没有任何 FTS5 开关**
-- （它只有 rtree / math_functions / dbstat_vtab 之类），而 FTS5 是编译期特性，
-- 装完才发现搜不了中文就只能重来（见 docs/DEPENDENCIES.md §3）。
-- 自带 amalgamation 之后：开关归我们、没有第二套 runtime、版本随仓库走。
target("Sonora.ThirdParty.Sqlite")
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
    name = "Sonora.Platform.Storage",
    dir = P .. "storage",
    deps = {"Sonora.Kernel", "Sonora.ThirdParty.Sqlite"},
})
lib({
    name = "Sonora.Platform.FileSystem",
    dir = P .. "filesystem",
    deps = {"Sonora.Kernel"},
    qt = {"QtCore"},
})
lib({
    name = "Sonora.Platform.AudioRing",
    dir = P .. "audio/ring",
    -- 纯 C++、零 Qt：SPSC 环形缓冲是音频线程与解码线程之间唯一的数据通道，
    -- 它必须能被 sonora_tests 直接链进来跑两线程压测（M4「单测先行」那一条）。
    -- 一旦把它塞进 Sonora.Platform.Audio，想测它就得拖 Qt DLL，那条压测就写不成了。
    deps = {"Sonora.Kernel"},
})
lib({
    name = "Sonora.Platform.Audio",
    dir = P .. "audio",
    -- 只依赖播放模块的**契约**：音频输出是实现 IAudioOutput 端口的适配器，
    -- 它不该知道 PlayQueue 的存在。
    -- M3：加 QtMultimedia（QMediaPlayer 过渡引擎，见 docs/IMPLEMENTATION.md §M3）。
    -- M4：加 ffmpeg（vcpkg 构建，见 docs/DEPENDENCIES.md §3），换掉 QMediaPlayer。
    deps = {"Sonora.Kernel", "Sonora.Playback.Contracts", "Sonora.Platform.AudioRing"},
    qt = {"QtCore", "QtMultimedia"},
})
lib({
    name = "Sonora.Platform.Win",
    dir = P .. "win",
    -- 凭据存储端口 online/contracts/SecretStore.h 的实现落在这里：DPAPI 要
    -- <windows.h>/<dpapi.h>，而平台层依赖某个模块的**契约**不会把 Qt 带上去
    -- （先例：Platform.Audio → Playback.Contracts）。
    deps = {"Sonora.Kernel", "Sonora.Online.Contracts"},
    qt = {"QtCore"},
})
-- DPAPI 在 crypt32.lib 里（CryptProtectData / CryptUnprotectData）。
target("Sonora.Platform.Win")
add_links("crypt32")

-- 网络：全项目**唯一** link QtNetwork 的地方。别处只认 online/contracts/Transport.h
-- （那个头文件零 Qt），所以 online/adapters 与 sonora_tests 都不必拖 Qt DLL。
-- 想核实这条规矩：grep -rn "QtNetwork" xmake/ modules/  —— 只该命中这一处。
lib({
    name = "Sonora.Platform.Net",
    dir = P .. "net",
    deps = {"Sonora.Kernel", "Sonora.Online.Contracts"},
    qt = {"QtCore", "QtNetwork"},
})

-- online 的适配器要多一条到平台网络的边：BUSINESS 循环给的是"标准三件套"依赖，
-- 这条只有它需要。反向边（Platform.Net → Online.Contracts）是有意的：契约零 Qt，
-- 平台层依赖契约不会把 Qt 带回上层（先例同 Platform.Audio → Playback.Contracts）。
target("Sonora.Online.Adapters")
add_deps("Sonora.Platform.Net")

-- FFmpeg 的 include/link 只挂在这一个 target 上：全项目只有它懂 FFmpeg。
-- 注意 lib() 最后会把当前 target 设成别人，所以这里必须重新 target() 一次。
target("Sonora.Platform.Audio")
add_includedirs(path.join(VCPKG_INSTALLED, "include"))
add_links("avformat", "avcodec", "swresample", "avutil")
-- vcpkg 的 x64-windows 是 release + debug 两套：lib/ 用 /MD 编，debug/lib/ 用 /MDd 编。
-- 我们 debug 构建走的是 /MDd（Qt 的 debug DLL 逼的），链错那一套得到的还是跨 CRT 的
-- 堆损坏，只不过这次炸在 FFmpeg 的 av_malloc 里，症状更难认。
add_linkdirs(path.join(VCPKG_INSTALLED, is_mode("debug") and "debug/lib" or "lib"))

-- TagLib 只挂在 Catalog.Adapters 上：全项目只有它读标签（M5）。
-- 走 xmake-repo 源码构建（taglib 2.3.2），与项目同一个 /MD、同一个 C++20；
-- 用 vcpkg 装它会引进第二套 CRT，debug 构建的症状是进程静默退出。
-- 这里同样要重新 target() 一次，理由同上面的 FFmpeg 段。
target("Sonora.Catalog.Adapters")
add_packages("taglib")

-- 内嵌歌词也要读标签（M7 的第二来源），所以 Lyrics.Adapters 同样挂 taglib。
-- 只读不写：我们从不 setProperties，MPL-1.1 那条"改动过的 TagLib 源文件继续开源"
-- 对我们不适用（引用许可证的理由见 docs/DEPENDENCIES.md §3）。
--
-- 第三来源是在线歌词（N3）：它要调 Online.Contracts 的查询，所以这里多一条
-- "模块 → 别人的契约"的依赖。方向是 lyrics → online 的 contracts，不是 Core，
-- 也不是反向——Online 至今不知道 Lyrics 的存在。
target("Sonora.Lyrics.Adapters")
add_packages("taglib")
add_deps("Sonora.Online.Contracts")

-- ── 表现层（MVVM）───────────────────────────────────────────────────────────
-- 只依赖各模块的 Contracts。**这一行下面不许出现任何 *.Core。**
local presentation_deps = {"Sonora.Kernel", "Sonora.Contracts"}
for _, module in ipairs(BUSINESS) do
    table.insert(presentation_deps, "Sonora." .. capitalize(module) .. ".Contracts")
end
lib({
    name = "Sonora.Presentation",
    dir = "presentation",
    deps = presentation_deps,
    qt = {"QtCore", "QtGui", "QtQml", "QtQuick"},
})

-- ── 组合根：唯一同时看得见 Adapters 与 Presentation 的地方 ───────────────────
target("sonora")
set_kind("binary")
msvc()
add_rules("qt.quickapp")
add_rules("qt.qrc")
add_files(rel("host", "*.cpp"))
add_files(rel("resources.qrc"))
-- Windows 资源：exe 图标 + 版本信息。rc.exe 的相对路径以 .rc 文件所在目录为基准，
-- 所以 assets/app.rc 里写的是 "icons/sonora.ico"。
add_files(rel("assets", "app.rc"))
add_deps({
    "Sonora.Presentation",
    "Sonora.Catalog.Adapters",
    "Sonora.Playback.Core",
    "Sonora.Playback.Adapters",
    "Sonora.Playlists.Adapters",
    "Sonora.Lyrics.Adapters",
    "Sonora.Preferences.Adapters",
    "Sonora.Platform.Storage",
    "Sonora.Platform.Audio",
    "Sonora.Platform.FileSystem",
    "Sonora.Platform.Win",
    "Sonora.Platform.Net",
    "Sonora.Online.Adapters",
})

-- vcpkg 装出来的 DLL 不会自己走到 exe 旁边。少这一步的症状是：`xmake` 一切正常、
-- 双击 exe 立刻退出且没有任何输出（系统找不到 avcodec-*.dll）。
after_build(function(target)
    local bin = path.join(VCPKG_INSTALLED, is_mode("debug") and "debug/bin" or "bin")
    if not os.isdir(bin) then
        return
    end
    for _, pattern in ipairs({"av*.dll", "sw*.dll"}) do
        for _, dll in ipairs(os.files(path.join(bin, pattern))) do
            os.cp(dll, target:targetdir(), {force = true})
        end
    end
end)

-- ── 测试二进制 ──────────────────────────────────────────────────────────────
-- 只链纯 C++ target，所以它不初始化 Qt、不加载 Qt DLL、能当纯单测跑。
-- 领域层测不干净，说明领域层已经被污染了。
target("sonora_tests")
set_kind("binary")
set_default(false)   -- 不参与默认构建：xmake build sonora_tests 显式构建
set_languages("c++20")
set_encodings("utf-8")
add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
add_cxflags("/Zc:__cplusplus", "/permissive-", "/bigobj", {tools = "cl"})
add_files(rel("tests", "*.cpp"))
add_includedirs(rel("tests"))
add_packages("doctest")
add_deps({
    "Sonora.Kernel",
    "Sonora.Contracts",
    "Sonora.Catalog.Contracts",
    "Sonora.Catalog.Core",
    "Sonora.Playback.Contracts",
    "Sonora.Playback.Core",
    "Sonora.Playlists.Contracts",
    "Sonora.Playlists.Core",
    "Sonora.Lyrics.Contracts",
    "Sonora.Lyrics.Core",
    "Sonora.Preferences.Contracts",
    "Sonora.Preferences.Core",
    -- Storage 与 Catalog.Adapters 都不链 Qt，所以可以进这个二进制。
    -- 有它们才能测真实的 SQL、迁移与导入链路——M1 的验收（导入 20 首 → 列表 20 行）
    -- 就是在这里被证明的，不是靠假端口。
    "Sonora.Platform.Storage",
    "Sonora.Catalog.Adapters",
    -- 环形缓冲同样是纯 C++：M4 的两线程压测（100 万样本，不丢不重不越界）跑在这里。
    "Sonora.Platform.AudioRing",
    -- 歌词的两个来源（同名 .lrc、内嵌标签）也不链 Qt，所以真适配器可以在这里测：
    -- M7 的"5 个真实 LRC 跑快照测试"就是靠它，而不是靠假端口。
    "Sonora.Lyrics.Adapters",
    -- 在线：只链零 Qt 的两层。JSON 解析与策略纯函数都在 core，夹具单测才能跑在这里；
    -- 适配器的 Qt 部分（transport、worker）由 Sonora.Platform.Net 提供，不进这个二进制。
    "Sonora.Online.Contracts",
    "Sonora.Online.Core",
    -- R5：缓存淘汰规则写在 SQL 里，只有链上适配器才能用真库证明它。
    -- Online.Adapters 自身零 Qt（传输由 Platform.Net 注入），所以这个二进制
    -- 仍然不拖 Qt DLL——这一条正是"适配器层零 Qt"的直接好处。
    "Sonora.Online.Adapters",
})
