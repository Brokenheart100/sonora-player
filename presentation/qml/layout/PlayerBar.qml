import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

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

    // 队列抽屉由 Shell 负责显示（它是整窗的浮层，不属于这条栏）。
    signal queueRequested

    // ── 封面位（M8 后接上真实封面）──────────────────────────────────────
    MangaPanel {
        id: cover
        width: 56
        height: 56
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        shadowOffset: 3
        padding: 0
        fillColor: Theme.paperAlt

        Image {
            id: art
            anchors.fill: parent
            source: Player.coverUrl
            sourceSize: Qt.size(112, 112)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
            // 没有封面（或图还没读出来）时回到"空唱片"音符，别留一个白框
            visible: Player.coverUrl.length > 0 && status === Image.Ready
        }

        Text {
            anchors.centerIn: parent
            visible: !art.visible
            text: qsTr("♪")
            color: Theme.ink
            font.family: Fonts.display
            font.pixelSize: 26
        }
    }

    // ── 曲目信息 ────────────────────────────────────────────────────────
    Column {
        id: meta
        width: 150
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
                    return Player.stateText;
                }
                const parts = [];
                if (Player.artist.length > 0)
                    parts.push(Player.artist);
                if (Player.queueText.length > 0)
                    parts.push(Player.queueText);
                return parts.join("  ·  ");
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
        width: 230
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
            onCommitted: v => Player.seekFraction(v)
        }

        // 缓冲条：只在网络流（Player.buffered >= 0）时压在进度条底下，
        // 本地文件不画。故意细一档，不跟播放进度抢视觉。
        // 外面套一个 0 高的 Item：Column 内部的子项不许用 anchors，
        // 直接给 Rectangle 写 anchors.top 会让整条 Column 失效（QML 只在运行时报）。
        Item {
            width: parent.width
            height: 0

            Rectangle {
                width: parent.width * (Player.buffered > 0 ? Math.min(Player.buffered, 100) / 100 : 0)
                height: 2
                y: -3
                color: Theme.inkSoft
                opacity: 0.45
                visible: Player.buffered >= 0
            }
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
                // 只在真出错时露出来，平时不占视觉。
                // 判断条件必须是 Player.failed（枚举派生的布尔），而不是
                // stateText 里有没有"失败"两个字——标签写的是"出错"，旧条件永远 false。
                visible: Player.failed
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
                seek.value = Player.progress;
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

        InkSlider {
            id: volumeSlider
            width: 70
            anchors.verticalCenter: parent.verticalCenter
            from: 0
            to: 100
            value: 100
            onCommitted: v => Player.setVolume(Math.round(v))
        }
    }

    Connections {
        target: Player
        function onVolumeChanged() {
            if (!volumeSlider.pressed) {
                volumeSlider.value = Player.volume;
            }
        }
    }

    Component.onCompleted: {
        volumeSlider.value = Player.volume;
        if (!seek.pressed) {
            seek.value = Player.progress;
        }
    }

    // ── 三个按钮 ────────────────────────────────────────────────────────
    Row {
        id: buttons
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        ComicButton {
            // 模式按钮的文字就是当前模式：点一下换下一个，不用开菜单。
            text: Player.modeLabel
            tone: "paper"
            onClicked: Player.cycleMode()
        }
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
        ComicButton {
            text: qsTr("队列")
            tone: "paper"
            onClicked: bar.queueRequested()
        }
    }
}
