import QtQuick
import QtQuick.Shapes
import "../theme"
// 注入的单例在这里也要显式 import：缺了不会编译失败，只会让 Player 变 undefined
import Sonora.Presentation 1.0

// 会看戏的小猫（ARCHITECTURE.md §10.6 的"角色立绘"落地版）。
//
// 不用立绘 PNG、不用精灵图：整个角色是 6 个图元画出来的——这样它永远不会
// 和 Theme 里的墨色/纸色脱节，改主题它跟着变，也不需要再管贴图打包。
//
// 三种状态（跟播放状态绑定，不需要手动喂事件）：
//   sleeping  没歌      -> 闭眼、蜷着、Zzz 往上飘
//   idle      暂停      -> 睁眼、轻微起伏
//   singing   播放中    -> 张嘴、上下弹、吐两个音符
Item {
    id: root

    property string mood: !Player.hasTrack ? "sleeping"
                                        : (Player.playing ? "singing" : "idle")

    implicitWidth: 132
    implicitHeight: 132

    readonly property bool awake: mood !== "sleeping"
    readonly property bool singing: mood === "singing"

    // 起伏用"偏移量 + Translate"而不是直接动 y：直接绑定 y 会和外部锚点打架，
    // 而且 to: root.y - 6 是自引用，一动就抖。
    property real bob: 0
    transform: Translate { y: -6 * root.bob }

    // 只有醒着才动。睡着的猫呼吸幅度小到看不出来，动反而假。
    SequentialAnimation on bob {
        running: root.awake
        loops: Animation.Infinite
        NumberAnimation {
            to: 1
            duration: root.singing ? 260 : 900
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            to: 0
            duration: root.singing ? 260 : 900
            easing.type: Easing.InOutQuad
        }
    }

    // ── 尾巴（睡着的猫把尾巴圈在身前）──────────────────────────────
    Shape {
        anchors.fill: parent
        layer.enabled: false

        ShapePath {
            strokeColor: Theme.ink
            strokeWidth: Theme.stroke
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: 96; startY: root.height - 22
            PathCubic {
                x: root.width - 6
                y: root.height - 46
                control1X: root.width - 2
                control2Y: root.height - 18
            }
            PathCubic {
                x: 104
                y: root.height - 8
                control1X: root.width - 22
                control2Y: root.height - 4
            }
        }
    }

    // ── 头 ────────────────────────────────────────────────────────
    Rectangle {
        id: head
        x: 26
        y: 30
        width: 82
        height: 74
        radius: 26
        color: Theme.paperHigh
        border.color: Theme.ink
        border.width: Theme.stroke

        // 耳朵：两个实心三角压在本体外面
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: Theme.ink
                strokeWidth: Theme.stroke
                fillColor: Theme.paperHigh
                startX: 8; startY: 12
                PathLine { x: 20; y: -16 }
                PathLine { x: 40; y: 8 }
                PathLine { x: 8; y: 12 }
            }
            ShapePath {
                strokeColor: Theme.ink
                strokeWidth: Theme.stroke
                fillColor: Theme.paperHigh
                startX: parent.width - 40; startY: 8
                PathLine { x: parent.width - 18; y: -16 }
                PathLine { x: parent.width - 6; y: 12 }
                PathLine { x: parent.width - 40; y: 8 }
            }
        }

        // 眼睛：醒着是两个墨点，睡着是两条下弯的弧
        Row {
            visible: root.awake
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -6
            spacing: 26

            Rectangle {
                width: 7
                radius: 3.5
                color: Theme.ink
                // 唱歌时眼睛弯成月牙（变矮、下移一点）
                height: root.singing ? 4 : 9
                y: root.singing ? 3 : 0
            }
            Rectangle {
                width: 7
                radius: 3.5
                color: Theme.ink
                height: root.singing ? 4 : 9
                y: root.singing ? 3 : 0
            }
        }

        Row {
            visible: !root.awake
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -4
            spacing: 20

            Shape {
                width: 14; height: 8
                ShapePath {
                    strokeColor: Theme.ink
                    strokeWidth: 2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: 1; startY: 1
                    PathQuad { x: 13; y: 1; controlX: 7; controlY: 8 }
                }
            }
            Shape {
                width: 14; height: 8
                ShapePath {
                    strokeColor: Theme.ink
                    strokeWidth: 2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: 1; startY: 1
                    PathQuad { x: 13; y: 1; controlX: 7; controlY: 8 }
                }
            }
        }

        // 嘴：唱歌时张开成一个墨色椭圆，其余时候是一条小弧
        Rectangle {
            visible: root.singing
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            width: 16
            height: 12
            radius: 6
            color: Theme.ink
        }
        Shape {
            visible: !root.singing
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 18
            width: 16
            height: 8
            ShapePath {
                strokeColor: Theme.ink
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: 1; startY: 1
                PathQuad { x: 15; y: 1; controlX: 8; controlY: 7 }
            }
        }

        // 腮红：一张朱红的短横，漫画里"被夸了一句"那个意思
        Rectangle {
            visible: root.singing
            x: 6
            y: parent.height - 26
            width: 12
            height: 3
            radius: 1.5
            color: Theme.accent
        }
    }

    // ── 睡着时的 Zzz ──────────────────────────────────────────────
    Repeater {
        model: 3
        delegate: Text {
            required property int index
            text: "z"
            color: Theme.inkSoft
            font.family: Fonts.display
            font.pixelSize: 14 + index * 5
            x: 104 + index * 10
            y: 30 - index * 8
            opacity: root.awake ? 0 : 0.75 - index * 0.15
        }
    }

    // ── 唱歌时吐出的音符 ──────────────────────────────────────────
    Repeater {
        model: 2
        delegate: Text {
            id: note
            required property int index
            text: index === 0 ? "♪" : "♫"
            color: index === 0 ? Theme.accent : Theme.ink
            font.pixelSize: 20
            x: 106 + index * 12
            y: 26
            opacity: 0

            SequentialAnimation {
                running: root.singing
                loops: Animation.Infinite
                PauseAnimation { duration: root.singing ? index * 380 : 0 }
                ParallelAnimation {
                    NumberAnimation {
                        target: note
                        property: "y"
                        from: 26
                        to: -6
                        duration: 1300
                        easing.type: Easing.OutQuad
                    }
                    NumberAnimation {
                        target: note
                        property: "opacity"
                        from: 0.9
                        to: 0
                        duration: 1300
                        easing.type: Easing.InQuad
                    }
                }
            }
        }
    }
}
