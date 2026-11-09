import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 歌词列：整份加载、只推进"当前行"，当前行居中靠 contentY 动画（不用 Behavior，
// 那会和用户拖动抢同一个属性）。抽成组件是因为它是整页里唯一自己滚动的区域。
Item {
    id: root
        width: Math.max(280, parent.width * 0.48)
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 8
        anchors.bottomMargin: 4

        Text {
            id: lyricsHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 20
            text: Lyrics.statusText.length > 0
                  ? qsTr("歌词 · ") + Lyrics.statusText
                  : qsTr("歌词")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textSmall
            elide: Text.ElideRight
        }

        ListView {
            id: lyricList
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: lyricsHeader.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: 8
            clip: true
            spacing: 16
            visible: Lyrics.visible
            model: Lyrics
            currentIndex: Lyrics.currentIndex
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: lineRow
                required property string text
                required property string translation
                required property bool active

                width: lyricList.width
                height: textLine.height + (translation.length > 0 ? translationLine.height + 6 : 0)

                Text {
                    id: textLine
                    width: lineRow.width
                    text: lineRow.text
                    color: lineRow.active ? Theme.accent : Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: lineRow.active ? 21 : 16
                    font.bold: lineRow.active
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter

                    Behavior on font.pixelSize {
                        NumberAnimation { duration: Theme.animBase }
                    }
                }

                Text {
                    id: translationLine
                    width: lineRow.width
                    y: textLine.height + 6
                    visible: lineRow.translation.length > 0
                    text: lineRow.translation
                    color: lineRow.active ? Theme.ink : Theme.inkSoft
                    opacity: 0.75
                    font.family: Theme.fontText
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            function syncTo(index, animated) {
                if (index < 0) {
                    return
                }
                const item = itemAtIndex(index)
                if (!item) {
                    return
                }
                const desired = item.y + item.height / 2 - height / 2
                const target = Math.max(0, Math.min(Math.max(0, contentHeight - height), desired))
                if (animated) {
                    scrollAnim.to = target
                    scrollAnim.start()
                } else {
                    scrollAnim.stop()
                    contentY = target
                }
            }

            onCurrentIndexChanged: syncTo(currentIndex, true)
            // 切歌、或者刚打开这一页：模型一换就立刻对到当前句，不要从第一行滑过去
            // （动画只在"当前句往前走"时才用）。
            onCountChanged: syncTo(currentIndex, false)
            Component.onCompleted: syncTo(currentIndex, false)
        }

        NumberAnimation {
            id: scrollAnim
            target: lyricList
            property: "contentY"
            duration: Theme.animBase
            easing.type: Easing.OutCubic
        }

        // 没有歌词（含纯音乐、读不到、编码不对）时只留一句话，不画空盒子。
        Text {
            anchors.centerIn: parent
            width: parent.width - 40
            visible: !Lyrics.visible
            text: Lyrics.statusText.length > 0 ? Lyrics.statusText : qsTr("没有歌词")
            color: Theme.inkSoft
            opacity: 0.6
            font.family: Theme.fontText
            font.pixelSize: Theme.textSmall
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        // 在线歌词的来源标记（N3-3）。用户会问"这词哪来的"——本地没有 .lrc、
        // 也没内嵌标签，答案就是"网上取的"。放右下角、比正文小一号，
        // 不抢歌词的位置；小圆点是同一个朱红的低饱和版本，不与当前句争色。
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 2
            spacing: 5
            visible: Lyrics.onlineSource && Lyrics.visible
            opacity: 0.55

            Rectangle {
                width: 5
                height: 5
                radius: 2.5
                color: Theme.accent
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: qsTr("在线歌词")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall - 1
            }
        }
}
