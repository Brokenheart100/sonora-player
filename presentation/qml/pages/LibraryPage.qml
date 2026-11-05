import QtQuick
import "../comic"
import "../theme"
import MambaMusic 1.0

// 曲库页：M1 那张"能看见数据"的列表，M2 换成漫画分格。
//
// 这里只做视觉，不动数据通路：模型仍然只是注入进来的 Library 单例。
Item {
    id: page

    // 列宽比例，表头和数据行共用一套，保证对齐
    readonly property var cols: [0.05, 0.42, 0.68, 0.85]
    readonly property int rowHeight: 46

    MangaPanel {
        anchors.fill: parent
        padding: 0

        Column {
            anchors.fill: parent
            spacing: 0

            // ── 表头 ────────────────────────────────────────────────────
            Item {
                width: parent.width
                height: 34

                Rectangle {
                    anchors.fill: parent
                    color: Theme.paperAlt
                }

                Repeater {
                    model: [qsTr("#"), qsTr("标题"), qsTr("艺术家"), qsTr("专辑"), qsTr("时长")]

                    Text {
                        required property int index
                        required property string modelData

                        y: 8
                        x: index === 4
                           ? page.width - page.cols[0] * page.width - 24
                           : page.cols[index] * page.width
                        text: modelData
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                        font.letterSpacing: 1
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.stroke
                    color: Theme.ink
                }
            }

            // ── 列表 ────────────────────────────────────────────────────
            ListView {
                id: list
                width: parent.width
                height: parent.height - 34
                clip: true
                model: Library
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 400

                delegate: Item {
                    required property int index
                    required property string title
                    required property string artist
                    required property string album
                    required property string duration

                    width: list.width
                    height: page.rowHeight

                    Rectangle {
                        anchors.fill: parent
                        color: rowHover.hovered ? Theme.paperAlt : "transparent"
                    }

                    // 双击播放。用 TapHandler 的 onDoubleTapped，而不是自己数点击次数：
                    // 双击间隔、按下后移动的容差这些细节它都处理过了。
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: Library.activate(index)
                    }

                    // 行间用一条浅浅的墨线，比斑马纹更"印刷"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        height: 1
                        color: Theme.screentone
                        opacity: 0.7
                    }

                    Text {
                        x: page.cols[0] * page.width
                        anchors.verticalCenter: parent.verticalCenter
                        text: (index + 1).toString().padStart(2, "0")
                        color: Theme.inkSoft
                        font.family: "Consolas"
                        font.pixelSize: Theme.textSmall
                    }
                    Text {
                        x: page.cols[1] * page.width
                        anchors.verticalCenter: parent.verticalCenter
                        width: page.width * 0.24
                        elide: Text.ElideRight
                        text: title
                        color: Theme.ink
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textBody
                        font.weight: Font.DemiBold
                    }
                    Text {
                        x: page.cols[2] * page.width
                        anchors.verticalCenter: parent.verticalCenter
                        width: page.width * 0.15
                        elide: Text.ElideRight
                        text: artist
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textBody
                    }
                    Text {
                        x: page.cols[3] * page.width
                        anchors.verticalCenter: parent.verticalCenter
                        width: page.width * 0.12
                        elide: Text.ElideRight
                        text: album
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textBody
                    }
                    Text {
                        x: page.width - 90
                        width: 60
                        horizontalAlignment: Text.AlignRight
                        anchors.verticalCenter: parent.verticalCenter
                        text: duration
                        color: Theme.inkSoft
                        font.family: "Consolas"
                        font.pixelSize: Theme.textSmall
                    }

                    HoverHandler { id: rowHover }
                }
            }
        }
    }

    // ── 空状态 ──────────────────────────────────────────────────────────
    Column {
        visible: Library.count === 0
        anchors.centerIn: parent
        spacing: 12

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("还没有歌")
            color: Theme.ink
            font.family: Fonts.display
            font.pixelSize: 26
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("带一个文件夹启动，或把曲库放进应用数据目录")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
        }
    }
}
