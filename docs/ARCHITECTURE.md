# Sonora 架构设计

> **栈**：Qt 6.12 (MSVC；实测 VS 2026) · C++23 · QML · FFmpeg · TagLib · SQLite · xmake · Windows x64 only
> **架构**：Clean Architecture（同心圆）+ 严格 DDD（战术模式）+ CQRS（通道分离）+ MVVM（表现层）+ 模块化单体（Modular Monolith）
> **边界**：模块边界由 **xmake 目标图强制**，不靠自觉
>
> 实现顺序见 [实现步骤](IMPLEMENTATION.md)。

文档状态：**v2**。v1 是"分层目录 + 约定"式设计，已被本版取代。

---

## 0. 定位与范围

| 项           | 结论                                                                                        |
| ------------ | ------------------------------------------------------------------------------------------- |
| 产品         | 本地曲库为主的高性能桌面音乐播放器，视觉为漫画分格风格                                      |
| 平台         | **Windows 10 1809+ / Windows 11，x64**。不交叉编译，不做 Linux/macOS                  |
| 工具链       | **MSVC**（`/MD` 动态 CRT；本机是 VS 2026）+ Qt 6.12 `msvc2022_64` + xmake         |
| 架构         | Clean Architecture 同心圆；Domain 零外部依赖；CQRS 读写通道分离；MVVM 表现层                |
| 组织         | 模块化单体：**6 个业务模块 + 4 个横切组件**，单进程、单可执行文件、单 SQLite 库       |
| 差异化       | 漫画风**组件化**（分格/网点/拟声词/翻页），而非皮肤换色                               |
| 最大技术风险 | ① C++ 里保持模块边界不腐化 ② FFmpeg 在 MSVC 下的依赖获取 ③ 严格 DDD 对万级曲库的写入性能 |

> 具体用哪些库、用在哪一层、为什么不用别的，见 **[DEPENDENCIES.md](DEPENDENCIES.md)**。本文只讲结构与决策。

### 非目标（v1 明确不做）

| 不做                                   | 原因                                                                                                                                 |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| 在线音源（网易云/酷狗/B 站/QQ 音乐）   | v1 不做；**v1.1 已由 `online` 模块实现**（见 [ONLINE.md](ONLINE.md)）。注意代码里**从来没有** `IMediaSource` 接缝，不要假装它存在。它要背账号、扫码、降级与接口失效维护，所以另立一份排期，不塞进本地曲库 |
| **Event Sourcing / Event Store** | 本机单用户、无审计需求、无重放需求。上了它=每条记录额外一次序列化和一次重放成本，换不到任何东西。CQRS 只要通道分离就够了（见 §5.1） |
| 微服务 / 进程拆分                      | 本地播放器拆进程只增加 IPC 延迟和部署复杂度                                                                                          |
| 插件系统                               | 内核未稳定时插件契约 = 锁死错误设计                                                                                                  |
| 跨模块数据库外键与 JOIN                | 模块化单体的核心纪律，破坏它等于回到大泥球（见 §6.4）                                                                               |
| 桌面歌词 / 迷你窗 / 移动端 / Web       | 各自都是独立小项目                                                                                                                   |

### 命名（2026-11 由 MambaMusic 改名为 Sonora）

| 用途             | 名称                            | 落在哪                                                                                                           |
| ---------------- | ------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| 产品名           | Sonora                          | 文档、`AGENTS.md`、版本信息                                                                                    |
| GitHub 仓库      | `sonora-player`               | remote                                                                                                           |
| 构建工程         | `Sonora`                      | `xmake.lua` 的 `set_project("Sonora")`                                                                       |
| C++ 命名空间     | `Sonora`（小写 `sonora::`） | 所有源码；头文件根目录 `include/sonora/`                                                                       |
| 可执行程序       | `sonora`                      | `xmake/targets.lua` 的 `target("sonora")` → `sonora.exe`                                                  |
| QML 模块         | `Sonora.Presentation`         | `qmlRegisterSingletonInstance("Sonora.Presentation", …)` + 每个 QML 文件的 `import Sonora.Presentation 1.0` |
| Windows 显示名称 | Sonora Music                    | `setApplicationDisplayName()` + `assets/app.rc` 的 `ProductName`                                           |

改名的连带效应（都属于预期）：SQLite 库名变成 `sonora.sqlite`，`QStandardPaths` 的数据目录随应用名变成 `<AppData>/Sonora/...`，所以**老的 `mamba.sqlite` 不会被自动迁移**——开发机换名前把库搬过去或重新扫描即可。

---

## 1. 参考项目：抄机制，不抄结构

