import QtQuick
import "../comic"
import "../theme"
import MambaMusic 1.0

// 「正在播放」页（M6）。
//
// 这里刻意**不做真封面**：封面字节只在导入时被 `ICoverStore` 落过一次盘，
// 而播放队列里的 `QueueEntry` 只带了标题/艺术家（播放只需要这两样）。
// 要把封面接上来，得让 `PlayQueue` 认识 coverPath——那是"播放器持有曲库字段"，
// 违反"两个模块只通过契约说话"。所以这一页画的是唱片本身：转起来、有歌名。
//
// 转不转由 `Player.playing` 决定：暂停时唱片停在原地，比"永远在转"诚实。
Item {
    id: page

    // 唱片转速：33⅓ 转/分 = 每圈 1.8 秒。这里慢一点，视觉上不晃眼。
    readonly property int spinMs: 5400

    // ── 唱片 ────────────────────────────────────────────────────────────
    Item {
        id: record
        width: Math.min(parent.height - 24, 340)
        height: width
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.top: parent.top
        anchors.topMargin: 18

        // 错位的实心阴影，和 MangaPanel 同一套语言
        Rectangle {
            anchors.fill: plate
            x: plate.x + 10
            y: plate.y + 10
            radius: width / 2
            color: Theme.ink
            opacity: 0.28
        }

        MangaPanel {
            id: plate
            anchors.fill: parent
            cornerRadius: width / 2
            padding: 0
            shadow: false
            strokeWidth: 3
            fillColor: Theme.paperHigh
        }

        // 黑胶：一层圆 + 几圈纹路
        Item {
            id: disc
            anchors.fill: parent
            anchors.margins: 14

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.ink
            }

            // 纹路：同心的细圆环，靠 border 画，不引图片
            Repeater {
                model: 7
                Rectangle {
                    required property int index
                    anchors.centerIn: parent
                    width: disc.width - 26 - index * 18
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: index % 2 === 0 ? "#4A453D" : "#2B2822"
                }
            }

            // 中心标签：漫画里最爱在唱片中心写歌名
            Rectangle {
                anchors.centerIn: parent
                width: disc.width * 0.46
                height: width
                radius: width / 2
                color: Theme.accent
                border.width: 2
                border.color: Theme.paperHigh

                Column {
                    anchors.centerIn: parent
                    width: parent.width * 0.78
                    spacing: 3

                    Text {
                        width: parent.width
                        text: Player.hasTrack ? Player.title : qsTr("未选曲")
                        color: Theme.paperHigh
                        font.family: Fonts.display
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                        maximumLineCount: 2
                    }
                    Text {
                        width: parent.width
                        text: Player.hasTrack ? Player.artist : ""
                        color: "#FFE3DC"
                        font.family: Theme.fontText
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }

                // 中心孔
                Rectangle {
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.paperHigh
                }
            }
        }

        // 转速：只在播放时转。旋转用 NumberAnimation 循环，
        // 暂停时 stop() 会停在当前角度（不是"回到 0 度"）。
        RotationAnimation on rotation {
            running: Player.playing
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: page.spinMs
        }
    }

    // ── 唱针 ────────────────────────────────────────────────────────────
    Item {
        id: arm
        width: 96
        height: 150
        x: record.x + record.width - 46
        y: record.y + 6
        rotation: Player.playing ? 18 : 0

        Behavior on rotation {
            NumberAnimation { duration: Theme.animSpring; easing.type: Easing.OutBack }
        }

        Rectangle {
            width: 7
            height: 108
            x: 44
            y: 16
            radius: 3
            color: Theme.ink
        }
        Rectangle {
            width: 22
            height: 22
            x: 36
            y: 4
            radius: 11
            color: Theme.paperHigh
            border.width: 2
            border.color: Theme.ink
        }
        Rectangle {
            width: 30
            height: 16
            x: 33
            y: 116
            radius: 3
            color: Theme.accent
            border.width: 2
            border.color: Theme.ink
        }
    }

    // ── 曲目信息与进度 ──────────────────────────────────────────────────
    Column {
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.right: lyricsPanel.left
        anchors.rightMargin: 28
        anchors.top: record.bottom
        anchors.topMargin: 26
        spacing: 10

        Text {
            width: parent.width
            text: Player.hasTrack ? Player.title : qsTr("还没有在放的东西")
            color: Theme.ink
            font.family: Fonts.display
            font.pixelSize: 30
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: {
                if (!Player.hasTrack) {
                    return qsTr("双击曲库里的任意一行开始")
                }
                const parts = []
                if (Player.artist.length > 0) parts.push(Player.artist)
                if (Player.queueText.length > 0) parts.push(Player.queueText)
                parts.push(Player.modeLabel)
                return parts.join("  ·  ")
            }
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
            elide: Text.ElideRight
        }

        Row {
            spacing: 12

            Text {
                text: Player.positionText
                color: Theme.ink
                font.family: "Consolas"
                font.pixelSize: 18
            }
            Text {
                text: "/ " + Player.durationText
                color: Theme.inkSoft
                font.family: "Consolas"
                font.pixelSize: 15
            }
        }

        InkSlider {
            id: seek
            width: parent.width
            from: 0
            to: 1
            value: 0
            interactive: Player.hasTrack
            onCommitted: (v) => Player.seekFraction(v)
        }
    }

    // ── 歌词 ────────────────────────────────────────────────────────────
    // 整份加载、只推进"当前行"（见 LyricsModel.h 的两条说明）。
    // 当前行居中靠一段 contentY 动画：不用 Behavior on contentY，
    // 那会和用户拖动抢同一个属性。
    Item {
        id: lyricsPanel
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

            onCurrentIndexChanged: {
                if (currentIndex < 0) {
                    return
                }
                const item = itemAtIndex(currentIndex)
                if (!item) {
                    return
                }
                const desired = item.y + item.height / 2 - height / 2
                scrollAnim.to = Math.max(0, Math.min(Math.max(0, contentHeight - height), desired))
                scrollAnim.start()
            }
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
    }

    Connections {
        target: Player
        function onPositionChanged() {
            if (!seek.pressed) {
                seek.value = Player.progress
            }
        }
    }

    Component.onCompleted: seek.value = Player.progress
}
