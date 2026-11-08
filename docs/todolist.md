# Sonora 网易云常用接口接入 · TodoList

> 目标：把网易云的**常用接口**接进现有 `online` 模块，让「在线」页从"只能搜歌"变成"能逛歌单、看榜单、进个人库、听私人 FM、有歌词"。
> 范围：**只做音频**。不做视频、不做评论、不做播客/电台节目、不做云盘、不做手机 App 端同步。
> 预计：12.5 人日（N0 0.5 / N1 3 / N2 2 / N3 1 / N4 1.5 / N5 3 / N6 1.5）。

---

## 0. 这份清单怎么用

- 一个 `[ ]` = **一次可独立提交的改动**，自带文件清单与验收口径；不要攒着一起提交。
- **硬顺序**：`N0 → N1 → (N3 ∥ N5a) → N2 → N4 → N6`。N2/N4 依赖 N0 的探通结论；N5 的详情页吃 N1 的 DTO。
- 每完成一项，必须跑：

```bash
export PATH="/c/Users/123/AppData/Local/xmake-portable:$PATH"
xmake build -y sonora && xmake build -y sonora_tests && ./build/windows/x64/debug/sonora_tests.exe
python xmake/checks/boundaries.py
```

- 提交信息统一 `N1: 歌单详情与全量歌曲分页` 这种格式（`N<段>-<序号>` 可省略）。
- 失败的实验**不要**留在代码里；探不通的端点写进本文档 §3 的"结论"列，并说明退化方案。

---

## 1. 现状基线（动手前先认账）

| 能力 | 现状 | 位置 |
| --- | --- | --- |
| 搜索 | ✅ 通 | `NeteaseUrls.h` 的 `makeSearchRequest`；`OnlineService::requestPage` |
| 播放直链 | ✅ 通 | `makeSongUrlRequest`（匿名最高 `exhigh`，一次 ≤200 首） |
| 账号校验 | ✅ 通 | `makeAccountRequest` |
| 扫码登录 | ✅ 通 | `makeQrKeyRequest` / `makeQrPollRequest`，二维码自己画 |
| 歌单 / 专辑 / 歌手 / 榜单 / 推荐 | ❌ **契约里有枚举，实现里拒绝** | `OnlineService.cpp:57` 的 `if (command.kind != SourceKind::Search)` 直接失败 |
| 我的歌单 | ❌ 空实现 | `OnlineService::handle(GetMyPlaylists)` 返回 `{}`（`OnlineService.cpp:335`） |
| 在线歌词 | ❌ 没接 | `LyricsSourceChain` 只挂了"同名 .lrc → 内嵌标签" |
| 降级/诊断 | ✅ 已就位 | `Diagnostics` + `TokenBucket(3/s, burst 6)` + `FailureCounter` + `[offset]`/`last_used_at` 缓存 |

---

## 2. 端点清单（N0 要逐条探通，`PENDING` 改成实测结论）

> 全部是 `https://music.163.com` 下的**明文 web 接口**，返回 JSON。未公开、无兼容承诺，所以**先探后写**。

