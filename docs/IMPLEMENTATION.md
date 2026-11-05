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
| **M4** | 换成 FFmpeg 引擎 | 4 | M3, M1 | 自研引擎，端口契约不变 |
| **M5** | 曲库完整（导入 + 扫描 + 投影） | 4.5 | M1 | 万首曲库，滚动不掉帧 |
| **M6** | 播放完整体验（队列聚合） | 4 | M4, M5 | 全流程无鼠标操作 |
| **M7** | 歌词 | 3.5 | M5, M6 | 歌词同步误差 < 50ms |
| **M8** | 漫画表现力 | 5 | M2 | 网点/集中线/拟声词/翻页就位，60fps |
| **M9** | 打包发布 | 1.5 | 全部 | 干净 Windows 机器双击能跑 |
| | **合计** | **≈ 33** | | 全职 ≈ 6.6 周；业余（每天 2h）≈ 4 个月 |

**关键路径**：`M0 → M1 → M3 → M4 → M6 → M7 → M9`。这条链上任何一环延期都顺延交付。

**进度**：M0 ✅、M1 ✅、M2 ✅、M3 ✅ 已完成并实测通过（构建、运行、边界探针、边界脚本、单测、截图、帧时间、真音频播放），记录见 §2 下的「M0」～「M3」小节。**下一步是 M4（换成 FFmpeg 引擎）**；M5 的扫描/投影部分不依赖 M4，可并行。

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
| vcpkg | ❌ 未装 | M0 不需要，M4（FFmpeg）前必须装 |

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

### 1.4 装 vcpkg 并编译 FFmpeg（**M4 前必须完成；建议 M1 期间就后台跑掉**）

```powershell
git clone https://github.com/microsoft/vcpkg D:\vcpkg
D:\vcpkg\bootstrap-vcpkg.bat
# 只要 FFmpeg，而且要裁剪：播放器不需要 avfilter / avdevice / swscale
D:\vcpkg\vcpkg install ffmpeg:x64-windows --no-default-features --features "avcodec,avformat,swresample"
```

首次编译 30–60 分钟。走完这一步，FFmpeg 就是 MSVC ABI 的，后面不会再有 `undefined symbol: __imp_av_*`。

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
| 5 | 投影用视图不用物化表 | 万级以内视图足够快；M5 数据量上来再改物化表 + `TrackImported` 订阅 |
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

### M4 · 换成 FFmpeg 引擎（4 人日，**技术风险最高**）

**目标**：在端口契约完全不变的前提下替换实现。做对了，后面所有 DSP 才有地基。

**步骤**

- [ ] `AudioRing`（SPSC 无锁环，容量 ≈ 2s）
- [ ] **单测先行**：两个线程，生产者写 100 万样本，消费者逐个校验，不丢不重不越界
- [ ] **开写前先过一遍 [DEPENDENCIES §8](DEPENDENCIES.md) 的 9.x 差异表**：`AVChannelLayout` 取代 `channels`/`channel_layout`、`send_packet`/`receive_frame` 取代 `avcodec_decode_audio4`、`swr_alloc_set_opts2`、输出强制转 `AV_SAMPLE_FMT_FLT`
- [ ] `FfmpegDecoder`：open / decode loop / seek / flush，带**代际号**
- [ ] `AudioOutDevice : QIODevice` + `QAudioSink` 拉模式
- [ ] 位置由样本计数推导：`startPts + framesWritten*1000/rate`
- [ ] `FfmpegOutput` 实现 `IAudioOutput`，在 Host 里替换掉 `QtMultimediaOutput`（**一行切换**）
- [ ] 输出设备热切换、采样率跟随设备

**验收**

- 操作体验与 M3 完全一致（最重要的验收项）
- 拖进度条后无前一秒残留（代际号生效）
- 连续切歌 50 次不崩、不爆音
- 播放 320kbps mp3 的 CPU 低于 M3 版本
- 播放中拔耳机/换输出设备不崩
- **`PlayerViewModel` 零改动**（用 `git diff` 证明）

