import QtQuick
import QtQuick.Shapes
import "../theme"

// 对话气泡（ARCHITECTURE.md §10.3）。
//
// 尾巴用 Shape 画的实心三角，**不描边**，并让它往气泡里压 3px 遮住接缝——
// 比"几何路径 + 描边"便宜，也更像印刷。
// 弹出用 SpringAnimation（阻尼 0.6~0.8），不用 Easing.OutBack（§10.4 第 3 条）。
Item {
    id: root

    property string text: "……"
    property string tailSide: "left"    // left | right | none
    property int    duration: 0         // > 0 时到点自动消失
    // > 0 时按这个宽度换行。留 0 保持"一句话气泡"的原样：
    // 状态提示会有整句报错（"接口可能已变更，稍后再试（…）"），不换行会顶出屏幕。
    property real   wrapWidth: 0
    property bool   autoShow: true

    signal dismissed()

    implicitWidth: body.width
    implicitHeight: body.height

    scale: root.autoShow ? 1.0 : 0.6
    opacity: root.autoShow ? 1.0 : 0.0

    Behavior on scale {
        SpringAnimation { spring: 2.2; damping: 0.65; mass: 1.0 }
    }
    Behavior on opacity {
        NumberAnimation { duration: Theme.animFast }
    }

    Rectangle {
        id: body
        width: root.wrapWidth > 0 ? root.wrapWidth + 34 : label.implicitWidth + 34
        height: label.implicitHeight + 22
        radius: 14
        color: Theme.paperHigh
        border.width: Theme.stroke
        border.color: Theme.ink
    }

    // 尾巴：实心三角，顶部压进气泡 3px 藏接缝
    Shape {
        visible: root.tailSide !== "none"
        width: 22
        height: 16
        x: root.tailSide === "right" ? body.width - width - 12 : 12
        y: body.height - 6
        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillColor: Theme.paperHigh
            startX: 0
            startY: 0
            PathLine { x: 22; y: 0 }
            PathLine { x: root.tailSide === "right" ? 20 : 3; y: 16 }
            PathLine { x: 0; y: 0 }
        }
    }

    Text {
        id: label
        anchors.centerIn: body
        width: root.wrapWidth > 0 ? root.wrapWidth : implicitWidth
        wrapMode: root.wrapWidth > 0 ? Text.WordWrap : Text.NoWrap
        horizontalAlignment: Text.AlignHCenter
        text: root.text
        color: Theme.ink
        font.family: Theme.fontText
        font.pixelSize: Theme.textBody
    }

    Timer {
        running: root.duration > 0
        interval: Math.max(400, root.duration)
        onTriggered: {
            root.autoShow = false
            root.dismissed()
        }
    }

    function show(): void {
        root.autoShow = true
    }
}
