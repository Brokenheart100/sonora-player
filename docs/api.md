# Sonora API 参考

Sonora 0.1.0 · 2026-11-09

这份文档同时描述两套「API」，因为它们在这项目里是配套的：

1. **在线 HTTP API**：`online` 模块实际请求的网易云公开接口，以及每个接口的真实返回形态。
2. **应用内部契约**：模块之间唯一的通信方式——命令、查询、领域事件与 DTO。

两套都从源码里逐条核对过（文件路径见每一节），不臆造未验证的字段。

> 阅读顺序建议：先看 §1 通用约定，再按需要跳到 §2（对接在线接口）或 §3（写模块代码）。

---

## 0. 三层边界

```
QML（只认注入的单例：Library / Player / Lyrics / Online / Playlists）
   │  Q_INVOKABLE 调用
   ▼
Presentation（ViewModel：只持 DTO + 视图状态，不持聚合）
   │  CommandBus / QueryBus（进程内，同步调用）
   ▼
模块 Core（领域 + 应用层，不链 Qt）
   │  端口（IPlaylistRepository、IHttpTransport、ISettingsStore …）
   ▼
Adapters（SQLite / FFmpeg / Win32 / Qt Network）
```

- **命令**（`Commands.h`）是写；**查询**（`Queries.h`）是读。没有第三种入口。
- 跨模块只发**事件**（`modules/contracts/include/sonora/contracts/`），不直接调用别的模块的仓储。
- `platform/*` 提供能力，可以被模块的 Core 通过**端口**反转依赖，但 Core 不知道实现是谁。

---

## 1. 通用约定

### 1.1 返回类型

- 命令/查询返回 `kernel::Result<T>`（`modules/kernel/include/sonora/kernel/Result.h`）。
- `Result<T>` 对 `T` **没有隐式构造**：必须写 `Result<T>::ok(...)`，否则 `C2440`。
- 错误是 `kernel::Error{ErrorCode code, std::string message}`；`message()` 是**方法**不是字段。
- 常用 `ErrorCode`：`None`、`InvalidArgument`、`NotFound`、`Conflict`、`Unavailable`、`Internal`（`modules/kernel/include/sonora/kernel/Error.h`）。

### 1.2 命名与线程

- 命令/查询/事件都是**值类型 struct**，字段全部 public、带默认值；参数名就是语义，不额外加前缀。
- `CommandBus::on<C>(handler)` 收 `shared_ptr<Handler>` **或**可调用对象；`QueryBus::on<Q,R>(fn)` **只收可调用对象**（传 `shared_ptr` 是一屏模板报错）。
- 总线调用发生在**拥有该模块的线程**（本项目就是 GUI 主线程）。Handler 同步耗时上限 **5ms**；更慢的活投 `ITaskQueue::post()`。
- 网络 I/O 只允许发生在网络线程：`IHttpTransport::send()` 必须在队列线程调用（`QtHttpTransport.cpp` 里有 `Q_ASSERT`）。

### 1.3 编码与单位

| 项 | 约定 |
| --- | --- |
| 字符串 | 一律 UTF-8（`std::string`），Win32 边界用 `u8Path/u8String` |
| 文件路径 | `fs::path` 从 `std::string` 构造按 ANSI 解释，中文路径必须 `u8Path()` |
| 时间 | 时长 `durationMs`（`int64`）；时间戳 `*Unix`（秒）；超时 `std::chrono::milliseconds` |
| 标识 | 本地曲目 `trackId` 是 `uint64`；在线曲目 `songId` 是**字符串**；歌单 `playlistId` 本地 `int64`、在线 `string` |
| 在线曲目在播放队列里 | `trackId == 0`、`onlineSongId` 非空、`filePath` 是 CDN 直链 |

### 1.4 异步结果的统一形态

在线模块的所有异步操作都遵循同一套「先登记、再轮询」：

1. 命令里带 `token`（0 表示「新的一个」），服务返回后把结果写进该 token 的作业状态。
2. 界面每 **50ms** 查一次 `GetOnlineJob{token}` / `FetchOnlineRows{token,offset,limit}`。
3. `OnlineJobState.phase` 走 `Idle → Loading → (LoadingMore) → Ready/Empty/Error/Offline`。