**死法预警**：`LNK2019: __imp_av_*` → FFmpeg 不是 MSVC 构建，回 [§1.4](#14-装-vcpkg-并编译-ffmpeg这一步最慢但只需一次) 用 vcpkg 重装。

---

### M5 · 曲库完整（4.5 人日）

**目标**：从"能导入一个文件夹"到"能管理一万首"。

**步骤**

- [ ] `Catalog` 聚合补全：`Album`（`assignDiscOrder` 处理碟/轨冲突）、`Artist`
- [ ] 增量扫描：`file_mtime` 变了才重读标签
- [ ] 批量导入：500 条一个事务，每批之后 publish `TrackImported`
- [ ] `mv_track_row` / `mv_track_stat` 投影 + 事件订阅者
- [ ] 搜索：英文/数字走 `title_norm` 前缀索引 + 拼音首字母列；**中文子串走 FTS5**（`unicode61` + bigram 分词）——`SQLITE_ENABLE_FTS5` 在 M1 步 2 已编进 amalgamation
- [ ] `LibraryViewModel`：分组查询（专辑 / 艺人 / 文件夹）、分页、窗口缓存
- [ ] `CoverCache`：内嵌封面抽离 + `cover_key` 去重
- [ ] `pages/LibraryPage.qml` 换成 `TrackRow` delegate

**验收**

- 10000 首首扫 < 60s；二次扫描（无变更）< 5s
- **扫描期间 UI 不卡、能继续播放、能滚动列表**
- 列表滚动稳 60fps（`data()` 是 O(1)，不触发查询）
- 中文搜索 3 个字以上才有结果、输入即筛，延迟 < 30ms
- 封面加载不闪（异步 + 占位图）
- 关掉重开，库和播放次数都在
- 投影表与聚合表在异常中断后仍一致（每批一个事务，不存在半批）

---

### M6 · 播放完整体验（4 人日）

**步骤**

- [ ] `PlayQueue` 聚合补全：`RepeatOne` / `RepeatAll` / `Shuffle`
- [ ] `ShufflePlanner` 领域服务：Fisher–Yates 排列 + 游标，可序列化以支持恢复
- [ ] `goBack()` 在随机模式下精确回到真实上一首
- [ ] 队列持久化（`play_queue` / `play_queue_item` / `play_queue_shuffle`）
- [ ] `TrackFinished` 集成事件 → `Catalog` 累计 `play_count`（**跨模块只能用事件，不许直接调**）
- [ ] 播放条补全：队列面板、模式切换、音量弹层
- [ ] `pages/NowPlaying.qml`：大封面 + 唱针旋转 + 标题
- [ ] 快捷键：空格 / ←→ / Ctrl+←→ / Ctrl+O / Esc
- [ ] 空状态 / 加载状态 / 错误状态（文件被删、解码失败）

**验收**

- 连续听完一张专辑全流程无需鼠标
- 随机模式连续 200 首无重复；上一首能精确回溯
- 删掉正在播放的文件 → 优雅降级到下一首，不崩
- 播放次数在重启后正确

---

### M7 · 歌词（3.5 人日）

**步骤**

- [ ] `LrcParser`：标准 LRC、`[offset:]`、一行多时间标签、翻译行合并、增强 LRC 逐字（`<mm:ss.xx>`）
- [ ] 单测：5 个真实 LRC（中文 / 翻译 / 逐字）跑快照测试
- [ ] `Lyrics` 聚合：时间戳单调不减校验，非法行丢弃而非抛错
- [ ] `LyricTimeline`：二分查找当前行
- [ ] `LyricModel` + `NowPlaying` 歌词滚动（弹簧动画、居中高亮）
- [ ] 来源优先级：同名 `.lrc` → 内嵌标签 → "纯音乐，请欣赏"占位

**验收**

- 同步误差 < 50ms（用节拍明显的曲子人工核对）
- 滚动无跳帧；切歌无残影
- 无歌词时不显示空盒子

---

### M8 · 漫画表现力（5 人日，含 2 天可选项）

**步骤（前 3 天，必做）**

- [ ] `dot.frag`（网点）、`lines.frag`（集中线）→ `xmake qsb`
- [ ] 整页 ≤ 2 个全屏 `ShaderEffect`，其余全靠贴图与几何变换
- [ ] `SfxText` 池化 + 播放/暂停/切歌/收藏四个事件接入
- [ ] `PageTurner` 替换所有页面切换（先做快照 + 旋转的便宜版）
- [ ] **胶囊化**：Storybook 里的组件全部在真实页面用上，**没用上的删掉**

**步骤（后 2 天，可选）**

- [ ] 角色立绘（对播放状态有反应：idle / 唱歌 / 睡着）
- [ ] 手工 `qmltypes` + `qmlcachegen` 规则（xmake 无内置）

**验收**

- `QSG_RENDER_TIMING=1` 稳 60fps，无单帧 > 8ms
- CPU < 10% / GPU < 25%（1080p 播放页）
- 转场期间不掉帧（`ShaderEffectSource` 用完立刻 `live: false`）

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
