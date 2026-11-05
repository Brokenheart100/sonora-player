import QtQuick
import "comic"
import "pages"
import "layout"
import "theme"
import MambaMusic 1.0

// 应用外壳：侧栏 + 内容区 + 底部播放条，全部落在 MangaPanel 里
// （ARCHITECTURE.md §M2）。页面切换用 Loader，不用的页面不创建 (SqueUI 原则)。
Item {
    id: shell

    property int currentPage: 0        // 0 = 曲库，1 = 组件画廊


    // ── 背景：整页唯一的一个 shader ─────────────────────────────────────
    // 网点的密度/尺寸是调出来的：density 0.26 + dotSize 4 在 125% 缩放下半径不到
    // 一个物理像素，等于没画。0.42/7 才是"看得出是网点、又不抢字"的那个点。
    Halftone {
        anchors.fill: parent
        density: 0.42
        dotSize: 7
        ink: "#B9B1A2"
        paper: Theme.paper
    }

    // ── 侧栏 ────────────────────────────────────────────────────────────
    Item {
        id: sidebar
        width: 196
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: playerBar.top
        anchors.margins: 18

        Column {
            spacing: 10

            Item {
                width: sidebar.width
                height: 74

                Text {
                    text: qsTr("MAMBA")
                    color: Theme.ink
                    font.family: Fonts.display
                    font.pixelSize: 27
                }
                Text {
                    y: 34
                    text: qsTr("MUSIC")
                    color: Theme.ink
                    font.family: Fonts.display
                    font.pixelSize: 27
                }
                // 标题旁的一笔朱红：漫画里常用来压住标题
                Rectangle {
                    x: 4
                    y: 66
                    width: 96
                    height: 5
                    color: Theme.accent
                }
                Text {
                    x: 108
                    y: 60
                    text: qsTr("v0.1")
                    color: Theme.inkSoft
                    font.family: "Consolas"
                    font.pixelSize: Theme.textSmall
                }
            }

            NavItem {
                width: sidebar.width
                label: qsTr("曲库")
                hint: Library.count > 0 ? Library.count.toString() : "—"
                active: shell.currentPage === 0
                onClicked: shell.currentPage = 0
            }
            NavItem {
                width: sidebar.width
                label: qsTr("组件画廊")
                hint: qsTr("M2")
                active: shell.currentPage === 1
                onClicked: shell.currentPage = 1
            }
            NavItem {
                width: sidebar.width
                label: qsTr("歌单")
                hint: qsTr("M6")
                enabled: false
            }
            NavItem {
                width: sidebar.width
                label: qsTr("设置")
                hint: qsTr("M9")
                enabled: false
            }

            Item { width: 1; height: 8 }

            // 一张贴在侧栏的"便签"，漫画里常见的旁白框
            ComicBubble {
                x: 2
                text: qsTr("先把视觉钉死，再写业务。")
                tailSide: "left"
            }
        }
    }

    // ── 内容区 ──────────────────────────────────────────────────────────
    Loader {
        id: content
        anchors.left: sidebar.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.top: parent.top
        anchors.topMargin: 18
        anchors.bottom: playerBar.top
        anchors.bottomMargin: 14

        sourceComponent: shell.currentPage === 0 ? libraryPage : storybookPage

        // 页面叫 LibraryPage 而不是 Library：Library 是注入的单例名字，
        // 同名组件会被单例遮住，加载时报 "Element is not creatable"。
        Component { id: libraryPage; LibraryPage { } }
        Component { id: storybookPage; Storybook { } }
    }

    // ── 底部播放条 ──────────────────────────────────────────────────────
    MangaPanel {
        id: playerBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        height: 88
        shadowOffset: 5

        PlayerBar {
            anchors.fill: parent
        }
    }

    // ── 拖放：拖进来的音频直接进曲库并开播 ──────────────────────────────
    DropArea {
        anchors.fill: parent
        onEntered: (drop) => drop.accepted = true
        onDropped: (drop) => Library.dropUrls(drop.urls)
    }

    // ── 侧栏导航项 ──────────────────────────────────────────────────────
    component NavItem: Item {
        id: nav

        property string label: ""
        property string hint: ""
        property bool active: false

        signal clicked()

        implicitHeight: 38
        opacity: nav.enabled ? 1.0 : 0.45

        Rectangle {
            anchors.fill: parent
            color: nav.active ? Theme.paperHigh : "transparent"
            border.width: nav.active ? Theme.stroke : 0
            border.color: Theme.ink
            radius: 2
        }

        // 当前项左边一道朱红墨迹（accent 只给"当前"用）
        Rectangle {
            visible: nav.active
            width: 5
            height: parent.height - 10
            y: 5
            x: -9
            color: Theme.accent
        }

        Text {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            text: nav.label
            color: Theme.ink
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
            font.weight: nav.active ? Font.DemiBold : Font.Normal
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: nav.hint
            color: Theme.inkSoft
            font.family: "Consolas"
            font.pixelSize: Theme.textSmall
        }

        HoverHandler {
            id: navHover
            enabled: nav.enabled
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: nav.enabled
            onTapped: nav.clicked()
        }
    }
}
