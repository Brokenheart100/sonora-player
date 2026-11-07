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
            color: "#33000000"
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
}