| # | 接口 | 路径 | 登录 | 用途 | 结论 |
| --- | --- | --- | --- | --- | --- |
| 1 | 歌单详情 | `/api/v6/playlist/detail?id=` | 否 | 歌单头 + `trackIds`/`tracks` | ✅ 可用（匿名）。`trackCount` 与 `trackIds[]` 可信；`tracks` **只给前 10 首** |
| 2 | 歌单全部歌曲 | `/api/playlist/track/all?id=&limit=&offset=` | 否 | 大歌单分页 | ❌ **404「接口未找到」**（`/api/playlist/track/ids` 同样 404）。改用 `trackIds` + `/api/song/detail?ids=[…]` 分批 |
| 3 | 专辑详情 | `/api/v1/album/{id}` | 否 | 专辑页 + 曲目 | ✅ 可用（匿名），`songs[]` 是**旧版字段** `artists`/`album`/`duration` |
| 4 | 歌手详情 | `/api/v1/artist/{id}` | 否 | 歌手名/封面/简介 | ✅ 可用，但**必须是真实 id**：搜索结果里 `artists[0].id` 可能是 0，拿 0 请求得 404 |
| 5 | 歌手热门歌曲 | `/api/v1/artist/songs?id=&order=hot&limit=` | 否 | 歌手页曲目 | ❌ **400「参数错误」**。改用 `/api/v1/artist/{id}` 返回的 `hotSongs[]`（实测 8 首） |
| 6 | 排行榜列表 | `/api/toplist` | 否 | 20 个榜单 | ✅ 可用（匿名），实测 **65 张**；`list[].tracks` 恒为 null |
| 7 | 推荐歌单 | `/api/personalized/playlist?limit=` | 否 | 首页推荐 | ✅ 可用（匿名），`result[].{id,name,picUrl}` |
| 8 | 新歌速递 | `/api/personalized/newsong?limit=` | 否 | 首页新歌 | ✅ 可用（匿名），`result[].song` 是**新版字段** `ar`/`al`/`dt` |
| 9 | 相似歌曲 | `/api/discovery/simiSong?id=` | 否 | 详情页扩散 | ❌ **400「参数错误」**（换 id 仍 400）。N1 不做，以后单独试参数 |
| 10 | 每日推荐歌曲 | `/api/v3/discovery/recommend/songs` | **是** | 每日 30 首 | ⚠️ **匿名也返回 31 首**（与假设不符）。N2 用登录态复核；未登录先把它当可用，别写死“登录后可见” |
| 11 | 每日推荐歌单 | `/api/v1/discovery/recommend/resource` | **是** | 每日歌单 | ❌ 匿名 `code 301`，确认需登录（N2） |
| 12 | 用户歌单 | `/api/user/playlist?uid=&limit=&offset=` | **是** | 我的歌单；"我喜欢的音乐" = `specialType 5` | ⚠️ 只有 `uid=0`，返回 1 张空壳，**无法判定**；N2 用真 cookie 复核 |
| 13 | 收藏/取消收藏 | `/api/playlist/manipulate/tracks`（POST） | **是** + `__csrf` | 写入"我喜欢的音乐" | ⏸ **未探**（只读原则）。N2 再验 |
| 14 | 私人 FM | `/api/v1/radio/get` | **是** | 连续下一批 | ✅ **匿名也能取**（`code 200`，一次 1 首）；N4 可先按匿名做 |
| 15 | 歌词 | `/api/song/lyric?id=&lv=-1&tv=-1` | 否 | lrc / tlyric | ✅ 可用（匿名），`lrc.lyric` 实测 1471 字符，`tlyric`/`klyric` 都在 |

---

## 3. 任务分解

### N0 · 探通（0.5 人日）——不写生产代码

- [x] **N0-1 探通脚本**（2026-11-08 完成：`tools/probe_ncm_api.ps1`，匿名日志 `docs/probe/ncm-anon.log`、原始响应 `docs/probe/raw/`；**有 cookie 的那次没跑**——手上没有可用账号）
  - 文件：`tools/probe_ncm_api.ps1`（UTF-8 with **BOM** + CRLF，否则 PowerShell 5.1 解析中文注释会炸）
  - 要点：读 `-Cookie` 参数（可空）；对 §2 的 15 个端点逐个 `Invoke-WebRequest`，把 **HTTP 状态 + body 前 400 字 + `code` 字段**打进 `docs/probe/ncm-<日期>.log`；遇到 `302/301` 记下 `Location`。
  - 验收：无 cookie 与有 cookie 各跑一次，两条日志都进仓库（`docs/probe/` 不放进 `dist`）。

- [x] **N0-2 判读并填表**（15 行全部有结论，见 §2；解析后的可读版在 `docs/probe/ncm-anon-summary.md`）
  - 把 §2 的"结论"列改成：`可用` / `需登录（匿名 code=301）` / `已失效（404）` / `可用但字段不全（缺 xxx）`。
  - 验收：15 行全部有结论，没有 `PENDING` 残留。

