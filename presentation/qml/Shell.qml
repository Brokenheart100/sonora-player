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

    property int currentPage: 1        // 0 = 曲库，1 = 正在播放，2 = 组件画廊
    property bool queueOpen: false     // 右下角抽屉：当前播放队列


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
                label: qsTr("正在播放")
                hint: Player.hasTrack ? Player.queueText : "—"
                active: shell.currentPage === 1
                onClicked: shell.currentPage = 1
            }
            NavItem {
                width: sidebar.width
                label: qsTr("组件画廊")
                hint: qsTr("M2")
                active: shell.currentPage === 2
                onClicked: shell.currentPage = 2
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

        sourceComponent: shell.currentPage === 0
                         ? libraryPage
                         : (shell.currentPage === 1 ? nowPlayingPage : storybookPage)

        // 页面叫 LibraryPage 而不是 Library：Library 是注入的单例名字，
        // 同名组件会被单例遮住，加载时报 "Element is not creatable"。
        Component { id: libraryPage; LibraryPage { } }
        Component { id: nowPlayingPage; NowPlaying { } }
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
            onQueueRequested: shell.queueOpen = !shell.queueOpen
        }
    }

    // ── 队列抽屉 ────────────────────────────────────────────────────────
    // 浮在内容区右侧，不做"第二个页面"：听歌时打开队列不该把正在看的东西换掉。
    MangaPanel {
        id: queueDrawer
        visible: shell.queueOpen
        width: 320
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.top: content.top
        anchors.bottom: playerBar.top
        anchors.bottomMargin: 14
        shadowOffset: 6
        padding: 12

        Column {
            anchors.fill: parent
            spacing: 8

            Item {
                width: parent.width
                height: 30
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("播放队列")
                    color: Theme.ink
                    font.family: Fonts.display
                    font.pixelSize: 20
                }
                ComicButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 26
                    text: qsTr("关闭")
                    tone: "paper"
                    onClicked: shell.queueOpen = false
                }
            }

            Text {
                width: parent.width
                text: Player.queue.length > 0
                      ? qsTr("%1 首 · %2").arg(Player.queue.length).arg(Player.modeLabel)
                      : qsTr("队列是空的")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            ListView {
                id: queueList
                width: parent.width
                height: parent.height - 70
                clip: true
                model: Player.queue
                spacing: 2

                delegate: Item {
                    required property var modelData
                    width: queueList.width
                    height: 34

                    Rectangle {
                        anchors.fill: parent
                        color: modelData.current ? Theme.paperHigh : "transparent"
                        border.width: modelData.current ? 1 : 0
                        border.color: Theme.ink
                    }
                    Rectangle {
                        // 当前曲的左侧朱红墨条
                        visible: modelData.current
                        width: 3
                        height: parent.height - 8
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.accent
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            width: parent.width
                            text: modelData.title
                            color: Theme.ink
                            font.family: Theme.fontText
                            font.pixelSize: Theme.textSmall
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.artist
                            color: Theme.inkSoft
                            font.family: Theme.fontText
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    TapHandler {
                        onTapped: Player.jumpTo(modelData.trackId)
                    }
                }
            }
        }
    }

    // ── 键盘 ────────────────────────────────────────────────────────────
    // 空格 / ←→ 是"听完整张专辑不用摸鼠标"的全部条件；Esc 用来收浮层。
    Shortcut { sequence: "Space"; onActivated: Player.toggle() }
    Shortcut { sequence: "Left"; onActivated: Player.previous() }
    Shortcut { sequence: "Right"; onActivated: Player.next() }
    Shortcut { sequence: "Escape"; onActivated: shell.queueOpen = false }

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
