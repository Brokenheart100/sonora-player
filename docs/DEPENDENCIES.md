# Sonora 依赖清单

> 这份文档只回答三个问题：**用什么库、允许出现在哪一层、为什么不用别的**。
> 架构见 [ARCHITECTURE.md](ARCHITECTURE.md)，进度见 [IMPLEMENTATION.md](IMPLEMENTATION.md)。

---

## 0. 依赖预算（先立规矩，再谈选型）

这套架构（严格 DDD + Clean Architecture）最容易被自己毁掉的方式，不是写错代码，是**在领域层里加了一个"很方便"的库**。所以先定三条硬规矩：

1. **领域层零第三方依赖**。`Sonora.Kernel`、`Sonora.Contracts`、所有 `*.Core` 只许用 C++ 标准库。这条不是洁癖——[xmake/targets.lua](../xmake/targets.lua) 里这些 target 没有 Qt、没有 `add_requires`，物理上就链不到，编译器会替你执行。
2. **一个库只能出现在它被批准的那一层**（见 §1 矩阵）。要用到上层去，先改文档再改构建。
3. **新依赖必须过五问**（见文末清单）。过不了就不加——标准库或者 200 行手写，通常比一个新依赖便宜。

**预算**：运行时第三方库（不含 Qt / Windows SDK）**≤ 6 个**。当前规划用了 5 个：SQLite、SQLiteCpp、TagLib、FFmpeg、spdlog。剩下的一个名额留给未来的意外。

M0 阶段实际只用了 **Qt** 一个（其余都还没到引入的里程碑）。

---

## 1. 分层可见性矩阵

| 库 | Kernel / *.Core | *.Contracts | *.Adapters | Platform.* | Presentation | tests |
| --- | --- | --- | --- | --- | --- | --- |
| C++ 标准库 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Qt 6 | ❌ | ❌ | ✅ | ✅ | ✅ | ❌ |
| SQLite / SQLiteCpp | ❌ | ❌ | ❌ | ✅（仅 `storage`） | ❌ | ❌ |
| TagLib | ❌ | ❌ | ✅（仅 `Catalog.Adapters`） | ❌ | ❌ | ❌ |
| FFmpeg | ❌ | ❌ | ❌ | ✅（仅 `audio`） | ❌ | ❌ |
| spdlog | ❌ | ❌ | ✅ | ✅ | ❌ | ✅ |
| doctest | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |

这张表的实际效力来自构建系统：`*.Core` 不 `add_deps` 任何带 Qt 的 target，所以 `#include <QString>` 直接 `C1083`（M0 已实测）。**表只是给人看的解释，真正执行的是 [xmake/targets.lua](../xmake/targets.lua)。**

---

## 2. 已定清单

| 库 | 版本 | 许可 | 干什么 | 引入 | 获取 |
| --- | --- | --- | --- | --- | --- |
| **Qt** | 6.12.0（本机已装） | LGPLv3 / 商业 | UI、事件循环、网络、音频输出、SQL 驱动 | M0 ✅ | 本机 `D:/Software/Qt/6.12.0/msvc2022_64` |
| **SQLite** | 3.54.x | Public Domain | 持久化（聚合表 + 投影表 + 迁移版本表） | **M1** | **自带 amalgamation**（见下，不用包） |
| **SQLiteCpp** | 3.4.0 | MIT | sqlite3 的 RAII C++ 封装（Statement/Transaction） | **M1** | xmake-repo `sqlitecpp`（`sqlite3_external`） |
| **TagLib** | 2.3.2 | LGPL-2.1 / MPL-1.1 | 读 ID3v2 / FLAC / MP4 / OGG / APE 元数据与内嵌封面 | **M1** | xmake-repo `taglib` |
| **FFmpeg** | 9.0.2 | LGPL-2.1+（默认） | 解码 / 重采样 / 时长与格式探测 | M4 | **vcpkg** `ffmpeg:x64-windows` |
| **spdlog** | 1.17.0 | MIT | `ILogger` 的实现（异步、滚动、彩色控制台） | M1 | xmake-repo `spdlog` |
| **doctest** | 2.5.3 | MIT | 单测框架（替换 M0 里的手写断言） | M1 | xmake-repo `doctest` |
| **xxHash** | 0.8.4 | BSD-2 | 内容指纹（曲目去重、QualityHash 缓存键、封面缓存键） | M5 | xmake-repo `xxhash` |

