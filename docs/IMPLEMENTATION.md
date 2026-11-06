# MambaMusic 实现步骤

> 按什么顺序做、每步怎么算做完。
> 架构（Clean Architecture / DDD / CQRS / 模块化单体 / MSVC 约束）见 [架构设计](ARCHITECTURE.md)。

原则：**每个里程碑结束必须能 `xmake run` 并演示**。不接受"代码写完但没跑通"。
架构类的里程碑（M0、M1）额外一条：**错了要在 4 人日内发现**，而不是拖到第 20 天。

---

## 0. 里程碑总览

| 里程碑 | 目标 | 人日 | 依赖 | 可演示产出 |
| --- | --- | --- | --- | --- |
| **M0** ✅ | 构建骨架 + 边界强制 | 1.5 | — | 23 个 target 编译通过（3–8s），一个空窗口 |
| **M1** ✅ | 内核 + 总线 + **首个纵向切片** | 3 | M0 | 导入一个文件夹 → 列表显示曲目（走完全部四层） |
| **M2** ✅ | 漫画设计系统 v1 | 2.5 | M0 | Storybook 组件画廊页 |
| **M3** | 音频最小闭环 | 3.5 | M2 | 拖文件进去能出声 ✅ |
| **M4** | 换成 FFmpeg 引擎 | 4 | M3, M1 | ✅ 自研引擎，端口契约不变 |
| **M5** | 曲库完整（导入 + 扫描 + 投影） | 4.5 | M1 | 万首曲库，滚动不掉帧 · **✅ 已完成**（含封面/分组，一万首首扫 9.4s） |
| **M6** | 播放完整体验（队列聚合） | 4 | M4, M5 | 全流程无鼠标操作 |
| **M7** | 歌词 | 3.5 | M5, M6 | 歌词同步误差 < 50ms | **✅ 已完成**（滞后上限 33ms） |
| **M8** | 漫画表现力 | 5 | M2 | **✅ 已完成**（集中线改 Canvas，`qmlcachegen` 未做） |
| **M9** | 打包发布 | 1.5 | 全部 | 干净 Windows 机器双击能跑 |
| | **合计** | **≈ 33** | | 全职 ≈ 6.6 周；业余（每天 2h）≈ 4 个月 |

**关键路径**：`M0 → M1 → M3 → M4 → M6 → M7 → M9`。这条链上任何一环延期都顺延交付。

**进度**：M0 ✅、M1 ✅、M2 ✅、M3 ✅、M4 ✅ 已完成并实测通过（构建、运行、边界探针、边界脚本、单测、截图、帧时间、真音频四格式播放、FFmpeg 引擎替换），记录见 §2 下的「M0」～「M4」小节。**M5 ✅ 已完成**：搜索 / 增量扫描 / 真标签 / 分批与后台导入 / 分页 / 封面缓存 / 分组 / `TrackRow` 全部落地并实测（一万首首扫 9.4s、二扫 403ms、滚动帧 2–5ms）。**M6 ✅ 已完成**：四种播放模式与随机排列、随机模式精确回溯、`TrackFinished` 集成事件 → `play_count`、队列抽屉、`NowPlaying` 页、快捷键、文件被删的优雅降级（实测：连放自动推进、`play_count` 0→1→2、边界 143 文件、单测 94 用例 / 4686 断言）。**M7 ✅ 已完成**：LRC 解析（offset / 多标签 / 翻译 / 逐字）、两个来源与优先级、`LyricsModel` + `NowPlaying` 歌词列（实测：`D:\Music` 6 首真曲 24 行歌词滚动、同步滞后上限 33ms、边界 166 文件、单测 115 用例 / 4795 断言）。**M8 ✅ 已完成**：拟声字池 / 翻页转场 / 角色立绘（喵）/ 组件胶囊化 / 集中线（`Canvas` 重写）全部落地，1480 帧 median 1ms、p99 6ms，启动后无单帧 > 8ms（实测：边界 166 文件、单测 115 用例 / 4795 断言）。**下一步是 M9（打包发布）**。

> 依赖清单（哪个库在哪一层、版本、许可证、获取渠道）见 **[DEPENDENCIES.md](DEPENDENCIES.md)**。

**可并行**：M2（漫画系统）只依赖 M0；M5（曲库）依赖 M1 即可，不必等音频。

### 相比 v1 计划的变化

| 变化 | 原因 |
| --- | --- |
| **+5 人日**（28 → 33） | 架构税 4.5 人日：23 个 target、CQRS 多一层、投影表、严格的领域建模；另 0.5 是依赖接入（doctest / spdlog / SQLite amalgamation），初版漏算 |
| **新增 M1（纵向切片）** | 用 3 人日验证整套架构能否落地，是所有验证里最便宜的一次 |
| 平台从三平台缩到 **Windows only** | 省掉 AppImage / dmg / 交叉编译验证，约 −1.5 人日 |
| 编译器定为 **MSVC** | **FFmpeg 二进制来源变了**：MinGW 预编译包不能用（见 [架构 §9.5](ARCHITECTURE.md)） |

**如果一定要瘦身**，按这个顺序砍（能省 ~3 人日，代价是部分边界退化为靠 review）：
1. `*.Contracts` 合并进 `*.Core`，Presentation 改依赖 Core（省 1 人日，丢掉"Presentation 看不见 Domain"的物理保证）
2. `Lyrics` + `Preferences` 合并成一个 `aux` 模块（省 1 人日）
3. M1 的纵向切片只做"列表查询"，不做"导入命令"（省 0.5 人日）

不要砍 M0 的边界脚本和 M1 的切片——那是这套架构唯一的安全网。

---

## 1. 前置准备

**实测状态（M0 执行时）**：与本文档最初的假设相反，这台机器上编译器、Qt、Python 都有，缺的只有 xmake 和 vcpkg。

| 组件 | 实测 | 备注 |
| --- | --- | --- |
| MSVC | ✅ Visual Studio **2026**（x64） | `cl` 不在 PATH，xmake 自动检出并加载 vcvars |
| Qt | ✅ `D:/Software/Qt/6.12.0/msvc2022_64` | `find_qt` 自动命中，**不需要** `--qt=` |
| xmake | ❌ 全局未装 | M0 用 portable 包，见 §1.3 |
| vcpkg | ✅ 2026-10-29，`ffmpeg[core,avcodec,avformat,swresample]:x64-windows@9.0.2` | M4 已用它装上 FFmpeg |

### 1.1 编译器

只需要 **"使用 C++ 的桌面开发"** 工作负载。`cl` 是否在 PATH 无关紧要，xmake 自己会找。验证看 configure 日志：

```
checking for Microsoft Visual Studio (x64) version ... 2026
```

### 1.2 Qt（**必须是 msvc2022_64**）

已装 **Qt 6.12.0**，位于 `D:/Software/Qt/6.12.0/msvc2022_64`。

- 必须是 **MSVC 2022 64-bit** 包，与编译器同 ABI。装成 MinGW 包等于白装。
- 自检：`ls D:/Software/Qt/6.12.0/` 只应出现 `msvc2022_64`。
- 该版本含 Qt Quick / Multimedia / Shader Tools / SQL / SVG，M3、M7、M8 要的东西都在。
- 架构 §5 里"建议 Qt 6.8 LTS"因此作废——**以机器上实际安装的 6.12.0 为准**。

### 1.3 xmake

全局未装。M0 用的是 portable 包（免安装、不污染 PATH）：

```bash
# 一次性：下载解压（5.9M）
curl -fsSL -o /tmp/xmake.zip https://github.com/xmake-io/xmake/releases/download/v3.1.2/xmake-v3.1.2.win64.zip
# 解压到任意目录，例如 %TEMP%\xmake-test\xmake
```

每个新 shell 里：

```bash
export PATH="/c/Users/123/AppData/Local/Temp/xmake-test/xmake:$PATH"
xmake --version     # xmake v3.1.2+...
```

想长期用，装成全局命令：`powershell -c "irm https://xmake.io/psget.text | iex"`。版本要求 >= 2.9.5。

### 1.4 装 vcpkg 并编译 FFmpeg（**M4 已装好，2026-10-29 / 9.0.2**）

```powershell
git clone --depth 1 https://github.com/microsoft/vcpkg D:\vcpkg
cmd /c "D:\vcpkg\bootstrap-vcpkg.bat -disableMetrics"
# 只要 FFmpeg，而且要裁剪：播放器不需要 avfilter / avdevice / swscale
D:\vcpkg\vcpkg install "ffmpeg[core,avcodec,avformat,swresample]:x64-windows" --clean-after-build
```

**注意：这一版 vcpkg 的 classic 模式不认 `--features` / `--no-default-features`**（会报 `error: unexpected switch`）。要压掉默认 feature，就把 `core` 一起写进方括号——`core` 表示"不要默认集合，只要我列的"。

实测 12 分钟编完，产出 `ffmpeg[avcodec,avformat,core,swresample]:x64-windows@9.0.2`（`--enable-shared --disable-swscale --disable-avfilter --disable-avdevice --enable-mediafoundation`，LGPL）。

三个产物位置，`xmake/targets.lua` 里都接好了：

| 用途 | 路径 |
| --- | --- |
| 头文件 | `D:/vcpkg/installed/x64-windows/include` |
| release 库（/MD） | `installed/x64-windows/lib` |
| debug 库（/MDd） | `installed/x64-windows/debug/lib` |
| 运行时 DLL | `installed/x64-windows/bin`（debug 在 `debug/bin`） |

> **debug 构建一定要链 `debug/lib`**：Debug 的 exe 是 `/MDd`，链 `/MD` 编的 `.lib` 会得到跨 CRT 堆损坏，而且只在 `av_malloc` 里炸，看着像 FFmpeg 的 bug。
> **DLL 必须拷到 exe 旁边**：`MambaMusic` 的 `after_build` 已经做了；手动跑 exe 时如果没拷，症状是"双击立刻退出、没有任何输出"。