- [x] **N0-3 写进 `docs/ONLINE.md` §19「常用接口第二批」**
  - 内容：探测日期、逐个端点的请求样例与关键字段、匿名/登录两态的差异、以及"哪些字段不能依赖"。
  - 验收：`§19` 能被下一个人直接照着写代码，不用再猜。

- [x] **N0-4 定档：哪些进 N1**（见下方“N0 结论”）
  - 依据 N0-2：只把"匿名可用且字段齐"的放进 N1；需登录的推迟到 N2；失效的标灰并写退化方案。
  - 验收：本文档 §3 的 N1/N2 条目按结论增删，**不许带着"可能能用"开工**。

**N0 结论（2026-11-08，匿名探测；依据 `docs/probe/ncm-anon-summary.md`）**

- **进 N1（匿名可用、字段齐）**：歌单详情、歌曲批量详情（`/api/song/detail?ids=`，用来替代失效的 track/all）、专辑、歌手（`/api/v1/artist/{id}` + 其中 `hotSongs`）、排行榜、推荐歌单、新歌速递、歌词。
- **不进 N1**：相似歌曲（400）、歌手歌曲列表端点（400）、`playlist/track/all` 与 `playlist/track/ids`（404）。歌单曲目一律"`trackIds` → `song/detail` 分批"。
- **推迟到 N2（要登录）**：每日推荐歌单（301）、用户歌单、收藏写操作；**每日推荐歌曲**匿名竟有数据，N2 只做"登录态复核"，不阻塞 N1。
- **可提前做**：私人 FM 匿名就能取（一次 1 首），N4 不必等登录。
- **四种返回码**：顶层 `code == 200` 是成功；`301` 未登录；`400` 参数错；`404` 接口不存在。解析**只认顶层 `code`**（嵌套对象里也有 `code`，正则抓第一个会误判）。
- **两套字段**：`/api/song/detail`、`/api/v1/album` 是旧版 `artists`/`album`/`duration`；`/api/v6/playlist/detail`、`/api/personalized/*` 是新版 `ar`/`al`/`dt`。编解码必须两种都认。

---

### N1 · 只读资源（3 人日）

> 目标：歌单、专辑、歌手、榜单、推荐歌单、新歌——全部能列、能点、能播（相似歌曲 N0 实测 400，本期不做）。

- [x] **N1-1 契约：DTO 与枚举**
  - 文件：`modules/online/contracts/include/sonora/online/contracts/Types.h`
  - 改动：
    - `SourceKind` 追加 `Chart`、`Artist`、`NewSongs`、`SongDetail`（若探通后需要）；`toName` 同步（`Types.cpp`）。
    - 新增 `struct CollectionDto { std::string id; std::string name; std::string coverUrl; std::int32_t count{0}; SourceKind kind; std::string subtitle; }`（封面/标题/副标题已经是界面可直接用的形态）。
    - 新增 `struct CollectionPageDto { std::vector<CollectionDto> items; bool hasMore{false}; }`。
    - 新增 `struct PlaylistDetailDto { std::string playlistId; std::string name; std::string creator; std::string description; std::string coverUrl; std::int32_t trackCount{0}; }`。
  - 验收：`Types.h` 编译过；新增枚举在 `toName` 有分支（`-Wswitch` 不开，但要有兜底 `return "unknown"`）。

- [x] **N1-2 契约：查询与命令**
  - 文件：`Queries.h`、`Commands.h`
  - 改动：
    - `Queries.h`：`FetchCollections { SourceKind kind; std::int32_t offset{0}; std::int32_t limit{30}; std::string collectionId; }` → `CollectionPageDto`；`FetchPlaylistDetail { std::string playlistId; }` → `PlaylistDetailDto`。
    - `Commands.h`：`RequestOnlinePage` 增加 `std::string collectionId;`（歌单/专辑/歌手/榜单共用一个字段），**不要**为每种资源新增一个命令。
  - 验收：不改任何实现也能编译（只加字段/类型）。