不是这套形态的两个例外，都在下文单独标注：**歌词**（`FetchOnlineLyrics` 立即返回 `ready=false`，表现层 1 秒后重问）与**私人 FM**（`StartPersonalFm` 是命令，结果通过 host 注入的 listener 直接交给播放队列）。

---

## 2. 在线 HTTP API

源码：`modules/online/core/src/provider/NeteaseUrls.cpp`、`modules/online/core/src/codec/NeteaseJson*.cpp`。

### 2.1 基址与公共头

- 基址：`https://music.163.com`（`modules/online/core/include/sonora/online/core/provider/NeteaseUrls.h` 的 `kNeteaseBaseUrl`）。
- 公共头（`defaultHeaders()`）：`Referer: https://music.163.com/`、固定桌面 Chrome UA；有登录态时附 `Cookie`。
- 写操作额外：`Content-Type: application/x-www-form-urlencoded`。
- 单次响应体上限 `HttpRequest.maxBodyBytes = 8 MiB`；默认超时 8s。

### 2.2 返回码语义

| 顶层 `code` | 含义 | 处理 |
| --- | --- | --- |
| **200** / **0** | 成功（两代接口各用一套，`/api/v1/*` 与 `/api/v1/radio/get` 用 0） | 交给解码器 |
| 301 | 未登录 | `Unauthenticated`，提示「登录后可见」 |
| 400 | 参数错（常见：id 为 0、ids 没编码） | `InvalidArgument` / 上游错误 |
| 404 | 接口不存在（已废弃） | `Upstream`，消息「接口可能已变更」 |
| 429 | 频率限制 | `RateLimited` + `retryAfterMs` |

**只认顶层 `code`**：嵌套对象里也有 `code` 字段，正则抓第一个会把 `/api/v1/album` 的成功（0）误判成失败。

### 2.3 端点表

状态列来自 `docs/probe/*` 的实测记录与后续真机验证：✅ 已接通并验证、⚠️ 通但有限制、❌ 已失效。

| # | 端点 | 方法 | 参数 | 返回要点 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 1 | `/api/cloudsearch/pc` | GET | `s`（关键词，需 URL 编码）、`type=1`、`limit`（≤100）、`offset` | `result.songs[]`，新字段 `ar/al/dt` 与 `al.picUrl` 封面 | ✅ 搜索 |
| 2 | `/api/song/enhance/player/url/v1` | GET | `ids=[a,b,…]`（≤200）、`level`、`encodeType=aac` | 每条 `url/br/code`；`code != 200` 表示无版权 | ✅ 取直链 |
| 3 | `/api/nuser/account/get` | GET | — | `profile.userId/nickname/vipType`，未登录 `code=301` | ✅ |
| 4 | `/api/login/qrcode/unikey` | GET | `type=1` | `unikey` | ✅ |
| 5 | `/api/login/qrcode/client/login` | GET | `key`、`type=1` | `code` 800/801/802/803；成功时 cookie 在 `Set-Cookie` | ✅ |
| 6 | `/api/v6/playlist/detail` | GET | `id` | `playlist.{name,coverImgUrl,trackCount,description,creator.nickname,trackIds[]}`；`tracks` **只给前 10** | ✅ 歌单头 + 全量 id |
| 7 | `/api/song/detail` | GET | `ids=[a,b,…]`（**方括号必须 URL 编码成 `%5B…%5D`**） | `songs[]`，**旧字段** `artists/album/duration` | ✅ 批量取曲目 |
| 8 | `/api/v1/album/{id}` | GET | 路径参数 | `code=0`；`songs[]`（旧字段）+ `album.{name,picUrl,size,artists,briefDesc}` | ✅ |
| 9 | `/api/v1/artist/{id}` | GET | 路径参数 | `code=0`；`artist.{name,picUrl,img1v1Url,albumSize,musicSize}` + `hotSongs[]` | ✅ 热歌前 50 |
| 10 | `/api/toplist` | GET | — | `list[65]`，每张榜 `{id,name,updateFrequency,coverImgUrl}` | ✅ 榜单列表 |
| 11 | `/api/personalized/playlist` | GET | `limit` | `result[]`，`{id,name,picUrl,trackCount,playCount,copywriter}` | ✅ 推荐歌单 |
| 12 | `/api/personalized/newsong` | GET | `limit` | `result[]`，每项 `{song:{…}}`，`song` 用**旧字段** | ✅ 新歌速递 |
| 13 | `/api/song/lyric` | GET | `id`、`lv=-1`、`tv=-1` | `lrc.lyric`、`tlyric.lyric`；`nolyric`/`uncollected` 表示无词（不是错误） | ✅ |
| 14 | `/api/user/playlist` | GET | `uid`、`limit`（≤1000） | `playlist[]`；`specialType == 5` 是「我喜欢的音乐」 | ⚠️ 需登录 |
| 15 | `/api/v3/discovery/recommend/songs` | GET | — | `data.dailySongs[]` | ✅ 匿名也给（31 首） |
| 16 | `/api/playlist/manipulate/tracks` | **POST** | `op=add\|del`、`pid`、`trackIds=[id]`、`csrf_token` | 成功 `code=200`；`__csrf` 从 cookie 取、**只进 body** | ⚠️ 需登录 |
| 17 | `/api/v1/radio/get` | GET | — | `code=0`；`data[]` **一次只有一首**，旧字段；无 `limit` 参数 | ✅ 私人 FM（凑批要并发多次） |

