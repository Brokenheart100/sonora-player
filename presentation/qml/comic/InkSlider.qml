import QtQuick
import "../theme"

// 墨线滑块（ARCHITECTURE.md §10.3）。
//
// 没有用 QtQuick.Controls 的 Slider：它的视觉全是原生/样式化的，改到"像手画的一笔"
// 的成本远高于直接画三个矩形。已播放部分是**裁切**而不是变色（§10.3 原话）。
Item {
    id: root

    property real from: 0
    property real to: 100
    property real value: 0
    property bool interactive: true

    // 手指是否按在滑轨上。界面靠它判断"现在该听程序的，还是该听用户的"——
    // 拖动的过程中如果还让外部每 33ms 改写 value，手柄会一弹一弹的。
    readonly property bool pressed: pointer.pressed

    signal moved(real v)
    // 松手。拖拽过程中的 moved 是高频的，真正该提交的值只有这一个。
    signal committed(real v)

    implicitWidth: 220
    implicitHeight: 22

    readonly property real ratio: (root.to > root.from)
                                   ? Math.max(0, Math.min(1, (root.value - root.from) / (root.to - root.from)))
                                   : 0

    // 轨道：一条没画直的墨线，用两层矩形叠出"手写"的粗细差
    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: root.width
        height: 4
        radius: 2
        color: Theme.screentone

        Rectangle {
            width: track.width * root.ratio
            height: parent.height
            radius: 2
            color: Theme.ink
        }
    }

    // 手柄：一个小圆，压着墨线
    Rectangle {
        id: knob
        width: 14
        height: 14
        radius: 7
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(0, Math.min(root.width - width, track.width * root.ratio - width / 2))
        color: Theme.paperHigh
        border.width: Theme.stroke
        border.color: Theme.ink
        scale: pointer.pressed ? 0.86 : 1.0
        Behavior on scale { NumberAnimation { duration: Theme.animFast } }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        enabled: root.interactive
        cursorShape: Qt.PointingHandCursor

        function apply(x) {
            const r = Math.max(0, Math.min(1, x / track.width))
            root.value = root.from + (root.to - root.from) * r
            root.moved(root.value)
        }

        onPressed: (mouse) => apply(mouse.x)
        onPositionChanged: (mouse) => { if (pressed) apply(mouse.x) }
        onReleased: root.committed(root.value)
        onCanceled: root.committed(root.value)
    }
}
