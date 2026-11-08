import QtQuick
import "comic"
import "pages"
import "layout"
import "theme"
import Sonora.Presentation 1.0

// 应用外壳：侧栏 + 内容区 + 底部播放条，全部落在 MangaPanel 里
// （ARCHITECTURE.md §M2）。页面切换用 PageTurner：页面常驻、翻转入场，
// 不按需创建（实测按需创建会在转场首帧留 50ms 尖峰，见 §10.7）。
Item {
    id: shell

    property int currentPage: 0        // 0 曲库 / 1 正在播放 / 2 在线 / 3 设置 / 4 歌单
    property bool queueOpen: false     // 右下角抽屉：当前播放队列

    // 焦点落在文本输入框里时（曲库页的搜索框），空格和方向键属于输入法，
    // 不能拿去控制播放。`text` 是 TextInput/TextEdit 才有的属性。
    readonly property bool typing: {
        const item = Window.window ? Window.window.activeFocusItem : null;
        return item !== null && item !== undefined && item.text !== undefined;
    }




    // ── 背景：整页唯一的一个 shader ─────────────────────────────────────
    // 网点的密度/尺寸是调出来的：density 0.26 + dotSize 4 在 125% 缩放下半径不到
    // 一个物理像素，等于没画。0.42/7 才是"看得出是网点、又不抢字"的那个点。
    Halftone {
        anchors.fill: parent
        density: 0.42
        dotSize: 7
        ink: Theme.halftoneInk
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
                    text: qsTr("SONORA")
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
                    // 版本号来自 QCoreApplication::applicationVersion()，最终出处是
                    // xmake.lua 的 SONORA_VERSION——界面里不再写第二份。
                    text: qsTr("v") + Qt.application.version
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
                label: qsTr("在线")
                hint: Online.results.count > 0 ? Online.results.count.toString() : "—"
                active: shell.currentPage === 2
                onClicked: shell.currentPage = 2
            }
            NavItem {
                width: sidebar.width
                label: qsTr("歌单")
                hint: Playlists.count > 0 ? Playlists.count.toString() : "—"
                active: shell.currentPage === 4
                onClicked: {
                    shell.currentPage = 4
                    // 从别处回来时重新取一次：设置页里可能刚建过/删过。
                    Playlists.refresh()
                }
            }
            NavItem {
                width: sidebar.width
                label: qsTr("设置")
                hint: qsTr("外观")
                active: shell.currentPage === 3
                onClicked: shell.currentPage = 3
            }

            Item { width: 1; height: 8 }

            // 一张贴在侧栏的"便签"，漫画里常见的旁白框
            ComicBubble {
                x: 2
                // 英文比中文长得多，不给宽度就会顶出侧栏。
                wrapWidth: 198
                text: qsTr("先把视觉钉死，再写业务。")
                tailSide: "left"
            }
        }
    }



    // ── 内容区 ──────────────────────────────────────────────────────────
    // 页面切换走 PageTurner（转场），不是直接换 Loader：见 layout/PageTurner.qml
    // 顶部关于"为什么不用 ShaderEffectSource 快照"的说明。
    PageTurner {
        id: content
        anchors.left: sidebar.right
        anchors.leftMargin: 16
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.top: parent.top
        anchors.topMargin: 18
        anchors.bottom: playerBar.top
        anchors.bottomMargin: 14

        pages: [libraryPage, nowPlayingPage, onlinePage, settingsPage, playlistsPage]
        index: shell.currentPage
    }

    // 页面叫 LibraryPage 而不是 Library：Library 是注入的单例名字，
    // 同名组件会被单例遮住，加载时报 "Element is not creatable"。
    Component { id: libraryPage; LibraryPage { } }
    Component { id: nowPlayingPage; NowPlaying { } }
    Component { id: onlinePage; OnlinePage { } }
    Component { id: settingsPage; SettingsPage { } }
    Component { id: playlistsPage; PlaylistsPage { } }


    // ── 拟声字层 ────────────────────────────────────────────────────────
    // 盖在内容区之上、播放条之下：它是"画面上贴的字"，不属于任何一页。
    // 池化 6 个，不 new 对象（见 SfxText.qml 顶部的说明）。
    SfxText {
        id: sfx
        anchors.left: content.left
        anchors.right: content.right
        anchors.top: content.top
        anchors.bottom: content.bottom
    }

    Connections {
        target: Player

        function onPlayingChanged() {
            if (Player.playing) {
                sfx.burst(qsTr("咚！"), sfx.width * 0.26, sfx.height * 0.40, true)
            } else {
                sfx.burst(qsTr("呼——"), sfx.width * 0.24, sfx.height * 0.44, false)
            }
        }

        function onTrackChanged() {
            if (Player.hasTrack) {
                sfx.burst(qsTr("啪！"), sfx.width * 0.18, sfx.height * 0.22, true)
            }
        }

        function onModeLabelChanged() {
            sfx.burst(qsTr("转！"), sfx.width * 0.34, sfx.height * 0.16, false)
        }
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
                      ? qsTr("%n 首 · %1", "", Player.queue.length).arg(Player.modeLabel)
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
    Shortcut { sequence: "Space"; enabled: !shell.typing; onActivated: Player.toggle() }
    Shortcut { sequence: "Left"; enabled: !shell.typing; onActivated: Player.previous() }
    Shortcut { sequence: "Right"; enabled: !shell.typing; onActivated: Player.next() }
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
