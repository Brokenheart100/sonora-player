import QtQuick
import "../theme"

// 漫画按钮（ARCHITECTURE.md §10.3）。
//
// 按下感来自"阴影收窄 + 面板平移"，不是缩放也不是变色：
//  常态 shadow 4px → hover 3px → press 2px，同时面板本体往右下平移同样的距离。
// 这样看起来像手指把纸片按到桌面上。
//
// forceState 是给组件画廊用的：置成 normal / hover / press / disabled 可以把
// 某个状态钉住，使状态对照图可截图、可对比。正常业务里留空即可。
Item {
    id: root

    property string text: "按钮"
    property string tone: "ink"        // ink | accent | paper
    property real   pressOffset: 2
    property bool   strong: false      // 用粗描边（主行动按钮）
    property string forceState: ""     // "" | normal | hover | press | disabled

    signal clicked()

    implicitWidth: label.implicitWidth + 32
    implicitHeight: Math.max(38, label.implicitHeight + 18)

    readonly property bool isDisabled: root.forceState === "disabled" || !root.enabled
    readonly property bool hovered: !root.isDisabled
                                    && (root.forceState === "hover"
                                        || (root.forceState === "" && hover.hovered))
    readonly property bool pressing: !root.isDisabled
                                     && (root.forceState === "press"
                                         || (root.forceState === "" && tap.pressed))

    readonly property color faceColor: {
        if (root.isDisabled) {
            return Theme.paperAlt
        }
        if (root.tone === "accent") {
            return Theme.accent
        }
        if (root.tone === "paper") {
            return Theme.paperHigh
        }
        return Theme.ink
    }

    readonly property color labelColor: {
        if (root.isDisabled) {
            return Theme.inkSoft
        }
        return (root.tone === "ink" || root.tone === "accent") ? Theme.paper : Theme.ink
    }

    // 阴影当前偏移：按下时收窄
    readonly property real shadowNow: root.pressing ? root.pressOffset
                                   : (root.hovered ? root.pressOffset + 1 : Theme.shadowY)

    opacity: root.isDisabled ? 0.55 : 1.0

    // 硬阴影
    Rectangle {
        x: root.shadowNow
        y: root.shadowNow
        width: root.width
        height: root.height
        radius: 3
        color: Theme.ink
        Behavior on x { NumberAnimation { duration: Theme.animFast } }
        Behavior on y { NumberAnimation { duration: Theme.animFast } }
    }

    // 面板
    Rectangle {
        id: face
        width: root.width
        height: root.height
        x: root.pressing ? root.pressOffset : 0
        y: root.pressing ? root.pressOffset : 0
        radius: 3
        color: root.faceColor
        border.width: root.strong ? Theme.strokeBold : Theme.stroke
        border.color: Theme.ink
        Behavior on x { NumberAnimation { duration: Theme.animFast } }
        Behavior on y { NumberAnimation { duration: Theme.animFast } }
    }

    Text {
        id: label
        anchors.centerIn: face
        text: root.text
        color: root.labelColor
        font.family: Theme.fontText
        font.pixelSize: Theme.textBody
        font.weight: Font.DemiBold
    }

    HoverHandler {
        id: hover
        enabled: !root.isDisabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        enabled: !root.isDisabled
        onTapped: root.clicked()
    }
}
