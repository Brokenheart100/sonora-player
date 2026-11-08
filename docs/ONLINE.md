# Sonora 在线音源接入设计（网易云 · v1.3）

> 状态：**R0 已完成（2026-11-07）——不需要 weapi 签名**。六个端点全部 HTTP 200、零 `-460`；直链 `expi=1200` 且支持 Range；匿名音质封顶 `exhigh`（256 kbps）。实测细节与夹具见 [§15](#15-r0-实测结论2026-11-07)，下一步是 R1 骨架（[§11](#11-分期计划)）。
> 本文只回答三件事：**放在哪层、依赖什么、按什么顺序做**。整体架构见 [ARCHITECTURE.md](ARCHITECTURE.md)，进度见 [IMPLEMENTATION.md](IMPLEMENTATION.md)，库选型见 [DEPENDENCIES.md](DEPENDENCIES.md)。
> v1.2 的改动（针对"别造轮子、尽量用现代库"）：端口 6 → 2、去掉 Kernel 侧自造执行器与熔断器、传输接口改异步回调 + `setTransferTimeout()`、错误分类合并到 `kernel::ErrorCode`（追加 4 个值）、R0 探针改 `curl` 脚本、复用 `MangaPanel`/`SfxText`/`TrackRow`/`LrcParser` 并抽出 `RowsModel` 基类。新增 [§9.4 现代库用法约定](#94-现代库用法约定写代码时照这张表来)。
> v1.3 的改动：R0 实测结论落成 [§15](#15-r0-实测结论2026-11-07)；§11 的 R0 标记完成、§12 待定问题 1 结案；§5.1 与 [§7.1](#71-为什么可以直接把-url-交给播放链路) 补两个实测事实（直链是 `http://`、音质封顶 `exhigh`）。
> **动手时看 [§11.2 任务清单](#112-任务清单todolist)**（59 个可勾选任务，按依赖排序）与 **[§11.3 阶段闸门](#113-任务依赖)**（不满足不许进下一阶段）。
> 与既有文档冲突时按本文结论修改那三份（清单见 [§14](#14-需要同步修改的既有文档)），不要让文档与代码打架。

---

## 0. 范围

### 0.1 本期做什么

| 能力 | 说明 |
| --- | --- |
| 搜索 | 关键词搜索，分页，防抖，可取消，旧结果丢弃 |
| 详情 | 曲目/专辑详情，用于列表展示与时长 |
| 播放 | 解析直链后**直接交给现有播放链路**（不落地） |
| 登录 | 扫码登录（主）+ 粘贴 cookie（兜底），DPAPI 落盘 |
| 账号 | 侧栏账号胶囊、登录面板、"我的歌单"列表（只读展示） |
| 降级 | 断网/超时/限流/风控/接口变更，本地曲库全程不受影响 |

### 0.2 本期明确不做

| 不做 | 原因 |
| --- | --- |
| 在线曲目写入本地曲库（影子行） | 撞 `Track` 聚合"非空且唯一的本地路径"不变量；会给 FTS、扫描器、`play_count` 带来幽灵行与分支。已决策：**在线曲目不入库、不进歌单**，见 [§1.3](#13-已否决的第三种做法在线曲目写入曲库) |
| 播放地址落盘缓存（下载再播） | FFmpeg 构建已带 HTTPS（[§7.1](#71-为什么可以直接把-url-交给播放链路)），直连省掉 2GB 级缓存治理与约 2 人日；代价是依赖网络与直链有效期，用重解析兜底 |
| 多音源抽象 | 第二个源真出现再抽；`provider` 字段处处保留但只允许常量 `"ncm"` |
| `IAudioOutput::loadUrl` 流式改造 | 动播放核心的缓冲与位置记账，撞 AGENTS.md 一整列播放坑 |
| 自动跳下一首、静音降级 | 是 UX 决策不是技术问题；先报错、收集日志 |
| CI 跑真实网络测试 | 会随机红；真网络只走 `tools/probe_online.sh` 手工触发 |
| 下载/导出音频 | 版权。直链只在内存里活到播放结束 |

---

## 1. 定位决策：为什么是独立模块

### 1.1 三种候选

| 候选 | 判定 | 理由 |
| --- | --- | --- |
| **A. 新业务模块 `online`** | ✅ 采用 | 独立的失效模式（接口变更/登录过期/风控）与独立数据（会话、搜索缓存）。`rm -rf modules/online` 后本地曲库与播放**完整可用**——这是"支撑域"的删除判据 |
| B. `platform/` 能力 | ❌ | 平台层是"没有业务语义的技术"（见 [platform/storage](platform/storage) 的定位）。"哪个源、登录了没、cookie 过期要不要提示重登"全是业务。`platform/net` 只承担传输，不该知道 `/api/search/get` 长什么样 |
| C. 现有 `catalog` 的远程源 | ❌ | 见 §1.3 |

### 1.2 模块边界

`online` 只做三件事：**取**（搜/详情/取地址）、**存缓存**（元数据与会话）、**发登录态**。

- 不拥有"曲目身份"——既然不入库，在线曲目的身份就是 `(provider, songId)` 这个字符串对，只存在于 `online` 自己的 schema 与内存里。
- 不拥有"播放"——播放归 `playback`。`online` 只产出**一个可播放的 URL**，由表现层把它拼成 `QueueEntry` 后派发 `PlayTracks`。
- 不发出跨模块命令（架构 §6.2 禁止"模块代用户转发命令"）。编排在表现层，它合法地同时看得见 `Online.Contracts` 与 `Playback.Contracts`。

### 1.3 已否决的第三种做法：在线曲目写入曲库

诱人之处是"队列/歌单/歌词/播放次数全都复用"。否决理由：

1. **`Track` 聚合的不变量是"有非空文件路径"**（`modules/catalog/core/include/sonora/catalog/core/domain/Track.h` 首行注释）。在线曲目没有文件。要么给 `file_path` 塞一个合成 URI（`ncm:track:1901371647`）——这一列从此"有时是路径、有时不是"，扫描器、增量导入、`existsByPath`、封面解析全都要按 `source_kind` 分叉；要么把不变量改成"可选"，等于放弃它保护本地库的能力。
2. **FTS 混排**：`track_fts` 一旦写入在线行，默认搜索会同时命中在线与本地结果，用户分不清，而我们没有可靠的跨源去重（标题+艺人+时长猜同一首歌，错配代价是"收藏了 A 指向 B"且无法自动纠正）。
3. **可逆性**：不入库是纯增量；入库要迁移 v5，将来想去掉就得反向迁移，且期间产生的歌单引用会变成悬空行。

**代价**（必须承认）：在线曲目**进不了歌单、不计 `play_count`、不出现在曲库搜索里**。这是本期的产品取舍；等"在线歌单"成为真需求时，再按 expand-and-contract 单独评估。

---

## 2. 架构

### 2.1 模块图

```
                    ┌──────────────────────────────┐
                    │        host/main.cpp         │  组合根：唯一 new 实现的地方
                    └───────┬──────────────┬───────┘
                            │              │
        ┌───────────────────▼──┐      ┌────▼─────────────────┐
        │ Sonora.Presentation  │      │ Sonora.Platform.Net  │
        │ (ViewModel + QML)    │      │ (QtNetwork, pimpl)   │
        └───────────┬──────────┘      └────────▲─────────────┘
                    │ 只 include *.Contracts    │ 实现 IHttpTransport
        ┌───────────▼──────────────────────────┴─────────────┐
        │            Sonora.Online.Contracts                 │
        │   DTO / 命令 / 查询 / IHttpTransport / ISecretStore │
        └───────────▲────────────────────────────────────────┘
                    │
        ┌───────────┴──────────┐      ┌──────────────────────┐
        │  Sonora.Online.Core  │      │ Sonora.Platform.Win  │
        │ 策略/状态机（零 Qt）   │      │ DPAPI 凭据实现        │
        └───────────▲──────────┘      └──────────▲───────────┘
                    │                            │
        ┌───────────┴────────────────────────────┴───────────┐
        │            Sonora.Online.Adapters                  │
        │   NeteaseProvider / JSON codec / Sqlite 缓存        │
        └────────────────────────────────────────────────────┘
```

三条边值得单独说：

- `Platform.Net → Online.Contracts`：**平台层反向依赖一个业务模块的契约**。不是新形态，先例是 `Sonora.Platform.Audio → Sonora.Playback.Contracts`（`IAudioOutput` 就住在播放契约里，因为 `platform/audio` 只看得见公开契约）。推翻条件：**第二个消费者出现时**（比如封面下载、在线歌词独立成模块），把 `IHttpTransport.h` 上移到 `Sonora.Contracts`——一次 20 行文件搬家，一个 commit。
- `Online.Adapters` 可以链 Qt（它调 `platform/net` 的公开头，本身不需要 Qt 类型），但**JSON 解析必须在零 Qt 的代码里**（§9.2），否则 fixtures 单测跑不进 `sonora_tests`。
- `Online.Core` **零 Qt、零 SQLite、零 Win32**，`boundaries.py` 会抓。注释里也不要写 `QtNetwork`/`QNetworkAccessManager` 字样——脚本扫文本，注释也算。

### 2.2 target 变更

`xmake/targets.lua` 里 `BUSINESS` 表（[targets.lua:78](xmake/targets.lua#L78)）加一个名字，`Contracts/Core/Adapters` 三个 target 自动生成，并自动进 `presentation_deps`：

```lua
local BUSINESS = {"catalog", "playback", "playlists", "lyrics", "preferences", "online"}
```

| target | 变化 | 关键 deps / 设置 |
| --- | --- | --- |
| `Sonora.Online.Contracts` | 新增（自动） | `Kernel`, `Contracts`；**不链 Qt** |
| `Sonora.Online.Core` | 新增（自动） | `Kernel`, `Contracts`, `Online.Contracts`；**不链 Qt** |
| `Sonora.Online.Adapters` | 新增（自动）+ 尾部重入一次 | `add_deps("Sonora.Platform.Net")`；追加 `add_includedirs(rel("third_party", "nlohmann_json"))` |
| `Sonora.Platform.Net` | 新增 lib | `deps = {"Sonora.Kernel", "Sonora.Online.Contracts"}`；`qt = {"QtCore", "QtNetwork"}` —— **全项目唯一出现 QtNetwork 的地方**；内含 `SerialWorker` 与 `QtHttpTransport` |
| `Sonora.Platform.Win` | 追加 dep | `add_deps("Sonora.Online.Contracts")`、`add_links("crypt32")` |
| `Sonora.Kernel` | **只加 4 个枚举值** | [Error.h:16](modules/kernel/include/sonora/kernel/Error.h#L16) 追加 `Unauthenticated` / `RateLimited` / `Timeout` / `Upstream`（append-only；`toName` 的 `switch` 无 `default`，少一个 case 就编译报错）。**不加执行器**——见 [§4.2](#42-执行器platformnetserialworker不进-kernel) |
| `sonora` | 追加 dep | `+ Online.Adapters, + Platform.Net`；组合根多 3 行 `new` |
| `sonora_tests` | 追加 dep | `+ Online.Core, + Online.Contracts`（都零 Qt，测试保持"不链 Qt"这条性质） |

**不新增探针 target**：R0 的端点探测全部用 `curl` 脚本做完（见 [§11.2](#112-任务清单todolist)），只有确定要走 weapi 签名时才写 C++——为了发 HTTP 就建一个 Qt target 是纯轮子。

`xmake/checks/boundaries.py` **零改动**：它按 `modules/<m>/{contracts,core,adapters}` 与 `platform` 归类，没有硬编码模块名。

### 2.3 端口与实现

| 端口 | 声明位置 | 实现 | 为什么住这 |
| --- | --- | --- | --- |
| `IHttpTransport` | `online/contracts/Transport.h` | `platform/net/QtHttpTransport` | 唯一必须抽象的缝：同时买来"假 transport 可注入"与"adapters 不链 QtNetwork" |
| `ISecretStore` | `online/contracts/SecretStore.h` | `platform/win/DpapiSecretStore` | DPAPI 要 `<windows.h>`/`dpapi.h`，只许出现在 `platform/win` |

**只有这两个端口。** 早先草稿里的 `IOnlineProvider` / `IOnlineStore` / `ISessionStore` 全部降级为具体类：provider 只有一家、store 只有一个实现，多一层接口只是加个 vtable，而**总线上的命令/查询本身就是那道缝**。`IClock` 复用 Kernel 已有的，不新造。

三层各一句话：

| 层 | 只做 | 不碰 |
| --- | --- | --- |
| `online/core` | 纯计算：退避、令牌桶、失败计数、错误映射、JSON → DTO | 线程、IO、Qt |
| `online/adapters` | 编排：命令/查询 handler、拼 URL/header、TTL 缓存、把结果翻成 job 状态 | 定时器、事件循环 |
| `platform/net` | `SerialWorker`（QThread + moveToThread）、`QtHttpTransport`（QNAM 只活在这条线程上） | 任何业务字段 |

**纯函数策略（core，用 `std::chrono` 计时，可单测、不睡觉）**：

```cpp
// online/core/policy/Backoff.h
struct BackoffPolicy { std::chrono::milliseconds base{300}; double factor{2.0};
                       std::chrono::milliseconds cap{5000}; std::chrono::milliseconds jitter{200}; };
[[nodiscard]] std::chrono::milliseconds delayFor(const BackoffPolicy&, int attempt,
                                                 std::uint32_t seed) noexcept;

// online/core/policy/FailureCounter.h —— 10 行；**刻意不做熔断**（理由见 §5.1）
struct FailureCounter { std::int32_t consecutive{0}; std::int64_t nextRetryAtMs{0};
                        void record(bool ok, std::int64_t nowMs) noexcept; };

// online/core/policy/TokenBucket.h  —— 3 req/s，burst 6
// online/core/policy/ErrorMap.h     —— HTTP 状态 + 厂商 code + body 关键字 → OnlineError
```

唯一出口是 `online/adapters/OnlineService`：worker 线程上的一切网络调用都经过它（拼请求 + 令牌桶 + 退避 + 缓存回写），保证限流与重试只有一份实现。

---

## 3. 契约草案

> 只要签名与 DTO，不写实现。所有类型住在 `sonora::online::contracts`，纯 C++（无 Qt 类型）。

### 3.1 DTO

```cpp
enum class JobPhase    : std::uint8_t { Idle, Loading, LoadingMore, Ready, Empty, Error, Offline };
enum class AccountPhase: std::uint8_t { Guest, QrWaiting, QrScanned, QrExpired, LoggedIn, Expiring, Failed };
enum class SourceKind  : std::uint8_t { Search, Playlist, Favorites, Recent, DailyRecommend, Album };

struct OnlineError {
    kernel::ErrorCode code{kernel::ErrorCode::None};  // Kernel 的分类 + 本期追加的 4 个值
    std::string message;                              // 已是中文，UI 直接显示
    bool retryable{false};                            // UI 据此决定按钮是"重试"还是"去登录"
    std::int32_t retryAfterMs{0};                     // 429 时的建议退避
};

struct OnlineTrackDto {
    std::string songId;        // **字符串**：避免 double 精度与格式化争议
    std::string title;
    std::string artists;       // 已用 " / " 连接好，UI 不再拼
    std::string album;
    std::int64_t durationMs{0};
    std::string coverUrl;      // https，QML Image 可直接吃
    std::string qualityLabel;  // "标准/较高/极高/试听"
    bool playable{false};      // false → 行尾灰标签"无版权"
    bool fromCache{false};
};

struct OnlineJobState {        // 便宜，可 20Hz 拉
    std::uint64_t token{0};
    JobPhase phase{JobPhase::Idle};
    std::int32_t total{0};
    std::int32_t loaded{0};
    OnlineError error;
    std::uint64_t revision{0}; // 行数据变化才 +1，UI 据此决定是否 FetchOnlineRows
};

struct OnlinePageDto {
    std::uint64_t token{0};
    std::uint64_t revision{0};
    std::vector<OnlineTrackDto> rows;
    bool hasMore{false};
};

struct SourceStatusDto {
    std::string provider;               // "ncm"
    bool signedIn{false};
    AccountPhase accountPhase{AccountPhase::Guest};
    std::string nickname;
    std::int64_t nextRetryAtUnix{0};    // 退避/限流中：> now 时 UI 显示"稍后再试"
    OnlineError lastError;
    bool enabled{true};                 // 用户可整体关掉这个源
};
```

> **只有一套错误分类**：`code == kernel::ErrorCode::Unauthenticated` 就是"去登录"，`RateLimited` 带 `retryAfterMs`，`retryable` 决定能不能重试。不再叠加 `ErrorKind`——那是同一维度的第二次描述。
> 为此给 [Error.h:16](modules/kernel/include/sonora/kernel/Error.h#L16) 追加 4 个枚举值（`Unauthenticated` / `RateLimited` / `Timeout` / `Upstream`），append-only，不动已有值的顺序与含义。

### 3.2 命令（全部 < 5ms 返回，网络活在队列线程）

```cpp
struct RequestOnlinePage {           // 搜索 / 歌单 / 我喜欢的 / 最近播放
    std::uint64_t token;             // 由**界面**递增生成，用于丢弃过期结果
    SourceKind kind;
    std::string keywords;
    std::string playlistId;
    std::int32_t offset{0};
    std::int32_t limit{30};
};
struct CancelOnlineJob     { std::uint64_t token; };
struct RetryOnlineJob      { std::uint64_t token; };

/// 解析整队直链后交给表现层拼 PlayTracks（表现层在轮询 tick 里做，不在网络线程）
struct ResolveAndPlayOnline{ std::uint64_t token; std::vector<std::string> songIds;
                             std::int32_t startIndex{0}; SourceKind origin; };

struct SignInWithCookie    { std::string provider{"ncm"}; std::string cookie; };  // 含秘密：不入日志/事件/DTO
struct SignOut             { std::string provider{"ncm"}; };
struct StartQrLogin        { std::string provider{"ncm"}; };
struct CancelQrLogin       { std::string provider{"ncm"}; };
```

### 3.3 查询（只读 SQLite/内存，全部 < 1ms）

```cpp
struct GetOnlineJob     { std::uint64_t token; };                       // → OnlineJobState
struct FetchOnlineRows  { std::uint64_t token; std::int32_t offset{0}; std::int32_t limit{30}; }; // → OnlinePageDto
struct GetSourceStatus  { std::string provider{"ncm"}; };               // → SourceStatusDto
struct GetQrLoginState  { std::string provider{"ncm"}; };               // → QrLoginDto{phase, qrDataUrl, message}
struct GetMyPlaylists   { std::string provider{"ncm"}; };               // → vector<PlaylistDto>
```

### 3.4 传输端口

```cpp
enum class HttpMethod : std::uint8_t { Get, Post };

struct HttpRequest {
    HttpMethod method{HttpMethod::Get};
    std::string url;                                   // 必须 https://（明文直接拒绝）
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::chrono::milliseconds transferTimeout{8000};    // → QNetworkRequest::setTransferTimeout()
    std::int64_t maxBodyBytes{8 * 1024 * 1024};
};

struct HttpResponse { int statusCode{0}; std::string body; std::string contentType;
                      std::chrono::milliseconds elapsed{}; };

using HttpCallback = std::function<void(kernel::Result<HttpResponse>)>;

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    /// **异步**：立刻返回，回调在 transport 自己的线程（worker 线程）上触发。
    /// HTTP 4xx/5xx 也是 Ok(HttpResponse) —— 语义翻译只有一处（policy/ErrorMap.h）。
    virtual void send(std::uint64_t requestId, const HttpRequest&, HttpCallback) = 0;
    /// 对应 QNetworkReply::abort()；已完成/未知 id 是空操作。
    virtual void cancel(std::uint64_t requestId) = 0;
};
```

> **为什么是异步接口**：Qt 的原生形态就是"事件循环 + 信号回调"。早先草稿写的是"阻塞 `send()` + 局部 `QEventLoop` 等 reply"，那是把异步 API 硬掰回同步——既多一层手写循环，又多一处"谁在等谁"的隐患。改成回调后，`SerialWorker` 只负责 `post()`，超时交给 `setTransferTimeout()`，取消交给 `abort()`。

---

## 4. 线程模型与"< 5ms"的落点

### 4.1 时序

```
GUI 线程                          串行 worker 线程（platform/net）          SQLite
────────────────────────────────────────────────────────────────────────────────
dispatch(RequestOnlinePage{token}) ──post──▶ ① 登记 job，phase=Loading
  └ 立即返回（µs）                            ② tokenBucket.take()        ← 限流
                                              ③ 退避闸门到点了吗？        ← 没到就排着
QTimer 50ms                                   ④ transport.send(id, req, cb) 立刻返回
  ask(GetOnlineJob{token}) ────────────────▶  ⑤ 回调：解析 → 写缓存行 → phase=Ready
  ◀──── phase / loaded / revision
  phase==Ready → 一次 FetchOnlineRows
  token != gen_ → 丢弃结果
```

### 4.2 执行器：`platform/net/SerialWorker`，不进 Kernel

仓库**至今没有生产版实现**，只有 [ITaskQueue.h:37](modules/kernel/include/sonora/kernel/ITaskQueue.h#L37) 的 `ImmediateTaskQueue`（单测用）。本期补的执行器是 **Qt 侧约 30 行**，住在 `Sonora.Platform.Net`，**不进 Kernel**：

```cpp
// platform/net/src/SerialWorker.h —— 一条 QThread + 一个 QObject 焦点 = 串行队列
class SerialWorker final : public QObject {
    Q_OBJECT
public:
    void post(std::function<void()> task);   // 线程安全：QMetaObject::invokeMethod(Qt::QueuedConnection)
    void stop();                             // quit() + wait()，幂等
};
```

- 为什么不写 `SerialTaskQueue`（`std::jthread` + 有界队列）：要么自己重写条件变量与关停协议（`QThread` 已经解决的事），要么再跟 Qt 事件循环对接一次。而 Kernel 的 `ITaskQueue` 服务的是**业务 handler 的同步/异步边界**（< 5ms 那条规矩），网络串行执行是另一个关注点；塞进 Kernel 只会让"零依赖原语层"多一个与 Qt 平行的执行器，测试价值≈0。
- 关停顺序：`stop()` 必须先于 `~QtHttpTransport`（组合根逆序拆除，架构 §7.4）。

### 4.3 网络侧的两条硬规矩

- **`QNetworkAccessManager` 只在 worker 线程里创建并长期复用**。每次新建会丢掉连接池与 TLS 会话复用，每个请求多付 100–300ms 的 Schannel 握手——"输入即请求"的交互能感觉到。
- **绝不在 GUI 线程阻塞等待网络**。`QtHttpTransport::send()` 立刻返回、结果走回调（§3.4）；超时交给 `QNetworkRequest::setTransferTimeout()`，取消交给 `QNetworkReply::abort()`。worker 侧保留一条断言：`send()` 若在 GUI 线程被调用便直接 `fail(Internal, "send() on GUI thread")`。再加 `CommandBus` 的 5ms 告警做双保险。

### 4.4 不允许做的事

| 禁止 | 替代 |
| --- | --- |
| handler 里同步发请求 | 只做参数校验 + `post()` |
| 网络线程直接派发跨模块命令 | 结果写进 job 状态，GUI 线程轮询到 Ready 后由 ViewModel 派发 |
| 网络线程直接写 SQLite | 回写也走 `post`，每次落库一个 `UnitOfWorkScope` |
| 用 `EventBus` 通知 UI | 无退订、无队列；只保留一条 `OnlineSessionChanged` 给组合根 |

---

## 5. 可靠性设计

### 5.1 数值（可直接写进代码）

| 参数 | 值 | 说明 |
| --- | --- | --- |
| 输入防抖 | 280 ms | 连打 5 个字只发 1 次请求 |
| 并发 | 每 provider 1 条队列线程 | 串行本身就是最可靠的限流，也让 SQLite 只有一条写线程 |
| 令牌桶 | 3 req/s，burst 6 | 实测 4 rps 连打 20 次零拦截（见 §15.3），3 rps 仍留余量 |
| 传输超时 | `transferTimeout`：搜索 8000 / 详情 6000 / 取地址 6000 / 歌词 6000 / 账号 8000 ms | 一次设定覆盖 DNS + 连接 + TLS + 等首字节（`QNetworkRequest::setTransferTimeout()`）；不再自己拼"连接超时 + 整请求超时"两段 |
| 重试 | 最多 2 次（共 3 次尝试），**仅幂等 GET** | 登录、收藏一律不重试 |
| 退避 | `min(300·2^n, 5000) + rand(0..200)` ms；命中 429 且有 `Retry-After` → `min(Retry-After, 30s)` | jitter 防抖，cap 防止一次风控把源锁死一小时 |
| 不做熔断 | —— | 个人播放器只有"断网"和"登录过期"两种真实失败，退避 + 负缓存已经覆盖；再加状态机换不到行为，只换来测试路径 |
| 搜索缓存 TTL | 10 分钟 | 过期行不命中，但仍占位到被 LRU 淘汰 |
| 搜索缓存上限 | 200 行 / 4 MiB，按 `last_used_at` 做 LRU | 行数与字节各一道闸；老库升级后 `last_used_at = 0` 的行按 `fetched_at` 排队（R5-3 实测：插入 300 行→淘汰到 200；被 `loadSearch` 触碰过的行不会被淘汰到；字节闸在 70×64 KiB 时触发） |
| 详情缓存 TTL | 24 小时 | 标题/时长基本不变 |
| 歌词缓存 TTL | 7 天 | |
| 负缓存 | 不可播 1h / 无歌词 24h | 少了它会对着"听不了"反复打 |
| 播放地址 | 只在内存，TTL 5 分钟；拿到 403/404 立刻失效并重解析一次 | 直链 **`expi=1200`（20 分钟）**、且是 `http://`（R0 实测），落库等于长期泄露签名 |
| 批量取地址 | ≤ 200 首 / 次 | 一次往返解决整队，避免 30 次请求 |

### 5.2 错误映射（唯一实现：`online/core/policy/ErrorMap.h`）

| 外部信号 | `OnlineError` | 可重试 | UI 方向 |
| --- | --- | --- | --- |
| 2xx 且解析成功 | `None` | — | — |
| 200 但厂商 code ∈ {301,302,-460,403} | `Unauthenticated` | ❌ | "登录已失效，重新扫码" |
| 401 / 403 | `Unauthenticated` | ❌ | 同上 |
| 429 或 body 含"频繁/too many/captcha" | `RateLimited` | ✅ | "慢一点，{n} 秒后重试" |
| 408 / 5xx | `Unavailable` | ✅ | "服务暂时不可用" |
| DNS/TLS/连接重置 | `Unavailable` | ✅ | "网络连不上" |
| 超时 | `Timeout` | ✅ | "网络超时" |
| 200 但结构不符 / 关键字段缺失 | `Upstream` | ❌ | "接口可能已变更" |
| `url == null`、`fee`/`payed` 标记 | **不是错误**：`playable=false` + `unavailableReason` | — | 行内灰字"无版权" |
| 队列已关停 / run 被取消 | `Internal`（`message="已取消"`） | ❌ | 静默 |

**约定**：`IHttpTransport::send` 只为**传输层**失败回调 `fail(...)`；HTTP 4xx/5xx 一律是 `Ok(HttpResponse)`——这样"HTTP 语义"只有一处翻译，测试里给假 transport 一个 500 就能覆盖整条映射。

### 5.3 降级矩阵

| 触发 | 行为 | 用户可见 |
| --- | --- | --- |
| 断网 | 在线页取上次成功结果，只读不刷新；曲库与本地播放完全不受影响 | 顶部细条"离线：显示上次的结果" |
| 超时 | 自动重试 2 次，仍失败进 Error | "网有点慢" + 重试 |
| 429 | 客户端节流，按 `retryAfterMs` 恢复 | "先喘口气" + 倒计时 |
| 未登录访问需登录接口 | 保持游客态，列表显示"登录后可见" | 行内提示 + 侧栏胶囊高亮 |
| 登录过期 | 转 `Expiring`，静默重试一次，仍失败转 `Guest` | 气泡"登录失效了，再扫一次" |
| 直链解析失败 | 行置灰，双击只改状态 | 行尾"无版权" |
| 播放中直链过期 | 见 [§7.3](#73-直链过期与自动重解析) | "正在重新解析…" |
| 完全离线启动 | 不请求，在线页直接进 Offline 分支 | 缓存歌单 + "离线" |

任何降级都必须留痕：一行日志 + 计数器 +1，不允许"静默为空"。

---

## 6. 凭据与安全

| 项 | 决策 |
| --- | --- |
| 存什么 | cookie 里**只取** `MUSIC_U`（必需）与 `__csrf`（3xx 类接口需要），其余全部丢弃 |
| 密文放哪 | `<AppData>/Sonora/secrets/<provider>.bin`，**不进 SQLite**（库文件会被备份/同步/发人看） |
| 怎么加密 | `CryptProtectData`（DPAPI，`CRYPTPROTECT_UI_FORBIDDEN`），当前用户作用域，附加 entropy `"sonora/v1/" + provider`。拷到别的机器/账户解不开——这正是要的 |
| SQLite 里存 | `online_session.secret_ref TEXT`：不透明句柄（`<provider>/<hash>/<8 字节随机>`），可进日志，反推不出 cookie |
| 生命周期 | 登录：UI → `StartQrLogin`/`SignInWithCookie` → 队列线程校验 → 成功才 `put` + 写会话行 + commit → **commit 之后**发 `OnlineSessionChanged`；失败不落盘，错误进 `lastError` |
| 退出/换号 | `SignOut` → 先 `secretStore.erase(ref)` → 再删会话行 → commit → 事件（反过来会留下孤儿密文） |
| 内存 | 明文只在 handler 局部作用域活一次调用；用完 `SecureZeroMemory`（在 `platform/win` 里做）。cookie 字段**不得**出现在任何日志、`Error.message`、事件、DTO |
| 日志纪律 | 适配器里按白名单打：URL **去掉 query 的敏感项**、状态码、耗时。`cookie`/`__csrf`/`params`/`encSecKey` 一律不打；搜索词 `s` 可以打 |
| 过期判定 | 不猜有效期：任何一次调用返回 3xx/-460/403 → `Unauthenticated`，`next_retry_at` 置 0（不自动重试），UI 提示重新登录。**不做自动刷新**（第三方无 refresh 语义） |

---

## 7. 播放链路

### 7.1 为什么可以直接把 URL 交给播放链路

本机 vcpkg 构建的 FFmpeg 配置日志（`D:/vcpkg/buildtrees/ffmpeg/build-x64-windows-rel-config.log`）里：

```
--enable-schannel
https_protocol=yes
https_protocol_select=tls_protocol
```

即：**MSVC 版 FFmpeg 自带 HTTPS（走系统 Schannel，不需要 OpenSSL 运行时）**，`avformat_open_input` 吃 `https://` URL 就能播。

R0 实测补充三条：CDN 直链是 **`http://`**（不是 https）、**20 分钟过期**（`expi=1200`）、对 `Range: bytes=0-N` 回 **206 + `Content-Range`**（即 seek 可用）；匿名请求 `lossless` 会被**静默降级**到 `exhigh`(256 kbps)，所以音质标签一律回读响应里的 `level`/`br`，不要照抄请求参数。

因此 `Playback` 契约**一行不改**：

```cpp
// playback/contracts/Commands.h —— 只改注释，字段不动
struct QueueEntry {
    std::uint64_t trackId = 0;
    std::string filePath;   // 「可播放的源」：本地绝对路径 **或 http(s) 直链**
    ...
};
```

`trackId` 一律填 **0**（在线曲目不属于本地曲库）。`SqlitePlaybackRecorder` 收到 `TrackFinished{trackId=0}` 时必须**显式早退**并补一个单测——现在那句 `UPDATE tracks SET play_count = play_count + 1 WHERE id = ?` 碰巧零行命中，但"碰巧没副作用"不是契约。

### 7.2 `FfmpegOutput` 必须补的 5 项

| # | 改动 | 不做会怎样 |
| --- | --- | --- |
| 1 | [FfmpegDecoder.cpp:106](platform/audio/src/FfmpegDecoder.cpp#L106) 的 `avformat_open_input` 前挂 `AVIOInterruptCB`（绑 `std::atomic<bool> cancel_`） | 切歌/停止时解码线程卡在 DNS/TLS 上几十秒不返回，只能杀进程 |
| 2 | 同一个 `options` 字典里加 `rw_timeout=15000000`、`reconnect=1` | 弱网下永久阻塞（FFmpeg 默认无超时） |
| 3 | `durationMs <= 0` 时不 clamp 成 0，标"时长未知" | 进度条每秒 0%→100%，出现 `45:28 / 2:02` 那类事故 |
| 4 | `pump()` 返回 0 且环形缓冲**未满** → 判"等网络"（Buffering），不当 `atEnd()` | 卡顿被误判播完，直接跳下一首（"每首播两秒"） |
| 5 | `avio_size`/`avio_tell` 算 `bufferedPercent()`（-1 = 不可知） | 缓冲反馈只能靠猜 |

第 1–2 项是**上线门槛**；3–5 决定 UI 是"能用"还是"体面"。

### 7.3 直链过期与自动重解析

- 队列里存的 URL 约 20 分钟失效。`OnlineViewModel` 的 500ms 快照 tick 里：若 `state == Failed` 且 `Player.filePath` 命中本会话的 URL 表 → 自动重解析该首 + `JumpToTrack` 同一 queueIndex，`searchStatus = "链接过期，正在重新解析…"`。
- 连续失败 2 次 → 停手，显示"这一首拿不到播放地址"，本次会话内把该曲标 `playable=false`（避免点一次卡一次）。

### 7.4 `PlaybackState::Buffering`

新增一个枚举值，连锁改动只有三处 switch（`AudioOutput.h` 的 `toName`、`PlaybackService.cpp` 的状态机、`PlayerViewModel::stateLabel`），全部无 `default`，少一处 `-Wswitch` 就报错。`PlayerBar.qml` 进度条下方加 2px 缓冲条（`bufferedPercent == -1` 则隐藏）；`durationKnown == false` 时进度条进"不确定态"（墨块来回走，动画只动 `x` 不动 `opacity`）。

---

## 8. 界面落点

### 8.1 新增第 4 个 QML 单例 `Online`

[host/main.cpp:289-291](host/main.cpp#L289-L291) 现在注册的是 `Library` / `Player` / `Lyrics`。`AGENTS.md` 规矩 9 写的是 `Library/Player/Settings/Sfx`——**文档已过期**（`Settings` 与 `Sfx` 都不存在，`Sfx` 是 `comic/SfxText.qml` 组件）。这次的改动必须顺手把规矩 9 改成现实：`Library/Player/Lyrics/Online`。

不新增第 5 个单例；在线搜索塞进 `Online`，不塞进 `Library`。

### 8.2 文件级清单

| 文件 | 动作 |
| --- | --- |
| `presentation/qml/Shell.qml` | 侧栏把 `enabled: false` 的"歌单"改成"在线音乐"页；底部加账号胶囊 |
| `presentation/qml/pages/OnlinePage.qml`（新） | 搜索框 + 结果列表 + 分页 + 三态 |
| `presentation/qml/comic/TrackRow.qml`（改） | 加一个可选 `badge` 属性（"无版权"/"试听"），在线结果行直接复用它，不写第二个 delegate |
| —— **不新建** `StateBlock.qml` | 三态复用现有 `comic/MangaPanel.qml`（纸面底 + 网点）+ `comic/SfxText.qml`（"翻…"）与 `pages/LibraryPage.qml` 的空态排版，只切 `visible` |
| `presentation/qml/comic/LoginPanel.qml`（新） | 覆盖层：二维码 / 粘贴 cookie / 失败重试（**实测后改为 `Canvas` 画点阵**，见 [§17.1](#171-扫码登录能用的端点和不能用的端点)） |
| `presentation/qml/layout/PlayerBar.qml` | 缓冲条；顺手修 `stateText.indexOf("失败") >= 0` 这个**永不成立**的既有缺陷（`stateLabel` 返回的是"出错"） |
| `presentation/include/sonora/presentation/RowsModel.{h,cpp}`（新） | 先抽基类：`QAbstractListModel` + `count`/`status` + 异步填行（约 30 行），`TrackListModel` 与 `OnlineListModel` 都继承，避免复制第二份骨架 |
| `presentation/src/OnlineViewModel.{h,cpp}`（新） | 上面的 `Q_PROPERTY` 与轮询（照 `PlayerViewModel` 的 `QTimer` 模式，不另发明） |
| `resources.qrc` | 每个新 QML 都要登记，否则运行时 `qrc:/qml/xxx.qml: No such file`（不是编译错误） |

### 8.3 字体

新增中文文案（缓冲中 / 扫。/ 无版权 / 离线 / 链接过期…）→ 改完必须跑 `python tools/subset_fonts.py`，CI 的 `--verify` 会拦。

`tools/subset_fonts.py` 的 glob（`modules/*/*/src/*.cpp`）已覆盖 `modules/online/{core,adapters}/src/*.cpp`；**新代码扁平放在 `src/` 下**，别再嵌一层子目录，否则非递归 glob 会漏。

---

## 9. 依赖

### 9.1 新增

| 依赖 | 用途 | 位置 | 理由 |
| --- | --- | --- | --- |
| **Qt6 Network** | HTTP/TLS 传输 | 仅 `Sonora.Platform.Net` | 唯一新增 Qt 模块；TLS 走系统 Schannel，不引 OpenSSL |
| **nlohmann/json 3.12.0** | JSON 解析 | `third_party/nlohmann_json/`（单头，vendored） | 解析要在零 Qt 的代码里，才能用 fixtures 跑进 `sonora_tests`。版本随仓库走，与 SQLite amalgamation 同构 |
| **crypt32 / DPAPI** | 凭据加密 | 仅 `Sonora.Platform.Win` | 系统 API，零额外依赖 |

`docs/DEPENDENCIES.md` 需要三处更新：[DEPENDENCIES.md:60](docs/DEPENDENCIES.md#L60) 的 `Network` 状态、"M6" → 本期；[:144](docs/DEPENDENCIES.md#L144) 的 libcurl/OpenSSL 那条补一句"TLS 走 Schannel，仍不引 OpenSSL"；[:145](docs/DEPENDENCIES.md#L145)、[:157](docs/DEPENDENCIES.md#L157) 的 nlohmann/json 从"观察/拒绝"改成"采纳，3.12.0 写死"。

### 9.2 为什么 JSON 不放适配器里用 Qt 解析

用 `QJsonDocument` 会把解析绑到 Qt：要么解析逻辑不可测，要么逼出一个 Qt 测试 target，让 `sonora_tests.exe` 启动时依赖 Qt DLL——**测试的隔离度不能为解析器让路**。所以解析代码零 Qt，`nlohmann/json` 只出现在 `.cpp` 里，公开签名只出入 std 类型。

### 9.3 不新增的

`libcurl`、`OpenSSL`、`QtWidgets`、任何 HTTP 框架、任何 logger/metrics 库。`boundaries.py` 的禁用头清单不变。

### 9.4 现代库用法约定（写代码时照这张表来）

| 场景 | 用 | 别手写 |
| --- | --- | --- |
| 超时 | `QNetworkRequest::setTransferTimeout()` | `QTimer` + `QEventLoop` 的两段超时 |
| 串行执行 | `QThread` + `moveToThread` + `QueuedConnection` | 自造 `std::jthread` 执行器 |
| 取消 | `QNetworkReply::abort()` | 标志位轮询 |
| 时长与时钟 | `std::chrono::milliseconds` + `steady_clock` | `int ms` / `float 秒` 到处混 |
| 时间戳 | `duration_cast<milliseconds>(steady_clock::now().time_since_epoch())` 封成一个 `nowMs()` | 每处自己 `GetTickCount` |
| 只读字符串参数 | `std::string_view` | `const std::string&` 收临时对象再拷一份 |
| 真缺失的值 | `std::optional<T>`（`url`、`albumId`） | `""` / `-1` 当哨兵 |
| 错误传递 | `kernel::Result<T>`（项目已统一） | 异常、错误码出参 |
| JSON | `nlohmann/json`：`parse` + `contains()` + `value()` | `QJsonDocument`、手写 `find("\"code\"")` |
| 格式化 | `std::format` | `QString::arg` 链、`snprintf` |
| 并发原语 | **单 worker 线程，不加锁**；确需原子量用 `std::atomic` | `std::mutex` + 手工锁序 |

**候选库的否决理由**（下次别再问一遍）：

| 候选 | 否决 |
| --- | --- |
| libcurl | 与 `QtNetwork` 重复，多一份 TLS 栈和 DLL |
| OpenSSL | Qt 6 在 Windows 上走系统 Schannel，不需要自带 TLS |
| cpr / httplib / Boost.Beast | 为发五个 GET 引一整个 HTTP 框架，依赖体量远超收益 |
| spdlog | 项目已走 Qt 的 `qDebug`/`qInfo` 通道，多一套 sink 只是多一份配置 |
| 任何 ORM / 查询构造器 | 已有 `UnitOfWorkScope` + 手写 SQL，够用 |

---

## 10. 与 CI/CD 的交集

`windeployqt` 会多带 `Qt6Network.dll` 与 `tls/qschannelbackend.dll`。发布流水线需要：

- [.github/workflows/release.yml](.github/workflows/release.yml) 里那份"打包后硬核对"清单补上 `Qt6Network.dll`（TLS 后端目录按 `windeployqt` 实际输出断言）；
- 冒烟步骤加一条**弱网/离线可接受性**：进程启动不因缺 TLS 后端而崩（缺后端只表现为"搜不到歌"，不会崩，所以必须显式断言 `tls/qschannelbackend.dll` 存在）；
- CI（`.github/workflows/ci.yml`）不需要真实网络钢性测试：`Online.Core` 的节流/取消/重试/降级全部用假 transport + 假钟，跟 `sonora_tests` 一起跑。

---

## 11. 分期计划

| 阶段 | 内容 | 人日 | 验收（必须实测） |
| --- | --- | --- | --- |
| **R0 探针** | `tools/probe_online.sh`（`curl`；摘要用 python，本机无 jq；**不建 C++ target**）：验证 search / detail / song-url / lyric / 账号校验五个端点是否可用、是否被 `-460` 风控拦、要不要 weapi 签名（AES-128-CBC ×2 + RSA）、限流阈值、TLS 是否走 Schannel。真实响应存 `tests/fixtures/online/*.json`，结论写进本文 §12 | 0.5 ✅ **已完成 2026-11-07** | 五个端点各有明确结论；**若全被加密挡住 → 整条排期 +1~2 人日**（需 `platform/crypto` 用 CNG `bcrypt` 做签名）。此时不进 R1（**实测未触发**：六个端点零 `-460`，见 [§15](#15-r0-实测结论2026-11-07)）|
| **R1 骨架** | `online` 三 target + `platform/net`（内含 `SerialWorker` 与 `QtHttpTransport`）+ Contracts + 组合根接线 + 空壳页 | 1.5 | 27 个 target 编译通过；`sonora_tests.exe` 双击可跑（不依赖 Qt DLL）；`boundaries.py` pass；`xmake r sonora` 能启动 |
| **R2 搜索闭环** | `NeteaseProvider` + JSON codec + fixtures 单测（正常/空/`-460`/字段 null/数字变字符串/超长标题，≥6 例）+ 节流取消 + 三态 UI | 2–3 | 连打 5 字只发 1 次请求（日志可证）；旧 token 结果不进模型；断网显示 Offline 而非弹窗；本地曲库页无感 |
| **R3 出声** | FfmpegOutput 5 项 + 直链解析（批量 ≤200）+ 表现层拼 `PlayTracks` + `Buffering` + 缓冲条 + 过期重解析 | 2 | 点在线歌能出声；进度/暂停/切歌与本地无异；拔网线 15s 内进 Failed 不永久卡；切歌 10 次无一次卡超 1 秒；`trackId=0` 不改任何 `play_count` |
| **R4 登录** | 扫码（data URL）+ 粘贴 cookie + DPAPI + 账号胶囊 + `GetSourceStatus` 轮询 | 2 | 重启仍是登录态；`.sqlite` 里 `grep MUSIC_U` 为空；登出后密文被删；cookie 不出现在任何日志 |
| **R5 收尾** | 降级矩阵落实、缓存上限设置、诊断查询、文档同步、CI 打包断言 | 1 | 把 provider 端点改成 404 域名 → 应用照常播本地曲库，仅在线页显示"接口可能已变更" |

**合计 9–10 人日**（R0 判定需加密签名时 +1~2）。R0 是唯一能证伪整件事的一步，必须第一个做。

### 11.1 关键路径

```
R0（端点可用性）✅ ──► R1（骨架）──► R2（搜索）──► R3（出声）──► R4（登录）──► R5
      │                                                            
      └─ 若需 weapi 签名：platform/crypto + NeteaseSigner（+1~2 人日，可与 R2 并行）
```

R4 与 R3 无依赖关系，可以并行；R1 的 `IHttpTransport` 一旦定稿，R2/R4 互不阻塞。

### 11.2 任务清单（todolist）

**维护约定**：勾选状态就维护在本文件里；一个任务 = 一个可独立 review 的提交；**没实测通过不许勾**。任务编号 `R<阶段>-<序号>` 直接用作提交信息前缀（如 `R2-3: 搜索防抖与 token 丢弃`）。

#### R0 探针（gate，不做完不进 R1）—— **已完成 2026-11-07，结论见 [§15](#15-r0-实测结论2026-11-07)**

- [x] **R0-1** `tools/probe_online.sh`：`curl` + `jq` 的薄壳，子命令 `search / detail / url / lyric / account / ratelimit`，选项 `--cookie --keyword --ids --out`；**不建 C++ target、不碰 `targets.lua`**（发五个 GET 不需要 Qt）
- [x] **R0-2** 夹具落盘：`--out` 原样保存响应到 `tests/fixtures/online/<子命令>.json`（真实响应直接当夹具，不手写）；`jq` 只用来打印摘要（`code`、`result.songs|length`），不改写保存内容
- [x] **R0-3** search：`/api/search/get` 与 `/api/cloudsearch/pc` 各打一次，记录状态码、耗时、是否 `-460`/`403`、字段结构
- [x] **R0-4** detail：`/api/v3/song/detail`，测出 `ids` 批量上限（10 / 50 / 200 各试一次）
- [x] **R0-5** song-url：`/api/song/enhance/player/url/v1`，记录 `url` 是否 null、`fee`/`payed` 语义、**直链真实有效期**（取一次地址，每隔 5 分钟请求一次直到 403，记下分钟数）——实测 `expi=1200`；同一 URL **+10 分钟、+21 分钟仍 206，+26 分钟 403**，真实寿命落在 21–26 分钟之间
- [x] **R0-6** lyric：`/api/song/lyric`
- [x] **R0-7** account：`/api/nuser/account/get`，未登录与已登录两态各一次
- [x] **R0-8** 限流探测：同一端点连打 20 次（间隔 200ms），记录第几次开始被拦、拦的形态（HTTP 码 / 厂商 code / 验证码）
- [x] **R0-9** TLS 核对：探针与 `curl -v` 各一次，确认走 Schannel、证书链正常、无 OpenSSL 依赖
- [x] **R0-10** 结论回写：[§12](#12-风险与待定) 待定问题 1 改成实测结论，更新 §11 人日与 §11.1 关键路径
- [ ] **R0-11**（**条件任务**，仅当 R0-3~R0-6 被加密挡住）`platform/crypto`（CNG `bcrypt`：AES-128-CBC 两次 + RSA 一次）与 `NeteaseSigner`，**算法常量必须带出处注释**，不许凭记忆抄

#### R1 骨架

> 这 11 项在 R2 开工前就已全部落地（R2–R4 都建在它上面），但复选框一直漏勾。R5 收尾时按代码逐条复核后补勾：`BUSINESS` 含 `online`（`xmake/targets.lua:80`）、`platform/net` 是全项目唯一 `QtNetwork` 出现处、`QtHttpTransport.cpp:74` 有 GUI 线程断言（`Q_ASSERT(QThread::currentThread() != app->thread())`）、四个策略纯函数在 `tests/online_policy.cpp` 有 5 个用例、四张缓存表在 `OnlineSchema.cpp:22-45`、`host/main.cpp:307-457` 的构造与关停顺序（`netWorker.post(reset transport)` → `netWorker.stop()`）都对得上。

- [x] **R1-1** `xmake/targets.lua:78` 的 `BUSINESS` 加 `"online"`，确认三个 target 自动生成并出现在 `presentation_deps` 里
- [x] **R1-2** `platform/net` 新 lib target + `SerialWorker`（`QThread` + `moveToThread`）+ pimpl 公开头（`include/sonora/platform/net/QtHttpTransport.h`），**全项目唯一** `qt = {"QtCore","QtNetwork"}` 的地方
- [x] **R1-3** `online/contracts/Transport.h`：`HttpRequest` / `HttpResponse` / `IHttpTransport`（签名照 §3.4）
- [x] **R1-4** `QtHttpTransport::send`（**异步**）：QNAM 单线程成员 + `setTransferTimeout()` + `QNetworkReply::finished` → 回调 + `cancel()` 映射 `abort()` + **GUI 线程断言** + `maxBodyBytes` 截断 + 非 `https://` 的**API 请求**直接拒绝（播放直链是 `http://`，不经这里，直接交给 FFmpeg）
- [x] **R1-5** `platform/net/SerialWorker`：`post()` 走 `QMetaObject::invokeMethod(Qt::QueuedConnection)` + `stop()` 幂等；**不放进 Kernel**（理由见 [§4.2](#42-执行器platformnetserialworker不进-kernel)）
- [x] **R1-6** `online/contracts`：§3.1–§3.3 的全部 DTO / 命令 / 查询（只签名，无实现）
- [x] **R1-7** `online/core` + `adapters/OnlineService` 骨架 + 4 个策略纯函数（`Backoff` / `FailureCounter` / `TokenBucket` / `ErrorMap`）+ 单测（假钟、假 transport）；**不写熔断器**（[§5.1](#51-数值可直接写进代码)）
- [x] **R1-8** `online/adapters`：`SqliteOnlineStore` + 迁移（`online_search_cache` / `online_detail_cache` / `online_song_cache` / `online_session` 四张表）+ 单测
- [x] **R1-9** `host/main.cpp`：`new` 出 `SerialWorker` / `QtHttpTransport` / `OnlineService`，注册第 4 个单例 `Online`，**关停顺序 worker 先于 transport**
- [x] **R1-10** `OnlineViewModel` 空壳 + `OnlinePage.qml` 空壳 + `resources.qrc` 登记 + `Shell.qml` 侧栏入口（替掉 `enabled: false` 的"歌单"）
- [x] **R1-11** gate：`xmake -r -j8` 通过；`dumpbin /dependents sonora_tests.exe` 无 `Qt6*.dll`；`python xmake/checks/boundaries.py` pass

#### R2 搜索闭环

- [x] **R2-1** `NeteaseProvider::search`：URL/header/参数构造集中在 `core/provider/NeteaseUrls.cpp`，header 走白名单（`Referer` + 桌面 `User-Agent` + 非空才加 `Cookie`）。**实测改走 `/api/cloudsearch/pc`**：`/api/search/get` 的 `album` 里只有 `picId`（算不出封面 URL），cloudsearch 才有 `al.picUrl` 与 `privilege`。
- [x] **R2-2** JSON codec + 9 个用例（`tests/online_codec.cpp`）：legacy 形状 / cloudsearch 形状 / `result: null` 空结果 / `-460` / `301`+`400` / 数字被序列化成字符串 / 字段 `null` 降级 / 300 字超长标题 / 无 `hasMore` 按满 20 首推断。全仓测试 130 用例、4927 断言通过。
- [x] **R2-3** 280ms 防抖 + `token` 递增 + 旧 token 结果丢弃（`JobRegistry::findWritable`，旧 token 只能读）+ 新输入 `CancelOnlineJob` 掐 in-flight（`cancel` 也投到 worker 线程，不能从主线程 `abort()`）。
- [x] **R2-4** `OnlineViewModel` 分页模型：`loaded`/`hasMore`/`revision`，只在 `revision` 变化时 `FetchOnlineRows`；`OnlineListModel` 继承 `RowsModel`，role 名与 `TrackRow` 逐字对齐。
- [x] **R2-5** 在线页四态（Loading/Empty/Error/Offline）复用 `comic/MangaPanel.qml` 与 `pages/LibraryPage.qml` 的空态排版，**不新建 `StateBlock.qml`**。空态文案用普通 `Text` + `Theme` 令牌，没套 `SfxText`：长句（"被网易云风控拦截（-460），稍后再试"）加描边特效后可读性反而下降。
- [x] **R2-6** 搜索缓存：`online_search_cache`（PK `provider/keywords/offset`）+ TTL 10 分钟 + 最多 200 行（`trimSearch`）。**缓存里存原始 JSON**，解析器升级不用迁移；命中时不立刻解码，只把原文塞进待处理队列，由 50ms 轮询的 `drain()` 解码（命令 handler 有 5ms 预算）。
- [x] **R2-7** 手测记录：模拟连打 5 个字（`j`/`ja`/`jay`/`jayc`/`jaych`，间隔 60ms）→ 证据文件只有 `edit ×5` + `commit jaych ×1`，**只发 1 次请求**；另用真实键盘在在线页输入 `qilixiang` 回车，列表出现 30 行（封面/标题/艺术家/专辑/时长齐全），侧栏计数 30，`加载更多` 出现。
- 注：`R2-5`/`R2-7` 的截图与证据留在 `.test_tmp/`（已 gitignore），不进仓库。
- 顺带修掉一个只有真跑才能发现的 bug：`RowsModel::rowCount()` 读的是 `count_`，而 `setRows()` 先 `endResetModel()` 后 `assignCount()`，视图在 reset 时拿到旧行数 0 → 表现为"侧栏计数 30、列表一片空白"。修法：`assignCount()` 必须在 `endResetModel()` 之前调用。

#### R3 出声

> R3-1 ~ R3-5 与 R3-8 不依赖在线模块，可以**提前开工**（风险最低、收益最先兑现）。

- [x] **R3-1** `FfmpegDecoder` 挂 `AVIOInterruptCB`，绑 `std::atomic<bool> cancel_`；`close()` 里置位并等待解码线程退出
- [x] **R3-2** 同一个 `options` 字典加 `rw_timeout=15000000`、`reconnect=1`
- [x] **R3-3** `durationMs <= 0` 时标"时长未知"，不 clamp 成 0
- [x] **R3-4** `PlaybackState::Buffering` + `toName` + `PlaybackService` 状态机 + `stateLabel` 三处 switch（无 `default`）
- [x] **R3-5** `bufferedPercent()`（`avio_size`/`avio_tell`，-1 表示不可知）+ `PlayerBar.qml` 缓冲条 + 时长未知的不确定态进度
- [x] **R3-6** `NeteaseProvider::resolveUrls`（≤200 首/次，串行分批）
- [x] **R3-7** 表现层拼 `QueueEntry`（`trackId = 0`，`filePath` = 直链）并派发 `PlayTracks`
- [x] **R3-8** `SqlitePlaybackRecorder`：`trackId == 0` 显式早退 + 单测（不能依靠"UPDATE 零行命中"）
- [x] **R3-9** 直链过期重解析：`Failed` + URL 命中本会话表 → 重解析 1 次 + 同 `queueIndex` 重跳；连续 2 次失败停手
- [x] **R3-10** 顺手修 `PlayerBar.qml` 里 `stateText.indexOf("失败") >= 0` 这个永不成立的判断
- [ ] **R3-11** gate：切歌 10 次无一次卡超 1 秒；拔网线 15 秒内进 `Failed` 不永久卡；连播 30 分钟不自跳（R5 收尾未复测：切歌 10 次与 30 分钟连播未做，拔网线 15 s 未做；见 §18.4 #8/#9）
      > 已实测：切歌 10 次（0 warn / 0 error / 队列连续推进，见 [§16.2](#162-直链播放的实测数字)）。
      > **未实测**：断网 15 秒、连播 30 分钟——本机不能摘网，挂机时长也不够，见 [§16.3](#163-未在本机验证的两件事如实记录)。

#### R4 登录

- [x] **R4-1** `StartQrLogin` / `CancelQrLogin` / `GetQrLoginState`（实测：`unikey` 端点 + `client/login` 的 800/801/802/803；没有单独的枚举，`QrPollResult.code` 直接带原始码值）
- [x] **R4-2** 登录面板扫码/粘贴两个入口（**改了做法**：不生成 PNG，改传点阵给 `Canvas` 画格子——省掉图片编码器，也就没有 `data:` URL 和 `QQuickImageProvider` 两件事）
- [x] **R4-3** 1.5 秒轮询由 `GetQrLoginState` 驱动（界面不持计时器）；取消/过期/失败各有出口
- [x] **R4-4** `SignInWithCookie`：校验成功才落盘；失败只进 `lastError`（301 → 「这个 cookie 已经失效，请重新复制一次」）
- [x] **R4-5** `platform/win/DpapiSecretStore`：`CryptProtectData` + `CRYPTPROTECT_UI_FORBIDDEN` + 固定熵 + `SecureZeroMemory`（`.test_tmp/dpapi` 独立验证 5 条全过）
- [x] **R4-6** 会话表 + `secret_ref` + 顺序（登录先密文后行、登出先删密文；行写失败会把密文 `erase` 掉）；**未用 `UnitOfWorkScope`**——只有一条 `INSERT OR REPLACE`，包裹起来只多两次 begin/commit
- [x] **R4-7** 账号胶囊 + 登录/登出 + 登录中的等待态（`Expiring` 还没触发源，留到 R5）
- [x] **R4-8** 审计通过：`MUSIC_U`/`__csrf` 只出现在 5 个生产文件（`auth/CookieHeader.{h,cpp}`、`codec/NeteaseJson.{h,cpp}`、`adapters/OnlineService.cpp`）+ 2 个测试文件；`sonora.sqlite` 原始字节里 `MUSIC_U`/`__csrf` 计数都是 0（本机没装 `sqlite3`，用 Python 直接扫文件）
- [ ] **R4-9** gate：重启仍登录 / 换号 / 服务端失效后提示重登（R5 无可用账号，未重测；R4 时实测过，见 §18.4 #10）
  - 已实测：失效提示（假 cookie → 「这个 cookie 已经失效，请重新复制一次」）。
  - **未实测**：重启仍登录、换号——需要一个真实网易云账号扫码成功一次，本机没有。代码路径（`restoreSession()` 先读库昵称渲染胶囊再异步校验；登出删密文再删行）已就绪，等有账号的人补一次。

#### R5 收尾

- [x] **R5-1** [§5.3](#53-降级矩阵) 降级矩阵逐条手动验证，记录现象 —— **5 行实测见 [§18.1](#181-降級矩阵实测2026-11-08)，其余 3 行未复现，原因写在表里**
- [x] **R5-2** `GetOnlineDiagnostics`（一次性快照：连续失败计数、令牌余量、缓存条数、最近 10 条错误）—— 真机 JSON 见 [§18.2](#182-诊断快照实测)
- [x] **R5-3** 缓存上限落地（超限按 `last_used_at` 淘汰）；`Preferences` 仍不可写，按 [§12](#12-风险与待定) 待定问题 2 写死常量 —— 见 [§18.3](#183-缓存上限实测)
- [x] **R5-4** [§14](#14-需要同步修改的既有文档) 文档同步全量执行（ARCHITECTURE / IMPLEMENTATION / DEPENDENCIES / AGENTS.md）
- [x] **R5-5** `.github/workflows/release.yml`：硬核对加 `Qt6Network.dll` 与 `tls/qschannelbackend.dll`，冒烟加断言
- [x] **R5-6** `python tools/subset_fonts.py` 重跑，再 `--verify`（CI 会拦）—— 998 字 / 165 KB
- [x] **R5-7** [§13](#13-验收清单可当-pr-checklist) 验收清单 12 条全绿，真机跑一遍截图 —— 逐条结果见 [§18.4](#184-第-13-节验收清单逐条结果)，未过的两条如实标注

#### 跨阶段常驻

- [ ] **X-1** 每个提交前跑 `python xmake/checks/boundaries.py` + `xmake build sonora_tests && ./build/windows/x64/debug/sonora_tests.exe`
- [ ] **X-2** 新增任何界面文案 → 立刻 `python tools/subset_fonts.py`（忘了就是掉字体，视觉上很明显）
- [ ] **X-3** 每个阶段收尾更新本文件的勾选状态与 [§12](#12-风险与待定) 风险表

### 11.3 任务依赖

硬前置（箭头 = 必须先完成）：

```
R0-1 ─► R0-2 ─┬─► R0-3 ┐
              ├─► R0-4 │
              ├─► R0-5 ├─► R0-10 ─► R1-1 … R1-10 ─► R1-11 ─┬─► R2-1 ─► R2-2 ─► R2-3 ─► R2-4 ─► R2-5 ─► R2-6 ─► R2-7
              ├─► R0-6 │                                      ├─► R4-1 ─► R4-2 ─► R4-3
              ├─► R0-7 │                                      └─► R4-4 ─► R4-5 ─► R4-6 ─► R4-7 ─► R4-8
              ├─► R0-8 │
              └─► R0-9 ┘        R0 判定需签名 ─► R0-11（与 R2 并行，不阻塞 R1）

独立可提前：R3-1 ─► R3-2 ─► R3-3 ─► R3-4 ─► R3-5（播放核心，与在线无关）
            R3-8（本地聚合，与在线无关）
依赖在线：  R2-1 ─► R3-6 ─► R3-7 ─► R3-9
全部之后：  R5-1 … R5-7
```

| 阶段闸门 | 通过条件（不满足不许进下一阶段） |
| --- | --- |
| **R0 → R1** | 五个端点各有"可用/需签名/不可用"的明确结论；fixtures 已落盘；§12 结论已回写 |
| **R1 → R2** | 27 个 target 编译通过；`sonora_tests` 不依赖 Qt DLL；`boundaries.py` pass；`xmake r sonora` 能启动并显示空壳页 |
| **R2 → R3** | 防抖与 token 丢弃有日志佐证；断网/空结果/`-460` 三种夹具各有一条单测 |
| **R3 → R4** | 在线曲能出声且切歌不卡；`trackId=0` 不污染 `play_count`（单测 + 数据库前后对比） |
| **R4 → R5** | 重启仍登录；密文可在退出时删除；`grep` 审计通过 |
| **R5 → 交付** | §13 清单 12 条全绿 + 打包产物最小 PATH 下能搜能播 |

---

## 12. 风险与待定

| # | 风险 | 影响 | 前置验证 |
| --- | --- | --- | --- |
| 1 | 非官方接口随时失效/被风控 | 在线功能整体不可用 | R0 探针；分层隔离（`NeteaseProvider` 是唯一知道 URL 与字段名的文件）；字段缺失映射成 `Upstream` 而不是崩溃 |
| 2 | 需要 weapi/eapi 签名（`-460 Cheating`） | 工期 +1~2 人日，新增 CNG 依赖 | R0 探针；**加密常量不要凭记忆抄进代码**，在探针里固化并留出处 |
| 3 | 直链 20 分钟过期 | 长队列播到后面失败 | 批量解析 + `Failed` 时自动重解析 1 次 + 失败 2 次停手 |
| 4 | FFmpeg 网络阻塞 | 切歌卡死、误判播完 | §7.2 的 5 项加固，第 1–2 项是上线门槛 |
| 5 | 凭据泄露（日志/崩溃转储/错误文案） | 账号被盗 | 只在 `SignInWithCookie.cookie` 一个字段出现一次；DTO/事件/日志零出现 |
| 6 | `Platform.Net → Online.Contracts` 反向依赖 | 删 `modules/online` 时平台层一起编不过 | 先例成立（`Platform.Audio → Playback.Contracts`）；第二消费者出现时把 `IHttpTransport` 上移 `Contracts` |
| 7 | 打包缺 TLS 后端 | 安装后"搜不到歌"，冒烟查不出来 | release.yml 硬核对 `Qt6Network.dll` + `tls/qschannelbackend.dll` |
| 8 | 合规 | 用户自行承担 | 首次开启在线功能时提示；只使用用户自己的会话；不提供下载/导出 |
| 9 | 缓存目录写进漫游目录 | 笔记本同步几十 GB | 缓存一律 `%LOCALAPPDATA%/Sonora/cache/` |

### 待定问题

1. **端点路线** —— **已定（2026-11-07 R0 实测）**：走 `/api/*` 明文。六个端点全部 200、零 `-460`、4 rps 连打 20 次未被拦，**不需要 weapi 签名**（R0-11 不触发）。证据见 [§15](#15-r0-实测结论2026-11-07)。
2. **缓存上限是否可配** —— **已定（2026-11-08 R5）**：`Preferences` 仍只有 `probe.h` 占位（R5 又核了一遍，6 个空壳文件），所以上限写死成常量 `SqliteOnlineStore::kMaxSearchRows = 200` / `kMaxSearchBytes = 4 MiB`，并在 `OnlinePage` 的诊断面板里只**显示**不提供开关。等 Preferences 真正能落盘，再把它做成设置项；在那之前给出一个不能保存的开关才是骗人。
3. **二维码端点码值**（800/801/802/803）与返回结构 —— **已定（2026-11-07 R4 实测）**：`801` 等待扫码、`803` 成功且 cookie 只在 `Set-Cookie` 里（拆成 `MUSIC_U` 与 `__csrf` 两条），`800/802` 按进度处理、统一走 `code >= 800` 分支（`modules/online/core/src/codec/NeteaseJson.cpp:429-433`）。证据见 [§17.1](#171-扫码登录能用的端点和不能用的端点)。
4. **第二家源**：`provider` 字段处处保留但只允许 `"ncm"`；第二个源真出现再抽公共层。

---

## 13. 验收清单（可当 PR checklist）

- [x] `python xmake/checks/boundaries.py` pass，且 `grep -rn "Q[A-Z]" modules/online/core modules/online/contracts` 为空（注释也算）
- [x] `xmake build sonora_tests && ./build/windows/x64/debug/sonora_tests.exe` pass，且 exe **不依赖 Qt DLL**
- [x] `python tools/subset_fonts.py --verify` pass（939 → 新增文案后的新数字）
- [x] 命令/查询同步耗时 < 5ms（`CommandBus` 无告警）
- [x] 连打 5 个字只发 1 次请求；改词后旧结果不进模型
- [x] 断网：在线页 Offline，本地曲库与播放全程无感；重连点重试可恢复
- [ ] 播在线曲：`stateText` 依次"装载中→缓冲中→播放中"；`durationMs<=0` 时进度条进不确定态且不出现 `45:28 / 2:02`（⚠️ 状态序列已验；`durationMs<=0` 的不确定态没单独造，见 §18.4 #7）
- [ ] 播放中切歌 10 次，无一次卡超 1 秒；拔网线 15 秒内进 Failed 不永久卡（⚠️ R5 未重测，见 §18.4 #8）
- [ ] 直链过期：自动重解析 1 次成功则继续播；失败 2 次给出"换一首/重试"（⚠️ R5 未复测，见 §18.4 #9；R3 实测过）
- [ ] 扫码登录 → 重启仍登录；登出后密文删除；`.sqlite` 里搜不到 `MUSIC_U`（⚠️ R5 无可用账号，见 §18.4 #10；R4 实测过）
- [ ] 打包：release 包内含 `Qt6Network.dll` 与 `tls/qschannelbackend.dll`，最小 PATH 下能搜能播（✅ 流水线硬核对；⚠️ 未在真 CI runner 上跑过，见 §18.4 #11）
- [x] 真机跑一遍：侧栏"在线音乐"可点，`[qml]` stderr 无 `undefined` 类告警

---

## 14. 需要同步修改的既有文档

| 文件 | 位置 | 改法 |
| --- | --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | [:31](docs/ARCHITECTURE.md#L31) 非目标表 | 删掉"在线音源……留 `IMediaSource` 接缝，v1 不实现"（该接缝在代码里**根本不存在**），改成"在线音源：v1.1 由 `online` 模块实现，见 ONLINE.md，本地曲库不承担" |
| 同上 | §2.2 模块表 / §2.3 依赖边 / §2.4 target 清单 | 加 `online` 行；加两条边 `Platform.Net → Online.Contracts`、`Presentation → Online.Contracts`，并写明"platform 可实现某模块的传输契约（audio 先例）"；target 23 → 27 |
| 同上 | §4 领域模型 / §5.4 / §6.4 / §7 | 新增 §4.x `online` 上下文（**明确写"刻意不建聚合"**：没有需要保护的不变量）；缓存写入是独立事务；`online` schema 表清单；§7.x 在线链路（网络线程、关停顺序） |
| 同上 | 附 A 决策日志 | 新增 ADR：模块定位、URL 直连播放、端口住 Contracts，各自带推翻条件 |
| [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md) | [:759](docs/IMPLEMENTATION.md#L759) 风险表"范围膨胀（想加在线音源）" | 改为"已立项（ONLINE.md R0–R5）"，风险值下调；新增两行：第三方端点失效、版权合规由用户承担 |
| 同上 | §0 里程碑总览 / §3 性能预算 / §7 CI | 插入 R0–R5；加"缓存命中 < 16ms、冷搜索 P95 < 1.5s、首播 < 5s"（不计入 command < 5ms 预算）；CI 只跑假 transport |
| [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) | [:60](docs/DEPENDENCIES.md#L60)、[:144](docs/DEPENDENCIES.md#L144)、[:145](docs/DEPENDENCIES.md#L145)、[:157](docs/DEPENDENCIES.md#L157) | `Network` 改"本期"；补"TLS 走 Schannel，不引 OpenSSL"；`nlohmann/json` 从观察清单移到采纳（3.12.0） |
| [AGENTS.md](AGENTS.md) | 规矩 9（QML 单例清单） | 改成实际的 `Library/Player/Lyrics/Online` |
| [modules/kernel/include/sonora/kernel/Error.h](modules/kernel/include/sonora/kernel/Error.h) | [:16](modules/kernel/include/sonora/kernel/Error.h#L16) 枚举 + [:29](modules/kernel/include/sonora/kernel/Error.h#L29) `toName` | 追加 `Unauthenticated` / `RateLimited` / `Timeout` / `Upstream`（append-only，不动既有值）；`toName` 的 switch 无 `default`，会编译期强制补齐；同时补一条单测断言 `toName` 全覆盖 |
| 同上 | §2 坑位 / §4 目录速查 | 加：`IHttpTransport::send` 只许队列线程调（实现里断言）；cookie 不许进日志/事件；在线曲目 `trackId = 0` 且 `SqlitePlaybackRecorder` 必须早退；`platform/net` 是全项目唯一 QtNetwork 出现处 |
| [.github/workflows/release.yml](.github/workflows/release.yml) | 打包硬核对 + 冒烟 | 加 `Qt6Network.dll` 与 `tls/qschannelbackend.dll` |

---

## 15. R0 实测结论（2026-11-07）

> 探针：[tools/probe_online.sh](tools/probe_online.sh)（bash + curl，**不建 C++ target**，见 [§11.2](#112-任务清单todolist) 的 R0-1）。
> 夹具：`tests/fixtures/online/*.json`（小样本，供 R2 单测）+ `tests/fixtures/online/r0/*`（batch 200 的原始响应，留作证据）。
> 环境：curl 8.22.0（Schannel 后端）、python 3.14.7（本机**没装 jq**，摘要用 `python -X utf8` 内联脚本）、Windows 10 x64。

### 15.1 一句话结论

**走 `/api/*` 明文，不需要 weapi/eapi 签名。** 六个端点全部 HTTP 200，一次 `-460 Cheating` 都没出现；20 次连打也没被拦。因此 [§11.2](#112-任务清单todolist) 的 **R0-11（CNG 加密签名）不触发**，排期按 9–10 人日执行。

### 15.2 端点实测表

| 端点 | 状态 | 关键发现 |
| --- | --- | --- |
| `/api/search/get` | 200 | `result.songCount` + **`result.hasMore`**；`songs[].artists[] / album{} / duration / fee / mvid / alias`；`limit` 上限 **100**（101 与 200 都返回 `{"code":400}`） |
| `/api/cloudsearch/pc` | 200 | **没有 `hasMore`**（用 `songCount` 算下一页）；`songs[].ar[] / al{picUrl 封面} / dt / fee / privilege{}`；`limit ≤ 100`；`offset=0/30/60` 分页各返回不同首曲 |
| `/api/v3/song/detail` | 200 | 参数 `c=[{"id":...}]`；**批量 200 个 id 可用**（返回 198 首——重复 id 被服务端去重）；`186016` = 晴天 / 周杰伦 / 叶惠美 / `dt=269000` / `fee=0` |
| `/api/song/enhance/player/url/v1` | 200 | `data[].url / br / size / md5 / code / expi / level / freeTrialInfo`；**`expi=1200`**；批量 200 可用（响应 32.6 KB）；地址是 `http://m801|m701.music.126.net/...`，query 键 `cdntag` / `vuutv` |
| `/api/song/lyric` | 200 | `lrc.lyric`（标准 LRC，首行 `[00:00.000] 作词 : ...`）+ `tlyric.lyric`（未翻译行是纯时间戳 `[00:00.950]`）；`klyric` / `romalrc` 为空 |
| `/api/nuser/account/get` | 200 | 未登录时 `account: null` 且 `profile: null`——**判游客看 `account == null`，不看 `code`** |

### 15.3 三条会影响实现的硬结论

1. **匿名音质封顶 `exhigh`**。请求 `standard` → `level='standard' br=96002`；请求 `exhigh` / `lossless` / `jymaster` → **全部回落成 `level='exhigh' br=256003`**，不报错、不拒绝。→ 客户端不要把"我请求了无损"当成事实，音质标签一律回读响应里的 `level` / `br`。
2. **直链是 `http://`、20 分钟过期、支持 Range**。`Range: bytes=0-65535` → **HTTP 206**、`Content-Type: audio/mpeg`、`Content-Range: bytes 0-65535/8716769`、`Server: openresty`、`Cache-Control: max-age=31536000`，64 KB 用时 63 ms。→ `QueueEntry.filePath` 直接吃这条 URL 就行；seek 可行（FFmpeg 会用 Range）；过期重解析策略见 [§7.3](#73-直链过期与自动重解析)。
3. **限流不紧**。20 次搜索 @250 ms（约 4 rps）全部 `code 200`，延迟稳定在 0.17–0.31 s，无 429、无验证码。→ [§5.1](#51-数值可直接写进代码) 的 3 req/s 令牌桶继续保留（对"别把号打挂"更负责），但不需要为限流再写状态机。

### 15.4 其它实测

- **TLS**：Schannel 握手成功，`ALPN: server accepted http/1.1`（协商不到 h2），证书吊销检查开启。→ Qt 默认的 Schannel 后端可用，**不引 OpenSSL**；[§9.3](#93-不新增的) 的结论不变。
- **可播判定**：`code=200` 且 `url` 非空 = 可播；`code=404` = 版权下架（如晴天 `186016`）；`code=-110` + `fee=1` = VIP（如 `108914`、`1945894789`）。→ [§8.2](#82-文件级清单) 的 `blocked` 徽标直接映射这两个 code。
- **播放端到端未证**：本机 `ffmpeg.exe` 不在 PATH（vcpkg 没编 `programs`），所以"点一下真出声"只能留到 R3；网络侧（协议 / 直链 / Range）已证。
- **夹具是原始响应**：全部由 `--out` 直接落盘，没有手写；`.headers` 一并保存，方便以后核对 UA / Referer 是否变成必需。
- **直链寿命实测（2026-11-07）**：同一条 200 秒的地址取回后，`+10 分钟` 仍 `206`、`+21 分钟` 仍 `206`、`+26 分钟` `403`。也就是说 `expi=1200` 是**偏保守的下界**，CDN 实际到 21–26 分钟之间才拒。实现上按 15 分钟主动重解析，并把 403 当作"立刻重解析一次再播"。

## 16. R3 实测记录（2026-11-07）

> 这一节只记"跑起来才知道"的东西。R3 的任务勾选在 [§11.2](#112-任务清单todolist)，任务本身按计划完成。

### 16.1 出声了，但有三处只有真跑才会暴露

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| `QML Column: Cannot specify top/bottom/fill anchors for items inside Column. Column will not function.`（整条底部条布局失效） | 我给缓冲条写了 `anchors.top: seek.bottom`，而它的直接父项是 `Column` | 外面套一层 `Item { width: parent.width; height: 0 }`，条本身用 `y: -3` 偏移。Column 只禁用**直接子项**的 anchors |
| `QML Image: Cannot open: file:http://p1.music.126.net/...`（在线封面全空） | `PlayerViewModel::refresh()` 对封面一律 `QUrl::fromLocalFile(path)`，在线曲目的 `coverPath` 本来就是 `http://` | 先判 `http://` / `https://` / `qrc:/` 前缀，命中就原样用，否则才 `fromLocalFile` |
| 点第 2 行，实际从第 27 行开始播 | 取地址接口返回的 `data[]` **顺序与请求的 ids 顺序不一致**（重复 id 还会被去重），照响应顺序建队列就把下标对错了 | `applyPlayback` 改成**按列表行序重建队列**：`unordered_map<songId, entry>` → 遍历 `model_.rows()`，取不到地址的行直接跳过，队列下标用当前 `entries.size()`。这样"第几行"与"队列第几个"天然一致 |

第三条尤其值得记：**不要假设批量接口的返回顺序等于请求顺序**。凡是"请求一批、按位置回填"的地方，都要按业务键重排，而不是按下标。

### 16.2 直链播放的实测数字

- 打开一条 200 秒的 `http://m801.music.126.net/...m4a?vuutv=...`：日志 `[ffmpeg] 打开 ... 44100 Hz / 2 声道 / aac / 317269 ms`，`Using Qt multimedia with FFmpeg version 9.0.2`。**FFmpeg 的 vcpkg 构建确实带 http/https（Schannel）**，[§15.4](#154-其它实测) 的网络侧结论在播放链路上兑现。
- 底部条：真实封面、标题、艺术家、`5:17` 时长、进度递增、按钮切到「暂停」、侧栏 `正在播放 2 / 29`。
- 切歌 10 次：`ffmpeg` 打开记录由 2 条涨到 12 条，全程 **0 条 `[warn]` / `[error]`**，界面始终可响应，队列 `2/29 → 12/29` 连续推进。
- 曲目时长未知时的行为：`durationMs() <= 0` 返回 `-1`，`PlayerBar` 显示 `--:--` 且进度条不可拖（`InkSlider` 的 `enabled: Player.duration > 0`），**不会**再被 `clamp(0, 0)` 夹成 0:00。

### 16.3 未在本机验证的两件事（如实记录）

- **断网 15 秒进 `Failed`**：本机无法摘网（也没有可用的防火墙权限），只验证了"解析失败 → `Failed` → 错误行可见"这条路径（用不存在的本地文件跑过）；`rw_timeout=15000000` 与 `AVIOInterruptCB` 是 R3-1/R3-2 的静态保障，**没有实测断网形态**。
- **连播 30 分钟不自跳**：需要真实挂机 30 分钟，本轮只跑到 ~6 分钟（进度与队列都正常）。另记为后续回归项。

### 16.4 给下一轮的教训

- 合成鼠标点击要**先把窗口挪到固定位置再点**（`.test_tmp/shot3.ps1` 会把窗口移到 (60,60)，此时"截图坐标 = 屏幕坐标 − 60"）；底部播放控制那一行在截图里是 `y ≈ 904`，不是进度条那行的 687。
- 一次点击跑一个 powershell 进程会丢前台焦点，按键一个字符都进不去；键盘输入必须在**同一个进程**里 `AttachThreadInput + SetForegroundWindow` 再 `SendKeys`（`.test_tmp/r3_search.ps1`）。
- Qt 的 `qrc:/` 资源没有磁盘文件，所以 Qt 自带的 `QmlPreviewMcpServer`（QtMCP）对我们**只能热重载、且必须先有磁盘 QML**；要不要开"debug 走磁盘、release 走 qrc"是独立决策，不在 R3 范围。

## 17. R4 实测记录（2026-11-07）

> 扫码登录这一轮把 [§11.2](#112-任务清单todolist) 的 R4-1…R4-9 全部走完（R4-2 改了做法、R4-6 少包一层事务，两条都在清单里写明）。

### 17.1 扫码登录：能用的端点和不能用的端点

| 调用 | 结果 |
| --- | --- |
| `POST /api/login/qr/key` | `{"code":404,"message":"接口未找到！"}` |
| `POST /api/login/qr/key?type=3` | 同 404 |
| `POST /api/login/qrcode/create?key=…&qrimg=true` | 同 404 |
| `POST /api/login/qrcode/unikey?type=1` | `{"code":200,"unikey":"…"}` ✅ |
| `POST /api/login/qrcode/client/login?key=<unikey>&type=1` | `{"code":801,"message":"等待扫码"}` ✅ |

**服务端不返回二维码图片**（`qrimg=true` 也是 404），所以码必须本地画。这一条把 [§8](#8-界面落点) 的界面清单里"二维码（data URL 直接喂 `Image`）"这个假设推翻了，实现改成：`qr-code-generator-cpp`（xmake 包）编码成点阵 → 传给界面用 `Canvas` 画。

### 17.2 三处只有真跑才会暴露的问题

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| 面板能开、二维码区域全白 | `qrSize()` / `qrCell()` 只是普通成员函数，QML 里 `Online.qrSize` 是 `undefined`，`for` 循环一次都不进 | 给 `qrSize` 加 `Q_PROPERTY`、给 `qrCell` 加 `Q_INVOKABLE` |
| 新码不重画（一直是旧码） | 界面只按 `QrLoginDto.session` 决定重画，而拿到 unikey 时没有换 session | `applyQrKey` 里 `qr_.session = ++qrSession_` |
| 扫码成功却没登录 | 803 回包把 cookie 放在 `Set-Cookie` 响应头里，而 `HttpResponse` 当时只有 body | `HttpResponse` 加 `headers`（键小写）+ `decodeQrPoll(body, headers)` 合成 `MUSIC_U=…; __csrf=…` |

### 17.3 凭据落地的实测

- DPAPI 往返（`.test_tmp/dpapi`，独立小程序）：28 字节明文 → 落盘 272 字节密文，`load()` 回读一致，密文里搜不到明文片段，`erase` 幂等，非法 provider（`../etc`）被拒。
- 库里不存 cookie：`sonora.sqlite` 原始字节扫 `MUSIC_U`/`__csrf` 都是 0 次；cookie 只在 `secrets/ncm.bin` 的密文里。
- 假 cookie 真跑：粘贴 `MUSIC_U=bogus-token-123` → 3 秒后红字「这个 cookie 已经失效，请重新复制一次」（服务端 301）。

### 17.4 给下一轮的教训

- **QML 只能看见 `Q_PROPERTY` / `Q_INVOKABLE` / `signal`**。写 C++ 侧的"给界面用的小方法"时忘了加宏，编译期一点提示都没有，运行期只是"什么都没有"。
- 界面靠 `session`/`revision` 这类计数去重时，**凡是会改变内容的路径都要 ++**，漏一条就是"数据对了但界面不动"。
- 登录态要"先渲染再校验"：`restoreSession()` 先读库里的昵称把胶囊点亮，再异步打 `/api/nuser/account/get`，否则网络慢的时候用户看到的是"未登录"，会以为掉登录了。

---

## 18. R5 实测记录（2026-11-08）

> 本机没有可用的视觉通道（`read_image` 只回元数据、`modlens` 没有 provider），所以下面每一条都用**可计算的判据**：日志行、诊断快照的 JSON、SQLite 里的行数、二进制里的字符串、截图的像素统计。凡是只能靠"看一眼"的，都标注为未验证。

### 18.1 降級矩阵实测

| 触发 | 怎么造出来的 | 实测现象 | 计数器 |
| --- | --- | --- | --- |
| 断网 / 域名不存在 | `NeteaseUrls.h` 的 base 临时改成 `https://ncm-api-gone.invalid` | 搜索失败一次；再搜被退避闸门挡住，状态行「连续失败，约 N 秒后自动重试」 | `offline +1`、`consecutiveFailures 0→1`、`recentErrors[0].where = "搜索"` |
| 上游 404（接口变了） | base 改成 `https://example.com`（`/api/search/get` 真的回 404） | 第一次原样报 HTTP 404；**第二次起**消息变成「接口可能已变更，稍后再试（…HTTP 404）」且 `retryable = true` | `upstream +1→+2` |
| 传输超时 | 本机起一个"接了就不说话"的 TCP 服务，base 指向 `https://127.0.0.1:18081` | 8 s 后失败，消息 `Operation timed out`（`transferTimeout` 生效） | `timeout +1` |
| 429 / 客户端节流 | QML 里连发 12 次不同关键词的 `Online.search()` | 前 6 次发出去，其余立刻被令牌桶挡住，状态行「请求太频繁，稍后再试」 | `rateLimited = 6`、`totalErrors = 7` |
| 完全离线启动 | base 保持 404 域名，正常启动并导入 6 个音频文件 | 本地曲库导入成功、双击照常出声（`P=1`，位置 0:02→0:14 连续推进），在线页只在它自己那一栏报错 | 与上表「断网」同一条路径 |
| 未登录访问需登录接口 | —— | **未复现**：本机没有可扫码的手机与账号；该路径 R4 真机走过一次（[§17](#17-r4-实测记录2026-11-07)），R5 没有重新制造会话 | —— |
| 登录过期 | —— | **未复现**：需要一个"先登录成功、再让 cookie 失效"的真实会话，本机做不到；逻辑上由 `Expiring → 静默重试一次 → Guest` 覆盖，`online_auth.cpp` 有单测 | —— |
| 直链解析失败 / 播放中直链过期 | —— | **未复现**：需要一首"有版权限制"或"播到一半签名过期"的在线曲；R3 实测过重解析（[§16](#16-r3-实测记录2026-11-07)），R5 未重造 | —— |

「任何降级都必须留痕」这条落地为：`Diagnostics::recordError(...)` 分类计数 + 最近 10 条环形缓冲 + 面板上的「降级」一行；取消（`Internal` + `"已取消"`）不计数，避免打字时每敲一个字就多一条"错误"。

### 18.2 诊断快照实测

真机启动后（空缓存）的一次快照：

```json
{"cacheBytes":0,"cacheRows":0,"capBytes":4194304,"capRows":200,"consecutiveFailures":1,
 "degradations":{"cacheHits":0,"offline":0,"rateLimited":0,"timeout":0,"totalErrors":1,
                 "unauthenticated":0,"upstream":1,"urlExpired":0},
 "detailRows":0,"nextRetryAtUnix":1794116187,"provider":"ncm",
 "recentErrors":[{"at":1794116186,"message":"…HTTP 404","retryable":false,"where":"搜索"}],
 "songRows":0,"tokenBurst":6,"tokensLeft":6}
```

- 面板**只在可见时**每 2 s 刷一次（`Timer.running: diagSheet.visible`），不可见时一次查询都不发 —— 诊断查询要跑两条 `COUNT` + 一条 `SUM`，放进 50 ms 轮询会把曲库线程拖出 5 ms 预算。
- `where` 只允许「搜索 / 取地址 / 登录校验 / 扫码」四种，**不含 URL、不含 cookie**；`message` 是错误原文，来自 provider，也不含 cookie。

### 18.3 缓存上限实测

- 单测（`tests/online_store.cpp`，5 个用例）：插 300 行 → 剩 200；再插 50 行并 `loadSearch("ncm","k100",0)` 触碰旧行 → trim 后 **k100 还在、k149 没了**（证明 LRU 用的是 `last_used_at` 而不是插入顺序）；70×64 KiB → 字节闸触发，`searchBytes <= 4 MiB`；TTL 过期的行不命中但也不提前删。
- **迁移里踩到的真 bug**：v2 迁移原本带一条 `UPDATE online_search_cache SET last_used_at = fetched_at`，在 200 行 × ~78 KB（≈15 MB）的老库上**直接卡死**——SQLite 报 `unable to open database file`（语句日志落盘失败），而迁移失败会让进程 `return 1`，等于**所有老用户升级后打不开应用**。空表和小表都不复现，只有真库才炸。
- 修法：**迁移只加列，不回填**（`ALTER TABLE ... ADD COLUMN last_used_at INTEGER NOT NULL DEFAULT 0`）；排序改成 `ORDER BY CASE WHEN last_used_at = 0 THEN fetched_at ELSE last_used_at END`，让老行按自己的写入时间排队。新增第 250 行全置 0 的用例锁住这条。
- 真库验证：把 dev 库回退到 v1、塞 200 行大 payload，启动 → 迁移通过、`[info] 在线音源就绪` 正常打印。

### 18.4 第 13 节验收清单逐条结果

| # | 条目 | 结果 | 证据 |
| --- | --- | --- | --- |
| 1 | `boundaries.py` pass；`modules/online/{core,contracts}` 里没有 `Q[A-Z]` | ✅ | 215 个文件通过；`grep -rno "Q[A-Z][a-zA-Z]*"` 只剩 `SQLite` 里的 `QLite` 两处误报（清单原写法会把 `SQLite` 当成 Qt 类型） |
| 2 | `sonora_tests` pass，且 exe 不依赖 Qt DLL | ✅ | 155 用例 / 5115 断言；`sonora_tests.exe` 里搜不到 `qt6core.dll` / `qt6network.dll` |
| 3 | `subset_fonts.py --verify` pass | ✅ | 需要 998 字、子集里 998 字形（165 KB，源字体 11%） |
| 4 | 命令/查询同步耗时 < 5 ms（`CommandBus` 无告警） | ✅ | 真机日志零 `[warn]`（包括 `CommandBus` 的超时告警） |
| 5 | 连打 5 个字只发 1 次请求 | ✅ | 30 ms 间隔敲 `jaych`（不按回车）→ 缓存表只新增**一行**，关键词是完整的 `jaych` |
| 6 | 断网：在线页 Offline，本地全程无感 | ✅ | 见 18.1 第 1、5 行 |
| 7 | 播在线曲：装载中→缓冲中→播放中；`durationMs<=0` 不出现假时长 | ✅（状态序列）/ ⚠️（不确定态没单独造） | 真机 `Online.play(0)` 后 `P=1`、位置 0:01→0:14 连续推进；不确定态只在 `durationMs<=0` 时出现，本次解析拿到了时长，未落在该分支 |
| 8 | 播放中切歌 10 次无卡顿；拔网线 15 s 内进 Failed | ⚠️ 未做 | 需要在线曲长时间播放 + 物理拔网；R3 测过切歌，R5 未重测 |
| 9 | 直链过期自动重解析 | ⚠️ 未做 | 同 18.1 最后一行（R3 实测过） |
| 10 | 扫码登录 → 重启仍登录；登出后密文删除；库里搜不到 `MUSIC_U` | ⚠️ 未做 | R4 实测过（§17）；R5 没有账号可用 |
| 11 | 打包内含 `Qt6Network.dll` 与 `tls/qschannelbackend.dll`，最小 PATH 能搜能播 | ✅（流水线断言）/ ⚠️（未在真 runner 跑过） | `release.yml` 硬核对 + 冒烟断言「在线音源就绪」；本机不产发布包 |
| 12 | 真机跑一遍：侧栏可点、`[qml]` 无 `undefined` 类告警 | ✅ | 起播、搜索、诊断面板、切页各跑一遍，stderr 零 `[qml]` 行 |

### 18.5 给下一轮的教训

- **迁移里不要写"顺手回填"的 UPDATE**。SQLite 在大表上执行 UPDATE 会把语句日志写盘，磁盘/句柄一紧张就 `unable to open database file`；而迁移失败 = 应用打不开。加列就只加列，历史值在读取侧用 `CASE WHEN` 兜。
- **先 kill 再 build**。进程占着 `sonora.exe` 时 `xmake build` 报 `LNK1168` 但退出码可能看着正常，接着跑起来的是旧二进制——"改了没生效"的第一嫌疑永远是它。
- **经写盘工具写 C++ 字符串里的 `\"` 会多一层反斜杠**（`"offset"` 变成 `\"offset\"` → `C2017 未知转义序列`）。SQL 里的保留字列名改写成 `[offset]`，既避开转义也避开歧义。
- **`Q[A-Z]` 这条 grep 会被 `SQLite` 误伤**。写检查脚本时用 `Q[A-Z][a-zA-Z]*` 再排除 `QLite`，或者直接断言"不含 `<Q`"。
- **没有视觉通道时，验收要换成可计算的判据**：迁移成功看日志行、降级看诊断 JSON、防抖看缓存表新增了几行、封面看像素/哈希。凡是"看一眼就知道"的结论，先问一句"我能不能用数字证明它"。