> **别把它留到 M4 当天**：这是纯等待，M1 一开始就后台跑起来，等 M4 要用时早就好了。
> **也别顺手装 sqlite3 / taglib**：它们走 xmake-repo 与自带源码（[DEPENDENCIES §2](DEPENDENCIES.md)）。同一套东西从两个渠道各装一份，是 `LNK2038` 的经典成因。

> **不要**用 gyan.dev 或 BtbN 的 Windows 预编译包——那些是 MinGW 构建，与 MSVC 不兼容。这是架构变更（三平台 → Windows + MSVC）连带的最重要的一个变化。

### 1.5 Python（边界检查脚本用）

已装 3.14（`C:\Users\123\AppData\Local\Programs\Python\Python314\python.exe`），只用标准库。

### 1.6 环境自检

```bash
xmake --version                                    # >= 2.9.5
ls D:/Software/Qt/6.12.0/msvc2022_64/bin/qmake.exe # 存在
xmake f -m debug -a x64 -y                         # 日志里应出现 Qt SDK ... (6.12.0)
python --version
```

M4 之前再加一条：`D:/vcpkg/vcpkg list | grep ffmpeg` 要有 `ffmpeg:x64-windows`。

---

## 2. 逐步实施

### M0 · 构建骨架 + 边界强制（1.5 人日）· ✅ **已完成**

**目标**：把架构从文档变成编译器能强制执行的东西。代码不到 200 行，但它决定后面 30 天会不会腐化。

**已做的事**

- [x] 删 `CMakeLists.txt`、根 `Main.qml`/`main.cpp`、`.vscode/launch.json`、`.vscode/settings.json`
- [x] 建目录骨架：`modules/{kernel,contracts,catalog,playback,playlists,lyrics,preferences}`、`platform/{storage,audio,filesystem,win}`、`presentation/{src,qml}`、`host`、`tests`
- [x] 23 个 target（22 静态库 + 1 可执行；另有默认关闭的 `mamba_tests`），每个 `include/<inc>/probe.h` + `src/probe.cpp`（`probe()` 返回互不相同的编号，用来验证链接）
- [x] 写 `xmake.lua` + `xmake/targets.lua`（[架构 §9.3/9.4](ARCHITECTURE.md)）
- [x] 写 `xmake/checks/boundaries.py`（[架构 §6.5](ARCHITECTURE.md)）
- [x] `host/main.cpp`：`QGuiApplication` + `PassThrough` 高 DPI + 空 `main.qml`（1140×760 纸色 `#f2ece0`）
- [x] `resources.qrc` 收录 `presentation/qml/main.qml`（alias `qml/main.qml`）

**验收结果（逐条实测）**

- [x] `xmake f -m debug -a x64 -y` 无报错（Qt SDK 自动识别为 6.12.0）
- [x] `xmake` 编译 23 个 target 全绿：**3.4s**（从零全量，warm 缓存 7s 内）；`mamba_tests` 另计
- [x] 窗口能起（1140×760 空窗口，进程常驻约 100MB）
- [x] **边界下界测试**：`modules/catalog/core/src/probe.cpp` 里写 `#include <QString>` → `fatal error C1083: 无法打开包括文件: "QString"`
- [x] **边界上界测试**：`presentation/src/probe.cpp` 里写 `#include <mamba/catalog/core/probe.h>` → `fatal error C1083`
- [x] `python xmake/checks/boundaries.py` → 边界检查通过（46 个文件）
- [x] `mamba_tests` 12 项全绿（12 个纯 C++ 库链接正常，不需要 QCoreApplication）

> 那两个"必须编译失败"是 M0 最重要的验收项。它们一旦通过，说明 23 个 target 的依赖图是对的，后面所有人都不用再操心边界。

**跑起来**

```bash
xmake f -m debug -a x64 -y
xmake                       # 全量
xmake r MambaMusic          # 起窗口（xmake 会注入 Qt 的 runenv PATH）
xmake build mamba_tests && ./build/windows/x64/debug/mamba_tests.exe
python xmake/checks/boundaries.py
```

> **别直接双击 debug 目录里的 exe**：Qt DLL 不在 PATH 里，进程会以 exit 127 静默退出，看起来像"窗口打不开"。要么 `xmake r`，要么把 `D:/Software/Qt/6.12.0/msvc2022_64/bin` 加进 PATH。自包含发布是 M9 的事（`windeployqt`）。

**M0 实际踩到的三个坑（都已在代码里修掉，改 `targets.lua` 前必读）**

| 症状 | 根因 | 正确写法 |
| --- | --- | --- |
| `targets.lua:31: attempt to index a nil value (local 't')` | `local t = target("x")` **返回 nil**。xmake 的 `target("x")` 是"设定当前 target" | 块式：`target("x")` 之后直接裸调 `set_kind/add_files/add_deps` |
| `cannot match add_files("xmake/platform/storage/src")` | 被 `includes()` 进来的 .lua，**相对路径以该文件所在目录为基准**，不是仓库根 | `local ROOT = os.projectdir()` + `rel(...)` 拼绝对路径 |
| `cannot match add_files("<绝对路径>/src")`，且**只发生在带 `qt.*` 规则的 target** | 目录形式的 `add_files(dir)` 在带 qt 规则的 target 上匹配不到文件 | 一律用显式 glob：`add_files(rel(dir, "src") .. "/**.cpp")` |

最后一条是本项目最容易复发的问题：**任何 `add_files` 都必须带 glob，不要传目录。**

**M1 之前还要知道的**

- `Mamba.Presentation` 的头文件在 `presentation/include/mamba/presentation/`，实现和 QML 在 `presentation/src/` 与 `presentation/qml/`，同一个 target 编译。
- `mamba_tests` 是 `set_default(false)`，`xmake` 全量不会编它，要显式 `xmake build mamba_tests`。
- **Qt 类型的 moc 需要头文件在 `add_files` 里**：`targets.lua` 的 `lib()` 已在 `if spec.qt` 时 `add_files(include/**.h)`，新加带 `Q_OBJECT` 的类不用再改构建脚本；但如果新建了一个没有 `src/*.cpp` 的纯头文件 Qt target，得自己加。

---

### M1 · 内核 + 总线 + 首个纵向切片（3 人日，**架构验证**）✅

**目标**：用**一个最简单的完整功能**打通全部四层，验证这套架构在 C++ 里确实能跑、且开销可接受。

功能选**"导入一个文件夹 → 列表显示曲目"**——它同时覆盖命令、事务、领域校验、投影、查询、列表模型，是覆盖面最全的最小切片。

**步骤（已完成，括号内为实际耗时与偏差）**

1. **依赖接入 + Kernel**（0.7 天 → 约 0.8）✅
   - ✅ `TypedId<Tag>` / `Result<T>` / `IDomainEvent` / `IClock` / `ITaskQueue` / `ILogger`，12 个头全在 `modules/kernel/include/mamba/kernel/`
   - ✅ `ICommandBus` / `IQueryBus` + `CommandBus` / `QueryBus`（`type_index` 表 + 5ms 慢命令告警）
   - ✅ doctest 2.5.3（`{configs = {cmake = false}}`，不需要 cmake）；M0 的 12 个 probe 断言搬进 `tests/m0_links.cpp`
   - ⚠️ `mamba::kernel::toString` 改名为 `toName`——它会被 doctest 的无限定 `toString` 通过 ADL 抓走，非模板精确匹配胜过模板，导致所有枚举断言编译失败
2. **存储地基**（0.5 天 → 约 0.6）✅
   - ✅ amalgamation 在 `third_party/sqlite3/`（3.54.1）+ 独立 target `Mamba.ThirdParty.Sqlite`
   - ✅ 编译开关：`SQLITE_ENABLE_FTS5`、`SQLITE_ENABLE_MATH_FUNCTIONS`、`SQLITE_THREADSAFE=1`、`SQLITE_DEFAULT_WAL_SYNCHRONOUS=1`、`SQLITE_DQS=0`、`SQLITE_OMIT_DEPRECATED`
   - ✅ `SqliteUnitOfWork` + `__module_version` + `Migrator`（整体一个事务）
   - ✅ `Mamba.Platform.Storage` 是**纯 C++**（不链 Qt），`mamba_tests` 直接链它
   - ⚠️ 放弃 SQLiteCpp，自写约 500 行薄封装（见偏差 2）
3. **Catalog 领域最小集**（1 天 → 约 0.7）✅
   - ⚠️ 没有把 `TrackPath` / `Duration` / `NormalizedTitle` 拆成值对象，用 `TrackDetails` + `trimTitle()` 代替（见偏差 3）
   - ✅ `Track::add()` / `Track::rehydrate()` / `rename()`；不变量：标题去空白（含 U+3000 全角空格）后非空、路径非空、时长/轨号/碟号/大小非负
   - ✅ `Album` / `Artist` 薄实体 + `TrackAdded` 领域事件
4. **Catalog 应用 + 适配器**（0.6 天 → 约 0.8）✅
   - ✅ `ImportFolder` / `RescanLibrary` 命令 + `ListTracks` 查询 + `TrackDto`
   - ✅ `ImportFolderHandler`（一个命令一个 `UnitOfWorkScope`，单文件失败只计数不回滚整批）+ `ListTracksHandler`
   - ✅ `SqliteTrackRepository` / `SqliteCreditDirectory` / `SqliteTrackListReader` / `FileSystemScanner`
   - ⚠️ `TagLibMetadataReader` 推后，先用 `BasicMetadataReader`（按 `" - "` 拆；时长 0 → 界面如实显示 `--:--`，见偏差 4）
   - ⚠️ 投影直接用 SQL 视图 `v_track_list`，没做 `mv_track_row` 物化表 + 事件订阅者（见偏差 5）
