import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 在线页（R1 的空壳）。
//
// 这一页在 R1 里**只会显示一种结果**：错误"在线搜索还没接通（下一步 R2）"。
// 那是有意的——先把"命令 → 状态表 → 50ms 轮询 → 三态界面"这条链走通，
// R2 换进去的只是"谁来填行"，界面一行都不用改。
//
// 与曲库页的三处差别，也是这一页存在的理由：
//   1. 搜索要回车才发（曲库是输入即筛：本地两千首是毫秒级查询，网络不是）
//   2. 有"账号胶囊"（未登录 / 昵称），因为在线结果是按账号权限给的
//   3. 行尾可能带灰标签（无版权 / 试听），曲库永远不会出现
Item {
    id: page

    // "" = 搜索页；"playlist" = 推荐歌单；"chart" = 榜单。
    // 浏览层是整页覆盖而不是塞进搜索页的布局里：搜索页的行列早就排满了。
    property string browseMode: ""


    // 与 LibraryPage 同一套列宽分配方式：表头与数据行共用一个数组，
    // 各写一份就一定会漂。
    // 四列起点（占列表宽度比例）：序号 / 标题(含封面) / 艺术家 / 专辑，时长贴右边。
    // 与曲库页同一套算法，但起点整体左移：在线结果的专辑名普遍更长
    // （"七里香 (钢琴版) [...]"），0.85 起步的专辑列只剩几十像素。
    readonly property var cols: [0.05, 0.36, 0.60, 0.76]
    readonly property int rowHeight: 46
    readonly property int headerHeight: 26
    readonly property int statusHeight: 34

    MangaPanel {
        anchors.fill: parent
        padding: 0

        Column {
            anchors.fill: parent
            spacing: 0

            // ── 搜索 + 账号 ────────────────────────────────────────────
            Item {
                id: searchBar
                width: parent.width
                height: 40

                Rectangle {
                    anchors.fill: parent
                    color: Theme.paperHigh
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.screentone
                }

                TextInput {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 150
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.ink
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.paperHigh
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                    clip: true
                    // 输入时走 280ms 防抖（连打 5 个字只发 1 次请求），回车立刻发。
                    // 不给 focus:true：窗口级 Shortcut（空格/←/→）
                    // 会因为输入框抢焦点而收不到键，用户会以为快捷键坏了。
                    onTextEdited: Online.queueSearch(text)
                    onAccepted: Online.search(text)
                }

                Text {
                    anchors.left: searchInput.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: searchInput.text.length === 0
                    text: qsTr("搜索网易云音乐，回车确认")
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }

                // 浏览入口：推荐歌单 / 榜单各一张卡片墙。
                Row {
                    anchors.right: diagChip.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    ComicButton {
                        text: qsTr("推荐")
                        tone: "paper"
                        onClicked: {
                            page.browseMode = "playlist"
                            Online.closeCollection()
                            Online.loadCollections("playlist")
                        }
                    }

                    ComicButton {
                        text: qsTr("榜单")
                        tone: "paper"
                        onClicked: {
                            page.browseMode = "chart"
                            Online.closeCollection()
                            Online.loadCollections("chart")
                        }
                    }
                }

                // 诊断入口：默认收起，点开才取快照（查两次 COUNT，不能进轮询）。
                Rectangle {
                    id: diagChip
                    anchors.right: accountChip.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56
                    height: 24
                    radius: 12
                    color: diagSheet.visible ? Theme.ink : "transparent"
                    border.color: Theme.stroke
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("诊断")
                        color: diagSheet.visible ? Theme.paperHigh : Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                    }

                    TapHandler {
                        onTapped: {
                            diagSheet.visible = !diagSheet.visible
                            if (diagSheet.visible)
                                Online.refreshDiagnostics()
                        }
                    }
                }

                // 账号胶囊：登录了显示昵称，点开是"退出登录"；没登录点开是粘贴框。
                // 不直接一点就登出 —— 误触一下把 cookie 删掉的代价太大。
                Rectangle {
                    id: accountChip
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(accountLabel.implicitWidth + 20, 76)
                    height: 24
                    radius: 12
                    color: Online.signedIn ? Theme.accent : "transparent"
                    border.color: Online.signedIn ? Theme.accent : Theme.stroke
                    border.width: 1

                    Text {
                        id: accountLabel
                        anchors.centerIn: parent
                        text: Online.accountText
                        color: Online.signedIn ? Theme.paperHigh : Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                    }

                    TapHandler {
                        onTapped: {
                            loginSheet.qrMode = true
                            loginSheet.visible = true
                            // 打开就立刻取一张二维码：扫码是默认进路，不该再点一次。
                            if (!Online.signedIn)
                                Online.startQrLogin()
                        }
                    }
                }
            }

            // ── 表头 ───────────────────────────────────────────────────
            Item {
                id: headerRow
                width: parent.width
                height: page.headerHeight

                Repeater {
                    model: [qsTr("#"), qsTr("标题"), qsTr("艺术家"), qsTr("专辑")]

                    Text {
                        required property int index
                        required property string modelData
                        x: Math.round(page.cols[index] * headerRow.width)
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                    }
                }
            }

            // ── 结果列表 ───────────────────────────────────────────────
            Item {
                width: parent.width
                height: parent.height - searchBar.height - headerRow.height - page.statusHeight

                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true
                    model: Online.results
                    spacing: 0

                    delegate: TrackRow {
                        cols: page.cols
                        rowHeight: page.rowHeight
                        onActivated: Online.play(index)
                        // 右键收藏：TrackRow 只负责喊一声，确认与发送在 ViewModel 里。
                        onFavoriteRequested: Online.requestFavorite(index)
                    }

                    // 空态：加载中 / 出错 / 真的没有结果，三种文案分开写。
                    // 合成一句"暂无数据"会让用户不知道要不要重试。
                    Text {
                        anchors.centerIn: parent
                        width: Math.min(parent.width * 0.7, 420)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: Online.results.count === 0
                        text: Online.errorText.length > 0
                              ? Online.errorText
                              : (Online.busy ? qsTr("正在连网易云…") : Online.statusText)
                        color: Online.errorText.length > 0 ? Theme.ink : Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textBody
                    }
                }
            }

            // ── 状态栏 ─────────────────────────────────────────────────
            Item {
                id: statusRow
                width: parent.width
                height: page.statusHeight

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: Theme.screentone
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: Online.busy ? qsTr("处理中…") : Online.statusText
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }

                ComicButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    visible: Online.hasMore && !Online.busy
                    text: qsTr("加载更多")
                    onClicked: Online.loadMore()
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    visible: Online.retryable && !Online.busy
                    text: qsTr("重试")
                    color: Theme.accent
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall

                    TapHandler {
                        onTapped: Online.retry()
                    }
                }
            }
        }
    }

    // ── 诊断面板 ──────────────────────────────────────────────────────────
    // 展开展示 §5.3 的降级留痕：连续失败、令牌余量、缓存占用、最近 10 条错误。
    // 面板打开时每 2 秒取一次快照；关着的时候一个查询都不发。
    MangaPanel {
        id: diagSheet
        visible: false
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.top: parent.top
        anchors.topMargin: 60
        width: 320
        height: diagColumn.implicitHeight + 56
        padding: 14
        fillColor: Theme.paperHigh

        Timer {
            running: diagSheet.visible
            interval: 2000
            repeat: true
            onTriggered: {
                Online.refreshDiagnostics()
            }
        }

        Column {
            id: diagColumn
            width: parent.width
            spacing: 6

            Text {
                text: qsTr("在线诊断")
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textBody
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    var d = Online.diagnostics
                    if (!d || d.provider === undefined)
                        return qsTr("暂无数据")
                    return qsTr("连续失败 %1 次 · 令牌 %2/%3")
                        .arg(d.consecutiveFailures).arg(d.tokensLeft.toFixed(1)).arg(d.tokenBurst)
                }
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    var d = Online.diagnostics
                    if (!d || d.cacheRows === undefined)
                        return ""
                    return qsTr("搜索缓存 %1/%2 行 · %3/%4 KB（LRU）")
                        .arg(d.cacheRows).arg(d.capRows)
                        .arg(Math.round(d.cacheBytes / 1024)).arg(Math.round(d.capBytes / 1024))
                }
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    var g = Online.diagnostics.degradations
                    if (!g || g.totalErrors === undefined)
                        return ""
                    return qsTr("降级：离线 %1 · 超时 %2 · 限流 %3 · 未登录 %4 · 上游 %5 · 地址过期 %6 · 缓存命中 %7")
                        .arg(g.offline).arg(g.timeout).arg(g.rateLimited).arg(g.unauthenticated)
                        .arg(g.upstream).arg(g.urlExpired).arg(g.cacheHits)
                }
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.screentone
            }

            Text {
                text: qsTr("最近错误")
                color: Theme.ink
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Repeater {
                model: {
                    var d = Online.diagnostics
                    return (d && d.recentErrors) ? d.recentErrors : []
                }

                delegate: Text {
                    required property var modelData
                    width: diagColumn.width
                    wrapMode: Text.WordWrap
                    // "搜索 · 请求太频繁，稍后再试"
                    text: modelData.where + " · " + modelData.message
                    color: Theme.accent
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }
            }
        }
    }

    // ── 登录面板 ──────────────────────────────────────────────────────────
    // 两种进路：扫码（默认，不用碰 cookie）与粘贴 Cookie（扫码不通时的后路）。
    // 二维码用 Canvas 按格画：明文接口不给二维码图，本地也不引图片编码器。
    Item {
        id: loginSheet
        anchors.fill: parent
        visible: false
        // 打开时默认落在扫码上：少一步点击，也少一次"cookie 从哪来"的解释。
        property bool qrMode: true

        MouseArea {
            anchors.fill: parent
            onClicked: loginSheet.visible = false
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.scrim
        }

        MangaPanel {
            anchors.centerIn: parent
            width: 560
            height: Online.signedIn ? 210 : 520

            Column {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                Text {
                    text: Online.signedIn ? qsTr("已登录") : (loginSheet.qrMode ? qsTr("扫码登录") : qsTr("粘贴网易云 Cookie"))
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.textTitle
                }

                // ---- 已登录：只给退出 ----
                Text {
                    visible: Online.signedIn
                    text: Online.accountText
                    color: Theme.accent
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }

                Text {
                    visible: Online.signedIn
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("退出后本地密文会被删除，下次要重新登录。")
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }

                // ---- 未登录：两个入口 ----
                Row {
                    visible: !Online.signedIn
                    spacing: 10

                    ComicButton {
                        text: qsTr("扫码")
                        tone: loginSheet.qrMode ? "accent" : "paper"
                        onClicked: {
                            loginSheet.qrMode = true
                            Online.startQrLogin()
                        }
                    }

                    ComicButton {
                        text: qsTr("粘贴 Cookie")
                        tone: loginSheet.qrMode ? "paper" : "accent"
                        onClicked: {
                            loginSheet.qrMode = false
                            Online.cancelQrLogin()
                        }
                    }
                }

                // ---- 二维码 ----
                Item {
                    visible: !Online.signedIn && loginSheet.qrMode
                    width: 240
                    height: 240

                    Canvas {
                        id: qrCanvas
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d")
                            var size = Online.qrSize
                            ctx.reset()
                            ctx.fillStyle = "white"
                            ctx.fillRect(0, 0, width, height)
                            if (size <= 0)
                                return
                            var cell = width / size
                            ctx.fillStyle = "#111111"
                            for (var row = 0; row < size; ++row) {
                                for (var col = 0; col < size; ++col) {
                                    if (Online.qrCell(row, col))
                                        ctx.fillRect(col * cell, row * cell, Math.ceil(cell), Math.ceil(cell))
                                }
                            }
                        }
                    }

                    Connections {
                        target: Online
                        function onQrChanged() { qrCanvas.requestPaint() }
                    }
                }

                Text {
                    visible: !Online.signedIn && loginSheet.qrMode
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: Online.qrMessage.length > 0 ? Online.qrMessage : qsTr("用网易云音乐手机端扫码")
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }

                // ---- 粘贴框 ----
                Rectangle {
                    visible: !Online.signedIn && !loginSheet.qrMode
                    width: parent.width
                    height: 150
                    color: Theme.paperHigh
                    border.color: Theme.stroke
                    border.width: 1

                    TextEdit {
                        id: cookieInput
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        color: Theme.ink
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                        Text {
                            anchors.fill: parent
                            visible: cookieInput.text.length === 0 && !cookieInput.activeFocus
                            text: qsTr("浏览器登录 music.163.com → F12 → Network → 任选一个请求，
复制请求头里的整段 Cookie（只要有 MUSIC_U 就行）")
                            color: Theme.inkSoft
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontText
                            font.pixelSize: Theme.textSmall
                        }
                    }
                }

                Text {
                    visible: !Online.signedIn
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: Online.signingIn ? qsTr("正在核对…") : Online.loginError
                    color: Theme.accent
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }

                Row {
                    spacing: 10

                    ComicButton {
                        visible: !Online.signedIn && loginSheet.qrMode
                        text: qsTr("刷新二维码")
                        tone: "paper"
                        onClicked: Online.startQrLogin()
                    }

                    ComicButton {
                        visible: !Online.signedIn && !loginSheet.qrMode
                        text: Online.signingIn ? qsTr("请稍候") : qsTr("登录")
                        tone: "accent"
                        onClicked: Online.signIn(cookieInput.text)
                    }

                    ComicButton {
                        visible: Online.signedIn
                        text: qsTr("退出登录")
                        tone: "ink"
                        onClicked: {
                            Online.signOut()
                            loginSheet.visible = false
                        }
                    }

                    ComicButton {
                        text: qsTr("关闭")
                        tone: "paper"
                        onClicked: {
                            loginSheet.visible = false
                            if (!Online.signedIn)
                                Online.cancelQrLogin()
                        }
                    }
                }
            }
        }
    }

    // 登录成功就自动收起，并停掉扫码轮询。
    Connections {
        target: Online
        function onStatusChanged() {
            if (Online.signedIn && loginSheet.visible) {
                loginSheet.visible = false
                Online.cancelQrLogin()
            }
        }
    }

    // ── 浏览层：推荐歌单 / 榜单 ─────────────────────────────────────
    Item {
        id: browseLayer
        anchors.fill: parent
        visible: page.browseMode.length > 0 && !Online.collectionActive

        Rectangle {
            anchors.fill: parent
            color: Theme.paperHigh
        }

        Item {
            id: browseBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 50

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.screentone
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                ComicButton {
                    text: qsTr("推荐")
                    tone: page.browseMode === "playlist" ? "accent" : "paper"
                    onClicked: {
                        page.browseMode = "playlist"
                        Online.loadCollections("playlist")
                    }
                }

                ComicButton {
                    text: qsTr("榜单")
                    tone: page.browseMode === "chart" ? "accent" : "paper"
                    onClicked: {
                        page.browseMode = "chart"
                        Online.loadCollections("chart")
                    }
                }

                ComicButton {
                    text: qsTr("新歌速递")
                    tone: "paper"
                    onClicked: Online.openCollection("newsongs", "latest", qsTr("新歌速递"))
                }

                ComicButton {
                    text: qsTr("每日推荐")
                    tone: "paper"
                    onClicked: Online.openDailyRecommend()
                }

                // N4：私人 FM。追加式队列，播到末尾会自动再来要一批。
                ComicButton {
                    text: qsTr("私人 FM")
                    tone: Online.fmMode ? "accent" : "paper"
                    onClicked: {
                        page.browseMode = ""
                        Online.closeCollection()
                        Online.startPersonalFm()
                    }
                }

                ComicButton {
                    text: qsTr("我的")
                    tone: page.browseMode === "mine" ? "accent" : "paper"
                    onClicked: {
                        page.browseMode = "mine"
                        Online.closeCollection()
                        Online.loadMyPlaylists()
                    }
                }
            }

            ComicButton {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("返回搜索")
                tone: "paper"
                onClicked: {
                    page.browseMode = ""
                    Online.closeCollection()
                }
            }
        }

        GridView {
            id: cardGrid
            visible: page.browseMode !== "mine"
            anchors.top: browseBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            anchors.topMargin: 20
            clip: true
            cellWidth: 188
            cellHeight: 248
            model: Online.collections

            delegate: CollectionCard {
                onActivated: Online.openCollection(page.browseMode, collectionId, name)
            }
        }

        // "我的歌单"：登录后从 playlist[] 拉下来的，卡片样式与推荐/榜单一致。
        GridView {
            id: mineGrid
            visible: page.browseMode === "mine" && Online.myPlaylists.length > 0
            anchors.top: browseBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            clip: true
            cellWidth: 188
            cellHeight: 248
            model: Online.myPlaylists

            delegate: Item {
                width: 168
                height: 228

                Rectangle {
                    anchors.fill: parent
                    color: Theme.paperHigh
                    border.width: 3
                    border.color: Theme.ink
                }

                Image {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 10
                    height: 140
                    source: modelData.cover
                    sourceSize: Qt.size(320, 320)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }

                Text {
                    anchors.top: parent.top
                    anchors.topMargin: 158
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 10
                    text: modelData.name
                    elide: Text.ElideRight
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: 16
                }

                Text {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 10
                    text: modelData.trackCount > 0 ? qsTr("%1 首").arg(modelData.trackCount) : ""
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: 13
                }

                TapHandler {
                    onTapped: Online.openCollection("playlist", modelData.playlistId, modelData.name)
                }
            }
        }

        Text {
            id: mineEmpty
            anchors.centerIn: parent
            visible: page.browseMode === "mine" && Online.myPlaylists.length === 0
            text: Online.signedIn ? qsTr("还没有歌单") : qsTr("登录后可见")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: 18
        }

        Text {
            anchors.centerIn: cardGrid
            width: Math.min(parent.width * 0.7, 420)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: Online.collections.count === 0
            text: Online.collections.status.length > 0 ? Online.collections.status : qsTr("正在拉取…")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
        }
    }

    // ── 卡片详情：与搜索页共用 Online.results ──────────────────────
    CollectionPage {
        anchors.fill: parent
        visible: Online.collectionActive
    }

    // ── 收藏确认（只在线页有；右键行触发） ─────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: Online.favoritePromptVisible
        color: Theme.ink
        opacity: 0.42
        z: 40

        MouseArea {
            anchors.fill: parent
            onClicked: Online.cancelFavorite()
        }
    }

    MangaPanel {
        anchors.centerIn: parent
        visible: Online.favoritePromptVisible
        z: 41
        width: Math.min(460, parent.width - 96)
        height: 196

        Column {
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 16

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: Online.favoritePromptText
                color: Theme.ink
                font.family: Theme.fontText
                font.pixelSize: Theme.textBody
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 14

                ComicButton {
                    text: qsTr("收藏")
                    strong: true
                    onClicked: Online.confirmFavorite()
                }

                ComicButton {
                    text: qsTr("取消")
                    tone: "paper"
                    onClicked: Online.cancelFavorite()
                }
            }
        }
    }

}