- [x] **N1-3 端点构造：每接口一个 maker**
  - 文件：`modules/online/core/include/sonora/online/core/provider/NeteaseUrls.h` + `.cpp`
  - 改动：`makePlaylistDetailRequest`、`makeSongDetailsRequest`（批量 id，替代失效的 track/all）、`makeAlbumRequest`、`makeArtistRequest`、`makeToplistRequest`、`makePersonalizedPlaylistsRequest`、`makeNewSongsRequest`；全部走同一个 `defaultHeaders(cookie)`；常量集中在文件顶部（`kMaxSongDetailsPerRequest = 500`）。**不要**再写 `makePlaylistTracksRequest`/`makeArtistSongsRequest`/`makeSimilarSongsRequest`——N0 实测它们分别 404/400/400。
  - 验收：`tests/online_codec.cpp` 加 URL 断言（含中文关键词与分页参数），不留任何 URL 拼接散落在服务层。

- [x] **N1-4 编解码：解析器**
  - 文件：`modules/online/core/src/codec/NeteaseJson.cpp` + `codec/NeteaseJson.h`
  - 改动：`parsePlaylistDetail`（只取头 + `trackIds`，**不信 `tracks`**）、`parseSongDetails`（旧版 `artists`/`album`/`duration`）、`parseAlbum`、`parseArtist`（`artist` + `hotSongs`）、`parseToplist`、`parsePersonalizedPlaylists`、`parseNewSongs`（新版 `ar`/`al`/`dt`）。统一规则：**顶层** `code != 200` → `OnlineError`（嵌套也有 `code`，别抓第一个）；字段缺失 → 该行跳过并计数，**不抛异常**；多歌手连成 `" / "`；时长统一 ms（旧版 `duration`、新版 `dt`）。
  - 验收：`tests/online_codec.cpp` 每类响应至少 1 个"正常 + 1 个残缺"夹具（放 `tests/fixtures/ncm/`），断言行数与首行字段。

- [x] **N1-5 服务：按 kind 分发**
  - 文件：`modules/online/adapters/src/OnlineService.cpp`（删掉 `:57` 的 `kind != Search` 拒绝）、`NeteaseProvider.h/.cpp`（新增 `fetchPlaylistDetail` 等入口，回调把结果塞回服务的 pending 队列，模式与 `startSearch` 一致）
  - 要点：
    - 每类资源一个**独立的 token 通道**：翻页用 `offset`，不要复用搜索的游标，否则"先搜歌再点歌单"会把状态搅在一起。
    - 大歌单：先 `playlist/detail` 拿 `trackIds[]`（实测 `trackCount=100` 时 `trackIds` 有 100 条、`tracks` 只有 10 条），再按 500/批调 `/api/song/detail?ids=[…]`；单批失败只标记该批，已加载的保留。
    - 所有失败继续走 `noteFailure(error, "歌单"/"专辑"/"歌手"/"榜单")`，`where` 只用固定词，**不带 id**（诊断面板要给人看，不是给日志抓）。
  - 验收：`tests/online_service.cpp` 用假 transport 跑"歌单 1200 首 → 3 页 → 行数 1200、页尾 hasMore 正确"。

- [x] **N1-6 缓存：两类新数据**
  - 文件：`modules/online/adapters/src/OnlineSchema.cpp`（迁移 v3：**只建表**，不改老表）
  - 改动：
    - `online_collection_cache(provider, kind, params, payload, fetched_at, last_used_at)`，TTL 30 分钟；
    - `online_playlist_tracks_cache(provider, playlist_id, "offset", payload, fetched_at, last_used_at)`，TTL 10 分钟；
    - 清理复用 R5 的 LRU 写法（`CASE WHEN last_used_at = 0 THEN fetched_at ELSE last_used_at END`），上限 200 行 / 4 MiB，跟搜索缓存**共用一个上限**还是各算各的，在实现时二选一并在注释里写清（推荐共用，省一次 COUNT）。
  - 验收：`tests/online_store.cpp` 加"新表 LRU 淘汰"用例；老库升级（已有 v2 表）后启动不报错。

