import QtQuick
import QtQuick.Shapes
import "../theme"

// 漫画分格框（ARCHITECTURE.md §10.3）。
//
// 阴影 = 错位的第二个实心剪影，不用 DropShadow / MultiEffect：
// 印刷感的阴影是**纯色偏移**，带模糊反而不像漫画。
//
// 用法：
//   MangaPanel {
//       width: 400; height: 200
//       Text { text: "内容" }
//   }
Item {
    id: root

    property real strokeWidth: Theme.stroke
    property real shadowOffset: Theme.shadowY
    property bool dashed: false
    property bool shadow: true
    property real cornerRadius: 3
    property real padding: 12
    property color fillColor: Theme.paperHigh
    property color strokeColor: Theme.ink
    // 硬阴影单独一个令牌：深色配色下用纯黑，用线稿色会变成"发光块"
    property color shadowColor: Theme.shadow

    default property alias content: contentArea.data
    property alias contentItem: contentArea

    implicitWidth: 200
    implicitHeight: 120

    // 硬阴影
    Rectangle {
        visible: root.shadow
        x: root.shadowOffset
        y: root.shadowOffset
        width: root.width
        height: root.height
        radius: root.cornerRadius
        color: root.shadowColor
    }

    // 面板本体
    Rectangle {
        id: face
        anchors.fill: parent
        radius: root.cornerRadius
        color: root.fillColor
        border.width: root.dashed ? 0 : root.strokeWidth
        border.color: root.strokeColor
    }

    // 虚线框（Rectangle 的 border 不能画虚线，这里交给 Shape）
    Shape {
        anchors.fill: parent
        visible: root.dashed
        ShapePath {
            strokeColor: root.strokeColor
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            strokeStyle: ShapePath.DashLine
            dashPattern: [7, 5]
            startX: root.strokeWidth / 2
            startY: root.strokeWidth / 2
            PathLine { x: root.width - root.strokeWidth / 2; y: root.strokeWidth / 2 }
            PathLine { x: root.width - root.strokeWidth / 2; y: root.height - root.strokeWidth / 2 }
            PathLine { x: root.strokeWidth / 2; y: root.height - root.strokeWidth / 2 }
            PathLine { x: root.strokeWidth / 2; y: root.strokeWidth / 2 }
        }
    }

    Item {
        id: contentArea
        anchors.fill: parent
        anchors.margins: root.strokeWidth + root.padding
    }
}