### 2.4 已失效或不可依赖

| 端点 | 现象 | 替代方案 |
| --- | --- | --- |
| `/api/playlist/track/all` | `code=404 接口未找到` | `v6/playlist/detail` 取 `trackIds`，再 `song/detail` 每批 500 |
| `/api/playlist/track/ids` | `code=404` | 同上 |
| `/api/v1/artist/songs` | `code=400 参数错误` | `v1/artist/{id}` 的 `hotSongs` |
| `/api/discovery/simiSong` | `code=400` | 未接（相似歌曲不做） |
| `/api/v1/discovery/recommend/resource` | `code=301`（需登录） | 每日推荐只用 `recommend/songs` |

### 2.5 调用约定

- **大歌单**：`detail.trackIds`（1000 首就有 1000 个 id）→ `song/detail` **每批 500**（`kMaxSongDetailsPerRequest`），一次页面请求按 100 行切片。
- **搜索结果里的 `artistId` 常常是 `"0"`**（占位），不能拿去拼 `/api/v1/artist/0`；解码器把 0 归一成空串，界面据此隐藏「查看歌手」。
- **`playlist/manipulate/tracks` 的 `trackIds` 在 body 里不编码**（URL 里才需要 `%5B`）。
- **私人 FM** 一次一首，`StartPersonalFm{count}` 会并发发 `count` 次请求（上限 5）。
- 降级必须留痕：一行日志 + 一个计数器（`DegradationCounters`）。

---

## 3. 应用内部契约

每个模块的契约都在 `modules/<模块>/contracts/include/sonora/<模块>/contracts/`。
「已接线」指该命令/查询在 `host/main.cpp` 里真的注册了。

### 3.1 catalog（曲库）

| 类型 | 名称 | 入参 | 返回 |
| --- | --- | --- | --- |
| 命令 | `ImportFolder` | `std::string folderPath` | `void` |
| 命令 | `RescanLibrary` | `bool fullRescan = false` | `void` |
| 查询 | `ListTracks` | `offset=0`、`limit=200`、`searchText` | `TrackListResult{rows, totalCount}` |

`TrackDto`：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | `uint64` | 本地曲目 id |
| `title` / `artistName` / `albumTitle` | `string` | 已解析的显示名 |
| `durationMs` | `int64` | 毫秒 |
| `filePath` | `string` | UTF-8 绝对路径 |
| `coverPath` | `string` | 封面文件路径，可空 |

约束：`ListTracks` 的 `limit` 上限 **10000**，超过直接 `InvalidArgument`；搜索走 FTS5，中文按字切。