- [x] **N1-7 表现层：集合模型**
  - 文件：`presentation/include/sonora/presentation/RowsModel.h`（`OnlineListModel` 旁边加 `OnlineCollectionModel`）+ `presentation/src/RowsModel.cpp` + `OnlineViewModel.h/.cpp`
  - 改动：`Q_PROPERTY(OnlineCollectionModel *collections …)`、`Q_INVOKABLE void loadCollections(kind, bool more)`、`Q_INVOKABLE void openPlaylist(id)`；`OnlineCollectionModel` 暴露 `id/name/cover/count/subtitle`。
  - 验收：QML 能 `Collections.count` 与 `Collections.get(i)`；模型增删走 `beginResetModel`（一次性换整页，不做增量插入）。

- [x] **N1-8 界面：在线页多标签**
  - 文件：`presentation/qml/pages/OnlinePage.qml`（+ 新建 `presentation/qml/pages/CollectionPage.qml`、`presentation/qml/comic/CollectionCard.qml`）
  - 改动：顶部四个胶囊 `搜索 / 推荐 / 榜单 / 我的`（"我的"在 N2 前显示空态并提示登录）；推荐页两张横滑（推荐歌单、新歌速递）；榜单页 20 个卡片；点卡片进 `CollectionPage`（封面 + 标题 + 曲目数 + "播放全部"）。
  - 注意：`CollectionCard` 里的封面 `Image` 必须给 `sourceSize`（列表性能），且 `import Sonora.Presentation 1.0`（否则 `Player` 未定义只在运行时炸）。
  - 验收：`[qml]` 告警 0；点推荐歌单 → 详情页出曲目 → 点歌出声（这条会复用 `ResolveAndPlayOnline`，不改播放层）。

- [x] **N1-9 字体与门禁**
  - 文案新增（"推荐""榜单""播放全部""每日推荐"…）→ `python tools/subset_fonts.py` 重跑，`--verify` 通过。
  - 验收：`--verify` 输出"覆盖完整"；字体文件进提交。

> **N1 实施记录（2026-11-08）**
>
> - N1-1…N1-5、N1-7…N1-9 已落地；**N1-6（两类新表的 DB 缓存）本轮没做**——见下。
> - 实际签名与计划的偏差，以代码为准：
>   - `SourceKind` 只加了 `Chart`/`Artist`/`NewSongs`；**没有 `SongDetail`**——批量歌曲详情是内部手段，不对外成一种资源。
>   - `CollectionPageDto` 多了 `revision`/`loaded`/`error`（拉失败时界面能显示"重试"而不是"空列表"）。
>   - 查询是 `FetchCollections{kind}`（内存读）与 `FetchCollectionDetail{kind,collectionId}`；`LoadCollections{kind,limit}` 是命令——列表是"先要、再轮询读"，和搜索/队列一致。
>   - 服务层没有"每类资源一个 token 通道"，而是**一个 token 代表一次页面请求**，每类资源各自缓存 plan（`playlists_`/`fullPages_`/`details_`/`collections_`）。翻页复用同一 token，靠 `offset` 递进。
> - 榜单/专辑/歌手在 N0 的实测结论下都走"一次取全、内存切片分页"（榜单的 trackIds 只有 100 条，专辑一次性给全，歌手 hotSongs 只有 8 首）。
> - **N1-6 未做的原因**：R5 刚把搜索缓存的 LRU 与迁移的"只加列不回填"钉死，本轮若再加两张表和一次迁移，验收面会翻倍；而只读资源每次拉取 < 1 s、失败可重试，缓存的收益主要在"断网/限流时不至于空白"。决定推迟到 N5 界面成型之后再评估（届时若做，迁移只建表、LRU 复用 `CASE WHEN` 写法）。
> - 门禁：`sonora_tests` **164 用例 / 5163 断言**、`boundaries.py` 218 文件、字体 1008 字 / 167 KB `--verify` 覆盖完整；真机卡片墙 30 张、榜单 100 行、翻页 100→200、在线曲目真出声，`[qml]` 告警 0。