[QueMusic](https://github.com/BroNekoX/QueMusic) 的**机制**值得抄，**结构**不值得抄——它是 CMake + 按技术类型平铺目录（`cpp/audio`、`cpp/playback`、`api/`、`components/`），没有业务边界，业务逻辑和 Qt 类型混在一起。我们采用完全不同的组织方式。

| 机制                                                                                   | 来源                 | 我们的决策                                                                      |
| -------------------------------------------------------------------------------------- | -------------------- | ------------------------------------------------------------------------------- |
| `FfmpegDecoder → AudioRing(SPSC) → QAudioSink` 拉模式                              | 抄                   | 放进 `platform/audio` adapter，实现 Playback 模块定义的 `IAudioOutput` 端口 |
| 状态语义对齐 Qt Multimedia（`PlaybackState` / `MediaStatus`）                      | 抄                   | 作为 `IAudioOutput` 的契约值，别处不再重复定义                                |
| 计算 / IO 全下沉工作线程，主线程只跑绑定                                               | 抄                   | 与 CQRS 天然契合：命令在 UI 线程发起、在 IO 池执行                              |
| QML 少嵌套、定位优先级 `x/y/wh → anchors → Row/Column → Layouts`、页面按需 Loader | 抄                   | Presentation 层直接采用                                                         |
| shader 合并（取样→模糊→提饱和→圆角裁剪合进一个 frag）                               | 抄                   | 漫画网点/集中线同样走单 shader                                                  |
| 按技术类型平铺的目录结构                                                               | **不抄**       | 我们要的是按业务模块切分（§6）                                                 |
| 业务逻辑写在 QML/Model 里                                                              | **不抄**       | 万恶之源。我们的 QML 只允许写布局与动画                                         |
| 插件系统 / 在线音源层                                                                  | **不抄（v1）** | 见非目标                                                                        |
| CMake Presets + justfile                                                               | 换成 xmake           | 用户指定                                                                        |

---

## 2. 架构总览

### 2.1 Clean Architecture 同心圆

```
        ┌───────────────────────────────────────────────────────────┐
        │  Host（组合根）                                            │
        │  唯一知道所有实现的地方：装配、注入、生命周期              │
        │  ┌─────────────────────────────────────────────────────┐  │
        │  │  Presentation（MVVM）                                │  │
        │  │  View = QML ；ViewModel = QObject                     │  │
        │  │  只依赖各模块的 *.Contracts，看不到 Domain            │  │
        │  └──────────────────────▲──────────────────────────────┘  │
        │                         │ ICommandBus / IQueryBus / 端口   │
        │  ┌──────────────────────┴──────────────────────────────┐  │
        │  │  Application（每个模块一份）                          │  │
        │  │  CommandHandler / QueryHandler / 端口声明            │  │
        │  └──────────────────────▲──────────────────────────────┘  │
        │                         │ 聚合、值对象、领域事件          │
        │  ┌──────────────────────┴──────────────────────────────┐  │
        │  │  Domain（每个模块一份，纯 C++，零外部依赖）           │  │
        │  └──────────────────────▲──────────────────────────────┘  │
        │                         │ 实现端口                       │
        │  ┌──────────────────────┴──────────────────────────────┐  │
        │  │  Adapters（Infrastructure）                           │  │
        │  │  SQLite / TagLib / FFmpeg / Win32 / 文件系统          │  │
        │  └─────────────────────────────────────────────────────┘  │
        └───────────────────────────────────────────────────────────┘
```

依赖方向**永远指向内圈**。Adapters 和 Presentation 互不认识——这一点很关键：它们在 v1 里完全解耦，所以"把引擎从 QMediaPlayer 换成 FFmpeg"不会触碰任何一行 ViewModel。

### 2.2 模块化单体的组成

**业务模块（7）**：每个模块有自己的领域模型、应用层、适配器、契约。`Online` 是 v1.1 加的，**刻意不建聚合**。

| 模块            | 类型     | 聚合根                         | 职责                                                                                                                      |
| --------------- | -------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------- |
| `Kernel`      | 共享内核 | —                             | `TypedId` / `Result` / `IDomainEvent` / `ICommandBus` / `IQueryBus` / `IClock` / `ITaskQueue` / `ILogger` |
| `Catalog`     | 核心域   | `Track` `Album` `Artist` | 曲库事实：曲目元数据、专辑与艺人、导入与增量扫描                                                                          |
| `Playback`    | 核心域   | `PlayQueue`                  | 队列、播放模式、随机历史、上一首语义、播放事件                                                                            |
| `Playlists`   | 核心域   | `Playlist`                   | 用户歌单与排序                                                                                                            |
| `Lyrics`      | 支撑域   | `Lyrics`                     | LRC 解析、时间轴、逐字                                                                                                    |
| `Preferences` | 支撑域   | —（无聚合，纯 CRUD）          | 设置项。**故意不套 DDD**——键值对没有不变量也没有行为，套上去只有样板代码                                          |
| `Online` | 支撑域 | —（**刻意不建聚合**） | 在线搜索、直连播放、登录。数据全是「别人数据的本地副本 + 会过期的播放地址」，没有需要保护的不变量；失败必须可降级、可诊断 |

**横切组件（4）**：

| 组件                        | 是什么                                                                                                                    | 为什么单独存在                                   |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| `Sonora.Contracts`        | 跨模块集成事件 + 共享 DTO（`TrackRefDto` 等）                                                                           | 模块间唯一的通信媒介                             |
| `Sonora.Platform.Storage` | 自带 SQLite amalgamation（FTS5/JSON/WAL 编译开关固定）、基于 SQLiteCpp 的连接与语句封装、`IUnitOfWork` 实现、迁移执行器 | 被多个模块的适配器复用，但不是任何模块的领域概念 |
| `Sonora.Platform.Audio`   | `IAudioOutput` 的两个实现（QtMultimedia / FFmpeg）+ `AudioRing`                                                       | 技术能力，没有业务语义                           |
| `Presentation` + `Host` | MVVM 与组合根                                                                                                             | 见 §2.3 与 §4.4                                |

### 2.3 合法依赖边（**这是本架构的宪法**）

| 从 ＼ 到                                   | Kernel | `Sonora.Contracts` | `X.Contracts`                   | `X.Core`   | `X.Adapters` | Presentation | 外部库            |
| ------------------------------------------ | ------ | -------------------- | --------------------------------- | ------------ | -------------- | ------------ | ----------------- |
| `Kernel`                                 | —     | ✗                   | ✗                                | ✗           | ✗             | ✗           | ✗                |
| `Sonora.Contracts`                       | ✓     | —                   | ✗                                | ✗           | ✗             | ✗           | ✗                |
| `X.Contracts`                            | ✓     | ✓                   | —                                | ✗           | ✗             | ✗           | ✗                |
| **`X.Core`**（Domain+Application） | ✓     | ✓                   | 自己的 ✓ / **别的模块 ✗** | —           | ✗             | ✗           | **✗ 全部** |
| `X.Adapters`                             | ✓     | ✓                   | ✓                                | ✓           | —             | ✗           | ✓                |
| `Presentation`                           | ✓     | ✓                   | **所有模块的 ✓**           | **✗** | **✗**   | —           | Qt Core/Gui/Quick |
| `Host`                                   | ✓     | ✓                   | ✓                                | ✓           | ✓             | ✓           | ✓                |

三条要点：

1. **`X.Core` 不依赖任何外部库**。它连 Qt 都不链接。所以 `#include <QString>` 在里面是编译错误，不是 review 意见。
2. **模块之间只能通过对方的 `*.Contracts`**。`Catalog.Core` 不能 include `Playback.Core` 的任何头文件。
3. **Presentation 看不到 Domain**。ViewModel 只拿到 DTO，拿不到聚合，从物理上杜绝"在 View 里改聚合"。

4. **platform 可以实现某个模块的传输契约**（先例是 `Sonora.Platform.Audio` 实现 Playback 的 `IAudioOutput`）：`Sonora.Platform.Net` 实现 `Online.Contracts` 里的 `IHttpTransport`，所以 `Platform.Net → Online.Contracts` 合法；`Presentation → Online.Contracts` 本来就在允许集合里。反方向不成立：`Online.Core` 不许 include `platform/net/` 的任何头。

### 2.4 为什么在 C++ 里必须用目标图强制

Java / .NET 的 Clean Architecture 靠包名 + 分层目录，靠 review 维持；半年后必然出现 `Domain` 里 `import org.springframework.*`。

C++ 有一个别人没有的礼物：**头文件路径是构建系统给的**。所以：

- `X.Core` 目标**不 `add_deps` 任何带 Qt 的目标** → Qt 头文件不在 include 路径上 → 领域层污染直接编译失败。
- `Presentation` 目标**不 `add_deps` `*.Core`** → `#include <sonora/catalog/domain/Track.h>` 找不到文件。
- 跨模块只能 `add_deps(...Contracts)` → 想 include 别人聚合？编译失败。

剩下的靠一个 12 行的 CI 脚本兜底（§6.5）：禁止 `../` 跨 target 相对 include，禁止 `core/` 里出现 `Q[A-Z]`、`sqlite3.h`、`libav*`、`#include <windows.h>`。

> 代价：首次搭骨架要配 ~30 个 target，约 1.5 人日。收益：后面 30 天不需要任何人（包括三个月后的你自己）去检查边界有没有破。

### 2.5 依赖倒置：唯一一条"破了还能编译通过"的规矩

上面那些边界都能被 target 图拦下，只有一条拦不住：**把接口放错边**。

依赖倒置的准确含义不是"依赖抽象"，而是**抽象归高层所有**：

- 接口声明在**调用方**那一侧（`modules/<m>/core/include/.../ports/` 或 `Kernel`）；
- 实现方（`adapters/`、`platform/`）include 这个接口并实现它，依赖箭头指向高层；
- 只有**组合根**（`host/main.cpp`）同时认识抽象与实现，并由它决定注入哪一个。

**为什么它拦不住**：`adapters` 本来就依赖 `core`。如果有人在 `adapters/` 里定义 `ITrackRepository`、让 `core` 反过来 include 它，编译器不会说一个字——头文件路径恰好可见，链接也通。这是本架构里唯一一种"能过编译、能过边界脚本、但依赖方向已经反了"的破坏。半年后的症状：换一种存储实现要改领域层。

**可执行的判据**（比任何 review 都准）：

> 把 `modules/<m>/adapters/` 和 `platform/` 整个删掉，`X.Core` 还能不能编译通过？
> **能 → 方向是对的；不能 → 有实现细节住进了高层。**

**我们要的样子**（M1 已落地）：

```
modules/catalog/core/include/sonora/catalog/core/ports/Ports.h   ← 接口住在这里（高层定义）
modules/catalog/adapters/src/SqliteTrackRepository.cpp          ← 实现它（低层依赖高层）
host/main.cpp                                                   ← 唯一 new 实现的地方
```

**三条禁令**：

1. `adapters/` 里不许定义被 `core/` 依赖的接口——包括"为了方便"而转发的纯虚基类。
2. `core/`、`contracts/`、`kernel/` 的 `#include` 里不许出现 `adapters/` 或 `platform/`（`boundaries.py` 已拦这一条）。
3. **生产代码**里只有 `host/` 能 `new` 一个具体适配器。测试里允许——那只是测试在扮演自己的组合根。

> 为什么单列一节：另外几条规矩违反了会立刻报错，违背这一条却一切正常。**只有它会安静地烂掉。**

---

## 3. 分层的职责与禁令

### 3.1 Domain（严格 DDD）

**放**：聚合根、实体、值对象、领域事件、领域服务、仓储**接口**（端口）。
**只允许 include**：`<string> <vector> <chrono> <optional> <memory> <cstdint>`、`Kernel`、`X.Contracts`。

| 允许                                                                                      | 禁止                                                      |
| ----------------------------------------------------------------------------------------- | --------------------------------------------------------- |
| 用 `TrackId`、`Duration`、`PlayMode` 这类值对象做参数                               | 用 `QString` / `QDateTime` / `QUuid` 做参数或成员   |
| 聚合内部保证不变量，构造失败返回 `Result`                                               | 在聚合里查数据库、读文件、发网络请求                      |
| 抛出**唯一一种**异常：`InvariantViolation`（编程错误），业务失败一律走 `Result` | 用异常做业务流程控制                                      |
| 领域事件记录"已经发生了什么"                                                              | 领域事件带 `QObject*`、带 UI 意图（"显示弹窗"）、带指针 |
| 一个事务 = 一个聚合                                                                       | 一个事务改多个聚合根                                      |

**贫血 vs 充血的判定线**：如果某个方法只是 `setX()`，它是 DTO，不该进 Domain。`Track` 有没有行为？有——`Track::rename(const std::u8string&)` 要校验空标题、要发 `TrackRenamed`；`Album::assignDiscOrder()` 要处理碟号/轨号冲突。写不出行为说明这个"聚合"其实是查询结果，应该做成 DTO。

### 3.2 Application（CQRS 的落脚点）

**放**：Command、Query、Handler、DTO 的组装、事务边界、集成事件的发布、耗时任务的编排。

| 允许                                                           | 禁止                                          |
| -------------------------------------------------------------- | --------------------------------------------- |
| 加载聚合 → 调领域方法 → 提交 UoW → 发事件                   | 在 Handler 里写 SQL（那是 Adapters 的事）     |
| 通过端口（`ITrackRepository`、`IAudioOutput`）操作外部世界 | 直接 `#include <sqlite3.h>` 或调用 `av_*` |
| 把耗时工作投递到 `ITaskQueue`                                | 在 Handler 里 `sleep`、同步等 IO 超过 5ms   |
| 返回 `Result<T>`，用错误码枚举表达失败                       | 每个 Handler 自己造一套错误字符串             |

**Handler 的硬性时间预算：5ms**。这是 UI 线程同步调用命令的上限。超了就必须投递到执行器并立刻返回"已受理"。超过 5ms 的 Handler 会在 `M2` 上线前被一个 debug 断言抓出来（`SONORA_ASSERT_SLOW_COMMAND`）。

### 3.3 Adapters（Infrastructure）

**放**：仓储实现、读模型映射器、TagLib 封装、FFmpeg 解码、SQLite 事务、文件系统、Win32 API。
**这是唯一允许 `#include <Q*>`、`<windows.h>`、`<libav*>`、`<sqlite3.h>` 的地方**（另有 Presentation 允许 Qt）。

职责是把端口翻译成技术调用，**不含业务判断**。如果适配器里出现 `if (album == "Various Artists")`，说明业务规则漏到了外圈，把它移回 Domain。

### 3.4 Presentation（MVVM + QML）

| 层            | 是什么                                        | 只做                                                                       |
| ------------- | --------------------------------------------- | -------------------------------------------------------------------------- |
| View          | `presentation/qml/**`                       | 布局、绑定、动画。**零业务**：不排序、不过滤、不拼字符串、不算时长   |
| ViewModel     | `presentation/viewmodels/**`（`QObject`） | 持有 View 状态（选中项、错误文案、加载中）+ 把 DTO 映射成显示属性 + 发命令 |
| Model（列表） | `presentation/viewmodels/models/**`         | `QAbstractListModel` 适配器，喂 DTO 分页，**不打数据库**           |

**五条 MVVM 规矩**：

1. ViewModel 只依赖 `ICommandBus` / `IQueryBus` / 各模块 `*.Contracts` / `Kernel`。
2. **单向数据流**：ViewModel → View 靠属性通知；View → ViewModel 靠**显式命令**（`Q_INVOKABLE` 或 `Command` 对象）。禁止 QML 里双向绑定回写业务状态。
3. ViewModel **不持有聚合**，只持有 DTO 和视图状态。想改数据 → 发命令 → 重新查询 → 属性变化。
4. ViewModel 里**不出现** `QSqlQuery`、`QFile`、`std::filesystem`、`libav*`。
5. 错误统一：Handler 返回 `Result` → ViewModel 把 `error.code` 映射成 `errorText` 属性 → View 显示。**不跨层抛异常，不弹 QMessageBox**。

QML 侧只认四个注入的单例：`Player`、`Library`、`Settings`、`Sfx`，每个背后是一个 ViewModel。QML 里出现 `Catalog.importFolder(...)` 这种跨层直呼就是错的。

### 3.5 Host（组合根）

全项目**唯一**允许知道"哪个接口由谁实现"的地方。

- `host/main.cpp`：创建 `QGuiApplication`、设置 DPI 策略、加载 QML。
- `host/Bootstrap.cpp`：创建具体的 Adapters，注册 Handler，构造 ViewModel，注入到 QML 上下文。
- `host/Shutdown.cpp`：按逆序拆除（§7.4）。
- **任何模块内部都不允许出现 `new SqliteTrackRepository(...)`**——依赖注入只发生在 Host。

---

## 4. 领域模型

### 4.1 上下文划分与边界理由

| 模块            | 为什么单独成一个上下文                   | 如果合并会怎样                             |
| --------------- | ---------------------------------------- | ------------------------------------------ |
| `Catalog`     | "曲库有什么"是客观事实，与用户行为无关   | —                                         |
| `Playback`    | 关注"现在放什么、怎么放"，与曲库内容解耦 | 曲库扫描时播放状态被牵连，随机历史无处安放 |
| `Playlists`   | 用户的主观组织，生命周期独立于曲目       | 删歌单 vs 删曲目的事务互相纠缠             |
| `Lyrics`      | 独立的解析规则与第三方格式               | 歌词格式变更迫使曲库迁移                   |
| `Preferences` | 无行为，纯键值                           | 不值得套 DDD（见 §6.2 的例外条款）        |
| `Online` | 「外部世界有什么」与本地曲库无关，失败模式（断网/限流/接口变更）也不该污染曲库 | 在线接口一抖，整个曲库页跟着报错 |

**`Catalog` 和 `Playlists` 为什么分开**：曲目是"事实"，歌单是"用户的看法"。删掉一首歌，歌单里对应的条目要变成"不可用占位"而不是跟着消失——这是两个模块各自的不变量，合并会逼出一个既不属于曲库也不属于歌单的畸形规则。

### 4.2 聚合清单（事务边界与不变量）

| 聚合根        | 不变量                                                                                                                            | 规模上限              | 说明                                                                         |
| ------------- | --------------------------------------------------------------------------------------------------------------------------------- | --------------------- | ---------------------------------------------------------------------------- |
| `Track`     | 路径已规范化且非空；`duration >= 0`；标题为空时回落文件名；同一路径全局唯一                                                     | 1                     | **一轨一事务**。曲目数量大，绝不能聚合成大对象                         |
| `Album`     | 同一碟内 `trackNo` 唯一；`VariousArtists` 时 `artist` 为空而 `trackArtist` 必填；封面 `coverKey` 一旦确定不再随单曲变化 | 1                     | 提供 `assignDiscOrder()` 处理冲突                                          |
| `Artist`    | 名称规范化后唯一                                                                                                                  | 1                     | 近乎值对象，但有独立身份（改名要传播），所以仍是实体                         |
| `PlayQueue` | 游标永远指向有效元素或空队列；`RepeatOne` 不清历史；随机模式用预生成排列，`back()` 能精确回溯；模式切换不摧毁历史             | 1（含 N 个 track id） | **整个项目最值得建成聚合的地方**，随机/重复/上一首的交互组合是真复杂度 |
| `Playlist`  | 名非空且长度 ≤ 64；成员不重复；`pos` 连续无空洞（重排时整体重编号）                                                            | 1                     | 顺序是不变量的一部分，不能靠 `ORDER BY` 事后补救                           |
| `Lyrics`    | 时间戳单调不减；`offset` 已应用；空行不允许进时间轴                                                                             | 1                     | 解析后立刻校验，非法行丢弃而不是抛错                                         |

**`Online` 不在上表里**：它没有聚合根。缓存是「别人数据的副本」，会话是「一次登录的凭证」，播放地址是「20 分钟就过期的字符串」——三者都没有需要保护的不变量，所以它只提供 Query 契约 + 一条 `OnlineService` 流程（见 §4.5 与附 A）。

### 4.3 值对象

`TrackId` `AlbumId` `ArtistId` `PlaylistId`（`TypedId<Tag>` 模板提供类型安全，避免 int 互串）、`TrackPath`（规范化、大小写、长路径前缀处理）、`Duration`（毫秒 + 格式化）、`PlaybackPosition`、`PlayMode`、`CoverKey`、`LrcStamp`、`NormalizedTitle`（小写 + 全半角归一，供搜索排序）。

**规则**：值对象一律不可变、按值传递、互相比较靠 `==`。**两个值对象相等当且仅当所有字段相等**——`TrackPath` 不要偷偷只比文件大小。

### 4.4 领域事件

| 事件                                                    | 发布者        | 订阅者（同模块 / 跨模块）                                                 |
| ------------------------------------------------------- | ------------- | ------------------------------------------------------------------------- |
| `TrackImported` / `TrackRenamed` / `TrackRemoved` | `Catalog`   | 同模块更新读模型；跨模块 → `Lyrics`（清理）、`Playlists`（标记失效） |
| `TrackStarted` / `TrackFinished`                    | `Playback`  | 跨模块 → `Catalog`（播放统计）、`Preferences`（恢复点）              |
| `QueueAdvanced` / `QueueModeChanged`                | `Playback`  | 同模块读模型                                                              |
| `PlaylistCreated` / `PlaylistMembershipChanged`     | `Playlists` | 同模块读模型                                                              |
| `LyricsReplaced`                                      | `Lyrics`    | 同模块                                                                    |

事件的**载荷只能是值对象和 ID**，且**跨模块的集成事件只带原始字段（`int64 trackId` 等）**，不带对方的值对象类型——否则两个模块的 Contracts 会互相引用成环。

### 4.5 明确**不**建的东西

| 不做                                       | 理由                                                                                                                                                     |
| ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **一个 `Library` 聚合管所有曲目**  | 一万首 = 一次事务写一万个实体 + 内存常驻。这是最典型的"教科书式聚合"灾难。曲库是**查询视图**，不是聚合                                             |
| **把 `PlaybackPosition` 放进聚合** | 60Hz 更新、每次都要事务。位置是**高频读模型**，走旁路端口（§7.1）                                                                                 |
| **`PlaySession` 聚合**             | 播放状态（播放/暂停/位置）是易失状态，持久化只有"恢复点"一个需求，交给 `Preferences` 存一个 `{trackId, positionMs}` 就够                             |
| **领域服务滥用**                     | 全项目只允许两个：`TrackFactory`（从文件事实造 `Track`，处理标题回落与路径规范化）、`ShufflePlanner`（生成随机排列）。找不到第三个正当理由就不要建 |
| **`Online` 建聚合** | 缓存、会话、播放地址都没有不变量；建聚合只会把「接口换了怎么办」变成领域问题。它只需要 Query 契约 + 一条流程 + 一个诊断快照 |
| **仓储返回查询投影**                 | 仓储只进出聚合。列表/搜索结果走 Query 侧 DTO，别让 `ITrackRepository::findAll()` 返回 10000 个聚合                                                     |

---

## 5. CQRS

### 5.1 粒度选择：只做通道分离

| 方案                         | 内容                                                            | 决策                    |
| ---------------------------- | --------------------------------------------------------------- | ----------------------- |
| 轻量（同模型）               | 读写走不同 Handler，但共用一个领域模型和一个数据库              | ✗ 没解决万级列表的问题 |
| **通道分离（本方案）** | 写侧走聚合 + 事务；读侧走**投影表 + DTO**，完全不加载聚合 | ✓ **选它**       |
| 事件溯源                     | 状态由事件流重建                                                | ✗ 见非目标             |

**为什么必须做投影**：曲库列表要显示 10000 行 "标题 · 艺人 · 专辑 · 时长 · 播放次数"。如果按 DDD 老老实实加载 10000 个 `Track` 聚合 + 关联 `Album`/`Artist`，每次进曲库页面要几百毫秒和几十 MB 内存。而 CQRS 的读侧可以直接 `SELECT` 一张已经反规范化的 `mv_track_row` 表，只构造轻量 DTO。**这是本项目里 CQRS 唯一但确凿的收益**，不是教条。

### 5.2 契约

```cpp
// modules/kernel/include/sonora/kernel/messaging.h
namespace sonora::kernel {

struct ICommand { virtual ~ICommand() = default; };
struct IQuery   { virtual ~IQuery()   = default; };

class ICommandBus {
public:
    virtual ~ICommandBus() = default;
    virtual Result<void> dispatch(ICommand&& cmd) = 0;
};

class IQueryBus {
public:
    virtual ~IQueryBus() = default;
    // 返回值类型擦除；一个 Query 一次 std::any 分配，相对 DTO 本身可忽略
    virtual Result<std::any> ask(IQuery&& q) = 0;

    template <class Q, class R = typename Q::ResultType>
    Result<R> query(Q q) {
        auto r = ask(std::move(q));
        if (!r) return std::unexpected(r.error());
        return std::any_cast<R>(std::move(*r));
    }
};

} // namespace
```

一条命令长这样（模块自己定义，放在 `X.Contracts`）：

```cpp
// modules/catalog/contracts/include/sonora/catalog/commands.h
struct ImportFolderCommand final : kernel::ICommand {
    std::wstring folder;
    bool recursive = true;
};

struct ListTrackRowsQuery final : kernel::IQuery {
    using ResultType = TrackRowPageDto;      // 见 §5.5
    std::string  filter;
    std::string  sortBy = "title";
    int          offset = 0;
    int          limit  = 200;
};
```

### 5.3 总线实现（类型擦除，20 行）

```cpp
class CommandBus final : public ICommandBus {
public:
    template <class C, class H>
    void registerHandler(H handler) {
        m_handlers.emplace(std::type_index(typeid(C)),
            [handler](ICommand& c) { return handler.handle(static_cast<C&>(c)); });
    }

    Result<void> dispatch(ICommand&& cmd) override {
        auto it = m_handlers.find(std::type_index(typeid(cmd)));
        if (it == m_handlers.end()) return std::unexpected(Error::NoHandler);
        return it->second(cmd);
    }
private:
    std::unordered_map<std::type_index, std::function<Result<void>(ICommand&)>> m_handlers;
};
```

Handler 的注册**只发生在 Host**（`Bootstrap.cpp`）。任何模块内部都不许自己往总线里塞 Handler——否则模块间开始互相认识。

### 5.4 事务与事件顺序

```
Handler::handle(ImportFolderCommand)
  ├─ 1. itaskQueue.post(...)          ← 耗时部分立刻异步化，Handler 立即返回
  └─ （后台）
       2. 遍历目录，读 TagLib 元数据 → 批量 500 条
       3. 每条：TrackFactory 造聚合（校验不变量）
       4. IUnitOfWork::begin()  →  写聚合 + 写投影  →  commit()
       5. **提交成功之后**才发布领域事件
       6. 事件同步派发给本模块读模型订阅者；跨模块转成集成事件
```

**顺序不能颠倒**：先发事件后提交，一旦提交失败，别的模块已经按"导入成功"处理过了。

**不做 outbox**：本机单进程、失败即整体回滚、无跨进程投递。引入 outbox 表换不到可靠性，只换来一张表和一套补偿逻辑。（推翻条件：如果将来出现跨进程或离线同步，再上 outbox。）

**在线缓存的写入不走 UoW**：`online_*` 四张表由 `OnlineService` 在收到响应时单条写、写完就提交，与任何聚合事务无关（最坏丢一次缓存，不会让曲库回滚）。

### 5.5 读模型

每个模块可以有自己的投影表，命名 `mv_*`，由**同模块**的事件订阅者维护。

```cpp
// 读侧 DTO：只含显示所需字段，无行为，可整体 memcpy 风格拷贝
struct TrackRowDto {
    int64_t trackId;
    std::string title;
    std::string artist;
    std::string album;
    int32_t durationMs;
    int32_t playCount;
    std::string coverKey;
    bool    hasLyrics;
};

struct TrackRowPageDto {
    std::vector<TrackRowDto> rows;
    int64_t totalCount;      // 给滚动条与"共 N 首"
};
```

ViewModel 侧 `TrackListModel` 按页（200 条）拉取并做窗口缓存：`data()` 是 O(1) 下标访问，绝不触发查询。滚动到接近边界时提前拉下一页。

---

## 6. 模块化单体

### 6.1 一个模块的内部形状

```
modules/catalog/
├── contracts/include/sonora/catalog/
│   ├── commands.h        # ImportFolderCommand / RescanCommand
│   ├── queries.h         # ListTrackRowsQuery / GetTrackDetailQuery
│   ├── dtos.h            # TrackRowDto / TrackDetailDto / TrackRowPageDto
│   ├── events.h          # CatalogIntegrationEvents（跨模块，原始字段）
│   └── ports.h           # ICatalogQuery / 供其他模块用的只读端口
├── core/                 # 纯 C++，不链接 Qt
│   ├── include/sonora/catalog/core/
│   │   ├── domain/       # Track.h Album.h Artist.h 值对象 领域事件 领域服务
│   │   ├── ports/        # ITrackRepository.h IAlbumRepository.h IMetadataReader.h
│   │   └── app/          # ImportFolderHandler.h ListTrackRowsHandler.h IUnitOfWork.h
│   └── src/
└── adapters/
    ├── include/sonora/catalog/adapters/
    └── src/              # SqliteTrackRepository.cpp TagLibMetadataReader.cpp TrackRowMapper.cpp
```

`contracts/` 极薄（只有纯数据结构和接口），`core/` 是全部业务，`adapters/` 是全部技术细节。**看一个模块只需要看这三个目录**。

### 6.2 模块间通信：三种、且只有三种

| 方式        | 形态                                                    | 例子                                          | 约束                                 |
| ----------- | ------------------------------------------------------- | --------------------------------------------- | ------------------------------------ |
| ① 同步查询 | 依赖对方的 `ICatalogQuery` 端口                       | `Lyrics` 要查曲目路径                       | 只读、无事务、返回 DTO               |
| ② 集成事件 | `Y.Contracts` 里的事件结构 + `IIntegrationEventBus` | `TrackFinished` → `Catalog` 累计播放次数 | 单向、可异步、只能带原始字段         |
| ③ 命令转发 | **不允许**                                        | —                                            | 模块 A 不得代替用户触发模块 B 的命令 |

**绝对不允许**：模块 A 直接 `#include` 模块 B 的聚合/仓储/Handler；模块 A 读模块 B 的表。

### 6.3 每个模块的 target（构建级强制）

```
Sonora.Kernel              (static, 无外部依赖)
Sonora.Contracts           (static, 无外部依赖)
Sonora.Catalog.Contracts   (static, deps: Kernel, Sonora.Contracts)
Sonora.Catalog.Core        (static, deps: 上述三者；**不链接 Qt**)
Sonora.Catalog.Adapters    (static, deps: Core, Contracts, Platform.Storage, QtCore, sqlite3, taglib)
... playback / playlists / lyrics / preferences 同构 ...
Sonora.Platform.Storage    (static, QtCore + sqlite3)
Sonora.Platform.Audio      (static, QtCore + QtMultimedia + ffmpeg)
Sonora.Presentation        (static, QtCore/Gui/Quick + 所有 *.Contracts；**不依赖任何 *.Core**)
Sonora                (binary, qt.quickapp, deps: 全部 Adapters + Presentation + Host)
```

共 30 个 target（28 个静态库 + 可执行 `sonora` / `sonora_tests`）。xmake 处理这个规模毫无压力（并行编译），首次配置约多半天。

### 6.4 数据库：schema per module

单文件 `sonora.db`，但**每个模块拥有自己的表，且不跨模块 JOIN / 不跨模块外键**。

```sql
-- 迁移版本按模块独立，互不阻塞
CREATE TABLE __module_version (
  module  TEXT PRIMARY KEY,
  version INTEGER NOT NULL
);
```

| 模块            | 表                                                                                 | 允许 JOIN 的范围                                    |
| --------------- | ---------------------------------------------------------------------------------- | --------------------------------------------------- |
| `Catalog`     | `track` `album` `artist` `track_artist` `mv_track_row` `mv_track_stat` | 模块内任意 JOIN（曲目/专辑/艺人本就是一个上下文）   |
| `Playback`    | `play_queue` `play_queue_item` `play_history`                                | 模块内；`track_id` 只是裸整数，**不建外键** |
| `Playlists`   | `playlist` `playlist_item` `mv_playlist_summary`                             | 模块内                                              |
| `Lyrics`      | `lyric` `lyric_line`                                                           | 模块内                                              |
| `Preferences` | `kv`                                                                             | —                                                  |
| `Online` | `online_search_cache` `online_detail_cache` `online_song_cache` `online_session` | 模块内；`payload` 原样存 provider 的 JSON，不拆列（拆了就跟着接口一起变） |

**为什么允许 `Catalog` 内部 JOIN 却不允许跨模块**：`track`/`album`/`artist` 属于同一个限界上下文，它们的不变量天然耦合；而 `playlist_item.track_id` 跨到 `Catalog` 是另一个上下文的身份，用外键锁死会让"删除曲目时歌单怎么办"这个业务决策被数据库替你做了。

**写读分离的落地**：写侧 `X.Core` 通过仓储改聚合表；读侧 Query Handler 直接映射 `mv_*` 表。投影由事件订阅者更新，与写在同一事务内完成（本机场景下**同步投影**比最终一致更简单也更快）。

### 6.5 边界检查（CI，12 行）

```python
# xmake/checks/boundaries.py —— 只兜机器管不到的两类
FORBIDDEN_IN_CORE = [r'\bQ[A-Z]\w+', r'#include\s*<sqlite3\.h>', r'#include\s*<libav',
                     r'#include\s*<windows\.h>', r'#include\s*<QtCore']
# 1) core/ 下任何 .h/.cpp 出现外部库标识 → 失败
# 2) 任何地方出现跨 target 的相对 include（../ 或 ../../）→ 失败
# 3) adapters/ 里出现 if (album == "Various Artists") 这类业务字面量 → 警告（人工看）
```

进 CI 与 pre-commit。**它只补"目标图说不了"的部分**，其余交给编译器。

---

## 7. 运行时

### 7.1 播放链路（端口 + 高频旁路）

```
Presentation
  PlayerViewModel.pause()
    → ICommandBus::dispatch(PauseCommand{})
      → Playback.Core: PlayQueue 状态迁移 + PlaybackState 变更
        → IAudioOutput::pause()            ← 端口，实现在 Platform.Audio
```

位置更新**不走 CQRS**（60Hz 走总线 = 每秒 60 次类型擦除和 DTO 分配，纯浪费）：

```cpp
// X.Contracts 侧定义，Adapters 实现，Host 注入给 ViewModel
class IPositionStream {
public:
    struct Snapshot { int64_t trackId; int64_t positionMs; bool playing; };
    virtual ~IPositionStream() = default;
    virtual Snapshot read() const noexcept = 0;        // 原子读，无锁
    virtual void subscribe(std::function<void(Snapshot)> tick) = 0;  // 30Hz 推送
};
```

`PlayerViewModel` 订阅它，30Hz 更新 `positionMs` 属性。领域层完全不知道位置在流动；只有 `TrackFinished`、`SeekCommand`、`PauseCommand` 这类**离散事件**才进聚合。

**M3 落地情况（与上面的草图有出入，已实测）**：

- 接口缩到一行——`virtual std::int64_t positionMs() const noexcept = 0;`，**没有 `subscribe()`**。`QtMultimediaOutput` 同时实现 `IAudioOutput` 与 `IPositionStream`，位置存 `std::atomic<int64_t>`；`PlayerViewModel` 用 33ms 定时器**拉**，不是被推。
- 这样做的理由：`QMediaPlayer` 的位置本来就得靠回调写进一个变量，再包一层 pub/sub 只是把同一个值多复制一次；**拉取让"谁持有订阅"这个生命周期问题彻底消失**（关停时不用先退订）。
- 等 M4 换成 `AudioRing` + 采样计数，位置是解码线程的副产品，那时推送才是免费的，`subscribe()` 再回来。
- 曲目快照（标题/队列位置/状态）仍走查询总线，**2Hz** 拉一次（`GetPlayerSnapshot`），与 30Hz 的位置流分开——这两者混在一起会让每条位置更新都带一次 DTO 分配。

音频线程内部仍是 QueMusic 那套正确骨架：

```
 decode thread: av_read_frame → avcodec_send/receive → swr_convert(f32/2ch/48k)
                → 打代际号 gen++ → push 到 AudioRing (SPSC, ≈2s)
 audio thread : QAudioSink 拉模式 readData → pop → DSP → 淡变 → 写设备
                → 原子发布 {trackId, framesWritten}
```

三个必须做对的细节：**① seek 代际号**（`gen++` + `avcodec_flush_buffers`，旧代样本丢弃，否则拖进度条会听到 0.5 秒前一首）；**② 位置来自样本计数**（`startPts + framesWritten*1000/rate`，墙钟会漂移导致歌词错位）；**③ 回调里禁止 malloc/加锁/阻塞**。

**M4 落地情况（M3 的 `QtMultimediaOutput` 换成 `FfmpegOutput`，其余代码一行未动）**：

- 端口设计在这里兑现了：`host/main.cpp` 只改了两行（`#include` 与成员类型），`PlaybackService`、`PlayerViewModel`、QML 全部不认识 FFmpeg。想退回旧引擎就是改回那两行。
- **没有代际号，改用"线程独占 + 队列化回调"**：`FfmpegDecoder` 只在解码线程里被碰（`load()` 只登记路径并起线程，`open()` 在解码线程里做），所有 listener 经 `QMetaObject::invokeMethod(qApp, …, Qt::QueuedConnection)` 回投 GUI 线程。旧样本靠 `av_seek_frame` 后紧接着 `avcodec_flush_buffers` + `swr_convert(…, nullptr, 0, nullptr, 0)` 清掉，再加一次 `sink->reset()` 丢掉设备侧缓冲——比代际号少一个状态位，实测够用。
- 位置就是第 ② 条：`(seekStartSamples + writtenSamples - ring.availableToRead()) * 1000 / sampleRate`，末尾 clamp 到 `durationMs`。**样本与毫秒不能分两步算**（踩过：跳转后显示 `45:28 / 2:02`）。
- `AudioRing` 容量不是"≈2 秒"而是**固定 262144 样本**（`AudioRing(48000*2*2)` 向上取整到 2 的幂，掩码代替取模）。多出来的空间用来吸收"解码线程解大块、声卡取小块"的抖动；空读补零而不是返回 0（返回 0 会让 `QAudioSink` 提前结束）。
- 跳转精度靠**丢样本**：`avformat_seek_file(…, INT64_MIN, ts, INT64_MAX, 0)` 让容器给最接近的位置，首帧 `pts` 与目标之差换算成 `skipSamples_` 后从待播缓冲头部 `erase`。只用 `av_seek_frame(…, AVSEEK_FLAG_BACKWARD)` 会落到关键帧、常常早几十秒（踩过：位置一路走到 `2:24 / 2:02`）。
- 第 ③ 条同样成立：`readData` 零分配（int16 降级路径用构造时备好的 `scratch_`），`positionMs()` 只有一次原子读。

### 7.2 扫描链路

```
UI 线程: dispatch(ImportFolderCommand{folder})
         → Handler 立即返回（< 5ms），把活投给 ITaskQueue
后台:    4 个 worker 遍历目录 + TagLib 读标签
         每 500 条：UoW 事务写聚合 + 写 mv_track_row → commit → 发 TrackImported
UI 线程: 订阅进度读模型 → 每 200ms 刷一次进度条（不是每首歌刷一次）
```

**约束**：扫描期间 UI 必须能继续播放和滚动。进度**不进总线**，走一个 `IScanProgress` 读模型（同 `IPositionStream` 的旁路思路），否则 10000 首会打出一万次信号。

### 7.3 线程模型

| 线程                     | 跑什么                                       | 纪律                                  |
| ------------------------ | -------------------------------------------- | ------------------------------------- |
| GUI 线程                 | QML、ViewModel、`ICommandBus::dispatch`    | 任何阻塞 > 5ms 的操作都是 bug         |
| IO 池（`QThreadPool`） | 扫描、TagLib、SQLite 写、封面解码、歌词拉取  | 每任务自带事务边界                    |
| 音频线程                 | FFmpeg 解码 + sink 填充                      | 无 Qt 事件循环、无分配、无锁、无日志  |
| 数据库                   | **不用独立线程**，连接归 IO 池线程所有 | 单写连接串行化；SQLite WAL 允许并发读 |
| 网络线程                 | `QNetworkAccessManager`（`Sonora.Platform.Net`） | `IHttpTransport::send` 只许队列线程调用；回调统一 marshal 回队列线程 |

**`X.Core` 对线程无感知**：它只知道 `ITaskQueue` 端口。所有线程亲和性由 Adapters/Host 决定。这让领域层可以直接用单线程测试。

### 7.4 关停顺序（逆依赖）

```
1. PlayerViewModel 断开 IPositionStream 订阅（防止回调打到已析构对象）
2. AudioOutput::stopAndDrain()      → stop flag → decode thread join → ring 清空 → sink stop
3. OnlineService::shutdown()        → 取消在途请求（网络线程 → 队列线程），之后不再有回调
4. TaskQueue::cancelAll() + waitForDone()
5. 未提交的 UoW 回滚
6. 按创建逆序销毁 ViewModel → Adapters → Core
7. QGuiApplication 退出
```

---

### 7.5 在线链路（第二批端点，N1–N4）

第二批只加端点与解析器，**没有新增线程、没有新增网络栈**，全部复用第一批的链路：

```
Presentation (OnlineViewModel)
  → ICommandBus: RequestOnlinePage / LoadCollections / LoadMyPlaylists / ToggleFavorite / StartPersonalFm
    → Online.Adapters: OnlineService（令牌桶 + 失败退避 + 缓存 + 诊断计数）
      → NeteaseProvider（URL 构造与解码在 Online.Core，纯 C++，单测覆盖）
        → IHttpTransport（Platform.Net: QtHttpTransport）
          → SerialWorker：QNetworkAccessManager，send 只许队列线程
```

- **回投**：网络线程只把回包塞进各服务的 pending 队列；GUI 侧 50ms 轮询里的查询（`GetOnlineJob` / `FetchCollections` / `FetchMyPlaylists` / `GetSourceStatus`）负责排水与切页，不在网络线程里碰 DTO。
- **分页**：歌单 / 榜单先取 `playlist.trackIds[]`，再按 500 一并发 `/api/song/detail`；专辑 / 歌手 / 新歌一次取全、在内存里切片。
- **追加式队列**：Playback 只发布集成事件 `QueueNeedsMore`，组合根收到后发 `StartPersonalFm` ——播放模块不知道"FM"是什么，`PlayQueue` 只多一个 `appendable` 位。
- **缓存来路**：页面数据来自本地缓存时 `GetOnlineJob` 带 `fromCache`，界面显示「离线·上次结果」；真正的网络回包在 `drain()` 里清掉标记，不会一直挂着"上次结果"。

---

## 8. 目录结构

```
Sonora/
├── xmake.lua                       # 项目定义 + includes
├── xmake/
│   ├── targets.lua                 # 全部 target 的生成（唯一的依赖声明处）
│   ├── rules/qsb.lua
│   └── checks/boundaries.py
├── modules/
│   ├── kernel/                     # Sonora.Kernel
│   ├── contracts/                  # Sonora.Contracts（跨模块集成事件、共享 DTO）
│   ├── catalog/     {contracts/, core/, adapters/}
│   ├── playback/    {contracts/, core/, adapters/}
│   ├── playlists/   {contracts/, core/, adapters/}
│   ├── lyrics/      {contracts/, core/, adapters/}
│   ├── preferences/ {contracts/, core/, adapters/}
│   └── online/      {contracts/, core/, adapters/}   # v1.1：无聚合，只有端口 + 流程
├── platform/
│   ├── storage/                    # Sonora.Platform.Storage：SqliteConnection、UnitOfWork、Migrator
│   ├── audio/                      # Sonora.Platform.Audio：QtMultimediaOutput、FfmpegOutput、AudioRing
│   ├── filesystem/                 # 路径规范化、目录遍历（长路径 / UNC）
│   ├── net/                        # Sonora.Platform.Net：QtHttpTransport（全项目唯一 QtNetwork 出现处）
│   └── win/                        # 高 DPI、托盘、SMTC、单实例锁、DWM
├── presentation/
│   ├── viewmodels/                 # PlayerViewModel / LibraryViewModel / TrackListModel / ...
│   └── qml/
│       ├── main.qml  Shell.qml
│       ├── theme/     Theme.qml  Fonts.qml
│       ├── comic/     MangaPanel Halftone SpeedLines ComicBubble SfxText ...
│       ├── components/
│       └── pages/     Home Library Playlist NowPlaying Search Settings Storybook
├── host/
│   ├── main.cpp
│   ├── Bootstrap.cpp               # 组合根：装配、注册 Handler、注入
│   └── Shutdown.cpp
├── assets/{fonts,images,icons,shaders}/  resources.qrc
├── tests/                          # tests.catalog.core / tests.playback.core / tests.viewmodels ...
└── docs/
```

> **一条经验**：`modules/` 下不要出现 `common/`、`utils/`、`shared/`。这类目录三个月后会变成第二套领域模型。需要共享就放进 `Kernel`（必须是零依赖的纯抽象），或者接受重复——重复比错耦合便宜得多。

---

## 9. xmake 构建（MSVC / Windows）

### 9.1 能力边界（比 CMake 少一层，提前知道省两天）

| 能力                                                  | xmake                                                                           | 对策                                                                       |
| ----------------------------------------------------- | ------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| 找 Qt                                                 | 内置 `find_qt`，读 `--qt=` / `QTDIR` / `qmake`                          | ✓ 够用                                                                    |
| moc / qrc / ts                                        | `qt.moc` / `qt.qrc` / `qt.ts`                                             | ✓ 够用                                                                    |
| 静态库用 Qt                                           | `add_rules("qt.static")`                                                      | ✓ 够用                                                                    |
| 链接 Qt 模块                                          | `add_frameworks("QtMultimedia")`（带 `Qt` 前缀，自动补主版本与 Debug 后缀） | ✓                                                                         |
| `qt_add_qml_module`（URI + qmltypes + qmlcachegen） | **无整包等价物**，只有零散 `qt.qmltyperegistrar`                        | 手工 `qmlRegisterSingletonInstance` + qrc；qmltypes 与预编译列 M8 可选项 |
| shader → `.qsb`                                    | **没有**                                                                  | 自定义 `task("qsb")`，产物入库（§9.7）                                  |
| `windeployqt`                                       | `install.windows` 不够精细                                                    | 直接跑 `windeployqt`                                                     |
| Qt 包源                                               | xmake-repo 的 `packages/q/` 只有 qt6base/core/gui/network/qml/quick/widgets   | **没有 multimedia / shadertools / sql** → Qt 必须本机装             |

### 9.2 MSVC 的目标环境约束

| 项      | 要求                                                          | 不满足会怎样                                         |
| ------- | ------------------------------------------------------------- | ---------------------------------------------------- |
| CRT     | `/MD`（`set_runtimes("MD")`）                             | 与 Qt 的 `/MD` 不一致 → 链接错或跨 dll 释放堆崩溃 |
| Qt 套件 | `msvc2022_64`                                               | 用错套件会在链接期爆炸，且报错信息完全指不到根因     |
| 字符集  | `/utf-8` + 源文件 UTF-8 with BOM 不必需                     | 中文注释变乱码、`L"中文"` 编译警告                 |
| 标准    | `c++23`（xmake 探测出 `/std:c++23preview`）+ `/Zc:__cplusplus` | `__cplusplus` 谎报 199711；`/std:c++latest` 会带进 C++26 预览 |
| 严格性  | `/permissive-`                                              | 依赖 MSVC 扩展的代码悄悄通过                         |
| 宏      | `NOMINMAX` `WIN32_LEAN_AND_MEAN` `UNICODE` `_UNICODE` | `<windows.h>` 的 `min/max` 宏摧毁 `std::min`   |
| 大对象  | `/bigobj`                                                   | 模板 + `std::variant` 多的 TU 报 C1128             |

### 9.3 主 `xmake.lua`

```lua
set_project("Sonora")
set_version("0.1.0")
set_xmakever("2.9.5")
set_encodings("utf-8")
set_languages("c++23")

add_rules("mode.debug", "mode.release")

set_defaultplat("windows")
set_defaultarchs("x64")

set_runtimes("MD")                 -- 必须与 Qt 一致
set_policy("build.optimization.lto", true)

add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "UNICODE", "_UNICODE")
add_cxflags("/utf-8", "/Zc:__cplusplus", "/permissive-", "/EHsc", "/bigobj", {tools = "cl"})

includes("xmake/targets.lua")
```

### 9.4 `xmake/targets.lua`（唯一的依赖声明处）

```lua
-- ── 0. Kernel / Contracts：纯 C++，一个 Qt 目标都不依赖 ──────────────
target("Sonora.Kernel")
    set_kind("static")
    add_files("modules/kernel/src/**.cpp")
    add_headerfiles("modules/kernel/include/**.h")
    add_includedirs("modules/kernel/include", {public = true})

target("Sonora.Contracts")
    set_kind("static")
    add_files("modules/contracts/src/**.cpp")
    add_headerfiles("modules/contracts/include/**.h")
    add_includedirs("modules/contracts/include", {public = true})
    add_deps("Sonora.Kernel")

-- ── 1. 平台适配器 ──────────────────────────────────────────────────
target("Sonora.Platform.Storage")
    add_rules("qt.static")
    add_files("platform/storage/src/**.cpp")
    add_includedirs("platform/storage/include", {public = true})
    add_deps("Sonora.Kernel")
    add_frameworks("QtCore")
    add_packages("sqlite3")

target("Sonora.Platform.Audio")
    add_rules("qt.static")
    add_files("platform/audio/src/**.cpp")
    add_includedirs("platform/audio/include", {public = true})
    add_deps("Sonora.Kernel", "Sonora.Contracts")
    add_frameworks("QtCore", "QtMultimedia")
    add_packages("ffmpeg")

-- ── 2. 业务模块：三件套，全部由这张表生成 ──────────────────────────
local modules = { "catalog", "playback", "playlists", "lyrics", "preferences" }

for _, name in ipairs(modules) do
    local M  = "Sonora." .. name:sub(1,1):upper() .. name:sub(2)
    local d  = "modules/" .. name

    target(M .. ".Contracts")
        set_kind("static")
        add_files(d .. "/contracts/src/**.cpp")
        add_includedirs(d .. "/contracts/include", {public = true})
        add_headerfiles(d .. "/contracts/include/**.h")
        add_deps("Sonora.Kernel", "Sonora.Contracts")

    -- ★ 注意：不 add_rules("qt.static")，不 add_frameworks。
    --   后果：Qt 头不在 include 路径上，Domain 层污染 = 编译错误。
    target(M .. ".Core")
        set_kind("static")
        add_files(d .. "/core/src/**.cpp")
        add_includedirs(d .. "/core/include", {public = true})
        add_headerfiles(d .. "/core/include/**.h")
        add_deps("Sonora.Kernel", "Sonora.Contracts", M .. ".Contracts")

    target(M .. ".Adapters")
        add_rules("qt.static")
        add_files(d .. "/adapters/src/**.cpp")
        add_includedirs(d .. "/adapters/include", {public = true})
        add_deps(M .. ".Core", M .. ".Contracts", "Sonora.Platform.Storage")
        add_frameworks("QtCore")
        add_packages("sqlite3", "taglib")
end

-- ── 3. 表现层：只依赖 Contracts，故意不依赖任何 *.Core ──────────────
target("Sonora.Presentation")
    add_rules("qt.static")
    add_files("presentation/viewmodels/**.cpp")
    add_includedirs("presentation/viewmodels", {public = true})
    add_deps("Sonora.Kernel", "Sonora.Contracts")
    add_deps("Sonora.Catalog.Contracts", "Sonora.Playback.Contracts",
             "Sonora.Playlists.Contracts", "Sonora.Lyrics.Contracts",
             "Sonora.Preferences.Contracts")
    add_frameworks("QtCore", "QtGui", "QtQuick")

-- ── 4. 可执行文件 ──────────────────────────────────────────────────
target("sonora")
    add_rules("qt.quickapp")
    add_files("host/*.cpp")
    add_files("assets/resources.qrc")
    add_includedirs("host")
    add_deps("Sonora.Presentation",
             "Sonora.Catalog.Adapters", "Sonora.Playback.Adapters",
             "Sonora.Playlists.Adapters", "Sonora.Lyrics.Adapters",
             "Sonora.Preferences.Adapters",
             "Sonora.Platform.Audio")
    add_frameworks("QtSql", "QtConcurrent", "QtSvg")
    if is_plat("windows") then
        add_files("assets/win/app.rc")
        add_syslinks("ole32", "shell32", "dwmapi", "shlwapi", "user32")
    end
    if is_mode("debug") then
        add_defines("SONORA_QML_FROM_SOURCE")     -- 开发期从磁盘读 QML，改完不重编
    else
        set_strip("all")
        add_defines("QT_NO_DEBUG_OUTPUT")
    end
```

### 9.5 FFmpeg 在 MSVC 下的三条路（**这条最容易浪费一天**）

| 路线                       | 做法                                                                                                 | 评价                                                                |
| -------------------------- | ---------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| **A. vcpkg（推荐）** | `vcpkg install ffmpeg:x64-windows`，然后 `xmake f --vcpkg=D:/vcpkg` + `add_requires("ffmpeg")` | 全 MSVC 编译，`.lib` 与 Qt 同 ABI，一劳永逸。首次编译 30–60 分钟 |
| B. 源码自建                | MSYS2 bash + `vcvars64` + `--toolchain=msvc`                                                     | 可控但要维护一套脚本                                                |
| C. 预编译二进制            | 只接受**明确标注 MSVC 构建**的包                                                               | 见下面的警告                                                        |

> **警告：gyan.dev 与 BtbN/FFmpeg-Builds 的 Windows 预编译包是 MinGW 编译的**（`.dll.a` import lib），**不能**与 MSVC 链接。把它们喂给 MSVC 的典型症状是链接期成片的 `undefined symbol: __imp_av_*` 或 `__mingw_*`。v1 计划里写"下载 gyan.dev"的那一版在切换到 MSVC 后已作废——这是本次架构变更连带的变化。
>
> 若确实要预编译，去找显式带 `msvc` 字样的构建（部分 GitHub Actions 产物会标），并核对 `dumpbin /exports avcodec-*.dll` 能对上。

### 9.6 常用命令

```bash
# 配置（Qt 前缀、vcpkg 根、debug）
xmake f -m debug -a x64 -y          # Qt 由 find_qt 自动检出（实测 D:/Software/Qt/6.12.0/msvc2022_64）

# 编译（30 个 target 并行）
xmake -j

# 运行（qt.env 会把 Qt bin 加进 PATH）
xmake run

# 单个模块快速编译
xmake build Sonora.Catalog.Core

# 边界检查
python xmake/checks/boundaries.py

# 单测
xmake build tests.catalog && xmake run tests.catalog

# 发行
xmake f -m release && xmake -j
xmake qsb
windeployqt --qmldir presentation/qml build/windows/x64/release/sonora.exe
```

### 9.7 `task("qsb")`

```lua
task("qsb")
    set_category("plugin")
    set_description("assets/shaders/*.frag → *.qsb")
    on_run(function ()
        import("core.project.config")
        local qt = config.get("qt") or os.getenv("QTDIR")
        assert(qt, "先执行 xmake f --qt=<Qt 前缀>")
        local qsb = path.join(qt, "bin", "qsb.exe")
        for _, src in ipairs(os.files("assets/shaders/*.frag")) do
            local out = src .. ".qsb"
            print("qsb  %s", src)
            os.vrunv(qsb, {src, "-o", out, "--glsl", "100 es,120", "--hlsl", "50", "--msl", "12"})
        end
    end)
```

`.qsb` 产物入库并由 `resources.qrc` 收录：三套 CI/同事机器不需要各装一遍 ShaderTools，也避免不同 qsb 版本产物不一致。

### 9.8 坑（按踩到概率排序）

1. **Qt 套件选错**：装上 `mingw_64` 的 Qt 配 MSVC 编译 → 报错完全指不到根因。唯一解：`ls D:/Software/Qt/6.12.0/` 只认 `msvc2022_64`。
2. **CRT 不一致**：忘了 `set_runtimes("MD")`，或某个第三方库是 `/MT`。
3. **FFmpeg ABI**：见 §9.5，是本次变更引入的新坑。
4. **`add_frameworks` 名字**：写 `"QtMultimedia"`，xmake 的 `load.lua` 会自动拼成 `Qt6Multimedia` 并处理 MSVC 的 `d` 后缀。
5. **静态库链接顺序**：xmake 的 `add_deps` 拓扑排序通常够用；出现 `LNK2019` 找不到自己的符号，多半是某个符号只在一个没被依赖的 target 里。
6. **改了 QML 要全量重编**：QML 在 qrc 里。`SONORA_QML_FROM_SOURCE` 就是为这个存在——debug 下 `engine.load(QUrl::fromLocalFile("presentation/qml/main.qml"))`。
7. **`windeployqt` 漏 QML 依赖**：必须带 `--qmldir presentation/qml`，否则发行版启动白屏。

---

## 10. 漫画风格设计系统

（与架构分层无关，纯视觉层规范；实现全部落在 `presentation/qml/comic/`。）

### 10.1 强度档位

| 档                        | 做法                                                        | 成本 |
| ------------------------- | ----------------------------------------------------------- | ---- |
| L1 点缀                   | 现代 UI + 漫画配件（气泡提示、拟声词按钮、网点背景）        | 低   |
| **L2 分格（选它）** | 整页按漫画版面重组：粗描边分格、黑白 + 单色、硬阴影、页边距 | 中   |
| L3 全漫画                 | L2 + 翻页转场 + 立绘 + 手绘字体 + 网点动画                  | 高   |

以 **L2 为主体**，把 L3 的组件做成可选开关逐页点亮。这样哪怕 L3 做一半没时间，产品仍是完整的。

### 10.2 Design Tokens（`theme/Theme.qml` 单例）

暗/亮两套配色，靠 `Appearance.dark` 切换；组件只认令牌名，不认具体色值。

| 令牌 | 浅色（暖纸） | 深色（暗纸） | 用途 |
| --- | --- | --- | --- |
| `ink` | `#12100E` | `#EDE6D6` | 线稿 / 正文 |
| `inkSoft` | `#3A3630` | `#A9A08D` | 次要文字 |
| `paper` | `#F4EFE4` | `#141210` | 纸底 |
| `paperAlt` | `#E7DFCE` | `#1D1A16` | 次级纸底 |
| `paperHigh` | `#FFFCF4` | `#24201A` | 面板底 |
| `screentone` | `#C9C2B4` | `#3B372F` | 网点 / 分隔线 |
| `accent` | `#D93A3A` | `#E75B52` | 朱红：只用于"当前播放 / 当前选中" |
| `accentAlt` | `#2E5F8A` | `#7FA8CC` | 靛蓝：只用于"可交互焦点" |
| `shadow` | `#12100E` | `#000000` | 硬阴影 |
| `halftoneInk` | `#B9B1A2` | `#2A2721` | 网点图案的墨色 |
| `grooveA` / `grooveB` | `#4A453D` / `#2B2822` | `#6A6152` / `#464034` | 唱片纹路 |
| `scrim` | `#33000000` | `#33000000` | 遮罩 |
| `onAccent` | `#FFE3DC` | `#FFF3EE` | 压在 accent 上的字 |

派生色（`shadow` / `halftoneInk` / `grooveA` / `grooveB` / `onAccent`）不是"多几个色"，
而是把原本写死在组件里的十六进制收进唯一真源：深色下用线稿色当阴影会变成"发光块"，
唱片纹路和遮罩同理。**除了二维码画布**（必须白底黑块，扫得出来才有意义），
QML 里不许再出现裸十六进制颜色。

线宽、阴影偏移、字号、网点参数、动画时长这些与主题无关的令牌保持单值：

```qml
readonly property real stroke: 2.5        // 线宽只有两档
readonly property real strokeBold: 4.0
readonly property real shadowX: 4         // 硬阴影只有一个偏移
readonly property real shadowY: 4
readonly property int  textBody: 14       // 字号：正文 / 小字 / 标题 / 拟声词
readonly property int  textSmall: 12
readonly property int  textTitle: 22
readonly property int  textSfx: 40
readonly property real dotSize: 3.0       // dot.frag 的 uniform，名字必须同名
```

**实现注记（M2 实测）**：`Theme.qml` 由 `theme/qmldir` 声明为 `singleton Theme 1.0 Theme.qml`，
QML 里 `import "theme"` 后直接用。三条踩过的红线：① `Fonts.qml` 的 `FontLoader` 在
`pragma Singleton` 的 `QtObject` 里作为 `readonly property` 持有是可行的，但字体文件必须是
子集版（`tools/subset_fonts.py`），且 `name_IDs=["*"]` 要保留，否则 `FontLoader.name` 为空、
静默回落系统字体；② shader 的 uniform 与 QML 属性**必须同名**（见 §10.4 第一行）；
③ 文件级单例（`Theme`/`Fonts`）只对**自己文件里的 import** 生效——靠调用方的作用域动态解析
能跑起来，但 `QQmlEngine::retranslate()` 一重算绑定就报 `Theme is not defined`。

### 10.3 组件契约（`presentation/qml/comic/`）

| 组件            | 关键属性 / 信号                                           | 实现要点                                                                                         |
| --------------- | --------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| `MangaPanel`  | `strokeWidth, tailSide, shadowOffset, dashed`           | `BorderImage` 9 宫格 + 手绘边框贴图；阴影用偏移的第二个 BorderImage，**不用 DropShadow** |
| `Halftone`    | `dotSize, angle, density, ink, paper, fadeFrom, fadeTo` | `ShaderEffect` + `dot.frag`                                                                  |
| `SpeedLines`  | `count, speed, focusX, focusY`                          | `ShaderEffect` + 极坐标条纹，`speed` 驱动相位                                                |
| `ComicBubble` | `text, tailSide, duration`, `signal dismissed()`      | `Shape` + 路径画尾巴；`SpringAnimation` 弹出                                                 |
| `SfxText`     | `text, size, rotation`, `function burst()`            | **池化 6 个实例复用**；`ParallelAnimation` 上浮+放大+淡出；禁止每次 `createObject`     |
| `ComicButton` | `text, icon, tone, pressedOffset`                       | 常态画偏移阴影，按下时阴影收窄 + 平移 `(2,2)`                                                  |
| `InkSlider`   | `from, to, value, fillImage`                            | 轨道为手绘墨线贴图；已播放部分用 `clip` 裁切 `Image`                                         |
| `PageTurner`  | `sourceCurrent, sourceNext, direction`                  | 先做便宜版：两张 `ShaderEffectSource` + 绕 Y 轴旋转；shader 版后置                             |
| `CoverFrame`  | `source, spinAngle`                                     | 直角 + 粗描边 + 唱片旋转                                                                         |
| `TrackRow`    | `title, artist, duration, isPlaying, index`             | 高度固定（虚拟化前提）；`isPlaying` 时左缘出 `accent` 墨迹                                   |

**M2 落地情况**：已实现 `MangaPanel` / `ComicButton` / `ComicBubble` / `Halftone` / `InkSlider` 五个，其余留给 M6（`TrackRow`、`CoverFrame`）与 M8（`SpeedLines`、`SfxText`、`PageTurner`）。两处实现偏差记在这里，免得后来人对不上：

- `MangaPanel` **没有用 BorderImage 九宫格**：M2 阶段没有手绘边框贴图，用 `Rectangle` 描边 + 偏移实心矩形当阴影（`dashed` 时改用 `Shape` + `ShapePath.DashLine`，`dashPattern [7,5]`）。将来换成贴图时，组件对外的属性集不用变。
- `ComicBubble` 的尾巴用 `Shape` 实心三角，顶部压进气泡 6px 藏接缝——比路径布尔运算便宜，且在任意缩放下都不出缝。

### 10.4 特效实现阶梯（先便宜后昂贵）

1. **贴图**（零成本）：手绘边框、墨线轨道、网点 PNG。
2. **几何变换**：硬阴影 = 平移复制；按下感 = 阴影收窄 + 平移。
3. **Spring 动画**：`SpringAnimation`（阻尼 0.6–0.8）替代 `Easing.OutBack`。
4. **单层 ShaderEffect**：网点、集中线。**整页最多 2 个全屏 shader**。
5. **`ShaderEffectSource` 快照**：仅翻页转场期间启用，结束立刻 `live: false`。

> 反面案例（QueMusic 自己踩过并修复）：`ShaderEffectSource` + `MultiEffect` + 圆角遮罩三层叠加，最终合并成一个 shader。我们要**一开始就合并**。

**ShaderEffect 的三条硬规矩**（M2 每个都踩了一次）：

1. uniform 成员名与 QML 属性名**必须逐字相同**（`dotSize` ↔ `dotSize`）。不一致时**不报错**，而是整个 UBO 为 0——表现为"整片纯黑"，最容易误判成 shader 编译失败。
2. `ShaderEffect` 没有 `live` 属性（那是 `ShaderEffectSource` 的）。写上去会让整个 QML 组件加载失败，而宿主只报"加载失败"，看不出是哪一行。
3. `.qsb` 用 `qsb --qt6 <in>.frag -o <out>.qsb` 烘焙，产物入库（`assets/shaders/`）。`xmake qsb` 找 `qsb.exe` 只认 `QTDIR` 环境变量与 PATH——task 沙箱里没有 `import()`，拿不到 Qt 探测结果。

### 10.5 字体

- 标题/拟声词：站酷快乐体（免费商用）或马善政楷书。
- 正文：`Microsoft YaHei UI`（Windows 自带，零体积）。**正文绝不用手绘字体**。
- 打包字体必须子集化，否则安装包翻倍。做法见 `tools/subset_fonts.py`：扫所有 `.qml` 收集实际用到的字符（ASCII 可打印 + 中文标点兜底），`layout_features=["*"]`、`name_IDs=["*"]`。**M2 实测 1479 KB → 84 KB（559 字，6%）**。代价是加新文案要重跑一次，忘了就掉回系统字体。

### 10.6 可读性五条红线

1. 正文不用手绘字体；只有标题和拟声词用。
2. 网点只做背景与阴影，**绝不压在文字下面**；文字必须落在纯 `paper` 底色上。
3. `accent` 只出现在"当前播放 / 当前选中"，不做装饰色。
4. 线宽全局只有 `stroke` 和 `strokeBold` 两档。
5. 硬阴影偏移全局只有一个值；hover/press 靠收窄，不靠变模糊。

---

### 10.7 主题与语言的运行时切换

`presentation/Appearance.{h,cpp}` 是唯一的"界面偏好"入口，注册成 `Sonora.Presentation`
的第五个单例（`Library` / `Player` / `Lyrics` / `Online` / `Appearance`）。

| Q_PROPERTY | 说明 |
| --- | --- |
| `themeMode` | 0 跟随系统 / 1 浅色 / 2 深色 |
| `dark` | 实际生效的明暗（`themeMode==2`，或 `==0` 且系统是深色） |
| `systemDark` | 系统的明暗，来自 `QGuiApplication::styleHints()->colorScheme()` |
| `language` | `zh_CN` / `en` |

- 持久化在数据目录的 `settings.ini`（`QSettings::IniFormat`，键 `ui/theme`、`ui/language`），
  与曲库 `sonora.sqlite` 同目录。
- 跟随系统时接 `QStyleHints::colorSchemeChanged`，系统换主题界面立刻跟着换。
- 切语言：装 `QTranslator`（`:/i18n/*.qm`）后调 `QQmlEngine::retranslate()`，**不重启**；
  C++ 侧的状态文案（`stateText_` / `modeLabel_` / `statusText_`）在轮询里每个 tick 重算，
  所以切完语言最多一个 tick 就跟上。
- 翻译流水线：源码中文为基准 → `lupdate presentation/qml presentation/src presentation/include
  -ts translations/sonora_en.ts` → 填译文 → `lrelease … -qm translations/sonora_en.qm`。
  **`.qm` 入库**（构建机不一定装 lrelease，缺了 `resources.qrc` 直接编不过）；
  `tools/subset_fonts.py` 也扫 `translations/*.ts`，否则英文标题会缺字形。
- 契约层（`*.contracts`）里的中文常量（如播放模式 `顺序/循环/单曲/随机`）不进翻译文件：
  core 层零 Qt，表现层按枚举重新映射一次再 `tr()`。

## 11. C++ 接口草案

### 11.1 Kernel 的五个原语

```cpp
// modules/kernel/include/sonora/kernel/
// types.h
template <class Tag, class T = std::int64_t>
class TypedId {
public:
    constexpr TypedId() = default;
    constexpr explicit TypedId(T v) : m_v(v) {}
    constexpr T value() const noexcept { return m_v; }
    constexpr bool valid() const noexcept { return m_v > 0; }
    friend constexpr bool operator==(TypedId a, TypedId b) { return a.m_v == b.m_v; }
private:
    T m_v{};
};

// result.h —— 内部存 std::expected<T, Error>（C++23）；对外的 Result<T> 保持原名
// （500 多处调用点不动），只在实现里把存储与异常安全交给标准库
enum class Error : std::uint16_t {
    None = 0, NotFound, InvalidArgument, Conflict, Unsupported, IoFailure, Internal,
    NoHandler, QueueFull, Canceled,
};
template <class T> class Result;             // 提供 value() / hasValue() / error()
using ResultVoid = Result<void>;

// domain_event.h
struct IDomainEvent { virtual ~IDomainEvent() = default; };
using DomainEvents = std::vector<std::unique_ptr<IDomainEvent>>;

// clock.h —— 领域层不直接拿 std::chrono::system_clock，为了可测
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::int64_t nowUnixMs() const noexcept = 0;
};
```

`TypedId` 的价值：`PlaylistId` 和 `TrackId` 都是 `int64`，一旦传错编译器会挡下来。这类 bug 在播放器里非常致命（把歌单 id 当曲目 id = 播放失败而且很难查）。

### 11.2 一个聚合长什么样

```cpp
// modules/catalog/core/include/sonora/catalog/core/domain/Track.h
class Track {
public:
    static Result<Track> create(TrackPath path, const TrackFileFacts& facts, IClock& clock);

    TrackId                     id() const noexcept;
    const TrackPath&            path() const noexcept;
    const NormalizedTitle&      title() const noexcept;
    Duration                    duration() const noexcept;
    CoverKey                    coverKey() const noexcept;

    ResultVoid rename(std::u8string_view newTitle);      // 校验非空 → 发 TrackRenamed
    ResultVoid relocate(TrackPath newPath);              // 路径变更 → 发 TrackRelocated

    const DomainEvents& pendingEvents() const noexcept;
    DomainEvents        takeEvents();

private:
    Track(TrackId, TrackPath, NormalizedTitle, Duration, CoverKey);
    TrackId         m_id;
    TrackPath       m_path;
    NormalizedTitle m_title;
    Duration        m_duration;
    CoverKey        m_coverKey;
    DomainEvents    m_events;
};
```

`create` 用工厂而不是构造：路径规范化、标题回落、时长校验都是**创建期不变量**，造不出来就不该存在。

### 11.3 `PlayQueue`：本项目最值得建成聚合的地方

```cpp
class PlayQueue {
public:
    enum class Mode { Sequential, RepeatOne, RepeatAll, Shuffle };

    ResultVoid setTracks(std::vector<TrackId> ids, std::int64_t startIndex);
    ResultVoid setMode(Mode m);                       // 切换不摧毁历史
    ResultVoid advance();                             // 游标前移，RepeatOne 原地不动但发 TrackStarted
    ResultVoid goBack();                              // 随机模式下必须回到真实上一首

    std::optional<TrackId> current() const noexcept;
    Mode mode() const noexcept;

    // 随机排列与游标由 ShufflePlanner 领域服务生成后注入，可序列化以支持恢复
    std::vector<std::int64_t> shuffleOrder() const;
    std::int64_t              shuffleCursor() const;
};
```

随机模式用 **Fisher–Yates 预生成排列 + 游标**，不用"每次抽一首"——后者会重复，且无法精确回溯上一首。这是这个聚合存在的全部理由，也是唯一不能被 DTO 替代的部分。

### 11.4 一个 Handler 与它的端口

```cpp
// core/app/ImportFolderHandler.h
class ImportFolderHandler {
public:
    ImportFolderHandler(ITrackRepository&, IMetadataReader&, IUnitOfWork&,
                        ITaskQueue&, IDomainEventBus&, IClock&);
    ResultVoid handle(const ImportFolderCommand& cmd);   // 立即返回，活投给 ITaskQueue
};

// core/ports/ITrackRepository.h  —— 只进出聚合
class ITrackRepository {
public:
    virtual ~ITrackRepository() = default;
    virtual Result<Track>            byId(TrackId) = 0;
    virtual Result<std::vector<Track>> byPath(TrackPath) = 0;
    virtual ResultVoid               save(const Track&) = 0;
    virtual ResultVoid               remove(TrackId) = 0;
};

// core/ports/ICatalogQuery.h  —— 读侧，返回 DTO，别的模块用这个
class ICatalogQuery {
public:
    virtual ~ICatalogQuery() = default;
    virtual Result<TrackRefDto> byId(TrackId) = 0;       // 跨模块只需要 id + 路径 + 标题
};
```

**仓储只进出聚合**：注意 `ITrackRepository` 没有 `findAll()`。列表走 Query 侧（§5.5）。谁需要万级列表还去加载聚合，就是这个架构里最严重的违规。

### 11.5 ViewModel 与 QML 的接缝

```cpp
// presentation/viewmodels/PlayerViewModel.h
class PlayerViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString stateText      READ stateText      NOTIFY stateTextChanged)
    Q_PROPERTY(QString title          READ title          NOTIFY metaChanged)
    Q_PROPERTY(qint64  positionMs     READ positionMs     NOTIFY positionChanged)
    Q_PROPERTY(qint64  durationMs     READ durationMs     NOTIFY metaChanged)
    Q_PROPERTY(int     volume         READ volume         WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString errorText      READ errorText      NOTIFY errorTextChanged)
    Q_PROPERTY(bool    canGoNext      READ canGoNext      NOTIFY navChanged)
public:
    PlayerViewModel(kernel::ICommandBus&, kernel::IQueryBus&,
                    IPositionStream&, QObject* parent = nullptr);

    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    void setVolume(int v);
private:
    // 成员只有总线/流引用 + DTO + 视图状态。没有聚合、没有仓储、没有 QSqlQuery。
    kernel::ICommandBus& m_commands;
    kernel::IQueryBus&   m_queries;
    IPositionStream&     m_position;
    PlaybackSnapshotDto  m_snapshot{};
    QString              m_errorText;
};
```

QML 里就是 `Player.togglePlayPause()`、`text: Player.title`。**没有任何一行 QML 知道 CQRS、DDD 或 SQL 的存在**——这是分层的验收标准。

---

## 12. 数据模型

### 12.1 迁移策略

每个模块独立版本，互不阻塞：

```sql
CREATE TABLE __module_version (
  module      TEXT PRIMARY KEY,
  version     INTEGER NOT NULL
);
```

启动时 Host 按模块顺序调用 `Migrator::migrate(module, migrations[])`，逐级升级。**不要用"列不存在就 ALTER"的临时判断**——三个月后必然失控。

### 12.2 Catalog

```sql
CREATE TABLE album (
  id INTEGER PRIMARY KEY, name TEXT NOT NULL, artist TEXT, year INTEGER,
  cover_key TEXT, disc_count INTEGER DEFAULT 1
);
CREATE TABLE artist  (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);
CREATE TABLE track (
  id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL DEFAULT '', title_norm TEXT NOT NULL DEFAULT '',
  album_id INTEGER,     -- 模块内引用，有外键
  track_no INTEGER DEFAULT 0, disc_no INTEGER DEFAULT 1, year INTEGER DEFAULT 0,
  duration_ms INTEGER DEFAULT 0, bitrate INTEGER DEFAULT 0,
  sample_rate INTEGER DEFAULT 0, channels INTEGER DEFAULT 2, codec TEXT,
  file_size INTEGER DEFAULT 0, file_mtime INTEGER DEFAULT 0,   -- 增量扫描判据
  cover_key TEXT, added_at INTEGER NOT NULL
);
CREATE TABLE track_artist (track_id INTEGER NOT NULL, artist_id INTEGER NOT NULL,
                           role INTEGER DEFAULT 0, PRIMARY KEY (track_id, artist_id));

-- 读模型（由 Track* 事件维护，与写在同一事务内）
CREATE TABLE mv_track_row (
  track_id INTEGER PRIMARY KEY, title TEXT, artist TEXT, album TEXT,
  duration_ms INTEGER, play_count INTEGER DEFAULT 0,
  cover_key TEXT, has_lyrics INTEGER DEFAULT 0,
  album_id INTEGER, artist_id INTEGER, path TEXT
);
CREATE TABLE mv_track_stat (track_id INTEGER PRIMARY KEY, play_count INTEGER,
                            last_played_at INTEGER);

CREATE INDEX idx_mvrow_title  ON mv_track_row(title);
CREATE INDEX idx_mvrow_album  ON mv_track_row(album_id);
CREATE INDEX idx_mvrow_artist ON mv_track_row(artist_id);
CREATE INDEX idx_track_norm   ON track(title_norm);
```

### 12.3 其他模块

```sql
-- Playback：track_id 只是裸整数，不跨模块建外键
CREATE TABLE play_queue      (id INTEGER PRIMARY KEY CHECK (id = 1),
                              mode INTEGER NOT NULL, cursor INTEGER NOT NULL DEFAULT 0);
CREATE TABLE play_queue_item  (seq INTEGER PRIMARY KEY, track_id INTEGER NOT NULL);
CREATE TABLE play_queue_shuffle (seq INTEGER PRIMARY KEY, order_index INTEGER NOT NULL);
CREATE TABLE play_history     (id INTEGER PRIMARY KEY, track_id INTEGER NOT NULL,
                               started_at INTEGER NOT NULL);

-- Playlists
CREATE TABLE playlist      (id INTEGER PRIMARY KEY, name TEXT NOT NULL,
                            created_at INTEGER NOT NULL);
CREATE TABLE playlist_item (playlist_id INTEGER NOT NULL, track_id INTEGER NOT NULL,
                            pos INTEGER NOT NULL,
                            PRIMARY KEY (playlist_id, track_id));   -- 成员唯一由主键保证
CREATE UNIQUE INDEX idx_plpos ON playlist_item(playlist_id, pos);   -- 顺序不变量由索引保证

-- Lyrics
CREATE TABLE lyric      (track_id INTEGER PRIMARY KEY, format TEXT,
                         offset_ms INTEGER DEFAULT 0, fetched_at INTEGER);
CREATE TABLE lyric_line (track_id INTEGER NOT NULL, seq INTEGER NOT NULL,
                         at_ms INTEGER NOT NULL, text TEXT, PRIMARY KEY (track_id, seq));

-- Preferences
CREATE TABLE kv (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at INTEGER);

-- Online：provider 原样 JSON，不拆列；last_used_at 为 0 表示"迁移前的老行"，排序时回落到 fetched_at
CREATE TABLE online_search_cache (provider TEXT NOT NULL, keywords TEXT NOT NULL, "offset" INTEGER NOT NULL,
                                  fetched_at INTEGER NOT NULL, last_used_at INTEGER NOT NULL DEFAULT 0,
                                  payload TEXT NOT NULL, PRIMARY KEY (provider, keywords, "offset"));
CREATE TABLE online_detail_cache (provider TEXT NOT NULL, kind TEXT NOT NULL, id TEXT NOT NULL,
                                  fetched_at INTEGER NOT NULL, last_used_at INTEGER NOT NULL DEFAULT 0,
                                  payload TEXT NOT NULL, PRIMARY KEY (provider, kind, id));
CREATE TABLE online_song_cache (provider TEXT NOT NULL, song_id TEXT NOT NULL, fetched_at INTEGER NOT NULL,
                                payload TEXT NOT NULL, PRIMARY KEY (provider, song_id));
CREATE TABLE online_session (provider TEXT PRIMARY KEY, cookie_blob BLOB, expires_at INTEGER);
```

**注意 Playlists 的两个约束**：`PRIMARY KEY (playlist_id, track_id)` 让"成员不重复"成为数据库级不变量；`UNIQUE INDEX (playlist_id, pos)` 让"顺序连续无空洞"在重排时逼你整体重编号——**用 schema 承载不变量比用代码守住更可靠**，但前提是这个不变量真的属于该模块。

---

## 附 A 设计决策日志

记录"看起来随意、其实有理由"的选择，避免三个月后被自己推翻。

| 决策                                      | 理由                                                 | 推翻条件                                                                      |
| ----------------------------------------- | ---------------------------------------------------- | ----------------------------------------------------------------------------- |
| 模块边界用 xmake target 强制              | C++ 里 include 路径由构建系统给，是天生的边界执行者  | 若 target 数造成编译时间瓶颈（>60s 增量），合并 `*.Contracts` 进 `*.Core` |
| **只做通道分离 CQRS，不做事件溯源** | 无审计/重放需求；ES 换不到任何东西，只换来序列化成本 | 需要多设备同步或撤销历史时再评估                                              |
| 读侧用投影表而非聚合                      | 万级列表加载聚合会卡死，这是确凿的收益               | 若曲库长期 < 500 首，投影可以退化为直接查询（但要保留 Query 契约不变）        |
| `Preferences` 不套 DDD                  | 键值对无不变量无行为，套上去只有样板                 | 若出现"配置项之间互斥/迁移"的规则，升格为聚合                                 |
| 位置不进总线                              | 60Hz 走类型擦除是纯浪费                              | 若需要跨进程共享播放状态（如未来手机遥控），改为事件流                        |
| 不做 outbox                               | 单进程 + 事务回滚已足够                              | 出现跨进程/离线同步                                                           |
| 同步投影而非最终一致                      | 本机单写者，同步更简单也更快                         | 投影变慢影响命令延迟时改异步                                                  |
| Catalog/Playlists 分模块                  | 事实 vs 看法，删歌语义不同                           | 不要合并，会逼出畸形规则                                                      |
| 跨模块不建外键                            | 否则数据库替你决定业务语义（删曲目时歌单怎么办）     | 不推翻                                                                        |
| 一模块一 `*.Contracts` target | 让 Presentation 物理上看不见 Domain | 不推翻 |
| **`online` 定位成支撑域、不建聚合** | 全是别人数据的副本 + 会过期的地址，没有不变量 | 若将来要在本机维护「离线可用的在线库」（下载/收藏），那才有需要保护的不变量，届时要建聚合 |
| **在线播放直接吃 URL，不做下载** | 省掉下载-转码-存储三段，出错面小一半；`FfmpegOutput` 本来就吃 URL | 若需离线播放或精确 seek 稳定，改为「取地址 → 预取 30 s → 播放」 |
| **传输端口 `IHttpTransport` 放 `Online.Contracts`** | 让 core 零 Qt；`Platform.Net` 实现它，host 注入 | 若出现第二个模块要发 HTTP，抽到 `Sonora.Contracts`（共享端口），现在抽只会多一层抽象 |

## 附 B 这套架构的代价（诚实版）

| 代价             | 量级                                  | 什么时候值                                 |
| ---------------- | ------------------------------------- | ------------------------------------------ |
| 首次搭骨架       | ~1.5 人日（最初 23 个 target，现 30 个 + 边界脚本）  | 只有活过 v1 之后才回本                     |
| 每个功能多写一层 | 命令/DTO/映射器 ≈ 每功能 +30% 代码量 | 功能数 > 10 之后开始赚                     |
| 心智负担         | 新人需理解三层与总线                  | 单人项目只体现在"三个月后的自己"           |
| 调试链路长       | 一次点击跨 4 层                       | 用一条 `trace id` 串起来（debug 下打点） |

**一句话**：这套架构不是为了让 v1 更快写完（它会慢 4 人日），是为了让 v2、v3 不被推倒重来。如果这个项目只做一个月然后扔掉，那么 v1 的简单分层就够。
