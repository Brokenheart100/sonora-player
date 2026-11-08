import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 顶部条：搜索框 + 登录头像 + 用户名。
//
// 一条全局的搜索框有两种目标页，路由按 `page` 走：
//   - 在「在线」页 → 网易云搜索（输入 280ms 防抖、回车立刻发）
//   - 其它页       → 曲库搜索（FTS5，输入即筛），并切到曲库页
// 这样"搜"永远只有一个入口，用户不用先想"我要搜的是本地还是网上"——
// 位置本身就是在回答这个问题。
Item {
    id: topBar

    height: 56
    // 0 曲库 / 1 正在播放 / 2 在线 / 3 设置 / 4 歌单，与 Shell.currentPage 同一套编号。
    property int page: 0

    signal loginRequested()
    signal libraryRequested()

    readonly property bool onlineMode: page === 2

    Rectangle {
        anchors.fill: parent
        color: Theme.paperHigh
    }

    // 漫画的粗底线：顶部条与内容是"两格"，得有一笔画开。
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: Theme.ink
    }

    // ── 搜索框 ──────────────────────────────────────────────────────────
    Rectangle {
        id: field
        anchors.left: parent.left
        anchors.leftMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        // 右侧给账号区让位；窗口窄的时候至少留 180，剩下的让账号区自己被挤。
        width: Math.max(180, Math.min(520, topBar.width - accountArea.width - 44))
        height: 34
        radius: 17
        color: Theme.paper
        border.width: searchInput.activeFocus ? 2 : 1.5
        border.color: searchInput.activeFocus ? Theme.accent : Theme.stroke

        // 放大镜：圆环 + 一根斜柄。不用图形字体，省一次"这个字形在不在子集里"。
        Item {
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16

            Rectangle {
                width: 11
                height: 11
                radius: 5.5
                color: "transparent"
                border.color: Theme.ink
                border.width: 1.5
            }
            Rectangle {
                x: 9
                y: 9
                width: 1.8
                height: 7
                color: Theme.ink
                transformOrigin: Item.TopLeft
                rotation: 45
            }
        }

        TextInput {
            id: searchInput
            anchors.fill: parent
            anchors.leftMargin: 40
            anchors.rightMargin: 12
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.ink
            selectionColor: Theme.accent
            selectedTextColor: Theme.paperHigh
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
            clip: true

            // 不给 focus:true。输入框抢到键盘焦点，窗口级 Shortcut（空格 / ← / →）
            // 就全部失效——用户会以为播放键坏了。点一下就能打字，这点成本更划算。
            onTextEdited: topBar.route(text, false)
            onAccepted: topBar.route(text, true)

            Keys.onEscapePressed: {
                // 在线搜索清空只清框（不重发请求）；曲库搜索清空就是"取消筛选"。
                if (topBar.onlineMode) {
                    searchInput.text = ""
                } else {
                    searchInput.text = ""
                    topBar.route("", false)
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: searchInput.text.length === 0
                text: topBar.onlineMode
                      ? qsTr("搜索网易云音乐，回车确认")
                      : qsTr("搜标题、艺术家、专辑（中文一个字也能搜）")
                color: Theme.inkSoft
                opacity: 0.65
                font.family: Theme.fontText
                font.pixelSize: Theme.textBody
            }
        }
    }

    // ── 账号区：头像 + 用户名 ────────────────────────────────────────────
    Item {
        id: accountArea
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: accountRow.width
        height: 40

        Row {
            id: accountRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            // 头像：登录了是朱红圆底 + 昵称首字；没登录是纸色圆 + 一个剪影。
            Item {
                width: 34
                height: 34
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Online.signedIn ? Theme.accent : Theme.paper
                    border.color: Online.signedIn ? Theme.accent : Theme.stroke
                    border.width: 1.5
                }

                Text {
                    anchors.centerIn: parent
                    visible: Online.signedIn
                    text: Online.accountText.length > 0 ? Online.accountText.charAt(0) : "?"
                    color: Theme.paperHigh
                    font.family: Theme.fontDisplay
                    font.pixelSize: 18
                }

                // 未登录的剪影：一个头 + 一个肩。两笔就够，多了像图标库。
                Item {
                    anchors.centerIn: parent
                    visible: !Online.signedIn
                    width: 18
                    height: 18

                    Rectangle {
                        x: 5
                        y: 0
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.inkSoft
                    }
                    Rectangle {
                        x: 0
                        y: 10
                        width: 18
                        height: 9
                        radius: 4.5
                        color: Theme.inkSoft
                    }
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Text {
                    text: Online.accountText
                    color: Online.signedIn ? Theme.ink : Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }
                Text {
                    text: Online.signedIn ? qsTr("网易云") : qsTr("点此登录")
                    color: Theme.inkSoft
                    opacity: 0.75
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall - 1
                }
            }
        }

        TapHandler {
            onTapped: topBar.loginRequested()
        }
    }

    // 一条路由：决定这次输入是问曲库还是问网易云。
    function route(text, submit) {
        if (onlineMode) {
            if (submit)
                Online.search(text)
            else
                Online.queueSearch(text)
        } else {
            Library.setSearchText(text)
            if (page !== 0)
                libraryRequested()
        }
    }
}