---

### N2 · 登录侧（2 人日）——依赖 N0 结论

- [x] **N2-1 我的歌单**
  - 文件：`OnlineService.h/.cpp`（把 `handle(GetMyPlaylists)` 的空实现换成真实拉取）、`NeteaseUrls`（`makeUserPlaylistsRequest`）、`NeteaseJson`（解析尾部的 `playlist[]`，取 `id/name/coverImgUrl/trackCount/specialType`）
  - 验收：登录后"我的"页列出歌单；含 `specialType == 5` 的那张显示为"我喜欢的音乐"。

- [x] **N2-2 每日推荐**
  - 端点：§2 的 #10、#11；进"推荐"页顶部两张卡。未登录时显示"登录后可见"，点了跳登录流程（现有 `startQrLogin` / `signIn`）。
  - 验收：登录态下当天返回 30 首 / N 张歌单；未登录态不发起请求（`Diagnostics` 里不出现新的 `unauthenticated` 计数）。

- [x] **N2-3 收藏 / 取消收藏（写操作）**
  - 文件：`Commands.h` 加 `ToggleFavorite { std::string songId; std::string playlistId; bool add; }`；服务层组 POST body（`op=add/rem`、`pid`、`trackIds`）；
  - 硬要求：
    1. `__csrf` 从 cookie 里取，**只放请求头/表单**，不许进日志、事件、DTO（AGENTS「在线音源」第 2 条）；
    2. UI 二次确认（漫画风小面板），失败回滚按钮状态；
    3. 写操作单独计数（`Diagnostics` 加 `favoriteWrites`），不和读错误混在一起。
  - 验收：收藏后刷新列表出现该曲；断网时按钮回滚并提示；`grep` 全仓确认 `__csrf` 没有出现在任何日志语句里。

- [x] **N2-4 未登录降级**
  - 校验 §5.3 矩阵里"未登录访问需登录接口"这一行：游客态 + "登录后可见"，且**不消耗令牌**（不请求）。
  - 验收：诊断面板里 `unauthenticated` 计数不因浏览"我的"页而增长。

---


> **N2 实施记录**
>
> - 每日推荐匿名可用（实测 31 首），所以"未登录 → 登录后可见"只对「我的歌单」成立；两者别混成一句。
> - 收藏的交互改成「结果行右键 → 确认面板」而不是行内按钮：行高 46、五列已经排满，多一个按钮会把"曲名在哪一列"挤歪。
> - 写操作不做乐观更新：成功前界面不变，失败只把原因写进状态行（不存在"回滚"这件事，也就不存在回滚错）。
> - 登录态的三个端点（我的歌单 / 收藏 / 每日歌单）**没有真实账号跑过**：本机没有 cookie。代码通、解码器有单测、未登录路径有可计算判据，仅此而已。
> - 迁移 v3 只加 `online_session.user_id` 一列，不回填（沿用 R5 的教训）。
> - 门禁：`sonora_tests` 168 用例 / 5182 断言、`boundaries.py` 218 文件、字体 1011 字 / 168 KB `--verify` 覆盖完整、`[qml]` 告警 0。

### N3 · 在线歌词（1 人日）✅ 2026-11-08

- [x] **N3-1 契约与端点**
  - `Queries.h` 加 `FetchOnlineLyrics { std::string songId; }` → `OnlineLyricDto { std::string lrc; std::string translated; bool instrumental{false}; }`；`NeteaseUrls` 加 `makeLyricRequest`；`NeteaseJson` 加 `parseLyric`（`lrc.lyric` / `tlyric.lyric` / `nolyric|uncollected` 视为无歌词）。