### 3.2 playback（播放）

| 类型 | 名称 | 入参 | 返回 |
| --- | --- | --- | --- |
| 命令 | `PlayTracks` | `entries[]`、`startIndex`、`appendable=false` | `void` |
| 命令 | `AppendTracks` | `entries[]`、`playWhenIdle=true` | `void` |
| 命令 | `TogglePlayPause` / `PlayNext` / `PlayPrevious` | — | `void` |
| 命令 | `SeekTo` | `positionMs` | `void` |
| 命令 | `SetVolume` | `value=100`（0–100） | `void` |
| 命令 | `SetPlaybackMode` | `mode` | `void` |
| 命令 | `JumpToTrack` | `trackId` | `void` |
| 查询 | `GetPlayerSnapshot` | — | `PlayerSnapshot` |
| 查询 | `GetPlayQueue` | — | `QueueView{mode, currentIndex, items[]}` |

`QueueEntry`：`trackId`、`filePath`、`title`、`artistName`、`coverPath`、`onlineSongId`（本地曲目为空）。

`PlayerSnapshot`：`hasTrack`、`trackId`、`title`、`artistName`、`durationMs`、`volume`、`state`、`mode`、`queueIndex`、`queueSize`、`filePath`、`coverPath`、`onlineSongId`。

枚举（`PlaybackMode.h` / `AudioOutput.h`）：

- `PlaybackMode`：`Sequential` / `RepeatAll` / `RepeatOne` / `Shuffle`，配套 `toName`、`modeFromName`、`nextMode`、`modeLabel`。
- `PlaybackState`：`Idle` / `Loading` / `Buffering` / `Playing` / `Paused` / `Stopped` / `Failed`。

**追加式队列**（私人 FM 用的那条路）：

- `PlayTracks{appendable=true}` 之后，队列到末尾**不停止**，`PlaybackService` 发 `QueueNeedsMore` 事件并进入等待。
- 组合根收到事件 → `StartPersonalFm{3}` → 拿到曲目 → `AppendTracks{playWhenIdle=true}`。
- 追加式队列下：随机**不重新洗牌**、循环**不绕回**（绕回等于重播同一批）、单曲循环**不重放**。

### 3.3 lyrics（歌词）

| 类型 | 名称 | 入参 | 返回 |
| --- | --- | --- | --- |
| 查询 | `GetLyrics` | `filePath`（可空）、`onlineSongId`（可空） | `LyricsView` |

`LyricsView`：`status`（`Found` / `Instrumental` / `NotFound`）、`pending`、`source`、`onlineSource`、`lines[]`。
`LyricLine`：`timeMs`、`text`、`translation`、`words[]{timeMs,text}`。

语义：

- 两个参数都空 → `InvalidArgument`。
- `pending == true` 表示「在线那份还没取回来」，**不是错误**；表现层 1 秒后重问，拿到词或确认没词就停。
- 来源链顺序：同名 `.lrc` → 内嵌标签 → 在线（`OnlineLyricsSource`，依赖 `Online.Contracts` 的 `FetchOnlineLyrics`）。
- 在线失败不能把已经拿到的本地歌词变成错误。

### 3.4 online（在线音源）

**命令**（`Commands.h`）：

| 名称 | 入参 | 说明 |
| --- | --- | --- |
| `RequestOnlinePage` | `token`、`kind`、`keywords`、`playlistId`、`collectionId`、`offset`、`limit=30` | 一页列表；`kind` 决定走哪个端点 |
| `LoadCollections` | `kind`（Playlist/Chart）、`limit=30` | 卡片墙（推荐歌单 / 榜单） |
| `StartPersonalFm` | `count=3`（1–5） | 私人 FM，一批 N 首 |
| `DislikeFmSong` | `songId` | 本地黑名单（上限 60），不发服务端 |
| `LoadMyPlaylists` | — | 拉「我的歌单」；未登录**不发请求** |
| `ToggleFavorite` | `songId`、`playlistId`、`add=true` | 收藏/取消收藏（写操作） |
| `CancelOnlineJob` / `RetryOnlineJob` | `token` | 取消/重放 |
| `ResolveAndPlayOnline` | `token`、`songIds[]`、`startIndex`、`origin` | 批量解析直链并交给播放 |
| `SignInWithCookie` | `provider="ncm"`、`cookie` | 粘贴登录 |
| `StartQrLogin` / `CancelQrLogin` / `SignOut` | `provider="ncm"` | 扫码与登出 |