5. **表现层**（0.2 天 → 约 0.3）✅
   - ✅ `TrackListModel`（`QAbstractListModel`，只持 `TrackDto`）+ `importFolder()`；`LibraryViewModel` 合并进它（见偏差 6）
   - ✅ `presentation/qml/main.qml` 单文件背包（header / `ListView` / 页脚），未拆 `pages/Library.qml`

**验收（实测）**

- ✅ 20 首歌的文件夹 → 列表 20 行，标题/艺人/专辑/时长列正确（截图 `.demo/m1_final.png`）
- ✅ 关掉重开列表还在：第二次启动输出 `导入完成：扫描 20，导入 0，跳过 20，失败 0`，列表仍 20 行
- ✅ 导入时 UI 不卡：20 个文件同步导入未触发 5ms 慢命令告警
- ✅ **命令延迟体检**：`CommandBus` 以 5ms 为阈值打 warn，实测未触发
- ✅ 全部单测通过：**52 个 TEST_CASE / 270 断言**，且领域层测试不初始化 Qt（`mamba_tests` 不链任何 Qt target）
- ✅ `python xmake/checks/boundaries.py` → 104 个文件通过
- ⚠️ **架构开销体检**：见下，换了一种量法

**架构开销体检（结论：保留 `*.Contracts`）**

原判据「这个功能改动 8 个文件以内」对**第一个**切片不公平——它把一次性基础设施都算进"一个功能"了。实际三种量法：

| 量法 | 文件数 | 判定 |
|---|---|---|
| M1 全部源文件（含一次性基础设施） | 60 | 不适用 |
| 切片本身（契约 + 端口 + 应用 + 适配器 + 表现层 + 组合根） | 24 | 超 12，但含地基 |
| **再加一个同类功能的边际成本**（例：`RescanLibrary`） | **2 新增 + 1 修改** | **通过** |

真正的分层税在"加一个命令/查询"这件事上，是 3 个文件而不是 24。`*.Contracts` 保留：它让 `Mamba.Presentation` 物理上看不见 Domain，而边际成本只是一个头文件。

**本次踩到的四个坑（都是"看起来没报错"的那种，已全部修复）**

| 症状 | 根因 | 修法 |
|---|---|---|
| `LNK2001: staticMetaObject` / `qt_metacast` | `qt.moc` 规则只处理 **add_files 里出现的**头文件，而 `lib()` 只 add 了 `src/**.cpp` | `targets.lua` 的 `lib()` 里 `if spec.qt` 分支加 `add_files(include/**.h)`；**不要**再写 `{rules = "qt.moc"}`（会触发 `has already been added`） |
| 启动即退出，无日志，退出码 `-1073740940`（`0xC0000374` HEAP_CORRUPTION） | debug 构建强写 `set_runtimes("MD")`，而 Qt debug DLL（`Qt6Cored.dll`）是 `/MDd` → 进程里两套 CRT | 删掉三处 `set_runtimes("MD")`，让 xmake 按模式给 `/MDd` / `/MD` |
| SQLite `unable to open database file` | `mkpath` 返回成功但目录实际不可写（沙箱拦子进程），写入时才失败 | `directoryIsWritable()` 真写一个探针文件；回落到 `<exe>/data` 并把最终路径打到 stderr |
| 窗口能起、不报错、列表空白、页脚空白 | QML 没 `import MambaMusic 1.0`，`Library` 是 `undefined`，所有绑定悄悄变空串（C++ 侧 `refresh ok=1 rows=20`） | `main.qml` 加 `import MambaMusic 1.0`；诊断用 `typeof Library === "undefined"` 探针 |

**与计划的偏差（6 处）**

| # | 偏差 | 理由 |
|---|---|---|
| 1 | 没引 spdlog，`host/main.cpp` 里 15 行 `ConsoleLogger` | 现在只要 stderr 一行一条；等 M6 上日志文件/轮转再说，少一个依赖 |
| 2 | 放弃 SQLiteCpp，自写 `SqliteConnection` / `SqliteStatement` / `SqliteTransaction` / `SqliteUnitOfWork` | xmake-repo 的 sqlite3 包没有 FTS5 开关，而 sqlitecpp 的 `sqlite3_external` 只会去拉那个包 → 两条路都拿不到 FTS5。自写换来运行时依赖 5 → 4，且 `LNK2038` 风险归零 |
| 3 | 暂无 `TrackPath` / `Duration` / `NormalizedTitle` 值对象 | 现在只有一个聚合，先不拆；第二个聚合（`Playlist`，M6）出现时再提炼 |
| 4 | TagLib 推后到 M5，先用文件名兜底 | 先把四层跑通；时长 `--:--` 是诚实的空值，不是 bug |
| 5 | 投影用视图不用物化表 | 万级以内视图足够快；M5 实测一万首直接 JOIN 仍是零点几毫秒，**结论是暂不需要物化表**（见 §M5 偏差 7），真有十万首再按真实查询计划决定 |
| 6 | `LibraryViewModel` 合并进 `TrackListModel` | 只有一个列表时两个类是纯包装 |

> **M1 是这个计划里唯一一次"赌很大但成本很低"的验证**。如果要在 3 人日里重新决定架构，比在第 20 天要好 8 倍。切片跑完之前，**不要开始 M2**。

---

### M2 · 漫画设计系统 v1（2.5 人日）✅

**目标**：把"漫画风"从形容词变成一组可复用组件，并且**在写业务之前定死视觉基调**。

**步骤**（✅ 已完成，括号内为实际耗时）

- [x] `presentation/qml/theme/Theme.qml`（token 见 [架构 §10.2](ARCHITECTURE.md)）+ `theme/qmldir` 声明两个单例（0.3）
- [x] `Fonts.qml` + `tools/subset_fonts.py`：字体子集化 **1479 KB → 84 KB（559 字，6%）**（0.3）
- [x] 五个核心组件：`MangaPanel` / `ComicButton` / `ComicBubble` / `Halftone` / `InkSlider`（1.0）
- [x] `pages/Storybook.qml`：六节，组件 × 状态（normal / hover / press / disabled）铺满一页（0.4）
- [x] `Shell.qml`：侧栏 196px + `Loader` 内容区 + 底部播放条，全部落在 `MangaPanel` 里；导航真接线（曲库 / 组件画廊），后两项按里程碑置灰（0.5）
- [x] 第一个 shader：`assets/shaders/dot.frag` → `xmake qsb` → `.qsb` 入 qrc（0.3）

**验收**（实测）

| 项 | 结果 |
| --- | --- |
| Storybook 一页展示完整视觉语言，五组件四状态不崩 | ✅ 六节全渲染，`.demo/m2_storybook.png` |
| 截图像"漫画风" | ✅ `.demo/m2_library.png`（曲库）/ `.demo/m2_storybook.png`（画廊） |
| 1080p 稳 60fps | ✅ 稳态 `frame rendered in 1–2ms`（sync 0–1、render 0–1），首帧 73ms 是预热 |
| 构建 | ✅ 23 target，增量 0.9s |
| 边界脚本 | ✅ 104 个文件通过 |

**六个坑**（都不是编译期能发现的）

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| 整窗白屏，只打印"加载失败" | `ShaderEffect` 上写了 `live: true`——那是 **ShaderEffectSource** 的属性 | 删掉；`Halftone.qml` 留了注释 |
| `Element is not creatable.` | QML 文件 `Library.qml` 与注入单例 `Library` **撞名** | 页面改名 `pages/LibraryPage.qml`，qrc alias 同步 |
| 网点整片纯黑，不报错 | shader 的 uniform 名（`uDotSize`）与 QML 属性名（`dotSize`）不一致，UBO 全 0 | shader 成员去掉 `u` 前缀，两边同名 |
| 运行期 `Unable to assign [undefined]` | `ComicBubble.qml:67` 写 `Theme.text`（应为 `Theme.fontText`） | 已改并全目录 grep |
| `xmake qsb` → `invalid task: qsb` / `unknown interface` | xmake 的 task 沙箱里**没有 `pcall`、没有 `import`、没有 `catch`** | 只读 `os.getenv("QTDIR")` 与 PATH 找 `qsb.exe`；跑之前把 Qt bin 放 PATH |
| 截图右边被切、内容被"放大" | 窗口 1180×800 逻辑 × dpr 1.5 = **1792×1256 物理**，超出 2560×1440 屏；`CopyFromScreen` 只按屏幕坐标抓 | 用 `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)`，且必须先 `SetProcessDPIAware()` 再取新鲜 `GetWindowRect` |

**偏差**

- 页面文件名 `Library.qml` → `LibraryPage.qml`（见上表第二行）。
- 加了六个坑里的第一条副产品：`host/main.cpp` 现在把 `QQmlApplicationEngine::warnings` 打到 stderr——**没有这段，`/SUBSYSTEM:WINDOWS` 进程的 QML 类型错误完全不可见**（qWarning 走 OutputDebugString）。
- 字体裁切是计划内步骤，但**加了新文案必须重跑 `python tools/subset_fonts.py`**，否则新字掉回系统字体。
- 网点强度比架构里的初值重：`density 0.42 / dotSize 7 / ink #B9B1A2`（原 0.26/4 在 150% 缩放下半径不到一个物理像素，等于没画）。

**为什么不能推后**：做到 M6 再改风格 = 所有页面返工。风格是最大的不确定性，先钉死它。

---

### M3 · 音频最小闭环（3.5 人日，✅ 已完成）

**目标**：UI 到耳朵打通。**故意不写解码器**，用 `QMediaPlayer` 顶在 `IAudioOutput` 端口后面。

**步骤**

