import QtQuick
import "../comic"
import "../theme"

// 组件画廊（ARCHITECTURE.md §M2 验收：一页展示完整视觉语言，五组件四状态不崩）。
//
// 这一页是 M2 的"验收现场"，不是产品界面：新加组件先在这里出现，
// 视觉上有争议也在这里吵，不要在产品页面上试颜色。
Flickable {
    id: page

    contentWidth: width
    contentHeight: column.height + 56
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    component SectionTitle: Item {
        id: sectionRoot
        width: column.width
        height: 34
        property alias title: titleText.text
        Rectangle {
            width: 10
            height: 22
            y: 6
            color: Theme.accent
        }
        Text {
            id: titleText
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.ink
            font.family: Fonts.display
            font.pixelSize: 20
        }
    }

    component Caption: Text {
        color: Theme.inkSoft
        font.family: Theme.fontText
        font.pixelSize: Theme.textSmall
        wrapMode: Text.WordWrap
    }

    Column {
        id: column
        x: 26
        y: 26
        width: page.width - 52
        spacing: 20

        // ── 标题 ────────────────────────────────────────────────────────
        Item {
            width: column.width
            height: 64
            Text {
                text: qsTr("漫画设计系统 v1")
                color: Theme.ink
                font.family: Fonts.display
                font.pixelSize: 34
            }
            Text {
                y: 42
                text: qsTr("M2 · 五组件 × 四状态 · 线宽两档 · 阴影一个方向")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }
        }

        // ── 1. MangaPanel ───────────────────────────────────────────────
        SectionTitle { title: qsTr("1 · 面板 MangaPanel") }

        Row {
            spacing: 22
            MangaPanel {
                width: 250
                height: 120
                Text {
                    text: qsTr("常态：实线框 + 4px 硬阴影")
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }
            }
            MangaPanel {
                width: 250
                height: 120
                dashed: true
                Text {
                    text: qsTr("虚线：未来 / 占位")
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }
            }
            MangaPanel {
                width: 250
                height: 120
                shadowOffset: 2
                fillColor: Theme.paperAlt
                Text {
                    text: qsTr("收窄阴影：抬起 / 选中")
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }
            }
        }

        // ── 2. ComicButton ──────────────────────────────────────────────
        SectionTitle { title: qsTr("2 · 按钮 ComicButton（四状态）") }

        Row {
            spacing: 26
            Column {
                spacing: 6
                ComicButton { text: qsTr("常态"); forceState: "normal" }
                Caption { text: qsTr("normal") }
            }
            Column {
                spacing: 6
                ComicButton { text: qsTr("悬停"); tone: "paper"; forceState: "hover" }
                Caption { text: qsTr("hover · 阴影 3px") }
            }
            Column {
                spacing: 6
                ComicButton { text: qsTr("按下"); tone: "accent"; forceState: "press" }
                Caption { text: qsTr("press · 2px + 平移") }
            }
            Column {
                spacing: 6
                ComicButton { text: qsTr("禁用"); forceState: "disabled" }
                Caption { text: qsTr("disabled") }
            }
            Column {
                spacing: 6
                ComicButton { text: qsTr("主行动"); tone: "accent"; strong: true }
                Caption { text: qsTr("strong 粗描边") }
            }
        }

        // ── 3. ComicBubble ──────────────────────────────────────────────
        SectionTitle { title: qsTr("3 · 气泡 ComicBubble") }

        Item {
            width: column.width
            height: 110
            ComicBubble {
                id: bubbleLeft
                text: qsTr("这条正在播的是《夜航西飞》吧？")
                tailSide: "left"
            }
            ComicBubble {
                x: 330
                y: 8
                text: qsTr("对，刚导入的 20 首。")
                tailSide: "right"
            }
            ComicBubble {
                x: 660
                y: 4
                text: qsTr("叮！")
                tailSide: "none"
            }
            ComicButton {
                x: 800
                y: 12
                text: qsTr("再弹一次")
                tone: "paper"
                onClicked: {
                    bubbleLeft.show()
                    bubbleLeft.opacity = 0.0
                    bubbleLeft.opacity = 1.0
                }
            }
        }

        // ── 4. Halftone ────────────────────────────────────────────────
        SectionTitle { title: qsTr("4 · 网点 Halftone（shader）") }

        Row {
            spacing: 22
            Item {
                width: 250
                height: 110
                Halftone {
                    anchors.fill: parent
                    density: 0.32
                }
                Text {
                    anchors.centerIn: parent
                    text: qsTr("0.32")
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }
            }
            Item {
                width: 250
                height: 110
                Halftone {
                    anchors.fill: parent
                    density: 0.55
                    angle: 45
                }
                Text {
                    anchors.centerIn: parent
                    text: qsTr("0.55 / 45°")
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }
            }
            Item {
                width: 250
                height: 110
                Halftone {
                    anchors.fill: parent
                    density: 0.85
                    angle: 15
                    dotSize: 6
                }
                Text {
                    anchors.centerIn: parent
                    text: qsTr("0.85 / 6px")
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                }
            }
        }

        // ── 5. InkSlider ───────────────────────────────────────────────
        SectionTitle { title: qsTr("5 · 墨线滑块 InkSlider") }

        Column {
            spacing: 14
            InkSlider {
                width: 420
                from: 0
                to: 100
                value: 0
            }
            InkSlider {
                width: 420
                from: 0
                to: 100
                value: 35
            }
            InkSlider {
                width: 420
                from: 0
                to: 100
                value: 80
            }
            Caption { text: qsTr("已播放部分靠裁切，不靠换色；手柄是可拖的") }
        }

        // ── 拟声字（L3 可选，先摆一个当参考）────────────────────────────
        SectionTitle { title: qsTr("6 · 拟声字 SfxText（L3 可选）") }

        Row {
            spacing: 28
            Text {
                text: qsTr("砰！")
                color: Theme.ink
                font.family: Fonts.display
                font.pixelSize: Theme.textSfx
                rotation: -6
            }
            Text {
                text: qsTr("哗——")
                color: Theme.accent
                font.family: Fonts.display
                font.pixelSize: Theme.textSfx
            }
            Text {
                text: qsTr("叮")
                color: Theme.accentAlt
                font.family: Fonts.display
                font.pixelSize: Theme.textSfx
                rotation: 5
            }
        }

        Item { width: 1; height: 8 }
    }
}