**查询**（`Queries.h`）：

| 名称 | 入参 | 返回 |
| --- | --- | --- |
| `GetOnlineJob` | `token` | `OnlineJobState` |
| `FetchOnlineRows` | `token`、`offset`、`limit=30` | `OnlinePageDto` |
| `GetSourceStatus` | `provider` | `SourceStatusDto` |
| `GetQrLoginState` | `provider` | `QrLoginDto` |
| `GetMyPlaylists` | `provider` | `vector<PlaylistDto>` |
| `GetOnlinePlayback` | — | `OnlinePlaybackDto` |
| `FetchCollections` | `kind` | `CollectionPageDto` |
| `FetchCollectionDetail` | `kind`、`collectionId` | `CollectionDetailDto` |
| `GetOnlineDiagnostics` | `provider` | `OnlineDiagnosticsDto` |
| `FetchOnlineLyrics` | `songId` | `OnlineLyricDto` |

**枚举**：

- `JobPhase`：`Idle`、`Loading`、`LoadingMore`、`Ready`、`Empty`、`Error`、`Offline`。
- `AccountPhase`：`Guest`、`QrWaiting`、`QrScanned`、`QrExpired`、`LoggedIn`、`Expiring`、`Failed`。
- `SourceKind`：`Search`、`Playlist`、`Favorites`、`Recent`、`DailyRecommend`、`Album`、`Chart`、`Artist`、`NewSongs`（**只许往后追加**）。

**主要 DTO**：

| 名称 | 关键字段 |
| --- | --- |
| `OnlineError` | `code`、`message`、`retryable`、`retryAfterMs` |
| `OnlineTrackDto` | `songId`、`title`、`artists`、`artistId`、`album`、`durationMs`、`coverUrl`、`qualityLabel`、`playable`、`fromCache`、`playUrl`（FM 专用） |
| `OnlineJobState` | `token`、`phase`、`total`、`loaded`、`error`、`revision`、`fromCache` |
| `OnlinePageDto` | `token`、`revision`、`rows[]`、`hasMore` |
| `CollectionDto` | `id`、`name`、`coverUrl`、`subtitle`、`count`、`kind` |
| `CollectionPageDto` | `items[]`、`hasMore`、`revision`、`loaded`、`error` |
| `CollectionDetailDto` | `id`、`name`、`creator`、`description`、`coverUrl`、`trackCount`、`kind`、`ok` |
| `SourceStatusDto` | `provider`、`signedIn`、`accountPhase`、`nickname`、`nextRetryAtUnix`、`lastError`、`enabled` |
| `QrLoginDto` | `provider`、`phase`、`rows[]`（二维码黑白格）、`session`、`message` |
| `PlaylistDto`（在线） | `playlistId`、`name`、`trackCount`、`coverUrl` |
| `OnlinePlaybackDto` | `revision`、`entries[]{songId,url,bitrateKbps}`、`startIndex`、`error` |
| `OnlineLyricDto` | `ready`、`lrc`、`translated`、`instrumental`、`error` |
| `OnlineDiagnosticsDto` | `provider`、`degradations`、`consecutiveFailures`、`nextRetryAtUnix`、`tokensLeft`、`tokenBurst`、`cache`、`favoriteWrites`、`favoriteFailures`、`fmFailures`、`recentErrors[]` |

`DegradationCounters`：`offline`、`timeout`、`rateLimited`、`unauthenticated`、`upstream`、`urlExpired`、`cacheHits`、`totalErrors`。
`OnlineCacheStats`：`searchRows`、`searchBytes`、`songRows`、`detailRows`、`capRows`、`capBytes`。

行为约定：

