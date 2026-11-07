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

                // 账号胶囊：点一下才做事（R4 之前它只会报"登录还没接通"）
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
                            if (Online.signedIn) {
                                Online.signOut()
                            } else {
                                Online.signInHint()
                            }
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
}
