import QtQuick
import QtQml.Models
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 曲库页：M1 那张"能看见数据"的列表，M2 换成漫画分格。
//
// 这里只做视觉，不动数据通路：模型仍然只是注入进来的 Library 单例。
// M5 后期多了一件"逻辑"：按专辑分组。它用 ListView 的 section 做的
// （见下方 section 段），不是加一条分组查询——分页数据上本来就有专辑名，
// 再向数据库要一遍分组结果是白花的往返。
Item {
    id: page

    // 列宽比例，表头和数据行共用一套，保证对齐
    // 四列起点（占列表宽度比例）：序号 / 标题(含封面) / 艺术家 / 专辑，时长贴右边。
    // 0.42→0.40 与 0.85→0.74 这两个改动是实测出来的：1180 宽的窗口里列表只有
    // 约 927 逻辑像素，0.85 起点的专辑列被右边 90px 的时长列挤到只剩 37px，
    // 一张专辑名只显示得出两个字。
    readonly property var cols: [0.05, 0.33, 0.56, 0.72]
    readonly property int rowHeight: 46

    // 分组开关。默认关：多数时候用户是"找一首歌"，插进来一堆小节标题
    // 反而让列表变短。
    property bool grouped: false

    MangaPanel {
        anchors.fill: parent
        padding: 0

        Column {
            anchors.fill: parent
            spacing: 0

            // ── 搜索 ────────────────────────────────────────────────────
            //
            // 搜索走 FTS5（中文按字切、英文按词），命中判定在 SQL 侧，
            // 这里只把每次输入丢给模型重新取第一页。故意不做防抖：
            // 两千首的查询是毫秒级的，加了 timer 只会让"输入即筛"变成"输入完才筛"。
            Item {
                id: searchBar
                width: parent.width
                height: 40

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

                TextInput {
                    id: searchInput
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    // 右边留出分组按钮的位置：不留的话输入到第 20 个字
                    // 就会跑到按钮底下。
                    anchors.rightMargin: 130
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.ink
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.paperHigh
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                    clip: true
                    // 不给 focus:true。搜索框一旦抢到键盘焦点，窗口级的
                    // Shortcut（空格 / ← / →）就再也收不到按键——用户会觉得
                    // "快捷键坏了"。点一下输入框就能打字，这点成本比"播放键失灵"划算。
                    onTextEdited: Library.setSearchText(text)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: searchInput.text.length === 0
                        text: qsTr("搜标题、艺术家、专辑（中文一个字也能搜）")
                        color: Theme.inkSoft
                        opacity: 0.6
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textBody
                    }
                }

                // 分组开关。用普通按钮而不是复选框：
                // 漫画语言里没有"勾选框"，只有"按下去变重的按钮"。
                ComicButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    height: 28
                    text: page.grouped ? qsTr("分组：专辑") : qsTr("分组：关")
                    strong: page.grouped
                    onClicked: page.grouped = !page.grouped
                }
            }

            // ── 表头 ────────────────────────────────────────────────────
            Item {
                width: parent.width
                height: 34

                Rectangle {
                    anchors.fill: parent
                    color: Theme.paperAlt
                }

                Repeater {
                    model: [qsTr("#"), qsTr("标题"), qsTr("艺术家"), qsTr("专辑"), qsTr("时长")]

                    Text {
                        required property int index
                        required property string modelData

                        y: 8
                        x: index === 4 ? page.width - 58 : page.cols[index] * page.width
                        text: modelData
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                        font.letterSpacing: 1
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.stroke
                    color: Theme.ink
                }
            }

            // ── 列表 ────────────────────────────────────────────────────
            ListView {
                id: list
                width: parent.width
                height: parent.height - 34 - searchBar.height
                clip: true
                model: Library
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 400

                // 行本身在 comic/TrackRow.qml。这里只传两件页面级的东西：
                // 列宽与行高——表头与数据行必须用同一组数字。
                delegate: TrackRow {
                    cols: page.cols
                    rowHeight: page.rowHeight

                    onActivated: Library.activate(index)
                }

                // ── 分组（按专辑）──────────────────────────────────────
                //
                // section.property 绑到 page.grouped：关的时候是空串，
                // ListView 就不画小节（空属性等于不分节）。这样做的好处是
                // 只有一份 delegate，不是"分组模式另写一个 ListView"。
                //
                // 注意 section 是按**已加载的行**分组的：分页之后一页之内
                // 的片段各自成节，跨页的同名专辑会出现两次。要做到全局唯一
                // 就得让数据库分组（一条 GROUP BY 加一条分页查询），
                // 那是 M5 明确砍掉的部分，理由见 docs/IMPLEMENTATION.md §M5。
                section.property: page.grouped ? "album" : ""
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    required property string section

                    width: list.width
                    height: 30

                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 6
                        color: Theme.paperAlt
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.screentone
                    }

                    Text {
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: section
                        color: Theme.ink
                        font.family: Fonts.display
                        font.pixelSize: 15
                    }
                }
            }
        }
    }

    // ── 空状态 ──────────────────────────────────────────────────────────
    Column {
        visible: Library.count === 0
        anchors.centerIn: parent
        spacing: 12

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("还没有歌")
            color: Theme.ink
            font.family: Fonts.display
            font.pixelSize: 26
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("带一个文件夹启动，或把曲库放进应用数据目录")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
        }
    }
}
