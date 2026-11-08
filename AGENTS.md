# AGENTS.md

给"要动手改这个仓库的 AI / 新人"的读前须知。**这里只放两类东西：破了会伤到架构的规矩、漏了会浪费半小时的坑。**
其余一律看文档：

- 系统长什么样 → [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- 按什么顺序做、怎么算做完 → [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md)
- 用哪个库、版本、许可证 → [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md)

**冲突裁决**：本文件与 docs/ 打架时，以 docs/ 与 `xmake/checks/boundaries.py` 为准，然后顺手把本文件改对。

---

## 0. 三分钟上手

```bash
# 每个新 shell：portable xmake 不在系统 PATH 里
export PATH="/c/Users/123/AppData/Local/xmake-portable:$PATH"

xmake f -m debug -a x64 -y     # Qt 自动识别，不需要 --qt
xmake                          # 编全部 target
xmake r sonora             # xmake 会补 Qt 的 runenv
```

直接跑 `build/windows/x64/debug/sonora.exe` 会静默退出（找不到 Qt DLL），要先把 `D:/Software/Qt/6.12.0/msvc2022_64/bin` 放进 PATH。

---

## 1. 不能破的规矩

带 ● 的有机器兜底，破了会编译失败或脚本报错；没带 ● 的靠自觉，破了不会立刻出事，但会慢慢烂掉。

1. ● `modules/*/core/`、`modules/*/contracts/`、`platform/*`（除 `win`）、`Sonora.Kernel` 里**不许出现 Qt / SQLite / FFmpeg / Win32 / pthread**。想用说明放错了层。
2. ● 不许 `#include "../..."` 跨 target 偷头文件——可见性由 target 依赖决定，不是由相对路径决定。
3. ● `Sonora.Presentation` 只依赖各模块的 `*.Contracts`，**永不依赖任何 `*.Core`**。这是"表现层看不见领域模型"的物理保证。
4. ● **依赖倒置 = 抽象归高层**。端口（接口）声明在 `core/.../ports/` 或 `Kernel` 里，`adapters/` 去实现它。**`adapters/` 里不许定义被 `core/` 依赖的接口**——包括"为了方便"而转发的纯虚基类。一句话判据：**把 `adapters/` 和 `platform/` 删掉，`X.Core` 还编译得过吗**。生产代码里只有 `host/` 能 `new` 具体适配器。（include 方向已由 `boundaries.py` 拦；**接口住在哪一侧机器拦不住，只能靠人守**。这是唯一一条破了还能编译通过的规矩。）
5. 模块之间只认对方的 `*.Contracts`；跨模块**不建外键、不 JOIN**（模块内可以）。
6. Handler 同步耗时 **< 5ms**；更慢的活投 `ITaskQueue::post()`，不要就地等。
7. **先 commit 事务，再发领域事件**。不做 outbox、不做最终一致。
8. 一个命令一个 `UnitOfWorkScope`；单个文件失败只计数，不回滚整批。
9. ViewModel 只持 DTO + 视图状态，**不持聚合**。QML 只认注入的单例 `Library` / `Player` / `Lyrics` / `Online` / `Settings` / `Sfx`（全部注册在 `Sonora.Presentation` 下）。
10. 播放位置走 `IPositionStream` 旁路，**不进命令总线**（60Hz 会淹掉事件循环）。
11. 仓储不提供 `findAll()`；列表查询走 `*.Contracts` 里的 Query + 投影视图，直接出 DTO。

---

## 2. 会吃掉你半小时的坑

**构建**

- **debug 构建不要 `set_runtimes("MD")`**。Qt 的 debug DLL 是 `/MDd`，两套 CRT 会让进程在第一次跨 DLL 分配时以 `0xC0000374`（STATUS_HEAP_CORRUPTION）**静默退出，零日志**。让 xmake 按模式自己给。
- 带 `Q_OBJECT` 的 Qt 类，**头文件必须出现在 `add_files` 里**，否则 `qt.moc` 看不见它，链接期报 `LNK2001 ... staticMetaObject`。`lib()` 工厂已在 `spec.qt` 时加了 `include/**.h`，**不要**再写 `{rules = "qt.moc"}`（会重复添加）。
- `add_files` 要带 glob（`"src/**.cpp"`），目录形式的匹配在带 `qt.*` 规则的 target 上会静默失败。
- **跨 target 的公开头里不许出现 Qt 类型**。Qt 的 includedirs 是 private 的，不随 `add_deps` 传给消费者，症状是下游 `fatal error C1083: 无法打开包括文件: "QAudioOutput"`。要藏就藏进 `struct Impl`（pimpl）：`platform/audio/QtMultimediaOutput.h` 是样板。
- 注册 handler 时注意两个总线的能力不同：`CommandBus::on` 有两个 overload（收可调用对象 / 收 `shared_ptr<THandler>`），**传值对象两边都不匹配**（`error C2665`）；`QueryBus::on` 只有可调用版本。统一 `std::make_shared` 或包一层 lambda。

**FFmpeg / vcpkg**

- FFmpeg 走 vcpkg（`D:/vcpkg`），`xmake/targets.lua` 顶部有 `VCPKG_ROOT` / `VCPKG_TRIPLET` 常量。**debug 构建必须链 `installed/x64-windows/debug/lib`**，链 release 那套是跨 CRT 堆损坏，只会在 `av_malloc` 里炸。
- 装 feature 要写进方括号：`ffmpeg[core,avcodec,avformat,swresample]:x64-windows`。这一版 vcpkg 的 classic 模式**不认** `--features` / `--no-default-features`（`error: unexpected switch`）。
- `Sonora` 的 `after_build` 会把 `av*.dll` / `sw*.dll` 拷到 exe 旁边。缺这一步的症状是"双击立刻退出、没有任何输出"（不是崩溃，是 DLL 找不到）。
- **`load()` 里不许同步 open 文件**：`avformat_open_input` + 建 `QAudioSink` 实测 225ms，会撞上 5ms 的命令延迟断言。现在 `FfmpegOutput::load()` 只登记路径并起线程，解码线程 open、GUI 线程建 sink。
- 位置一律"样本记账"：`(seekStartSamples + written - ring.availableToRead()) * 1000 / rate`，最后 clamp 到 `durationMs`。**别把样本和毫秒分开写**——写成两段会让跳转后位置显示成 `45:28 / 2:02`。
- 跳转精度：`av_seek_frame(..., AVSEEK_FLAG_BACKWARD)` 只落到关键帧附近，能早几十秒。用 `avformat_seek_file(fmt, stream, INT64_MIN, ts, INT64_MAX, 0)`，再按首帧 `pts` 把多出来的样本丢掉。
- 队列播完必须显式 `output_.stop()`。只打日志不停输出，界面上的按钮会永远停在「暂停」。

**曲库 / 搜索**

- 搜索走 FTS5，`track_fts.body` 存的是**逐字空格分隔**的文本（`夜 航 西 飞`），查询侧用短语 `"夜 航"`。原因：`unicode61` 把连续汉字当一个词，`MATCH '夜航'` 匹配不到 `夜航西飞`。改 `SearchText` 时**两侧必须一起改**——只改一边的症状是"搜不到"或"什么都搜到"。
- FTS5 里**标点不算 token**：`"%"*` 会直接是 MATCH 语法错误。`matchQuery()` 会丢掉没有 token 的段，纯标点输入等于不筛选——这是刻意的降级，不是 bug。
- **Windows 上 `TagLib::FileName` 不是 `const char*`**，而是一个同时收 `wchar_t*` 和 `char*` 的类；它的 `char*` 构造按**本地 8 位编码**（中文机器上是 GBK）打开文件。喂 UTF-8 字节，文件名带汉字就打不开——**不报错**，症状是"标签读不出来、时长 0、标题退回文件名"。传 `std::filesystem::path::c_str()`（Windows 上就是 `wchar_t*`）。
- 增量扫描靠 `tracks.file_mtime` + `file_size_bytes`：没变就**连标签都不读**。改动导入逻辑时别顺手把 `findFileState` 跳过，二次扫描的 250ms 全靠它。
- 导入在**后台线程**跑（`TrackListModel::startImport`）。`CommandBus` 的 5ms 告警只在总线所在的线程上触发，所以后台导入不会再刷警告——别以为是告警坏了。
- 界面拿不到 `ImportSummary`：它在 `Catalog.Core`，而 Presentation 只依赖 `*.Contracts`，且 `dispatch` 类型擦除只回 `Result<void>`。界面要计数就自己再查一次，或者去读 Handler 的日志。
- 封面走 `ICoverStore`（端口在 `Catalog.Core`，实现在 `Catalog.Adapters` 的 `FileCoverStore`）：键是 FNV-1a 64 + 由魔数判定的扩展名，**同一张图只落一份**。`TrackDto` 给的是 **`coverPath` 绝对路径**，QML 里必须再转成 `QUrl::fromLocalFile(...)`——裸路径会被 `Image.source` 当相对地址，症状是封面全不显示。
- 列表里的封面 `Image` 必须给 **`sourceSize`**（现在 56）。不给就按原图解码，几千行滚动立刻掉帧。
- 封面只在**导入时**写入：`cover_key` 为空的行在增量判断里仍算"未变"，所以老库升级后不会自动补封面，要补得重导或改 mtime。

**播放 / 事件**

- **跨模块只能用事件，不许直接调别人的仓储**。`Playback` 不知道 `Catalog` 存在；`TrackFinished` 由 `host/main.cpp` 订阅后转给 `Catalog.Adapters` 的 `SqlitePlaybackRecorder`。想加"播放次数"之外的联动，也走这条路。
- **事件要在状态推进之前发**。`handleFinished()` 里先 `publish(TrackFinished{queue_.current()->trackId})` 再 `queue_.advance()`；反过来 `current()` 已经指向下一首，播放次数会记到别人头上。
- 模块内裸写 `contracts::` 会解析到**本模块自己的** `*.Contracts`（`Playback` 里就是 `sonora::playback::contracts`），跨模块类型必须写全 `sonora::contracts::…`，否则 `C2039: 不是 …contracts 的成员`。
- `EventBus` 是**同步**分发，没有退订、没有队列、没有优先级。订阅者里不要再 publish（会递归），慢活投 `ITaskQueue`。
- `Result<A>` **不能**隐式转 `Result<void>`：逐层 `Result<void>::fail(x.error())`，和仓储里的写法保持一致。
- 注释里写 Qt 类名会被 `boundaries.py` 判成"Qt 侵入 core 层"——它扫的是文本，注释也算。写"队列化调用"这种描述，别写 `QMetaObject::invokeMethod`。
- 进度条/音量滑块的 `value` **不要绑定** `Player.progress`：33ms 一次的写入会打断绑定，手柄拖不动。用 `Connections { target: Player; function onPositionChanged() { if (!slider.pressed) slider.value = Player.progress } }`。
- 单曲循环只拦**自动**推进（`AdvanceReason::AutoFinished`）；用户点"下一首"照常前进。随机排列的第一项固定是用户点的那首，所以 Fisher–Yates 的循环条件必须是 `i > 1` + `dist{1, i}`（写成 `i > 0`/`dist{0, i}` 会把首项也换走）。

**歌词**

- **当前行的二分规则有两份**：`modules/lyrics/core/.../domain/LyricTimeline.h` 的 `indexAt()` 和 `presentation/src/LyricsModel.cpp` 里的同名逻辑（Presentation 看不到 `Lyrics.Core`）。改规则或加"同时间戳多行"的新语义时**两处一起改**。
- **增强 LRC 的纯文本初值必须是空串**：`std::string plain{content}` 会把原始标签拼进正文，得到 `<00:10.00>逐<00:10.35>字逐字`。
- **GBK 歌词一律拒绝**，`ErrorCode::Unavailable` + 提示"用记事本另存为 UTF-8"。不要猜编码：猜错是整屏乱码，比不显示难受得多。
- **来源顺序是"同名 `.lrc` → 内嵌标签"**，`LyricsSourceChain` 出错立刻停（不降级）。`Instrumental`（解析出零行）由 `GetLyricsHandler` 填占位行，界面不自己判断。
- 内嵌歌词只认 `LYRICS` / `UNSYNCEDLYRICS` / `SYNCEDLYRICS` 三个属性键；Windows 上 `TagLib::FileName` 必须传 `path.c_str()` 的 `wchar_t*`（见上面曲库那条）。
- **LRC 里有"有时间戳、没文字"的空行**（前奏/间奏占位，`[00:35.29]` 后面什么都没有）。`LyricsModel::setPositionMs()` 算出行号后要往回退到最近一行有字的，否则间奏期间屏幕上**一行红的都没有**，用户会报"歌词不同步"。两处二分规则都要改（见上一条）。
- 歌词列表要**换歌即对位**：`onCurrentIndexChanged` 用动画滚，`onCountChanged` 和 `Component.onCompleted` 直接设 `contentY`；只挂前者的话，进页面/切歌时停在第一行。

**播放状态**

- **`FfmpegOutput::play()` 起播后必须 `emitState(Playing)`**。只置 `playing=true` 的话，"暂停 → 播放"路径永远停在 `Paused`：声音在响、按钮还写"播放"、再点一次毫无反应（用户报的"播放暂停有问题"就是这个）。
- 已 `suspend()` 的 sink 用 `resume()`，**不要** `start()`——`start()` 会重开数据流并报 `QAudioSink::start() called while already started`。
- 进度位置用 `seekStartMs + sink->processedUSecs()/1000`，**不要**用"环形缓冲里被拉走的样本数"：那是解码头，比耳朵听到的早一截。
- 键盘焦点不在自己的输入框里时别让 `TextInput` 抢焦点：搜索框写 `focus: true` 会让窗口级 `Shortcut` 全部失效。快捷键要加 `enabled: !typing` 守卫，`typing` 由 `activeFocusItem` 是否带 `text` 判断。

**QML / shader**

- QML 要用注入的单例，文件头必须有 **`import Sonora.Presentation 1.0`**，否则 `Library` 是 `undefined` 且**不报任何错**，绑定静静变成空串。
- `ShaderEffect` 的 uniform 成员名与 QML 属性名**必须逐字相同**。不同名不报错，只是整个 UBO 为 0——表现为整片纯黑。
- `ShaderEffect` **没有 `live` 属性**（那是 `ShaderEffectSource` 的），写上去整个组件加载失败。
- 改完 `.frag` 要重烘焙：`PATH=".../Qt/6.12.0/msvc2022_64/bin:$PATH" xmake qsb`。xmake 的 task 沙箱里**没有 `pcall` / `import` / `catch`**，`qsb.exe` 只能从 `QTDIR` 或 PATH 找。
- **应用图标是生成物，不是手绘资产**：改图标 = 改 `tools/make_icon.py` + 重跑它；`.ico` 由 `assets/app.rc` 嵌进 exe（`rc.exe` 的相对路径以 `.rc` 所在目录为基准），运行期任务栏图标来自 qrc 里的 `:/icons/sonora.png`（`setWindowIcon`）——**两处都有才完整**，只做前者任务栏会是白纸。
- 加了新的界面文案（**C++ 里 `QStringLiteral("…")` 的状态文案也算**），要重跑 `python tools/subset_fonts.py`，否则新字掉回系统字体（视觉上很明显）。CI 用 `python tools/subset_fonts.py --verify` 拦这个——它只查"每个字都在子集里"，不比字节（子集字节依赖 fontTools 版本）。
- **动画不要作用在会被绑定的属性上**。`NumberAnimation { target: x; property: "opacity" }` 一旦跑过，`opacity` 上的绑定就被打断，动画结束值留在最后一帧——症状是"第一次闪了一下，之后再也没出现过"。动画只动一个中间量（`flash`），`opacity: flash` 保持绑定。
- **自定义 UBO 里的 `vec2` 不可信**：`float` 成员能到 shader，`vector2d` 实测到不了（现象是坐标全错、射线变成一团同心光晕）。要么拆成两个 `float`，要么别用 shader——集中线最后是 `Canvas` 画的。
- **`Canvas` 会裁剪到自身矩形**，画到边界外的部分被平切。射线、扩散、投影这类"要往外走"的图形，长度要按"还能走多远"收笔，末端收成针尖，末端落在哪都不像被切断。
- delegate 内部的 `id` **在外面看不见**（`item.pop.restart()` → `TypeError: Cannot call method 'restart' of undefined`）。给 delegate 加一个 `function start()` 当门面。
- `comic/`、`layout/` 这些子目录里的组件**也要** `import Sonora.Presentation 1.0`，否则 `ReferenceError: Player is not defined`——而且只在运行时炸。

**在线音源**

- `IHttpTransport::send()` **只许队列线程调用**（实现里有断言）。回调也在队列线程上回来；别在 GUI 线程直接 `send` 再等结果，那等于把网络延迟搬进 5ms 预算。
- **cookie 不许进日志、不许进事件载荷**。`OnlineErrorRecord.where` 只写"搜索 / 取地址 / 登录校验 / 扫码"这类固定词，`message` 用 provider 原文——原文里也没有 cookie，别自己拼。
- 在线曲目 `trackId = 0`，`SqlitePlaybackRecorder` 见到 0 必须早退，否则会给"一首不存在的歌"记播放次数。
- 缓存/会话/直链全在 `online_*` 四张表里，**不进任何聚合事务**；`last_used_at = 0` 表示"迁移前的老行"，排序要用 `CASE WHEN last_used_at = 0 THEN fetched_at ELSE last_used_at END`。
- **迁移里不要写"顺手回填"的 UPDATE**：200 行 × 78KB 的老库上，`UPDATE ... SET last_used_at = fetched_at` 会让 SQLite 报 `unable to open database file`，而迁移失败 = 应用打不开。加列就只加列。
- 降级必须留痕：失败走 `Diagnostics::recordError()`（分类计数 + 最近 10 条），取消（`Internal` + `"已取消"`）不计数，否则打字时每敲一个字就多一条"错误"。

**调试**

- QML 类型错误看 **stderr 的 `[qml]` 行**——`host/main.cpp` 把 `QQmlApplicationEngine::warnings` 接出来了。没有这段，`/SUBSYSTEM:WINDOWS` 进程里这类错误完全不可见。
- GUI 进程的 `qDebug` 走 OutputDebugString，要看就加 `QT_FORCE_STDERR_LOGGING=1`（`QSG_RENDER_TIMING` 等也靠它）。
- 在这个 harness 里 `Start-Process` 起 GUI 子进程会把调用挂住 → 用后台任务跑 exe，另开一个 shell 做别的。
- 截图/取窗口：窗口逻辑尺寸 × dpr 1.5 才是物理像素，`CopyFromScreen` 在窗口超出屏幕时抓不全，用 `PrintWindow(..., PW_RENDERFULLCONTENT)`，且先 `SetProcessDPIAware()`。
- **合成鼠标输入进不了窗口**（`mouse_event` 无反应、滚轮不滚）→ 改用 `PostMessage(hwnd, 0x0200/0x0201/0x0202, ...)` 发 `WM_MOUSEMOVE` / `WM_LBUTTONDOWN` / `WM_LBUTTONUP`，`lParam = (y<<16)|x` 用**客户区像素**（客户区比截图小：截图含窗框与标题栏）。

**路径 / 文件**

- `fs::path` 从 `std::string` 构造时按**当前 ANSI 代码页**解释，不是 UTF-8。中文文件名必须经 `u8Path()` / `u8String()`。
- 要进库的路径必须 **`fs::absolute()`**。`recursive_directory_iterator` 给的是相对路径，存进库以后换个工作目录就解不出来了（症状：列表能显示、一播就 `Failed`）。
- 测试的临时目录放工程内的 `.test_tmp/`，不要用 `%TEMP%`（沙箱可能拒绝写入，症状是 SQLite 报 `unable to open database file`）。
- 判断目录能不能写，要真的写一个探测文件再删掉——`mkpath()` 成功不代表 SQLite 打得开。

---

## 3. 改完必须做的事

```bash
xmake build sonora_tests && ./build/windows/x64/debug/sonora_tests.exe   # sonora_tests 默认不编
python xmake/checks/boundaries.py
```

再加一条：动过 `.frag` → `xmake qsb`；动过界面文案 → `subset_fonts.py`；动过 `presentation/qml/` → 真的把窗口跑起来看一眼（QML 的错误大多只在运行时现形）。
要听声音又手边没有音频文件：`python tools/make_test_audio.py` 会往 `tools/audio/` 合成几段正弦 WAV（`.demo/music/` 里那批 `.flac` 是空壳，只能用来测列表）。

---

## 4. 目录 / target 速查

| 路径 | 是什么 | target |
| --- | --- | --- |
| `modules/kernel/` | TypedId / Result / 总线 / 端口原语，零外部依赖 | `Sonora.Kernel` |
| `modules/contracts/` | 跨模块集成事件与共享 DTO | `Sonora.Contracts` |
| `modules/<m>/contracts/` | 本模块对外的命令 / 查询 / DTO | `Sonora.<M>.Contracts` |
| `modules/<m>/core/` | 领域 + 应用层，**不链 Qt** | `Sonora.<M>.Core` |
| `modules/<m>/adapters/` | SQLite / 文件系统等实现，默认也不链 Qt | `Sonora.<M>.Adapters` |
| `platform/storage/` | SQLite 封装 + Migrator + 工作单元 | `Sonora.Platform.Storage` |
| `platform/{audio,filesystem,net,win}/` | 平台能力；`net` 是全项目**唯一**出现 QtNetwork 的地方 | `Sonora.Platform.*` |
| `presentation/` | ViewModel（C++）+ QML | `Sonora.Presentation` |
| `host/main.cpp` | **组合根**：唯一一处把实现 new 出来并接线的地方 | `sonora` |
| `tests/` | doctest；只链纯 C++ target | `sonora_tests` |
| `third_party/sqlite3/` | amalgamation，编译开关见 `xmake/targets.lua` | `Sonora.ThirdParty.Sqlite` |

业务模块：`catalog` / `playback` / `playlists` / `lyrics` / `preferences` / `online`（**无聚合**，只有端口 + 流程）。新增模块 = 三个 target（Contracts / Core / Adapters）+ 在 `xmake/targets.lua` 的 `BUSINESS` 表里加一个名字。

---

## 5. 这份文件怎么维护

只加两类东西：**"新踩的、会让下一个人浪费半小时的坑"**，和**"破了会伤架构的规矩"**。

- 不写里程碑、不写步骤、不写测试数量——那些会烂，它们的位置是 docs/。
- 唯一的例外是**环境路径**（xmake portable 在哪、Qt bin 在哪）：它们不属于任何一份设计文档，重新发现一次就是半小时。
- 一条规矩如果已经能被 `boundaries.py` 拦住，就把检查写进脚本，然后在这里删掉——**机器能拦的不要靠人记**。
