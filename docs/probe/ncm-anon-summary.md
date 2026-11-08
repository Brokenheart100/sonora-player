# 网易云常用接口 · 匿名实测记录（N0）

探测日期：2026-11-08 · 脚本 `tools/probe_ncm_api.ps1` · 原始响应 `docs/probe/raw/` 与 `docs/probe/raw-extra/`
日志 `docs/probe/ncm-anon.log` 里的中文在终端是乱码（GBK 管道），本文件是解析后的正确内容。

## 一、匿名可用

| 端点 | 结论 | 关键字段 |
| --- | --- | --- |
| `/api/search/get?s=&type=1` | ✅ 可用 | `result.songs[].id/name/artists[]/album{}`、`result.songCount` |
| `/api/toplist` | ✅ 可用，65 张榜 | `list[].id/name/updateFrequency`（`tracks` 恒为 null） |
| `/api/v6/playlist/detail?id=` | ✅ 可用 | `playlist.{id,name,coverImgUrl,trackCount,trackIds[],creator{},tags[]}`；**`tracks` 只给前 10 首** |
| `/api/v3/playlist/detail?id=` | ✅ 可用（同一份数据，多 `urls`/`privileges`） | 同上 |
| `/api/song/detail?ids=[…]` | ✅ 可用，批量 | `songs[].{id,name,artists[],album{},duration}`（**旧版字段**，与 v6 详情的 `ar`/`al`/`dt` 不同） |
| `/api/song/enhance/player/url?id=&br=320000` | ✅ 可用 | `data[].url/br/size/type`；无版权时 `url` 为 null 且 `code` 仍 200 |
| `/api/song/lyric?id=&lv=-1&tv=-1` | ✅ 可用 | `lrc.lyric`（1471 字符）、`tlyric.lyric`、`klyric`（逐字） |
| `/api/v1/album/<id>` | ✅ 可用 | `songs[].{id,name,ar,al,dt}`、`album{}`（注意：以前看到 code=0 是脚本正则误抓了嵌套 `code`） |
| `/api/v1/artist/<id>` | ✅ 可用（**必须真实 id**） | `artist{name,img1v1Url}`、`hotSongs[]` |
| `/api/artist/<id>`（旧） | ✅ 可用 | 同上 |
| `/api/personalized/playlist?limit=` | ✅ 可用，6 张 | `result[].{id,name,picUrl,copywriter}` |
| `/api/personalized/newsong?limit=` | ✅ 可用，5 首 | `result[].song{id,name,ar,al,dt}` |
| `/api/v1/radio/get` | ✅ **匿名也能取**，一次 1 首 | `data[].{id,name,ar,al,dt}` |
| `/api/v3/discovery/recommend/songs` | ⚠️ 匿名返回 31 首 `data.dailySongs`（与文档假设的“需登录”不符） | 结构同登录态 |

## 二、已失效 / 用不了

| 端点 | 实际返回 | 替代 |
| --- | --- | --- |
| `/api/playlist/track/all?id=&limit=&offset=` | `{"code":404,"message":"接口未找到"}` | `playlist.detail.trackIds[]` + `/api/song/detail?ids=` 分批（500/批） |
| `/api/playlist/track/ids?id=` | `code 404` | 同上，用 detail 的 `trackIds` |
| `/api/discovery/simiSong?id=` | `code 400 参数错误` | 暂不用（N1 不做相似推荐） |
| `/api/v1/artist/songs?id=&limit=&order=hot` | `code 400 参数错误` | 用 `/api/v1/artist/<id>` 的 `hotSongs`（前 50 首） |

## 三、需要登录（匿名拿不到）

| 端点 | 匿名返回 |
| --- | --- |
| `/api/v1/discovery/recommend/resource`（每日推荐歌单） | `code 301` |
| `/api/user/playlist?uid=` | `uid=0` 时返回 1 张空壳列表，**无法代表登录态** |
| `/api/v1/playlist/manipulate/tracks`（收藏/取消收藏，写操作） | 未探（只读原则） |

## 四、字段坑

- **`artists[].id` 可能是 0**：搜索「晴天」得到的是 `2668397359`（翻唱/UP 主上传），其 `artists[0].id = 0`、`album.artist.id = 0`。拿 0 去请求 `/api/v1/artist/0` 得到 404、`/api/v1/artist/songs?id=0` 得到 400。**artist/album 页的 id 只能从详情接口里取，不能从搜索结果直接信。**
- **`v1` 系列成功码不统一**：脚本最初用正则抓第一个 `"code"`，`/api/v1/album/` 的第一个 `code` 是嵌套对象里的 0，被误读成失败。真实顶层 `code` 是 200。解析要取**顶层** `code`。
- **同一首歌有两套字段**：`/api/song/detail?ids=` 用 `artists`/`album`/`duration`（旧版），`/api/v6/playlist/detail` 与 `/api/personalized/*` 用 `ar`/`al`/`dt`（新版）。编解码必须兼容两种，不能只认一套。
- **`playlist.trackCount` 与 `tracks` 长度不等**：榜单 `19723756` 是 `trackCount=100`、`tracks` 只有 10 条、`trackIds` 有 100 条 → 列表必须按 `trackIds` 拉详情。
- **中文在终端乱码不代表数据坏**：PowerShell 输出走 GBK 管道，`docs/probe/raw/*.json` 落盘是 UTF-8，用 Python 读正常。

## 五、样例（真实名字）

- 搜索「晴天」第一条：`晴天 (原唱 周杰伦)`（id `2668397359`，时长 `270738` ms）
- 榜单第一张：`飙升榜`（id `19723756`，trackCount `100`）
- 该榜单名：`飙升榜`，创建者 `网易云音乐`
- 个性化推荐第一张：`「后摇」一只孤独的巨兽在深海里呼喊`
- 新歌第一首：`Hong Kong (feat. 王菲季候风)`
- 每日推荐（匿名）第一首：`你瞒我瞒`
- 私人 FM（匿名）第一首：`葛藤花语`
- 歌词首行：`[00:00.000] 作词 : Chieh-lun Chou`

## 六、第二轮补测（真实 id：artist `130742690` / song `3458269805`）

- A `song/detail?ids=` → status=200 code=200 `烂奇迹(不小心惹了上帝)` · artists=`杨羊烊` album=`烂奇迹…` duration=有
- E `song/enhance/player/url` → status=200 code=200 url=有 br=320002 size=8282925
- F `v1/artist/<id>` → status=200 code=200 `杨羊烊` · 热门 8 首
- H `artist/<id>` → status=200 code=200 `杨羊烊` · 热门 8 首
