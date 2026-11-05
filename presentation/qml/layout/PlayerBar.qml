import QtQuick
import "../comic"
import "../theme"
import MambaMusic 1.0

// 底部播放条（M3）。它是一个纯粹的"看板"：所有状态都来自注入的 Player 单例，
// 所有操作都变成对 Player 的调用。这里不许出现任何领域概念。
//
// 进度条的两个细节值得记住：
//   1. **不要写 `value: Player.progress`**。用户在拖的时候，程序每 33ms 改写一次
//      value，手指和手柄就会打架——绑定一旦被用户赋值打断，之后也回不来。
//      改成扫 Player.positionChanged 事件，且只在没按住时才更新。
//   2. 拖动过程中的 `moved` 是高频的，真正该提交的只有松手那一下（`committed`）。
Item {
    id: bar

    // ── 封面位（M6 接真实封面，先放"空唱片"）──────────────────────────
    MangaPanel {
        id: cover
        width: 56
        height: 56
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        shadowOffset: 3
        padding: 0
        fillColor: Theme.paperAlt

        Text {
            anchors.centerIn: parent
            text: qsTr("♪")
            color: Theme.ink
            font.family: Fonts.display
            font.pixelSize: 26
        }
    }

    // ── 曲目信息 ────────────────────────────────────────────────────────
    Column {
        id: meta
        width: 190
        anchors.left: cover.right
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            width: parent.width
            text: Player.hasTrack ? Player.title : qsTr("未选曲")
            color: Theme.ink
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: {
                if (!Player.hasTrack) {
                    return Player.stateText
                }
                const parts = []
                if (Player.artist.length > 0) parts.push(Player.artist)
                if (Player.queueText.length > 0) parts.push(Player.queueText)
                return parts.join("  ·  ")
            }
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textSmall
            elide: Text.ElideRight
        }
    }

    // ── 进度 ────────────────────────────────────────────────────────────
    Column {
        id: seekColumn
        width: 340
        anchors.left: meta.right
        anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        InkSlider {
            id: seek
            width: parent.width
            from: 0
            to: 1
            value: 0
            interactive: Player.hasTrack
            onCommitted: (v) => Player.seekFraction(v)
        }

        Row {
            spacing: 8
            Text {
                text: Player.positionText
                color: Theme.inkSoft
                font.family: "Consolas"
                font.pixelSize: Theme.textSmall
            }
            Text {
                text: "/ " + Player.durationText
                color: Theme.inkSoft
                font.family: "Consolas"
                font.pixelSize: Theme.textSmall
            }
            Text {
                // 只在真出错时露出来，平时不占视觉
                visible: Player.stateText.indexOf("失败") >= 0
                text: Player.stateText
                color: Theme.accent
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }
        }
    }

    // 位置每 33ms 变一次，但**只在用户没按住进度条时**才回写手柄。
    Connections {
        target: Player
        function onPositionChanged() {
            if (!seek.pressed) {
                seek.value = Player.progress
            }
        }
    }

    // ── 音量 ────────────────────────────────────────────────────────────
    Row {
        id: volumeRow
        anchors.right: buttons.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("音量")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textSmall
        }
        InkSlider {
            id: volumeSlider
            width: 84
            anchors.verticalCenter: parent.verticalCenter
            from: 0
            to: 100
            value: 100
            onCommitted: (v) => Player.setVolume(Math.round(v))
        }
    }

    Connections {
        target: Player
        function onVolumeChanged() {
            if (!volumeSlider.pressed) {
                volumeSlider.value = Player.volume
            }
        }
    }

    Component.onCompleted: {
        volumeSlider.value = Player.volume
        if (!seek.pressed) {
            seek.value = Player.progress
        }
    }

    // ── 三个按钮 ────────────────────────────────────────────────────────
    Row {
        id: buttons
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        ComicButton {
            text: qsTr("上一首")
            tone: "paper"
            enabled: Player.hasTrack
            onClicked: Player.previous()
        }
        ComicButton {
            text: Player.playing ? qsTr("暂停") : qsTr("播放")
            tone: "ink"
            strong: true
            enabled: Player.hasTrack
            onClicked: Player.toggle()
        }
        ComicButton {
            text: qsTr("下一首")
            tone: "paper"
            enabled: Player.hasTrack
            onClicked: Player.next()
        }
    }
}