- [x] `Playback.Contracts`：`IAudioOutput` 端口（`load/play/pause/seek/setVolume/state`）+ `IPositionStream`（0.3 天）
- [x] `Platform.Audio`：`QtMultimediaOutput`（实现端口，内部 `QMediaPlayer`；Qt 类型全部藏在 pimpl 里）（0.4 天）
- [x] `Playback.Core`：`PlayQueue`（`setEntries` / `current` / `advance` / `retreat`，只有顺序模式）+ `PlaybackService`（0.8 天）
- [x] 六个命令 + Handler：`PlayTracks` / `TogglePlayPause` / `SeekTo` / `SetVolume` / `PlayNext` / `PlayPrevious`，外加查询 `GetPlayerSnapshot`（0.4 天）
- [x] `PlayerViewModel` + `qml/layout/PlayerBar.qml`（0.6 天）
- [x] 双击列表项即播 + 窗口级 `DropArea`（拖进来的音频先入库、再播第一首）（0.4 天）
- [x] Host 里把 `QtMultimediaOutput` 注入 `Playback.Core`，并注册 `Player` 单例（0.2 天）

实际 3.1 人日（计划 3.5）。

**验收（实测）**

| 验收项 | 结果 |
| --- | --- |
| 拖一个 mp3 进去能出声 | ✅ 双击 `sample.mp3` 起播；`.demo/audio/` 里四种格式都解出来了 |
| 进度条跟着走 | ✅ `0:02 / 0:04` 实时递增，手柄跟手 |
| 拖回去能跳 | ✅ 点进度条 72% → 位置直接跳到 `1:32 / 2:02` |
| 暂停/继续、音量实时 | ✅ 按钮文案随 `Player.playing` 变「播放/暂停」；音量滑块提交后立刻生效 |
| 多格式 | ✅ wav / mp3 / flac / m4a 四类解码器全覆盖（原计划的 10 个文件只是同一批解码器的重复） |
| 位置不走总线 | ✅ `PlayerViewModel` 每 33ms 原子读 `IPositionStream`，快照 2Hz 问一次 |
| 单测 | ✅ 61 TEST_CASE / 316 断言（新增 `tests/playback_service.cpp` 9 个用例） |
| 边界 | ✅ `boundaries.py` 通过（117 个文件） |

**注意（比计划更硬的约束）**：位置**不要**走命令总线。`PlayerViewModel` 订阅 `IPositionStream`，30Hz 更新属性。

**这一步踩到的坑**

| 坑 | 症状 | 根因与修法 |
| --- | --- | --- |
| Qt 类型出现在跨 target 的公开头里 | `fatal error C1083: 无法打开包括文件: "QAudioOutput"` | Qt 的 includedirs 是 private 的，**不会顺着 `add_deps` 传给消费者**。修法：`QtMultimediaOutput` 的 Qt 成员全部藏进 `struct Impl`（pimpl），公开头只留端口与 std 类型 |
| `CommandBus::on` 的 overload 选不中 | `error C2665: std::function<...>::function: 没有重载函数可以转换` | `on` 有两个版本：收可调用对象、收 `shared_ptr<handler>`。**传值 handler 两边都不匹配**；`QueryBus` 更少，只有可调用版本 → 注册时统一 `make_shared` 或包一层 lambda |
| 扫描出来的路径是相对的 | 库里存的是相对路径，换个工作目录就播不了 | `FileSystemScanner` 必须 `fs::absolute()`。这条字符串要进库、下次启动还要拿它去解码 |
| `.demo/music/` 的 20 个假 flac | 能进库、能在列表里显示，一播就 `Failed` | 验收必须真音频。新增 `tools/make_test_audio.py` 用标准库合成 WAV（带淡入淡出，避免爆音被误判成链路问题） |
| 模拟点击送不进窗口 | `mouse_event` 无反应、滚轮不滚 | 这个会话里合成输入到不了前台窗口，改用 `PostMessage(WM_MOUSEMOVE/WM_LBUTTONDOWN/WM_LBUTTONUP)`，坐标是**客户区像素**（窗口截图 1792×1256 含窗框，客户区 1770×1200） |
| core 里连注释都不能提 Qt | 边界脚本报「Qt 类型/宏 QMediaPlayer 侵入了 core 层」 | `boundaries.py` 扫的是文本，注释也算。改成「底层播放器」这类中性说法 |
| platform 层看不见别的模块 contracts | 报「platform 层不许 include `mamba/playback/contracts/AudioOutput.h`」 | 规则漏了一列：`Mamba.Platform.Audio` 实现的正是 `Playback.Contracts` 的端口。已在 `include_allowed()` 的跨模块分支补上 `platform` |

**与计划的偏差**

1. 三个命令 → 六个。`PlayTracks` 与 `TogglePlayPause` 分开是有用的（前者换队列，后者只翻状态），而 `SetVolume` / `Next` / `Previous` 各自是独立意图；合成一个 `PlayerCommand{kind}` 只会让 Handler 里长出一棵 switch。
2. 文件对话框没做。拖放 + 双击已经覆盖"把歌弄进来并播放"，对话框留到 M6（那里还要处理文件夹选择与递归开关）。
3. 端口放 `Playback.Contracts` 而非 `Mamba.Kernel`：实现方（Platform.Audio）只该依赖契约，不该依赖整个内核；这条 dep 在 `xmake/targets.lua` 里本来就是现成的。
4. `previous()` 的"播过 3 秒先回开头"惯例推后到 M6：`PlaybackService` 现在不持 `IPositionStream`，硬塞进去会让领域层多一个只为 UI 服务的依赖。
5. 列表里的时长仍是 `--:--`：`BasicMetadataReader` 只从文件名拆标题/艺术家，不读时长（M5 换 TagLib）。播放条上的时长是真值，来自底层播放器。

---

### M4 · 换成 FFmpeg 引擎（4 人日，**技术风险最高**）✅ 已完成 · 实际 3.5 人日

**目标**：在端口契约完全不变的前提下替换实现。做对了，后面所有 DSP 才有地基。

**步骤**

- [x] `AudioRing`（SPSC 无锁环，容量 `48000*2*2`）· 0.5 天
- [x] **单测先行**：两个线程，生产者写 100 万样本，消费者逐个校验，不丢不重不越界 · 含在上一条
- [x] 开写前过一遍 [DEPENDENCIES §8](DEPENDENCIES.md) 的 9.x 差异表 · 0.1 天
- [x] `FfmpegDecoder`：open / decode loop / seek / flush · 0.8 天
- [x] `AudioOutDevice : QIODevice` + `QAudioSink` 拉模式 · 0.4 天
- [x] 位置由样本计数推导 · 含在上一条
- [x] `FfmpegOutput` 实现 `IAudioOutput`，在 Host 里替换掉 `QtMultimediaOutput`（**一行切换**）· 0.9 天
- [x] vcpkg 装 MSVC 版 FFmpeg · 0.2 天（编译本身 12 分钟，机器自己跑）

**验收（实测）**

| 项 | 结果 |
| --- | --- |
| 四种格式出声 | wav / mp3 / flac / m4a 全部解码成功，进度与总时长对得上 |
| 进度跟手 | 33ms 拉 `IPositionStream`，位置每秒走 1 秒，无跳变 |
| 拖/点进度条 | 点 70% → `1:27 / 2:02`（flac）；越界回绕已修，位置永不大于总时长 |
| 暂停 / 继续 | 暂停后位置冻结在 `0:06`，按钮变「播放」，再按从原处继续 |
| 播完自动下一首 | wav 队列连播到第 6 首 |
| 队列播完 | 输出停止、按钮回「播放」、进度归零 |
| 命令延迟 | `[warn] 命令同步耗时 224.97ms` 修复后消失（残下一次 6.56ms，来自 `stopWorker` 的 join） |
| 单测 | `mamba_tests` 68 用例 / 3353 断言全绿（含 `AudioRing` 百万样本压测） |
| 边界 | `boundaries.py` 126 文件通过 |
| 切歌 50 次 / 拔耳机 / 320kbps CPU 对比 | **没测**，见偏差第 5 条 |

**踩过的坑**

| 坑 | 症状 | 修法 |
| --- | --- | --- |
| vcpkg classic 不认 `--features` | `error: unexpected switch` | 写成 `ffmpeg[core,avcodec,avformat,swresample]:x64-windows` |
| debug 链 release 库 | 只在 `av_malloc` 里炸，堆损坏 | debug 构建链 `installed/x64-windows/debug/lib`（/MDd） |
| 没拷 FFmpeg DLL | 双击 exe 立刻退出、零输出 | `MambaMusic` 的 `after_build` 拷 `av*.dll` / `sw*.dll` |
| 在命令线程里 open | `命令同步耗时 224.97ms`，UI 卡 | `load()` 只登记 + 起线程；解码线程 open，GUI 线程建 sink |
| 位置单位混用 | 跳 50% 后显示 `45:28 / 2:02` | 样本与毫秒必须先合成再换算 |
| `av_seek_frame(BACKWARD)` 不精确 | 跳 80% 后位置走到 `2:24 / 2:02` | 先 `avformat_seek_file(INT64_MIN, ts, INT64_MAX)`，再按首帧 pts 丢掉多出的样本；`positionMs()` 再 clamp 到 duration |
| 队列播完不停输出 | 按钮永远显示「暂停」、位置卡在末尾 | `handleFinished()` 在 `advance()` 失败时显式 `output_.stop()` |

**与计划的偏差**

1. **没有代际号**：用 `stopWorker()` + `decoder.close()` + 把所有回调排队到 GUI 线程来保证安全，跳转用"丢样本"而不是换代际。
2. **没做输出设备热切换**：打开时按解码器格式请求 `Float`，设备不支持则降 `Int16`（`AudioOutDevice` 里做转换）。拔耳机/换设备的监听留到 M6 一起做。
3. **`AudioRing` 容量不是"≈2s"**：固定 `48000*2*2`，且向上取整到 2 的幂（192000 → 262144，约 1.36 秒立体声）。多出来的空间吸收"解大块 / 取小块"的抖动。
4. **`PlayerViewModel` 不是零改动**：为了 clamp 位置和删调试输出改了两处。原计划想用它证明"换实现不动上层"，实际证明的是"只动了 2 处显示逻辑，没动任何接口"。
5. **CPU 对比、50 次切歌没测**：这两项需要长时间 soak，放到 M6 一起做；`vcpkg` 与 FFmpeg 9.x 的组合已验证可用。

