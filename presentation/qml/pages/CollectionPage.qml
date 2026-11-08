import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 一张卡片点进去之后的页面：歌单 / 榜单 / 专辑 / 歌手 / 新歌共用这一页。
//
// 它不自己发请求：`Online.openCollection()` 已经把结果列表切过去了，
// 这一页只负责"封面 + 标题 + 两个动作 + 表头 + 行 + 翻页"。
// 这样翻页、重试、双击播放、加入队列都不必各写一遍。
Item {
    id: page

    readonly property var cols: [0.06, 0.44, 0.70, 0.86]
    readonly property int rowHeight: 46
    readonly property int headerHeight: 26
    readonly property int statusHeight: 34
    readonly property int heroHeight: 148

    // 当前展开了哪一行的「···」菜单（-1 = 没开）。菜单全局只有一份：
    // 每行各画一个的话，翻页时它会被回收掉，正好在用户点下去时消失。
    property int menuRow: -1

    function closeMenu() {
        page.menuRow = -1
    }

    // Esc：先关菜单，再退页面。两个 Shortcut 用 enabled 互斥，
    // 不然同一个键会同时触发两次（一次关菜单、一次退页面）。
    Shortcut {
        sequence: "Escape"
        enabled: page.menuRow >= 0
        onActivated: page.closeMenu()
    }

    Shortcut {
        sequence: "Escape"
        enabled: page.menuRow < 0
        onActivated: Online.closeCollection()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.paperHigh
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ── 封面 + 标题 + 动作 ───────────────────────────────────────
        Item {
            width: parent.width
            height: page.heroHeight

            Rectangle {
                anchors.fill: parent
                color: Theme.paperHigh
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.screentone
            }

            // 大封面：拿不到就退回一张"纸片 + ♪"。在线歌单的封面常常
            // 1000×1000，这里只给它 104 逻辑像素，所以必须解析时缩。
            MangaPanel {
                id: coverPanel
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                width: 104
                height: 104
                padding: 0

                Image {
                    id: coverImage
                    anchors.fill: parent
                    anchors.margins: 2
                    source: Online.collectionCover
                    asynchronous: true
                    cache: true
                    clip: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 208
                    sourceSize.height: 208
                    visible: status === Image.Ready
                }

                Text {
                    anchors.centerIn: parent
                    visible: coverImage.status !== Image.Ready
                    text: "♪"
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: 34
                }
            }

            Column {
                id: heroText
                anchors.left: coverPanel.right
                anchors.leftMargin: 18
                anchors.right: parent.right
                anchors.rightMargin: 118
                anchors.top: coverPanel.top
                spacing: 6

                Text {
                    width: parent.width
                    text: Online.collectionTitle
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: 26
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: Online.collectionSubtitle
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                    elide: Text.ElideRight
                }

                Row {
                    spacing: 10

                    ComicButton {
                        text: qsTr("播放全部")
                        tone: "accent"
                        strong: true
                        enabled: Online.results.count > 0
                        onClicked: Online.playAll()
                    }

                    ComicButton {
                        text: qsTr("加入队列")
                        tone: "paper"
                        enabled: Online.results.count > 0
                        onClicked: Online.enqueueAll()
                    }
                }
            }

            ComicButton {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.top: parent.top
                anchors.topMargin: 16
                text: qsTr("返回")
                tone: "paper"
                onClicked: Online.closeCollection()
            }
        }

        // ── 表头 ─────────────────────────────────────────────────────
        Item {
            id: headerRow
            width: parent.width
            height: page.headerHeight

            Repeater {
                model: [qsTr("#"), qsTr("标题"), qsTr("艺术家"), qsTr("专辑")]

                Text {
                    required property int index
                    required property string modelData
                    x: Math.round(page.cols[index] * headerRow.width)
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }
            }
        }

        // ── 行 ───────────────────────────────────────────────────────
        Item {
            width: parent.width
            height: parent.height - page.heroHeight - page.headerHeight - page.statusHeight

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: Online.results
                spacing: 0

                delegate: TrackRow {
                    cols: page.cols
                    rowHeight: page.rowHeight
                    showMenu: true
                    keyboardPlayable: true
                    onActivated: Online.play(index)
                    onFavoriteRequested: Online.requestFavorite(index)
                    onMenuRequested: {
                        page.menuRow = index
                    }
                }
            }
        }

        // ── 状态栏 ───────────────────────────────────────────────────
        Item {
            width: parent.width
            height: page.statusHeight

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.screentone
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: Online.busy ? qsTr("处理中…") : Online.statusText
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            ComicButton {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                visible: Online.hasMore && !Online.busy
                text: qsTr("加载更多")
                tone: "paper"
                onClicked: Online.loadMore()
            }
        }
    }

    // ── 空 / 加载 / 失败：三种状态共用一套气泡 ───────────────────────
    //
    // 断网、加载中、真的没有结果，用户看到的都是"这里现在是什么情况、
    // 我能做什么"。所以它们必须是同一个组件的同一个位置，而不是三种
    // 各画一份的提示——那样迟早有一页忘了画。
    Item {
        id: stateLayer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: page.heroHeight + page.headerHeight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: page.statusHeight
        visible: Online.results.count === 0

        // 网点做"底纹"：漫画里空白格也是印出来的，不是一张白纸。
        Halftone {
            anchors.fill: parent
            opacity: 0.35
        }

        MangaPanel {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 420)
            height: Math.min(parent.height - 40, 220)
            padding: 16

            Column {
                anchors.fill: parent
                spacing: 12

                ComicBubble {
                    wrapWidth: parent.width - 34
                    text: Online.errorText.length > 0
                          ? Online.errorText
                          : (Online.busy ? qsTr("正在加载…") : Online.statusText)
                    tailSide: "none"
                }

                ComicButton {
                    text: qsTr("重试")
                    tone: "paper"
                    visible: Online.retryable && !Online.busy
                    onClicked: Online.retry()
                }
            }
        }
    }

    // ── 行菜单 ───────────────────────────────────────────────────────
    Item {
        id: menuLayer
        anchors.fill: parent
        visible: page.menuRow >= 0

        // 点空白处收起：先吃掉这一次点击，别让它同时落到下面的行上。
        MouseArea {
            anchors.fill: parent
            onClicked: page.closeMenu()
        }

        MangaPanel {
            width: 176
            height: 148
            padding: 8
            x: Math.max(16, page.width - width - 24)
            y: Math.min(parent.height - height - 16,
                        page.heroHeight + page.headerHeight
                        + page.menuRow * page.rowHeight + page.rowHeight)

            Column {
                anchors.fill: parent
                spacing: 6

                ComicButton {
                    width: parent.width
                    text: qsTr("播放")
                    tone: "accent"
                    onClicked: {
                        Online.play(page.menuRow)
                        page.closeMenu()
                    }
                }

                ComicButton {
                    width: parent.width
                    text: qsTr("加入队列")
                    tone: "paper"
                    onClicked: {
                        Online.enqueue(page.menuRow)
                        page.closeMenu()
                    }
                }

                ComicButton {
                    width: parent.width
                    text: qsTr("查看歌手")
                    tone: "paper"
                    onClicked: {
                        Online.openArtist(page.menuRow)
                        page.closeMenu()
                    }
                }
            }
        }
    }
}
