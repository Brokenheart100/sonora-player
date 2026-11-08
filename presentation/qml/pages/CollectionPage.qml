import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 一张卡片点进去之后的页面：歌单 / 榜单 / 专辑 / 歌手 / 新歌共用这一页。
//
// 它不自己发请求：`Online.openCollection()` 已经把结果列表切过去了，
// 这一页只负责"标题 + 返回 + 表头 + 行 + 翻页"，和搜索页是同一个模型。
// 这样翻页、重试、双击播放都不必各写一遍。
Item {
    id: page

    readonly property var cols: [0.06, 0.44, 0.70, 0.86]
    readonly property int rowHeight: 46
    readonly property int headerHeight: 26
    readonly property int statusHeight: 34
    readonly property int titleHeight: 64

    Rectangle {
        anchors.fill: parent
        color: Theme.paperHigh
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ── 标题条 ───────────────────────────────────────────────────
        Item {
            width: parent.width
            height: page.titleHeight

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

            ComicButton {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("返回")
                tone: "paper"
                onClicked: Online.closeCollection()
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 110
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    width: parent.width
                    text: Online.collectionTitle
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: 20
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: Online.collectionSubtitle
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        // ── 表头 ─────────────────────────────────────────────────────
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

        // ── 行 ───────────────────────────────────────────────────────
        Item {
            width: parent.width
            height: parent.height - page.titleHeight - page.headerHeight - page.statusHeight

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

                Text {
                    anchors.centerIn: parent
                    width: Math.min(parent.width * 0.7, 420)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: Online.results.count === 0
                    text: Online.errorText.length > 0
                          ? Online.errorText
                          : (Online.busy ? qsTr("正在加载…") : Online.statusText)
                    color: Online.errorText.length > 0 ? Theme.ink : Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }
            }
        }

        // ── 状态栏 ───────────────────────────────────────────────────
        Item {
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
                tone: "paper"
                onClicked: Online.loadMore()
            }
        }
    }
}