### 逐个说明

**Qt 6.12** — 只启用实际用到的模块，别 `add_frameworks` 一堆不用的：

| 模块 | 用途 | 启用 |
| --- | --- | --- |
| `Core` `Gui` `Qml` `Quick` | 基础与 UI | M0 ✅ |
| `QuickControls2` | 控件（会和自定义漫画控件混用） | M2 |
| `QuickEffects` | `MultiEffect`：阴影/模糊/遮罩，省掉一半自写 shader | M2 |
| `Network` | 未来在线音源、封面下载 | M6 |
| `Multimedia` | `QAudioSink`（音频输出设备） | M3 |
| `Sql` | QSqlDatabase——**给它一句否决**：存储层走 SQLiteCpp，不用 QtSql（见 §4） | 不启用 |
| `Svg` | 手绘边框、图标 | M2 |
| `ShaderTools` | 构建期 `qsb.exe`（不是运行期依赖） | M8 |

> **不要用静态 Qt**：LGPLv3 下静态链接要求你提供可重新链接的目标文件，麻烦且容易做错。动态链接 + `windeployqt`（M9）。

**SQLite** — 一个文件、零配置、进程内。这个项目不需要数据库服务。

**不用 xmake-repo 的 `sqlite3` 包，自己收 amalgamation 源码**（`sqlite3.c` + `sqlite3.h`，约 250KB，Public Domain，放 `third_party/sqlite3/`）。原因很具体：那个包只暴露 `rtree` / `math_functions` / `dbstat` 之类的开关，**没有 `SQLITE_ENABLE_FTS5`**；而 SQLite 的功能是**编译期**决定的，`PRAGMA compile_options` 里没有的，写多少 SQL 都变不出来。FTS5 是 M5 中文/子串搜索的硬需求，所以编译开关必须自己控。

四个开关一次定死（改它们 = 重建库，越早越好）：

| 开关 | 为什么 |
| --- | --- |
| `SQLITE_ENABLE_FTS5` | 搜索走倒排索引，不是 `LIKE '%x%'` 全表扫 |
| `SQLITE_THREADSAFE=1` | 扫描线程写、UI 线程读，必须是 serialized 模式 |
| `SQLITE_DEFAULT_WAL_SYNCHRONOUS=1` | 开 WAL 后同步级别别用 FULL，否则每首歌都 fsync |
| `SQLITE_DQS=0` | 禁掉双引号当字符串用（SQL 里写错列名会被当字面量，静默通过） |

JSON 函数不用开关：3.38 起已内置。WAL 是运行期 `PRAGMA journal_mode=WAL`，不是编译开关。
**代价**：多一个长编译单元（首次约 10–15s，之后缓存）。换来的是一份确定的、不随包管理器漂移的 SQLite。

**SQLiteCpp** — 为什么不直接裸调 C API：`sqlite3_prepare_v2` / `sqlite3_bind_*` / `sqlite3_finalize` 的 RAII 包装、`Statement` 复用（避免每帧重复 prepare）、`Transaction` 自动回滚，这三件事自己写要 300 行且容易漏。
**代价**：它是 exception-based。规矩是——**异常不许穿过 adapter 边界**，`Storage` 层 catch 成 `Result`（[架构 §7.2](ARCHITECTURE.md)）。

**TagLib** — 比用 FFmpeg 读元数据好在：TagLib 专门干这个，对残缺标签容错好、能拿内嵌封面、API 是 C++ 的。许可证是 LGPL-2.1 **或** MPL-1.1 双授权，**选 MPL-1.1** 分支做静态链接最省心（文件级 copyleft，改动很少）。
> 如果 M5 发现 TagLib 对某个格式支持不好，退路是 FFmpeg 的 `avformat` 元数据接口——它已经在链接里了，不增加依赖。

