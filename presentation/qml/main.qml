import QtQuick
// 单例是 C++ 侧用 qmlRegisterSingletonInstance 注册到 "MambaMusic" 这个 URI 下的，
// 所以**必须 import 这个 URI**，否则 Library 在运行时是 undefined —— 而且不会
// 报任何错误，只是所有绑定悄悄变成空字符串。这是最容易排查半天的一类坑。
import MambaMusic 1.0

// M1 的界面：一张纸，一列曲目。
//
// M1 不追求好看，只追求"数据真的从 SQLite 走到了屏幕上"。所以这里只有
// 三块东西：标题栏、列表、空状态。M2 会把这些矩形换成漫画设计系统
// （网点、描边、拟声字），那时这个文件的 90% 会被重写——但它现在证明的是
// 架构里最难证明的那件事：QML 只认识一个注入的单例。
Window {
    id: shell

    width: 1140
    height: 760
    minimumWidth: 880
    minimumHeight: 560

    visible: true
    title: qsTr("MambaMusic")

    // 纸色。M2 会把它抽成 Theme.paper
    color: "#f2ece0"

    // 标题栏
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20
        height: 72
        color: "#fffdf8"
        border.width: 2
        border.color: "#1b1a18"
        radius: 3

        // 漫画里贴在框外的那层阴影，用一条错位的实心矩形做，比阴影效果更像印刷
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 4
            anchors.leftMargin: 4
            anchors.rightMargin: -4
            anchors.bottomMargin: -4
            z: -1
            color: "#1b1a18"
            radius: 3
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                text: "MAMBA MUSIC"
                color: "#1b1a18"
                font.pixelSize: 24
                font.bold: true
                font.letterSpacing: 3
            }
            Text {
                text: "M1 · 曲库"
                color: "#8a8272"
                font.pixelSize: 12
                font.letterSpacing: 1
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: shell.listCount === 0 ? "曲库是空的" : "共 " + shell.listCount + " 首"
                color: "#1b1a18"
                font.pixelSize: 13
                font.bold: true
            }

            Rectangle {
                width: 84
                height: 32
                color: refreshArea.pressed ? "#1b1a18" : "#fffdf8"
                border.width: 2
                border.color: "#1b1a18"
                radius: 3

                Text {
                    anchors.centerIn: parent
                    text: "重新读一次"
                    color: refreshArea.pressed ? "#fffdf8" : "#1b1a18"
                    font.pixelSize: 12
                }

                MouseArea {
                    id: refreshArea
                    anchors.fill: parent
                    onClicked: Library.refresh()
                }
            }
        }
    }

    // 曲目列表
    Rectangle {
        id: panel
        anchors.top: header.bottom
        anchors.topMargin: 16
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        anchors.bottomMargin: 12
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        color: "#fffdf8"
        border.width: 2
        border.color: "#1b1a18"
        radius: 3

        ListView {
            id: trackList
            anchors.fill: parent
            anchors.margins: 2
            clip: true
            model: Library
            spacing: 0

            delegate: Rectangle {
                id: row

                required property int index
                required property string title
                required property string artist
                required property string album
                required property string duration

                width: trackList.width
                height: 46
                color: rowArea.containsMouse ? "#f2ece0" : "transparent"

                // 行与行之间那道细线，是"表格感"的唯一来源
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: "#e0d8c7"
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 36
                    text: String(row.index + 1).padStart(3, " ")
                    color: "#a89f8c"
                    font.pixelSize: 12
                    font.family: "Consolas"
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 56
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.42
                    text: row.title
                    color: "#1b1a18"
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: parent.width * 0.5
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.22
                    text: row.artist
                    color: "#5f5a4e"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: parent.width * 0.73
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.18
                    text: row.album
                    color: "#8a8272"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.duration
                    color: "#5f5a4e"
                    font.pixelSize: 13
                    font.family: "Consolas"
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }
        }

        // 空状态：告诉用户怎么把歌放进来，而不是留一张白纸
        Column {
            anchors.centerIn: parent
            spacing: 10
            visible: shell.listCount === 0

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "还没有歌"
                color: "#1b1a18"
                font.pixelSize: 20
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "带一个文件夹启动，或把曲库放进应用数据目录"
                color: "#8a8272"
                font.pixelSize: 13
            }
        }
    }

    // 页脚：状态行。所有错误都从这里说出来，而不是弹窗。
    Text {
        id: footer
        anchors.left: parent.left
        anchors.leftMargin: 22
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        text: Library.status
        color: "#6b6558"
        font.pixelSize: 12
    }

    // count 通过 property 转一手，是为了让 header 与空状态都能绑定它，
    // 同时不在每个地方都写 Library.count（一个属性改名的成本 > 这一行）
    readonly property int listCount: Library.count
}