- 缓存上限 **200 行 / 4 MiB**，按 `last_used_at` 做 LRU；老行 `last_used_at = 0` 时按 `fetched_at` 排队。
- `fromCache` 由 `cachedTokens_` 决定：缓存命中置位，**网络回包必须擦掉**。
- 退避闸门先于令牌闸门：连续失败时返回 `Unavailable`「连续失败，约 N 秒后自动重试」，不消耗令牌。
- 私人 FM 的失败只记 `fmFailures`，**不进** `noteFailure`（否则一次 FM 抖动会把搜索的退避闸门按死）。

### 3.5 playlists（歌单）

| 类型 | 名称 | 入参 | 返回 |
| --- | --- | --- | --- |
| 命令 | `CreatePlaylist` | `name`（≤120 字符） | `PlaylistId`（`int64`） |
| 命令 | `RenamePlaylist` | `playlistId`、`name` | `void` |
| 命令 | `DeletePlaylist` | `playlistId` | `void` |
| 命令 | `AddTrackToPlaylist` | `playlistId`、`trackId` | `void`（重复加 → `Conflict`） |
| 命令 | `RemoveTrackFromPlaylist` | `playlistId`、`trackId` | `void`（没有该曲 → `NotFound`） |
| 命令 | `MovePlaylistTrack` | `playlistId`、`trackId`、`toIndex` | `void`（越界自动夹取） |
| 命令 | `MarkTrackUnavailable` | `trackId` | `void`（条目不删，只置 `available=false`） |
| 查询 | `ListPlaylists` | — | `PlaylistList` = `vector<PlaylistDto>` |
| 查询 | `GetPlaylistTracks` | `playlistId` | `PlaylistTrackList` = `vector<PlaylistEntryDto>` |

`PlaylistDto`：`playlistId`、`name`、`trackCount`、`createdAt`。
`PlaylistEntryDto`：`trackId`、`pos`（当前顺序）、`available`。

要点：

- 条目 DTO **故意不带标题/封面**——歌单模块不认识曲库，跨模块不 JOIN；显示名由表现层拼。
- 表 `playlist_item` 上有 `UNIQUE INDEX (playlist_id, pos)`，所以保存时必须**先删光再按新顺序插**；逐条 UPDATE 会在途中撞唯一索引。
- `MarkTrackUnavailable` 目前**没有发布者**（曲库还没有「移除曲目」功能），接口与测试先就位。

### 3.6 preferences（设置）

| 类型 | 名称 | 入参 | 返回 |
| --- | --- | --- | --- |
| 命令 | `SetSetting` | `key`、`value` | `void` |
| 命令 | `RemoveSetting` | `key` | `void` |
| 查询 | `GetSetting` | `key` | `SettingValue{key,value,found}` |
| 查询 | `GetSettings` | `keys[]` | `SettingList{entries[]}` |

- 表 `kv(key TEXT PRIMARY KEY, value TEXT, updated_at INTEGER)`。
- 键名空 → `InvalidArgument`；未写过的键返回 `found=false`，**不是错误**。
- 现用的键：`library/root`、`playback/volume`、`playback/mode`（`sequential` / `repeat-all` / `repeat-one` / `shuffle`）。
- 主题与语言是例外：由 `presentation/src/Appearance.cpp` 直接用 `QSettings` 存 `ui/theme`、`ui/language`。

### 3.7 平台端口（不是总线上的契约）

| 端口 | 位置 | 说明 |
| --- | --- | --- |
| `IHttpTransport` | `modules/online/contracts/include/sonora/online/contracts/Transport.h` | `send(requestId, HttpRequest, Callback)`；`HttpRequest{method,url,headers,body,transferTimeout,maxBodyBytes}`；**只许队列线程调用** |
| `ISettingsStore` | `modules/preferences/core/include/sonora/preferences/core/ports/SettingsStore.h` | `loadAll/upsert/remove` |
| `IPlaylistRepository` | `modules/playlists/core/include/sonora/playlists/core/ports/PlaylistRepository.h` | `insert/findById/save/list/remove/markTrackUnavailable` |
| `ILyricsSource` | `modules/lyrics/core/include/sonora/lyrics/core/ports/Ports.h` | `load(LyricsRequest{audioFilePath, onlineSongId})` |
| `IAudioOutput` / `IPositionStream` | `modules/playback/contracts/include/sonora/playback/contracts/AudioOutput.h` … | 引擎与位置旁路 |