**FFmpeg 9.0.2** — 只用一个子集：`avformat`（解容器）、`avcodec`（解码）、`swresample`（统一成 float32 交错/平面）、`avutil`。
- **必须走 vcpkg**：xmake-repo 的 `ffmpeg` 包在 MSVC 下要自己传一长串 `--enable-*`，且 Windows 预编译包（gyan.dev / BtbN）是 MinGW 构建，链接期报 `undefined symbol: __imp_av_*`。这是架构定为 MSVC 后最贵的一课。
- **默认构建就是 LGPL**：vcpkg 的默认 features 是 `avcodec avdevice avfilter avformat swresample swscale`，不含 `gpl` / `nonfree`。要做减法：`--no-default-features --features "avcodec,avformat,swresample"`，编得快、DLL 小。
- **不需要 LAME**：`mp3lame` 是**编码器**，放歌只会解码，MP3 解码器是 FFmpeg 内置的。

**spdlog** — 只在 Infrastructure 实现 `ILogger`，领域层只看接口。配置要显式写，否则默认会链它自带的 fmt：

```lua
add_requires("spdlog 1.17.0", {configs = {header_only = true, std_format = true}})
```

`std_format = true` 让它用 `std::format`（这个 config xmake-repo 是支持的），header-only 让我们自己的编译开关生效，也省一个静态库。

**doctest** — 选它不选 gtest/Catch2 的唯一理由：**单个头文件、编译开销近乎零**。测试 target 是纯 C++ 的（不链 Qt），doctest 保持这个特性。

**xxHash** — `XXH3_64bits` 用来算"文件内容 + 采样率 + 声道数"的指纹，做去重和缓存键。别用 `std::hash`（不保证跨进程稳定）。

---

## 3. 标准库直接顶掉的（不用装）

现代 C++ 已经把这些"必备第三方库"吃进标准库了。这条清单是本项目最省预算的地方：

| 需求 | 以前要装的库 | 现在用 |
| --- | --- | --- |
| 格式化字符串 | `{fmt}` | `std::format` / `std::print`（MSVC 完整支持） |
| 范围/管道 | `range-v3` | `std::ranges` / `std::views` |
| 预期值 | `tl::expected` / `boost::outcome` | `std::expected`（C++23，见下） |
| 文件系统 | `ghc::filesystem` | `std::filesystem` |
| 线程与停止 | `std::thread` + 手写 flag | `std::jthread` + `std::stop_token` |
| 时间 | `date` / `chrono` 第三方 | `std::chrono`（C++20 的日历与时区） |
| 类型安全的位转换 | — | `std::bit_cast` |
| 日志位置 | `BOOST_CURRENT_FUNCTION` | `std::source_location` |
| 线程池 | `TaskFlow` / `oneTBB` | 自己写：`std::jthread` + 队列，150 行（`ITaskQueue` 的实现） |
| 信号槽（领域层） | `boost::signals2` | `IDomainEvent` + 显式分发 |

**关于 `std::expected`**：它能让 `Kernel/Result.h` 瘦到 20 行，但在 MSVC 上目前仍要走 `/std:c++23preview`。**决定**：M1 的 `Result<T>` 先用 `std::variant<T, Error>` 实现（30 行，行为一致），等项目整体迁到 C++23 时再换——一行 typedef 的事。不要为了一个类型把全项目切到 preview 标准。

---

## 4. 明确拒绝的（附理由，别反复推翻）

