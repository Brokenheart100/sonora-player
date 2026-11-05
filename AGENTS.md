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
export PATH="/c/Users/123/AppData/Local/Temp/xmake-test/xmake:$PATH"

xmake f -m debug -a x64 -y     # Qt 自动识别，不需要 --qt
xmake                          # 编全部 target
xmake r MambaMusic             # xmake 会补 Qt 的 runenv
```

直接跑 `build/windows/x64/debug/MambaMusic.exe` 会静默退出（找不到 Qt DLL），要先把 `D:/Software/Qt/6.12.0/msvc2022_64/bin` 放进 PATH。

---

## 1. 不能破的规矩

带 ● 的有机器兜底，破了会编译失败或脚本报错；没带 ● 的靠自觉，破了不会立刻出事，但会慢慢烂掉。

1. ● `modules/*/core/`、`modules/*/contracts/`、`platform/*`（除 `win`）、`Mamba.Kernel` 里**不许出现 Qt / SQLite / FFmpeg / Win32 / pthread**。想用说明放错了层。
2. ● 不许 `#include "../..."` 跨 target 偷头文件——可见性由 target 依赖决定，不是由相对路径决定。
3. ● `Mamba.Presentation` 只依赖各模块的 `*.Contracts`，**永不依赖任何 `*.Core`**。这是"表现层看不见领域模型"的物理保证。
4. ● **依赖倒置 = 抽象归高层**。端口（接口）声明在 `core/.../ports/` 或 `Kernel` 里，`adapters/` 去实现它。**`adapters/` 里不许定义被 `core/` 依赖的接口**——包括"为了方便"而转发的纯虚基类。一句话判据：**把 `adapters/` 和 `platform/` 删掉，`X.Core` 还编译得过吗**。生产代码里只有 `host/` 能 `new` 具体适配器。（include 方向已由 `boundaries.py` 拦；**接口住在哪一侧机器拦不住，只能靠人守**。这是唯一一条破了还能编译通过的规矩。）
5. 模块之间只认对方的 `*.Contracts`；跨模块**不建外键、不 JOIN**（模块内可以）。
6. Handler 同步耗时 **< 5ms**；更慢的活投 `ITaskQueue::post()`，不要就地等。
7. **先 commit 事务，再发领域事件**。不做 outbox、不做最终一致。
8. 一个命令一个 `UnitOfWorkScope`；单个文件失败只计数，不回滚整批。
9. ViewModel 只持 DTO + 视图状态，**不持聚合**。QML 只认注入的 `Library` / `Player` / `Settings` / `Sfx`。
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
- `MambaMusic` 的 `after_build` 会把 `av*.dll` / `sw*.dll` 拷到 exe 旁边。缺这一步的症状是"双击立刻退出、没有任何输出"（不是崩溃，是 DLL 找不到）。
- **`load()` 里不许同步 open 文件**：`avformat_open_input` + 建 `QAudioSink` 实测 225ms，会撞上 5ms 的命令延迟断言。现在 `FfmpegOutput::load()` 只登记路径并起线程，解码线程 open、GUI 线程建 sink。
- 位置一律"样本记账"：`(seekStartSamples + written - ring.availableToRead()) * 1000 / rate`，最后 clamp 到 `durationMs`。**别把样本和毫秒分开写**——写成两段会让跳转后位置显示成 `45:28 / 2:02`。
- 跳转精度：`av_seek_frame(..., AVSEEK_FLAG_BACKWARD)` 只落到关键帧附近，能早几十秒。用 `avformat_seek_file(fmt, stream, INT64_MIN, ts, INT64_MAX, 0)`，再按首帧 `pts` 把多出来的样本丢掉。
- 队列播完必须显式 `output_.stop()`。只打日志不停输出，界面上的按钮会永远停在「暂停」。

**QML / shader**

- QML 要用注入的单例，文件头必须有 **`import MambaMusic 1.0`**，否则 `Library` 是 `undefined` 且**不报任何错**，绑定静静变成空串。
- `ShaderEffect` 的 uniform 成员名与 QML 属性名**必须逐字相同**。不同名不报错，只是整个 UBO 为 0——表现为整片纯黑。
- `ShaderEffect` **没有 `live` 属性**（那是 `ShaderEffectSource` 的），写上去整个组件加载失败。
- 改完 `.frag` 要重烘焙：`PATH=".../Qt/6.12.0/msvc2022_64/bin:$PATH" xmake qsb`。xmake 的 task 沙箱里**没有 `pcall` / `import` / `catch`**，`qsb.exe` 只能从 `QTDIR` 或 PATH 找。
- 加了新的界面文案，要重跑 `python tools/subset_fonts.py`，否则新字掉回系统字体（视觉上很明显）。

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
xmake build mamba_tests && ./build/windows/x64/debug/mamba_tests.exe   # mamba_tests 默认不编
python xmake/checks/boundaries.py
```

再加一条：动过 `.frag` → `xmake qsb`；动过界面文案 → `subset_fonts.py`；动过 `presentation/qml/` → 真的把窗口跑起来看一眼（QML 的错误大多只在运行时现形）。
要听声音又手边没有音频文件：`python tools/make_test_audio.py` 会往 `tools/audio/` 合成几段正弦 WAV（`.demo/music/` 里那批 `.flac` 是空壳，只能用来测列表）。

---

## 4. 目录 / target 速查

| 路径 | 是什么 | target |
| --- | --- | --- |
| `modules/kernel/` | TypedId / Result / 总线 / 端口原语，零外部依赖 | `Mamba.Kernel` |
| `modules/contracts/` | 跨模块集成事件与共享 DTO | `Mamba.Contracts` |
| `modules/<m>/contracts/` | 本模块对外的命令 / 查询 / DTO | `Mamba.<M>.Contracts` |
| `modules/<m>/core/` | 领域 + 应用层，**不链 Qt** | `Mamba.<M>.Core` |
| `modules/<m>/adapters/` | SQLite / 文件系统等实现，默认也不链 Qt | `Mamba.<M>.Adapters` |
| `platform/storage/` | SQLite 封装 + Migrator + 工作单元 | `Mamba.Platform.Storage` |
| `platform/{audio,filesystem,win}/` | 平台能力 | `Mamba.Platform.*` |
| `presentation/` | ViewModel（C++）+ QML | `Mamba.Presentation` |
| `host/main.cpp` | **组合根**：唯一一处把实现 new 出来并接线的地方 | `MambaMusic` |
| `tests/` | doctest；只链纯 C++ target | `mamba_tests` |
| `third_party/sqlite3/` | amalgamation，编译开关见 `xmake/targets.lua` | `Mamba.ThirdParty.Sqlite` |

业务模块：`catalog` / `playback` / `playlists` / `lyrics` / `preferences`。新增模块 = 三个 target（Contracts / Core / Adapters）+ 在 `xmake/targets.lua` 的 `BUSINESS` 表里加一个名字。

---

## 5. 这份文件怎么维护

只加两类东西：**"新踩的、会让下一个人浪费半小时的坑"**，和**"破了会伤架构的规矩"**。

- 不写里程碑、不写步骤、不写测试数量——那些会烂，它们的位置是 docs/。
- 唯一的例外是**环境路径**（xmake portable 在哪、Qt bin 在哪）：它们不属于任何一份设计文档，重新发现一次就是半小时。
- 一条规矩如果已经能被 `boundaries.py` 拦住，就把检查写进脚本，然后在这里删掉——**机器能拦的不要靠人记**。