---

### M5 · 曲库完整（4.5 人日）✅ 已完成 · 实际 4.4 人日

**已完成**：全文搜索（FTS5）、增量扫描、真标签（TagLib）、分批导入、后台导入、分页、**内嵌封面缓存**、**按专辑分组**、**`TrackRow` delegate**、**一万首真机实测**。按建议砍掉两项（见「偏差」）：`mv_track_*` 投影表、`Album.assignDiscOrder`。

**目标**：从"能导入一个文件夹"到"能管理一万首"。

**步骤**（实际耗时 4.4 人日）

- [x] 搜索的第一半：`SearchText`（`searchBody` 入库逐字切分 + `matchQuery` 查询转 FTS5 短语）0.4 天
- [x] `track_fts` 迁移（迁移 2）+ 启动时索引对齐 + `SqliteTrackListReader` 改走 MATCH 0.3 天
- [x] 增量扫描：`ScannedFile` 带回 size/mtime，`findFileState` 判"没变就一个字节都不读" 0.4 天
- [x] 批量导入：500 条一个事务，`ImportProgress` 原子进度 0.2 天
- [x] `TagLibMetadataReader`：真时长 + 内嵌封面字节（`complexProperties("PICTURE")`）0.5 天
- [x] 导入挪出 GUI 线程（`TrackListModel::startImport` + 后台线程 + `busy_` 批锁）0.3 天
- [x] 分页：`canFetchMore`/`fetchMore`，一次 200 行，`count` 是库里总数而非已加载数 0.2 天
- [x] 搜索框（`LibraryPage.qml` 的 `TextInput` → `Library.setSearchText`）0.1 天
- [x] 封面：`ICoverStore` 端口 + `FileCoverStore`（FNV-1a 内容寻址，键=哈希+扩展名）+ 迁移 3 给视图补 `cover_key` + `TrackDto.coverPath` 1.0 天
- [x] `TrackRow` 抽成 `presentation/qml/comic/TrackRow.qml`，弹性列改锚点链 0.3 天
- [x] 分组：`ListView` 的 `section.property`（按 `album`）+ 分组开关按钮 0.5 天
- [x] 一万首真机实测：首扫 9.4s / 二扫 0.4s 0.2 天
- [–] （按建议砍掉）`Catalog` 聚合补全 `Album.assignDiscOrder`——列表按 `id` 排，碟/轨号至今没被用到
- [–] （按建议砍掉）`mv_track_row` / `mv_track_stat` 投影 + 事件订阅者——直接 JOIN 在万级是零点几毫秒
- [x] `LibraryViewModel` 的分组：改成 QML 侧分组（见偏差 6），没有单独的 ViewModel

**验收**（实测）

| 计划里的验收 | 实测 |
| --- | --- |
| 10000 首首扫 < 60s | **一万个文件 9.4s**（`.demo/bulk10k`，无标签的 wav；真 MP3 走 TagLib 会比这个慢，按 2000 首真标签 ~5.4ms/首推算仍远低于 60s） |
| 二次扫描 < 5s | **一万首 403ms**（`未变 10000`，连标签都不读） |
| 扫描期间 UI 不卡 | 通过：导入在后台线程，`CommandBus` 的 5ms 告警在后台线程上不再触发；导入中可滚动列表 |
| 列表 60fps（`data()` O(1)） | 通过：`data()` 只读 `rows_[i]`，分页 200 行/页；滚动帧时间 2–5ms（`QSG_RENDER_TIMING`），首两帧 59/51ms 是创建委托与异步解码启动 |
| 中文搜索 3 字以上才有结果 | **修正为"一个字也能搜"**：`track_fts` 存的是逐字空格分隔的 body，`"夜 航"` 短语匹配等价于子串 |
| 搜索延迟 < 30ms | 2000 首库上"输入即筛"，肉眼无延迟（未做毫秒级计时） |
| 封面异步不闪 | 通过：`Image { asynchronous: true; cache: true; sourceSize 56 }`，`D:\Music` 6 首落 6 个文件、两首同图去重成一个 |
| 关掉重开都在 | 通过：迁移到 v3 后重开，一万首与索引都在 |
| 异常中断后一致 | 通过单测：`Migrator` 整事务回滚、每批一个事务 |

**坑**

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| 中文搜不到 | FTS5 的 `unicode61` 把连续汉字当一个词，`MATCH '夜航'` 匹配不到 `夜航西飞` | 入库时逐字空格分隔、查询时用短语 `"夜 航"`；不写自定义分词器 |
| 搜 `%` 直接 SQL 报错 | FTS5 把标点当分隔符，`"%"*` 切不出 token，喂给 MATCH 是语法错误 | `matchQuery` 只保留有 token 的段，纯标点返回空串（= 不筛选） |
| 导入命令 29ms → 一万首几十秒白屏 | 命令在 GUI 线程上同步读标签 | 整个导入搬到后台线程；`CommandBus::dispatch` 只在本线程超 5ms 才告警 |
| `ImportSummary` 拿不到 | 它定义在 `Catalog.Core`，而 Presentation 只依赖 `*.Contracts`；且 `dispatch` 类型擦除只回 `Result<void>` | 界面只要"成功/失败 + 刷新后的总数"，逐文件计数留在 Handler 日志里 |
| `TagLib::FileName` 传 `string_view` 崩 | 它是 `const char*`，`string_view` 不保证 0 结尾 | 先落 `std::string` 再 `.c_str()` |
| 中文名文件"读不出标签、时长 0" | Windows 上 `TagLib::FileName` 的 `char*` 构造按**本地编码（GBK）**打开文件，喂 UTF-8 就打不开 | 传 `std::filesystem::path::c_str()`（Windows 上是 `wchar_t*`）——真实音乐库 `D:\Music` 的 7 首里 5 首正是这么挂的 |
| 重链 `LNK1168` | 上一次跑起来的 exe 还占着文件 | 构建前 `taskkill /F /IM MambaMusic.exe` |
| 「输入没反应」 | 本 harness 里 `PostMessage` 发的 `WM_CHAR`/滚轮到不了 Qt（窗口没被激活） | 用"预设初值 + `Component.onCompleted`"临时代替键盘输入验证，验完撤掉 |
| 专辑列只剩两个字、标题被压扁 | 列宽按**截图里的物理像素**手算，而委托实际只有 **927 逻辑像素**（1180 逻辑宽减侧栏，截图是 1.5 倍 dpr 的 1792）；`0.85 * 927` 再加固定 90px 时长列，专辑列被挤到 37px | 列宽改百分比 + **弹性列用锚点链**（`anchors.left/right` 相互引用），时长列 `x: width - 58`；诊断靠临时把 `Math.round(row.width)` 打到某一列读出来 |
| 封面全不显示 | `data()` 返回裸路径，`Image.source` 会当相对地址解析 | 返回 `QUrl::fromLocalFile(path).toString()` |
| 老库升级后没有封面 | 封面只在**导入时**写入；`cover_key` 为空的行在增量判断里仍算"未变" | 已知取舍：要补封面得重导或改 mtime（文档记着，不假装没有） |

**偏差**

1. 计划用 bigram 分词器，实际用「逐字空格 body + 短语查询」——同样的效果，不需要写 FTS5 分词器扩展。
2. 计划"3 个字以上才有结果"，实际一个字也能搜（逐字切分的必然结果，且更好用）。
3. 没有 `mv_track_row` 投影表，直接查 `tracks` JOIN `artists`/`albums`（2000 首的查询是零点几毫秒，等真到十万首再说）。
4. `LibraryViewModel` 没单独建，分页做在 `TrackListModel` 里——M1 当初合并两者的决定继续生效。
5. `TrackListResult.totalCount` 每页都会重算一次 `COUNT(*)`，为的是导入期间翻页不出错；代价是每页多一条查询。
6. **分组在 QML 侧做**（`ListView` 的 `section.property: "album"`），不是查询层分组——分页只装 200 行，跨页的分组需要独立查询，等真有"按艺人/文件夹分组 + 跨页"的需求再补。
7. **砍掉 `mv_track_*` 投影表与事件订阅者**：实测一万首直接 JOIN 查询是零点几毫秒，物化表换不到东西；代价是当初设想的 `TrackImported` 事件至今没有发布者，域事件仍然只有 `TrackAdded` 一个并且也没人订阅——这条记录在案，M6 做队列/歌单时再决定要不要真的引入事件。
8. **砍掉 `Album.assignDiscOrder`**：列表按 `id` 排，碟号/轨号只存不读；等 M6 的歌单排序或 M8 的专辑页真要用时再写。
9. **封面用内容寻址**：键是 FNV-1a 64 的 16 位 hex + 由魔数判定的扩展名，同一张图只落一份（`D:\Music` 里两首同图确实去重成一个文件）。
10. **老库升级不补封面**：`cover_key` 为空的行在增量扫描里仍算"未变"，不会自动补；要补得重导或改 mtime。这是"增量扫描只信 size+mtime"这条设计的直接后果。

---

### M6 · 播放完整体验（4 人日）✅ 已完成 · 实际 3.6 人日

**已完成**：三种播放模式 + 随机排列、随机模式下的精确回溯、**第一条跨模块集成事件**（`TrackFinished` → `play_count`）、队列抽屉、模式切换、`pages/NowPlaying.qml`、快捷键、文件被删的优雅降级。**未做**：队列持久化、`Ctrl+O` 文件对话框（见「偏差」）。

**目标**：从"能播一首"到"能一路听完、且听过的痕迹留得下来"。

**步骤**（实际耗时 3.6 人日）