| 候选 | 为什么不用 |
| --- | --- |
| **Boost**（整体或 Asio） | 体积、编译时间、TLS 依赖全部超标。事件循环用 Qt 的，异步 IO 不存在（本地播放器没有服务端）。 |
| **Qt Widgets** | 与 QML 混用会分裂成两套样式系统。架构 §10 已经定了纯 Qt Quick。 |
| **QtSql / QSqlDatabase** | 驱动是运行期插件，错误信息含糊；而且它会把 Qt 类型渗进存储层。SQLiteCpp 更可控。 |
| **任何 SQLite ORM**（sqlite_orm / ODB / hiberlite） | 本项目的查询是手写的投影表 `SELECT`，ORM 生成的 SQL 不可控、编译极慢。手写 SQL + 强类型 DTO 映射，代码量反而更小。 |
| **range-v3** | 50k 行模板，编译时间翻倍；C++20 `std::ranges` 够用。 |
| **gtest** | 要单独编译、要额外 main、CMake 味重。doctest 一个头解决。 |
| **Catch2 v3** | 同样要编译成库；本项目不需要 BDD 语法。 |
| **ICU** | 为了中文排序/搜索引入 ~30MB 字典。中文搜索用 SQLite FTS5 + 自带 bigram 分词即可。 |
| **PortAudio / miniaudio / RtAudio** | 音频输出用 Qt 的 `QAudioSink`（WASAPI 共享模式，拉模式 QIODevice，正好对上 `AudioRing` 的设计）。真需要独占/低延迟时，直接在 `platform/win` 里调 WASAPI——不引入第三方。 |
| **OpenCV / 任何图像库** | 封面加载、缩放、模糊全用 Qt（`QImage` + `MultiEffect` + shader）。 |
| **libcurl / OpenSSL** | v1 没有网络。将来要联网用 `QtNetwork`（自带 TLS）。 |
| **nlohmann/json（现在不引）** | v1 没有插件、没有导入导出、配置存 SQLite。等 M6 真需要 JSON 时再加（3.12.0，header-only）。 |
| **spdlog 之外的日志库**（log4cxx / quill） | quill 更快但要自己管后端线程；本项目日志不是瓶颈。 |
| **Crashpad / Breakpad（现在不引）** | M9 若要做崩溃上报再说。 |
| **Qt Quick 3D** | 2D 漫画风，不用 3D 场景。 |
| **任何 QML 之外的 UI 框架** | 已经定了 Qt Quick，这就是终局。 |

---

## 5. 观察清单（有触发条件才引入）

| 库 | 版本 | 触发条件 |
| --- | --- | --- |
| `nlohmann/json` | 3.12.0 | M6 要做播放列表导入/导出，或插件清单 |
| `utf8proc` | 2.11.3 | 中文搜索需要 NFC 归一化 / 大小写折叠（FTS5 的 unicode61 对中文分词无力） |
| `Crashpad` | — | M9 想做崩溃上报 |
| `libebur128` | 1.2.x | 若做"响度归一化"（ReplayGain 风格） |

> `kissfft` **不需要**：频谱可视化用 FFmpeg 自带的 `av_fft`（`avutil/tx.h`），已经链上了。

---

## 6. 获取渠道与一致性

两个渠道，分工固定：

**渠道 A：xmake-repo**（`add_requires`）——纯源码构建，和项目同编译器、同 `/MD`、同 C++20：

```lua
-- 版本在引入那一刻按仓库最新写死，别留浮动版本
add_requires("sqlitecpp 3.4.0", {configs = {sqlite3_external = true}})
add_requires("taglib 2.3.2", "doctest 2.5.3", "xxhash 0.8.4", {configs = {shared = false}})
add_requires("spdlog 1.17.0", {configs = {header_only = true, std_format = true}})
```

> SQLite 不在这个列表里——它来自 `third_party/sqlite3/` 的自带源码（见 §2）。

**渠道 B：vcpkg**——只给 FFmpeg（以及将来任何需要精细二进制配置的重型库）：

```bash
# 只装 FFmpeg。不要顺手把 sqlite3/taglib 也丢进 vcpkg——
# 同一套东西从两个渠道各装一份，是 LNK2038 的经典成因。
D:/vcpkg/vcpkg install ffmpeg:x64-windows --no-default-features --features "avcodec,avformat,swresample"
xmake f --vcpkg=D:/vcpkg -m debug -a x64
```

**为什么不全用 vcpkg**：xmake 的 `add_requires` 与 target 图天然集成（自动带上 includedirs/links/defines），不用维护一串 triple 依赖；每个包只编你要的那部分。
**为什么不全用 xmake-repo**：FFmpeg 的 MSVC 构建在 vcpkg 里由官方维护，xmake-repo 那份需要手传大量 `--enable-*` 且更容易编出不兼容的产物。