- [x] **N3-2 作为歌词第三个来源**
  - 文件：新建 `modules/lyrics/adapters/include/sonora/lyrics/adapters/OnlineLyricsSource.h` + `.cpp`
  - 要点：**依赖 `Online.Contracts` 的查询**（模块 → 契约，合法），不依赖 `Online.Core`；查询是同步返回（在线服务本来就把结果放内存状态表）；返回空即"没有"，由链子继续/结束。
  - 链路顺序：同名 `.lrc` → 内嵌标签 → **在线**；在线失败**不阻断**本地已取到的歌词。
  - 验收：`tests/lyrics_sources.cpp` 加"本地无歌词时问在线、在线失败时是 NoLyrics 不是错误"两条。

- [x] **N3-3 界面体现来源**
  - `NowPlaying` 歌词区右下角小字标"在线歌词"（非必须，但用户会问"这词哪来的"）。
  - 验收：截图确认标签出现且不遮挡歌词。

#### N3 实施记录

- **签名与清单的差异**：`OnlineLyricDto` 多了 `bool ready`——异步查询必须能表达"还没回来"，否则"网络慢"会被当成"这首歌没词"。`LrcParser` 未改，翻译仍靠"同一时间戳再来一行"合并。
- **`ILyricsSource` 加的是重载不是改签名**：新增 `LyricsRequest{audioFilePath, onlineSongId}`，默认实现退回旧 `load(path)`，两个本地来源零改动；只有 `OnlineLyricsSource` 覆写。
- **重试在表现层**：`LyricsModel` 自己拿 1 秒定时器，服务层不重试——歌词是背景数据，不该占用退避和令牌。
- **歌词是内存缓存**：`OnlineService` 存 16 首，没有建表（见 ONLINE.md §22.5）。
- **验证**：`sonora_tests` 174 用例 / 5210 断言；真机 `src=在线歌词 online=true lines=64 idx=1→7`；截图 `.demo/n3_lyrics.png`。

---

### N4 · 私人 FM（1.5 人日）——依赖 N0 结论

- [ ] **N4-1 队列模型扩展**
  - 文件：`modules/playback/core/include/sonora/playback/core/domain/PlayQueue.h`
  - 改动：新增"追加式来源"（`AppendMode` 或 `SourcePolicy`）：队列不再一次性定长，`advance()` 到末尾时向上层要"下一批"；随机/单曲循环与它的组合规则要写进注释（FM 下随机=不洗牌，循环=继续要新歌）。
  - 验收：`tests/playback_modes.cpp` 加"追加式队列播放到末尾会请求下一批、且不重复已播曲目"。

- [ ] **N4-2 服务侧 FM**
  - 端点 #14；`NextPersonalFm` 命令（`Commands.h`）→ provider 取一批（约 3–5 首）→ 塞进播放队列尾部；`Diagnostics` 里 FM 失败单独计数。
  - 验收：连播 20 首不断流；每批间隔 < 1s；失败时保留当前曲目、只提示"FM 暂时不可用"。

- [ ] **N4-3 FM 界面**
  - "我的"页加"私人 FM"入口；进入后播放条上方出现 `FM` 角标，两个按钮"喜欢 / 不感兴趣"（喜欢 = N2 的收藏写操作）。
  - 验收：点"不感兴趣"立刻下一首，且该曲不再出现。

---

### N5 · 界面与交互（3 人日）

- [ ] **N5-1 详情页**
  - `CollectionPage.qml`：大封面 + 标题 + 副标题（歌手/创建者/曲目数）+ 两个按钮（播放全部 / 加入队列）+ 曲目列表（复用 `OnlineListModel`）；
  - 列表项右侧保留"···"菜单：播放 / 加入队列 / 查看歌手（N1 探通后才做）。
  - 验收：1000 首歌单滚动不卡（帧时间 median ≤ 2ms）；点击"播放全部"从第一首起播。