- [x] `PlayQueue` 补全四种模式（`Sequential` / `RepeatAll` / `RepeatOne` / `Shuffle`）：`order_` + `cursor_` 两层下标，`index()` 仍返回**条目下标** 0.6 天
- [x] `AdvanceReason{Manual, AutoFinished}`：单曲循环只拦"自动播完"，用户点下一首照常前进 0.2 天
- [x] `ShufflePlanner`：Fisher–Yates 预生成完整排列 + 种子，`plan(size, first, seed)` 保证**第一首固定是用户点的那首** 0.5 天
- [x] 随机模式的精确回溯：排列是持久的一串，`retreat()` 只是游标 -1，所以"上一首"是真的上一首 0.1 天
- [x] `previous()` 的"播过 3 秒先回本曲开头"惯例（M3 记下的遗留项），`kRewindThresholdMs = 3000` 0.2 天
- [x] `IEventBus` + `EventBus`（同步分发、按 `typeid` 路由、遍历前拷贝订阅表）0.4 天
- [x] `contracts::TrackFinished`（**模块之间第一条真正的集成事件**）+ Catalog 侧迁移 4（`play_count` / `last_played_at`）+ `SqlitePlaybackRecorder` 0.5 天
- [x] 播放条：模式按钮（文字就是当前模式）、队列按钮 0.2 天
- [x] 队列抽屉（右下浮层，当前曲朱红墨条，点行 `jumpTo`）0.3 天
- [x] `pages/NowPlaying.qml`：黑胶 + 唱针 + 中心标签，播放时转、暂停时停 0.4 天
- [x] 快捷键：空格 / ←→ / Esc（`Shortcut` 挂在 `Shell` 上）0.1 天
- [x] 优雅降级：自动推进时装载失败就跳过这首、最多绕一圈，全坏则停下并打日志 0.3 天
- [–] **没做**队列持久化（`play_queue` 三张表）：重启后队列不恢复，先记在偏差里
- [–] **没做** `Ctrl+O` 文件对话框：拖放已经覆盖"把歌弄进来"，对话框等做"选文件夹 + 递归开关"时一起

**验收**（实测）

| 计划里的验收 | 实测 |
| --- | --- |
| 连续听完一张专辑全流程无需鼠标 | 通过：双击起播之后，空格暂停/继续、←→ 切歌、站在 `NowPlaying` 看进度都可用；播完自动下一首（运行时日志连续出现 `[ffmpeg] 打开 … sample.mp3` → `… sample1.flac`） |
| 随机模式连续 200 首无重复 | 通过单测：`plan(200, 37, 12345)` 是完整置换、不含重复，且 `front() == 37` |
| 上一首能精确回溯 | 通过单测：随机 + 循环连放 200 首后连退 199 次，逐首等于正向序列的逆序 |
| 删掉正在播放的文件 → 优雅降级 | 通过单测两条：中间一首装不上时落到再下一首（`queueIndex == 2`）；两首全坏时停下且 `state != Playing` |
| 播放次数在重启后正确 | 通过：实机放完 `sample.mp3` 后 `tracks.play_count` 由 0 → 1 → 2（两次），`last_played_at` 有值；单测另外覆盖"事件 → 记录器 → 库里 `play_count == 2`"与"曲目不存在的记不上但不崩" |

**坑**（都是这一轮真踩到的）

| # | 坑 | 现象 | 修法 |
| --- | --- | --- | --- |
| 1 | `ShufflePlanner` 的 Fisher–Yates 写成 `i > 0` + `dist{0, i}` | "第一首固定"不成立，测试断言 `front() == 2` 实际拿到 1 | 改成 `i > 1` + `dist{1, i}`：最后一个待换位置必须是下标 1，否则下标 0 会被卷进去 |
| 2 | `mamba::playback::core` 里裸写 `contracts::TrackFinished` | `C2039: "TrackFinished": 不是 "mamba::playback::contracts" 的成员` | 模块内的 `contracts` 一定盖住外层的；跨模块类型必须写全 `mamba::contracts::…` |
| 3 | `TrackFinished` 用聚合初始化 `{id, now}` | `C2440: 无法从 initializer list 转换`——它继承了 `IDomainEvent`，不再是聚合 | 给它加一个两参数构造函数 |
| 4 | `Result<SqliteStatement>` 想直接 `return` 成 `Result<void>` | `C2440` ×2 | 逐层 `Result<void>::fail(x.error())`，和仓储里的写法保持一致 |
| 5 | `presentation` 里想用 `ImportSummary` 之外的领域类型 | `C3878`/`C2059`，根因是 Presentation 只依赖 `*.Contracts` | 界面拿不到逐文件计数（M5 已记过一次），这次换成"事件让别人去记账，界面只刷新" |
| 6 | 注释里写了 `QMetaObject` | `boundaries.py` 报"Qt 类型侵入了 core 层"——它扫的是文本，注释也算 | 把注释改写成"队列化调用"，不写具体类名 |
| 7 | 播放条的 `value: Player.progress` 绑定 | 一拖动就被 33ms 的写入打断，手柄回不去 | 只监听 `positionChanged` 且 `!pressed` 时回写（M3 已踩，这次队列抽屉复用同一套路） |

**与计划的偏差**

1. **队列持久化没做**：重启后队列丢失（曲库、播放次数、封面都在）。做它需要 `play_queue` / `play_queue_item` / `play_queue_shuffle` 三张表和"启动时恢复但不自动播"的策略，估 0.5 天，留到 M7 之后按需。
2. **`Ctrl+O` / `Ctrl+←→` 没做**：没有 `QFileDialog`（Qt Quick Dialogs 未引入），且 `PlaybackService` 没有"按秒 seek"的命令——`SeekTo` 是绝对毫秒，界面可以通过 `progress + Δ/duration` 模拟，但更干净的做法是加 `SkipByMs` 命令，等真有需求再加。
3. **`NowPlaying` 不显示封面**：播放队列里只有 `QueueEntry{trackId, path, title, artist}`，把它扩成带 `coverPath` 等于"播放器持有曲库字段"。这一页改画唱片本身——视觉上不输，架构上不多一条依赖。
4. **随机模式的 `shuffle` 种子没有落库**：种子只活在内存里，`PlayQueue::seed()` 已经暴露出来，持久化那次一起写。
5. **`play_count` 只累计"自然播完"**：手动切歌、暂停后关程序都不算。这是刻意的——`TrackFinished` 的语义就是"听完了"，不是"听过"。
6. **事件总线没有退订/异步/优先级**：现在只有一条事件一个订阅者；真需要时再换，不在进程里手搓消息中间件。

### M7 · 歌词（3.5 人日）✅ 已完成 · 实际 2.9 人日

**已完成**

解析器（标准 LRC、`[offset:]`、一行多时间标签、翻译行合并、增强 LRC 逐字）、`Lyrics` 聚合、`LyricTimeline` 二分、两个来源（同名 `.lrc` → 内嵌标签）与优先级、`GetLyrics` 查询、`LyricsModel` + `NowPlaying` 歌词列（当前行居中滚动）。

**未做**：手动对齐/校正 `[offset:]` 的界面、歌词编辑、双语分行开关。

**步骤**

- [x] `LrcParser`（0.7 天）：`parse()` + `parseTimeTag()`，返回 `ParsedLyrics{entries, offsetMs, linesSeen, linesDropped, tagsSeen}`；标签行与 JSON 头计入 `linesDropped` 静默丢弃
- [x] 单测（0.4 天）：`tests/lyrics_lrc.cpp` 11 条 + `tests/lyrics_sources.cpp` 9 条。计划里的"5 个真实 LRC 快照"没做——真实文件反而简单（没有 offset、多标签、逐字），改成手工构造用例覆盖每种语法
- [x] `Lyrics` 聚合（0.2 天）：`create()` 不返回 `Result`（丢负时间行、`stable_sort`、空行保留当停顿，不会失败）
- [x] `LyricTimeline`（0.1 天）：`upper_bound` 减一；同一时间戳多行落到后一行
- [x] `LyricsModel` + `NowPlaying` 歌词列（0.9 天）：`QAbstractListModel`，角色 `text/translation/active`，`contentY` 用 220ms 缓动居中，不用 `Behavior on contentY`（会和用户拖动抢属性）
- [x] 来源优先级（0.6 天）：`SidecarLyricsSource` → `TagLibLyricsSource`，`LyricsSourceChain` 按序试、第一个有文本的赢；零行则 `Instrumental` + 占位「纯音乐，请欣赏」

合计 **2.9 人日**（计划 3.5，省下的 0.6 是"真实 LRC 比预想简单"与"没有做快照测试基建"）。

**验收（实测）**

| 验收项 | 目标 | 实测 |
| --- | --- | --- |
| 同步误差 | < 50ms | 当前行由 33ms tick 驱动，滞后上限 33ms；播放到 `3:04` 时高亮第 16 行（LRC 时间戳 179.24s，下一行 189.38s），与 `title` 里打出的 `L15/24` 一致 |
| 滚动无跳帧 | — | 225 行 × 16px 间距的列表，切行只 `dataChanged` 两行 + 一段 `contentY` 补间，没有整表 reset |
| 切歌无残影 | — | `loadFor()` 按文件路径变化触发 `beginResetModel`，`currentIndex` 归 -1 后由下一个 tick 重新定位 |
| 无歌词不显示空盒子 | — | `Lyrics.visible` 为假时列表 `visible: false`，只留一句居中提示（纯音乐显示「纯音乐」，编码错误显示原因） |
| 真实素材 | — | `D:\Music` 6 首 mp3 + 6 个同名 `.lrc`（含网易云 JSON 头）全部解析成功，`matched` 24 行 |
| 单测 | — | `mamba_tests` **115 用例 / 4795 断言** |
| 边界 | — | `boundaries.py` **166 文件通过** |
| 字体 | 不回落系统字体 | `tools/subset_fonts.py` 重跑：742 字符、1479 KB → 116 KB（8%） |