---

## 4. 事件

位置：`modules/contracts/include/sonora/contracts/PlaybackEvents.h`。由模块发布、组合根订阅。

| 事件 | `kName` | 字段 | 发布者 | 订阅者 |
| --- | --- | --- | --- | --- |
| `TrackFinished` | `playback.track_finished` | `trackId`、`playedAtUnix` | playback | host 里注册的 `catalog::adapters::SqlitePlaybackRecorder`（累计 `play_count`） |
| `QueueNeedsMore` | `playback.queue_needs_more` | `atUnix` | playback（仅追加式队列） | host（→ `StartPersonalFm{3}`） |

约定：**先提交事务，再发事件**；不做 outbox，不做最终一致。`TrackRemoved → playlists` 仍是契约预留，**当前没有发布者**。

---

## 5. 端到端时序（三个典型场景）

### 5.1 搜索 → 播放

```
QML 输入 → Online.search(text)（280ms 防抖）
  → RequestOnlinePage{token, kind=Search, keywords}
  → cache 命中? 是: 直接可用（fromCache=true） 否: GET /api/cloudsearch/pc
  → 50ms 轮询 GetOnlineJob / FetchOnlineRows → 列表
双击行 → ResolveAndPlayOnline{token, songIds, startIndex}
  → GET /api/song/enhance/player/url/v1?ids=[…]
  → GetOnlinePlayback → 表现层拼 QueueEntry{onlineSongId, filePath=url, …}
  → PlayTracks
```

### 5.2 私人 FM 续批

```
点「私人 FM」→ StartPersonalFm{3}
  → 并发 3× GET /api/v1/radio/get（过滤已播/已踩）
  → 解析直链 → FmListener → host 拼 QueueEntry → AppendTracks{playWhenIdle=true}
队列播到末尾 → QueueNeedsMore → host → StartPersonalFm{3} → …
```

### 5.3 歌词（三段取值）

```
Player 换曲 → Lyrics.loadFor(filePath, onlineSongId)
  → GetLyrics → 来源链：同名 .lrc → 内嵌标签 → OnlineLyricsSource
  → 在线首次返回 pending=true（ready=false）
  → 1 秒后重问；取到词显示，确认没词则停
```

---

## 6. 未实现与未接线

| 项 | 现状 |
| --- | --- |
| `modules/*/contracts/**/probe.h` | catalog / lyrics / playback / playlists / preferences 各留一个 `probe.h`，**没有引用**，可删 |
| `MarkTrackUnavailable` 的事件发布 | 接口与测试就位，曲库暂无「移除曲目」功能 |
| 收藏的失败回滚 | 不做乐观更新，因此没有回滚；失败只写状态行 |
| 相似歌曲 / 评论 / 播客 / 云盘 | 未接（`simiSong` 实测 400） |
| 「精选 / 博客 / Quill 日志」 | 待启动；Quill 计划走原生富文本，不引入 QtWebEngine |
| 发布流水线 | `.github/workflows/release.yml` 已写好，**从未在真 runner 上跑过**；Inno 安装器脚本未编译 |

---

## 7. 改契约的规矩

1. **只追加**：新字段加在末尾、新枚举值加在末尾、`SourceKind` 的数值只许往后长。
2. **DTO 只加不改名**：字段改名等于把所有使用方一起改，且旧缓存/旧库里存的是老值。
3. **数据库迁移只建表/加列**，不顺手写 `UPDATE … SET` 回填——大表上那条 UPDATE 能让升级直接打不开（R5 的真实事故）。
4. 新端点先写进 §2 的表并标「⚠️ 未实测」，实测后再改状态；`code` 语义不确定时按「只认顶层 code」处理。
5. 契约层（`*/contracts/`）不许出现 Qt 类型：注释里都别写 `QML`，`boundaries.py` 按文本扫。