- [ ] **N5-2 加载与空态**
  - 骨架屏（漫画网点 `Halftone`）+ "加载失败/重试"（复用现有 `retryable` 机制）+ "登录后可见"三种状态，全部走同一套 `ComicBubble`。
  - 验收：断网时每页都有明确出口，不出现空白页。

- [ ] **N5-3 键盘与焦点**
  - `Esc` 从详情页返回；`Tab` 不把焦点锁死在搜索框（现有 `typing` 守卫别破坏）。
  - 验收：全流程可用键盘完成"进歌单→播放"。

---

### N6 · 收尾（1.5 人日）

- [ ] **N6-1 离线与降级复测**：把 `NeteaseUrls.h` 的 base 临时指向 `https://ncm-api-gone.invalid`，逐页检查"回到上次结果 + 离线角标"，改回来。
- [ ] **N6-2 文档**：`docs/ONLINE.md` §19 实测记录、§5.5 新端点表、§11 里程碑勾选、`docs/ARCHITECTURE.md` §7.5（新端点走同一条链路）、`AGENTS.md` 是否需要追加坑（`__csrf`、大歌单分页）。
- [ ] **N6-3 测试与门禁**：`sonora_tests`、`boundaries.py`、`subset_fonts.py --verify`、真机截图（在线页四标签 + 详情页 + 在线歌词）。
- [ ] **N6-4 汇报**：哪些端点真通了、哪些没通、缓存数字、未做项（评论/播客/云盘）。

---

## 4. 验收指标汇总

| 指标 | 目标 | 检查方式 |
| --- | --- | --- |
| 歌单 1000 首加载 | 首屏 < 2s，后续每页 < 800ms | 日志打每页耗时 |
| 缓存命中 | < 50ms | `OnlineDiagnosticsDto` 计数 + 日志 |
| 点歌到出声 | < 5s（沿用 R5 预算） | 从点击到 `state=Playing` 的时间差 |
| 歌词同步偏差 | < 40ms | 与现有歌词一致（同一套时间轴） |
| FM 连播 | 20 首不断流、不重复 | 播放记录 + 队列日志 |
| 降级 | 每个失败都有计数 + 一行日志 | 诊断面板 `degradations` |
| 门禁 | tests 全绿 + boundaries 通过 + 字体验证通过 | 命令输出 |

---

## 5. 风险与对策

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| 未公开接口变更/下线 | 功能整体失效 | 端点集中在 `NeteaseUrls`；解析缺字段就降级；N0 的探通日志可当回归基线 |
| 登录态接口需要 `__csrf` | 写操作 403 | 从 cookie 取，只放请求头；写操作失败必须回滚 UI |
| 大歌单只给 `trackIds` | 详情页没歌名 | 强制走 `playlist/track/all` 分页，500/页 |
| 私人 FM 与定长队列冲突 | 切歌/循环逻辑错 | 先改 `PlayQueue` 加"追加式来源"（N4-1），再碰 FM |
| 缓存膨胀 | 磁盘/内存 | 新表共用 200 行 / 4 MiB + LRU，`last_used_at` 排序规则照抄 R5 |
| 在线歌词版权与错配 | 显示错词 | 只做"没有就用本地"的降级；歌词来源在界面上标注 |

---

## 6. 待拍板（动手前确认）

1. **N2 要不要做？**（我的歌单 / 我喜欢的音乐 / 收藏写操作）——不做则"我的"标签保持空态。
2. **N4 私人 FM 要不要做？**——它是唯一"碰核心播放模型"的一项。
3. **评论/播客**明确不做（本轮），如需另开清单。

---

## 7. 进度记录

| 日期 | 段 | 结果 | 提交 |
| --- | --- | --- | --- |
| — | N0 | — | — |
| — | N1 | — | — |
| — | N2 | — | — |
| — | N3 | — | — |
| — | N4 | — | — |
| — | N5 | — | — |
| — | N6 | — | — |