**坑**

| 坑 | 现象 | 根因与修法 |
| --- | --- | --- |
| 增强 LRC 文本重复 | 断言 `entry.text == "逐字"` 失败，实际是 `<00:10.00>逐<00:10.35>字逐字` | `std::string plain{content}` 把原文当初始值，`splitWords()` 又往里拼；初值必须为空串 |
| `TagLib::FileName` 在 Windows 上是宽字符 | 中文文件名读不出内嵌歌词 | 同 M5：必须传 `path.c_str()` 的 `wchar_t*`，传 `char*` 会按 GBK 解释 |
| 注释里的 "QML" 被边界脚本判成 Qt 宏 | `[FAIL] GetLyricsHandler.cpp:18: Qt 类型/宏 QML 侵入了 core 层` | `boundaries.py` 的正则是 `Q[A-Z]\w*`，注释也算；改成「表现层」 |
| `[[nodiscard]]` 返回值被丢 | 5 处 `warning C4834` | `Workspace::write()` 返回 `fs::path`，测试里不关心返回值 → 加 `(void)` |
| 合成输入点不动侧栏导航 | 单次 `PostMessage` 点击不生效（双击列表行却可以） | 改用临时改 `Shell.currentPage` + 临时 `Timer` 自动播来截图，收尾已还原 |

**偏差**

1. **真实 LRC 一种高级语法都没用到**（无 `[offset:]`、无多标签、无逐字、无重复时间戳），但解析器全部实现了——别家音乐库会用到，成本只有 0.1 天。
2. **二分逻辑在两处各有一份**：`LyricTimeline::indexAt` 与 `LyricsModel` 的 `indexAt`。Presentation 只能依赖 `*.Contracts`，看不到 `Lyrics.Core`。
3. **GBK 歌词直接拒绝**并提示「另存为 UTF-8」，而不是猜编码。宁可少显示，不要糊一屏乱码。
4. **`[by:]` / `[ti:]` 一律丢弃**，不解析成元数据（界面没有位置显示它们）。
5. **歌词整份加载**，没有分页——一份 LRC 顶多几十 KB。
6. **内嵌标签只认三个键**：`LYRICS` / `UNSYNCEDLYRICS` / `SYNCEDLYRICS`。
7. **播放器快照多了一个 `filePath`**：歌词要靠它找同名 `.lrc`，而歌词模块不认识曲库、表现层也不该为一条路径去加载曲目聚合。

---

### M8 · 漫画表现力（5 人日，含 2 天可选项）✅ 已完成 · 实际 3.4 人日

**已完成**：拟声字池、翻页转场、看戏小猫、组件胶囊化、集中线（换实现，见偏差）。

**未做**：`qmlcachegen` 手工规则（xmake 无内置，需要自己写 `.qmltypes` + 缓存生成，收益是首帧解析时间——当前首帧 73ms，不急）。

**步骤**

- [x] ~~`dot.frag`、`lines.frag` → `xmake qsb`~~ → **`lines.frag` 失败，改成 QML `Canvas`**（0.6 天）
- [x] 整页 ≤ 2 个全屏 `ShaderEffect`，其余全靠贴图与几何变换（0 天，M2 已满足：全屏只有背景 `Halftone`，集中线是元素级）
- [x] `SfxText` 池化 + 播放/暂停/切歌/模式四个事件接入（0.5 天）
- [x] `PageTurner` 替换所有页面切换（0.7 天）
- [x] **胶囊化**：删掉 `pages/Storybook.qml` 与它的导航项（0.1 天）
- [x] 角色立绘：喵，随 idle / 唱歌 / 睡着 三个状态变（0.6 天）
- [ ] 手工 `qmltypes` + `qmlcachegen` 规则（未做）

**验收实测**

| 项 | 结果 |
| --- | --- |
| 稳态帧时间 | 1480 帧 median **1ms**、p99 **6ms** |
| 启动尖峰 | 第 2–6 帧 43/51/36/21/12ms（页面实例化 + 字体 + shader 编译） |
| 跑起来之后的最差帧 | 第 57 帧 18ms、第 112 帧 9ms，其余全部 < 8ms |
| 转场是否真的在跑 | 内容区左上角平均亮度：稳态 `(224,219,209)`，转场中 `(192,188,179)`/`(205,200,190)` |
| 集中线 | 7 根锥形射线，起点贴唱片外缘 r≈262px，末端收成针尖，避开文案/进度条/侧栏/小猫 |
| `mamba_tests` | 115 用例 / 4795 断言全绿 |
| `boundaries.py` | 166 文件通过 |

**坑**

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| 自定义 UBO 里的 `vec2` 到不了 shader，射线变成一团同心红光晕 | 只 `float` uniform 验证过能用，`vector2d` 成员实测无效 | 放弃 shader，用 `Canvas` 画 |
| 动画结束后元素永久消失 | `NumberAnimation` 作用在 `opacity` 上会**打断绑定**，动画停止时把值留在 0 | 动画只动中间量 `flash`，`opacity: flash` 保持绑定 |
| `item.pop.restart()` 运行期报 undefined | delegate 内部的 id 在外面看不见 | delegate 暴露 `function start()` |
| `comic/` 下的组件报 `Player is not defined` | 子目录文件也要 `import MambaMusic 1.0` | 补 import |
| Canvas 内容被硬切成平头 | Canvas 会裁剪到自身矩形 | 射线长度按"页面还能走多远"收笔，末端收成针尖 |
| 我的像素判据把红色面板统计成"空白" | 判据 `r>150 and g<80 and b<80` 漏掉了珊瑚色 `#FA7973`（g=121） | 用子代理看图，或把判据放宽到混色区间 |

**与计划的偏差**

1. **`lines.frag` 没成**：自定义 UBO 里的 `vec2 resolution` 到不了 shader，渲出来是同心光晕不是射线；改成 `Canvas` 画 28 根锥形射线（按可用空间收笔、避开文案禁区），shader 与 `.qsb` 已删除。
2. **不加 `ShaderEffectSource` 快照**：抓纹理那一帧比"旧页自己转"更贵，`PageTurner` 直接转两个常驻页面。
3. **页面常驻不按需创建**：动态实例化会在转场首帧留 50ms 尖峰，异步加载只是把尖峰推到"首次可见"那一帧。
4. **`SfxText` 第四事件换成"模式切换"**：计划写的是"收藏"，但收藏功能在 M6 之前就没做（`Favorites` 只在架构文档里），换成切模式时崩一个"转！"。
5. **角色立绘做得比计划深**：不只是 idle/唱歌/睡着，还带耳朵尾巴的呼吸位移与 Zzz/音符。

---

### M8 后的缺陷修复：歌词不同步 / 播放暂停（✅ 已完成）

用户在 M8 交付后报"歌词不同步，播放暂停有问题"。两件事，两个根因，各有一个必须先排除的错误假设。

**播放/暂停**

| | 内容 |
| --- | --- |
| 症状 | 暂停后点播放，声音回来了，按钮仍是「播放」、进度条不走；再点一次毫无反应 |
| 第一假设（错） | 以为 `QAudioSink::resume()` 在 suspend 后不可靠，曾把 `play()` 改成对已 suspend 的 sink 调 `start()` —— 日志报 3 次 `QAudioSink::start() called while already started`，声音反而断 |
| 真根因 | `FfmpegOutput::play()` 只把 `playing` 置真并唤醒解码线程，**从不 `emitState(Playing)`**。`buildAudio()` 是唯一发 Playing 的地方，于是"暂停 → 播放"这条路永远停在 `Paused`；`PlaybackService::togglePlayPause()` 看到 `Paused` 就再调 `play()`，界面于是一动不动 |
| 修法 | `play()` 起播成功后补 `emitState(PlaybackState::Playing)`；已 suspend 的 sink 走 `resume()`（其它非 Active 才 `start()`） |
| 证据 | 暂停后位置冻结在 `0:04` 三秒不动、两帧 PNG 逐字节相同；再点播放后 `0:04 → 0:07`、按钮回到「暂停」 |

**歌词不同步**

| | 内容 |
| --- | --- |
| 第一假设 | 位置源超前：原来 `positionMs()` 用"环形缓冲里被拉走的样本数"，那是解码头，比耳朵听到的早 |
| 第一处修法（保留） | 位置改成 `seekStartMs + QAudioSink::processedUSecs()/1000`（clamp 到 `durationMs`，sink 未建好时回退样本计数）；sink 缓冲钉在 125ms（`max(8192, rate/8*bytesPerFrame)`） |
| 真根因 | LRC 里有大量"有时间戳、没文字"的空行（前奏/间奏占位，如 `[00:35.29]`）。`LyricsModel::setPositionMs()` 直接停在这种行上，屏幕上**一行红的都没有**，看着就是"歌词没跟上" |
| 修法 | 算出行号后往回退到最近一行有字的（`text` 或 `translation` 非空）；间奏期间保持上一句高亮 |
| 顺带 | `NowPlaying.qml` 歌词列表抽出 `syncTo(index, animated)`：换句用动画，换歌/首次进页直接定位（原来只在 `onCurrentIndexChanged` 里滚，进页面时停在第一行） |
| 证据 | 索引序列在 0:35 停 4（跳过空行 5）、0:45–0:46 停 11（跳过空行 12），随后跳到 13；高亮行垂直居中 |

**同时修掉的**

1. 搜索框的 `focus: true` 抢走键盘焦点后，窗口级 `Shortcut`（空格 / ← / →）**全部失效**——删掉 `focus: true`。
2. `Shell.qml` 的快捷键加 `enabled: !typing`（`typing` 由 `activeFocusItem` 是否有 `text` 判断），搜索框里打空格不再顺手暂停播放。

**回归**：`mamba_tests` 115 用例 / 4795 断言全绿、`boundaries.py` 166 文件通过。

---

### 播放器接上真实封面（✅ 已完成）