**一致性四条**（违反任意一条，症状都是链接期的 `LNK2038` / `__imp_*` 找不到）：
1. 全部 **`/MD`**（vcpkg 的 `x64-windows` 三元组默认就是 `/MD`；`x64-windows-static` 是 `/MT`，**不要用**）；
2. 全部 **x64**；
3. 全部 **C++20**（库要求更高就统一抬高，不要混）；
4. 版本写死。xmake 侧写进 `add_requires`；vcpkg 侧用 manifest（`vcpkg.json` + `builtin-baseline`）锁定。

---

## 7. 许可合规（发布前必须处理的三件事）

1. **Qt（LGPLv3）**：动态链接 + `windeployqt` 分发 DLL；随包附 LGPLv3 全文与 Qt 版权声明；允许用户替换 Qt DLL。
2. **TagLib（选 MPL-1.1）**：静态链接时，改过的 TagLib 源文件需公开——本项目不改它，所以只需附许可证声明。
3. **FFmpeg（LGPL-2.1+）**：vcpkg 默认构建是 LGPL。**千万别开 `gpl` feature**（会引入 x264/x265/libass，整个程序变 GPL）。打包时附 FFmpeg 的 LICENSE 与"使用的源码版本"。

> 一句话原则：**任何库都保持动态链接，许可证随包分发**。这样上面三条都不会咬人。

---

## 8. 抄参考实现时的 FFmpeg API 差异

QueMusic 的解码循环可以直接借，但 FFmpeg 从 6.x 到 9.x 有几处是**旧写法已经删掉**的，照抄会编不过：

| 旧写法（≤5.x） | 现在（7/8/9 唯一路径） |
| --- | --- |
| `codecCtx->channels` / `channel_layout` | `AVChannelLayout ch_layout` + `av_channel_layout_copy()` / `av_channel_layout_default()` |
| `avcodec_decode_audio4()` | `avcodec_send_packet()` + `avcodec_receive_frame()`（一次 send 可能收多次） |
| `swr_alloc_set_opts()` | `swr_alloc_set_opts2()`（opts 直接带 `AVChannelLayout`） |
| `av_get_channel_layout_nb_channels()` | `ch_layout.nb_channels` |
| `AVCodecContext::sample_fmt` 直接当输出用 | 必须显式 `swr` 转成 `AV_SAMPLE_FMT_FLT`（`AudioRing` 只认 float32） |

另外：`avformat_find_stream_info()` 对长音频会**提前读几 MB**；本地播放器应设 `AVFormatContext::probesize` / `max_analyze_duration` 收小，否则点开一首歌要等半秒。

---

## 9. 速查：新机器一次性命令

```bash
# 1. Qt 6.12 msvc2022_64 装好，记下 <QT_PREFIX>（本机 D:/Software/Qt/6.12.0/msvc2022_64）
# 2. xmake >= 2.9.5
# 3. Python 3.9+（边界脚本）
# 4. vcpkg + FFmpeg（M4 之前必须完成，30–60 分钟；建议 M1 期间就后台跑）
git clone https://github.com/microsoft/vcpkg D:/vcpkg
D:/vcpkg/bootstrap-vcpkg.bat
D:/vcpkg/vcpkg install ffmpeg:x64-windows --no-default-features --features "avcodec,avformat,swresample"

# 5. 配置
xmake f -m debug -a x64 -y

# 6. 首次构建：xmake 源码编译 sqlitecpp/taglib/spdlog/doctest/xxhash；
#    外加 third_party/sqlite3/ 的 amalgamation（一个长编译单元，10–15s，之后缓存）
xmake
```

---

## 附：引入任何新依赖前的五问

1. **标准库有没有？** 有没有 `std::` 的东西能顶（§3 那张表）？
2. **它只出现在 adapters 吗？** 如果要进 `*.Core`，先改架构，别改构建。
3. **MSVC + `/MD` + x64 + C++20 上能一键装吗？** xmake-repo 或 vcpkg 有没有现成包？没有的话，源码编一遍要多久、要不要打补丁？
4. **许可证是什么？** GPL 一律不碰。LGPL 只允许动态链接。MIT/BSD/Apache 随便用。
5. **它失败时我损失多少？** 能用一个 `#if` 换掉、或者退回 200 行手写实现的，可以加；成了不可替换的基石的，必须有第二个方案。

五问过不了就**不加**。这个项目的护城河是"领域层干净"，不是"功能列表长"。
