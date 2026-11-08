import QtQuick
import QtQuick.Dialogs
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 设置页（M9 里只做"外观 + 语言"两种真正影响视图的选项）。
//
// 两条规矩：
//  · 主题与语言**立即生效**，不要求重启——主题靠 Theme.dark 的绑定，
//    语言靠 Appearance.setLanguage() 里的 engine.retranslate()；
//  · 这里不写任何业务设置（曲库目录在引导页选，播放偏好留在播放条上），
//    免得"设置页"慢慢变成一个什么都塞的抽屉。
Item {
    id: root

    // 一组选项：标题 + 若干互斥按钮。用 tone 表示选中（accent 只给"当前"用）。
    component ChoiceRow: Column {
        id: row

        property string title: ""
        property var options: []          // [{ label, value }]
        property int current: 0
        signal picked(int value)

        spacing: 8
        width: parent ? parent.width : 0

        Text {
            text: row.title
            color: Theme.ink
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
            font.weight: Font.DemiBold
        }

        Row {
            spacing: 10

            Repeater {
                model: row.options

                ComicButton {
                    required property var modelData
                    required property int index
                    text: modelData.label
                    tone: row.current === index ? "accent" : "paper"
                    strong: row.current === index
                    onClicked: row.picked(row.options[index].value)
                }
            }
        }
    }

    MangaPanel {
        anchors.fill: parent
        padding: 24

        Column {
            spacing: 22
            width: parent.width

            Text {
                text: qsTr("设置")
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: 34
            }

            Text {
                width: parent.width
                text: qsTr("主题和语言改完立刻生效，不用重启；选择会记进数据目录的 settings.ini。")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
                wrapMode: Text.WordWrap
            }

            Rectangle {
                width: parent.width
                height: Theme.stroke
                color: Theme.screentone
            }

            // ── 播放偏好 ────────────────────────────────────────────────
            // 音量与模式写在 Preferences（settings.ini）里，换会话不会被忘掉；
            // 这两项都是**播放器自己的状态**，设置页只做显示与转发。
            Column {
                spacing: 8
                width: parent.width

                Text {
                    text: qsTr("音量")
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                    font.weight: Font.DemiBold
                }

                Item {
                    width: parent.width
                    height: 26

                    Rectangle {
                        id: volumeTrack
                        anchors.left: parent.left
                        anchors.right: volumeText.left
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        height: 6
                        radius: 3
                        color: Theme.screentone

                        Rectangle {
                            width: volumeTrack.width * (Player.volume / 100.0)
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                        }

                        Rectangle {
                            x: Math.max(0, Math.min(volumeTrack.width - 14,
                                                   volumeTrack.width * (Player.volume / 100.0) - 7))
                            y: (parent.height - 14) / 2
                            width: 14
                            height: 14
                            radius: 7
                            color: Theme.paperHigh
                            border.width: Theme.stroke
                            border.color: Theme.ink
                        }

                        MouseArea {
                            anchors.fill: parent
                            // 拖动时按相对位置换算，和"点哪跳哪"是同一条路径。
                            onPositionChanged: function (mouse) {
                                if (pressed) {
                                    Player.setVolume(Math.round(100 * mouse.x / width))
                                }
                            }
                            onClicked: function (mouse) {
                                Player.setVolume(Math.round(100 * mouse.x / width))
                            }
                        }
                    }

                    Text {
                        id: volumeText
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 44
                        horizontalAlignment: Text.AlignRight
                        text: Player.volume + "%"
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                    }
                }

                Row {
                    spacing: 10

                    ComicButton {
                        width: 120
                        height: 32
                        // 按钮上写"当前是什么"，点一下换下一个。
                        text: Player.modeLabel
                        onClicked: Player.cycleMode()
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("播放模式：顺序 → 列表循环 → 单曲 → 随机")
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: Theme.stroke
                color: Theme.screentone
            }

            // ── 曲库 ────────────────────────────────────────────────────
            Column {
                spacing: 8
                width: parent.width

                Text {
                    text: qsTr("曲库")
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                    font.weight: Font.DemiBold
                }

                Row {
                    spacing: 10

                    ComicButton {
                        width: 108
                        height: 32
                        text: qsTr("重新扫描")
                        onClicked: Library.refresh()
                    }

                    ComicButton {
                        width: 108
                        height: 32
                        text: qsTr("换目录…")
                        onClicked: folderDialog.open()
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("已在库里 %1 首").arg(Library.count)
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: Theme.stroke
                color: Theme.screentone
            }

            ChoiceRow {
                title: qsTr("外观")
                current: Appearance.themeMode
                options: [
                    { label: qsTr("跟随系统"), value: 0 },
                    { label: qsTr("浅色"), value: 1 },
                    { label: qsTr("深色"), value: 2 }
                ]
                onPicked: function (value) { Appearance.themeMode = value }
            }

            ChoiceRow {
                title: qsTr("语言 / Language")
                current: Appearance.language === "en" ? 1 : 0
                options: [
                    { label: qsTr("中文"), value: 0 },
                    { label: "English", value: 1 }
                ]
                onPicked: function (value) { Appearance.language = value === 1 ? "en" : "zh_CN" }
            }

            Rectangle {
                width: parent.width
                height: Theme.stroke
                color: Theme.screentone
            }

            Column {
                spacing: 6
                width: parent.width

                Text {
                    text: qsTr("关于")
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                    font.weight: Font.DemiBold
                }

                Text {
                    width: parent.width
                    text: qsTr("Sonora %1 · 本地优先的漫画风音乐播放器").arg(Qt.application.version)
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // 换曲库目录：选完交给 Library，具体"记到哪儿"由宿主（host/main.cpp）决定。
    FolderDialog {
        id: folderDialog
        title: qsTr("选择音乐文件夹")
        // FolderDialog 给的是 url，交给 C++ 的必须是本地路径：
        // toString() 会写成 file:///D:/Music，落进 settings.ini 后重启认不出，
        // 用户会被弹回"先把歌放进来"的首次引导页。
        onAccepted: Library.chooseFolder(selectedFolder.toLocalFile())
    }
}