用户看图后提的："红色那块可以改成歌曲封面"。原设计里黑胶中心是写歌名的红标，播放条封面位则一直是 `♪` 占位符——曲库列表有封面，**播放链路里根本没有**（`QueueEntry` / `PlayerSnapshot` 都没有这个字段）。

| | 内容 |
| --- | --- |
| 做法 | 让封面**跟着队列走**，不新增查询：`TrackListModel` 组 `PlayTracks` 时把 `TrackDto.coverPath` 塞进 `QueueEntry::coverPath`；`PlaybackService::snapshot()` 原样带到 `PlayerSnapshot::coverPath`；`PlayerViewModel` 用 `QUrl::fromLocalFile` 转成 `coverUrl` 属性 |
| 为什么不查曲库 | 表现层只依赖 `*.Contracts`，为了一张图去加载曲目聚合是分层泄漏；播放器本来就在手边握着这一路信息，顺手带过去最省事 |
| QML | `NowPlaying.qml` 中心标签改成「白圈 + 圆形封面」：`Image` 本体 `visible: false`，可见的那份由 `MultiEffect { maskEnabled: true }` 用圆形 mask 画出来；无封面或未 `Ready` 回落到红标写歌名；中心孔永远在最上层。`PlayerBar.qml` 的 56px 封面位换成 `Image`，无封面仍回 `♪` |
| 坑 1 | `Image.source` 只认 URL，Windows 裸路径会被当成相对地址，症状是封面全不显示 |
| 坑 2 | `MultiEffect` 的 `maskSource` 必须是**独立渲染**的项，要 `layer.enabled: true`，否则遮罩读出空白 |
| 证据 | 真机截图：播放条 56px 块与唱片中心是同一张封面（中心那份随盘旋转），圆裁干净、无方角溢出、刻纹与唱臂零位移；无封面的曲目回落红标正常 |
| 回归 | `mamba_tests` 116 用例 / 4801 断言，新增 1 例「封面路径随队列进快照」（含"没有封面给空串、不把上一首的封面留下"）；`boundaries.py` 166 文件通过 |

---



### M9 · 打包发布（1.5 人日）

- [ ] 版本号从 `xmake.lua` 的 `set_version` 注入 `QCoreApplication`
- [ ] `windeployqt --qmldir presentation/qml`
- [ ] **必须一起打包**：`avcodec-*.dll` `avformat-*.dll` `avutil-*.dll` `swresample-*.dll`（vcpkg 的 `x64-windows/bin`，若是动态链接）
- [ ] 绿色版 zip + Inno Setup 安装包
- [ ] 首次启动引导页（选音乐目录）
- [ ] 单实例锁（避免两个进程抢同一个 SQLite）
- [ ] 数据目录用 `QStandardPaths::AppDataLocation`，不是 exe 旁边

**验收**

- 在**没装 Qt、没装 VS 运行时**的干净 Windows 上双击能跑（VS2022 应用需 `vc_redist`，或用 `/MT` 静态 CRT——但那样就不能用 Qt 的 `/MD` 库，所以选前者）
- 卸载后无残留目录

---

## 3. 性能预算（验收指标）

| 指标 | 目标 | 测量方式 | 从哪个里程碑守 |
| --- | --- | --- | --- |
| UI 帧率 | 1080p 稳 60fps | `QSG_RENDER_TIMING=1` | M2 |
| 单帧 GPU 时间 | < 8ms | `qmlprofiler` | M2 |
| **命令 dispatch 同步耗时** | **< 5ms** | debug 断言 | M1 |
| 首帧到可交互 | < 800ms | 打点日志 | M0 |
| 空闲内存 | < 220MB | 任务管理器 | M5 |
| 播放中 CPU | < 10%（单核） | `QtCreator` 分析器 | M4 |
| 10000 首滚动 | 无 > 16ms 帧 | `qmlprofiler` | M5 |
| 曲库首扫 | < 60s / 10000 首 | 内置计时 | M5 |
| 歌词同步误差 | < 50ms | 人工核对节拍 | M7 |
| 增量编译 | < 10s | `xmake -j` 计时 | M0 |

守住预算的四件事，按性价比排序：**列表虚拟化 > 少嵌套 > shader 合并 > 预编译**。
架构侧对应的一条：**命令必须快**。任何 Handler 超过 5ms 就是在 UI 线程上挖坑。

---

## 4. 风险登记表

| 风险 | 影响 | 概率 | 缓解 | 触发后 |
| --- | --- | --- | --- | --- |
| **架构过重**（分层开销压过收益） | 高 | 中 | M1 做"8 文件体检"；超 12 个文件就砍 `*.Contracts` | 执行 §0 瘦身选项，退化到简单分层 |
| **FFmpeg 在 MSVC 下拿不到可用二进制** | 高 | 中 | M0 阶段就先跑通 vcpkg 装 ffmpeg | 退到 M3 的 `QMediaPlayer` 长期服役，DSP 需求另想办法 |
| Qt 套件装成 MinGW | 高 | 中 | 安装后立刻 `ls D:/Qt/` 自检 | 重装，别试图混用 |
| 模块依赖成环（target 图报错） | 中 | 中 | 模块间只许依赖 `*.Contracts`；成环时先怀疑 Contracts 里放了业务 | 把共享部分下沉到 `Kernel` 或接受重复 |
| 严格 DDD 拖慢批量导入 | 中 | 中 | 500 条/事务；实在慢就加 `ICatalogImportWriter` 批量端口 | 批量路径绕过聚合持久化，但保留聚合校验 |
| 漫画风可读性翻车 | 中 | 高 | M2 出 Storybook 评审；文字区永久纸色底；五条红线进 review | M8 收窄特效，退回 L1 |
| 范围膨胀（想加在线音源） | 高 | 高 | 写进非目标；`IMediaSource` 接缝满足好奇心 | 记进 v2 backlog，当天不写代码 |
| 一个人做，进度失控 | 中 | 中 | 每里程碑可运行可演示；做不完砍 M8 可选项 | 砍 M8，保 M6 |
| Windows 专有 API 渗漏 | 中 | 低 | `platform/win` 是唯一允许 `<windows.h>` 的地方，边界脚本会抓 | 移回 `platform/win` |

---

## 5. 待拍板的决策

平台与编译器已定（**Windows x64 + MSVC**，实测是 VS 2026）。剩下两个：

1. ~~Qt 版本~~ → **已由环境决定：Qt 6.12.0**（`D:/Software/Qt/6.12.0/msvc2022_64`，机器上就这一个，没有选择余地）。
2. **漫画强度**：L1 / **L2（推荐）** / L3？见 [架构 §10.1](ARCHITECTURE.md)。
3. **要不要在线音源**：v1 建议不要。要的话额外 3–4 周，且合规责任自负。

另外两个是**架构内部的开关**，M1 切片跑完再定：
4. `*.Contracts` 是独立 target 还是并入 `*.Core`？（看 M1 的 8 文件体检结果）
5. 投影表是全量物化还是查询时聚合？（看 M5 的万首性能）

---

## 6. 日常命令（M0 / M1 / M2 已验证）

```bash
# 0. 每个新 shell：把 portable xmake 放进 PATH（见 §1.3）
export PATH="/c/Users/123/AppData/Local/Temp/xmake-test/xmake:$PATH"

# 1. 配置（Qt 自动识别，无需 --qt）
xmake f -m debug -a x64 -y

# 2. 编译（23 个 target，增量 < 1s）
xmake

# 3. 运行
xmake r MambaMusic

# 4. 单测（纯 C++，不链 Qt）
xmake build mamba_tests && ./build/windows/x64/debug/mamba_tests.exe

# 5. 边界脚本
python xmake/checks/boundaries.py

# 6. 边界探针（两条都必须编译失败；验完记得还原）
#    modules/catalog/core/src/probe.cpp  += #include <QString>                  → C1083
#    presentation/src/probe.cpp          += #include <mamba/catalog/core/probe.h> → C1083

# 7. 改完 .frag 之后重新烘焙（xmake 的 task 沙箱里没有 import，只能靠 QTDIR / PATH 找 qsb）
PATH="/d/Software/Qt/6.12.0/msvc2022_64/bin:$PATH" xmake qsb

# 8. 加了新的界面文案之后重跑字体子集化，否则新字掉回系统字体
python tools/subset_fonts.py          # 或加 --check 只看字符数

# 9. 帧时间（GUI 子系统进程必须加 QT_FORCE_STDERR_LOGGING，否则 qDebug 全丢）
QT_FORCE_STDERR_LOGGING=1 QSG_RENDER_TIMING=1 ./build/windows/x64/debug/MambaMusic.exe
```

跑起来看不到窗口但进程立刻退出时，先看 stderr 的 `[qml]` 行——`host/main.cpp` 把 `QQmlApplicationEngine::warnings` 转成了普通 stderr 输出。

Switch to release：`xmake f -m release -y && xmake`。

> **debug 构建不要写死 `/MD`**：Qt 的 debug DLL 是 `/MDd`，两套 CRT 会让进程在第一次跨 DLL 分配时以 `0xC0000374`（STATUS_HEAP_CORRUPTION）静默退出，且不打任何日志。`xmake/targets.lua` 现在不设 `set_runtimes`，让 xmake 按模式给。

---

## 附：每个里程碑的自检清单

做完任意一个里程碑，问这六个问题：

- [ ] `xmake run` 能起来吗？看到的和预期一致吗？
- [ ] 上一里程碑的功能还正常吗？（尤其 M4 换引擎后）
- [ ] 性能预算那一行（§3）还达标吗？
- [ ] **有没有往 `core/` 里漏进 Qt / SQL / FFmpeg？**（`boundaries.py` 通过了吗）
- [ ] 有没有出现"模块 A 直接调模块 B 的 Core"？只看 Contracts 吗？
- [ ] 这次新增的聚合/值对象，在 `tests/` 里有单测吗？
