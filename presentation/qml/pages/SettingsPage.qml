import QtQuick
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
}
