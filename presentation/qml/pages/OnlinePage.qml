import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 在线页（R1 的空壳）。
//
// 这一页在 R1 里**只会显示一种结果**：错误"在线搜索还没接通（下一步 R2）"。
// 那是有意的——先把"命令 → 状态表 → 50ms 轮询 → 三态界面"这条链走通，
// R2 换进去的只是"谁来填行"，界面一行都不用改。
//
// 与曲库页的三处差别，也是这一页存在的理由：
//   1. 搜索要回车才发（曲库是输入即筛：本地两千首是毫秒级查询，网络不是）
//   2. 有"账号胶囊"（未登录 / 昵称），因为在线结果是按账号权限给的
//   3. 行尾可能带灰标签（无版权 / 试听），曲库永远不会出现
Item {
    id: page

    // "" = 搜索页；"playlist" = 推荐歌单；"chart" = 榜单。
    // 浏览层是整页覆盖而不是塞进搜索页的布局里：搜索页的行列早就排满了。
    property string browseMode: ""


    // 与 LibraryPage 同一套列宽分配方式：表头与数据行共用一个数组，
    // 各写一份就一定会漂。
    // 四列起点（占列表宽度比例）：序号 / 标题(含封面) / 艺术家 / 专辑，时长贴右边。
    // 与曲库页同一套算法，但起点整体左移：在线结果的专辑名普遍更长
    // （"七里香 (钢琴版) [...]"），0.85 起步的专辑列只剩几十像素。
    readonly property var cols: [0.05, 0.36, 0.60, 0.76]
    readonly property int rowHeight: 46
    readonly property int headerHeight: 26
    readonly property int statusHeight: 34

    MangaPanel {
        anchors.fill: parent
        padding: 0

        Column {
            anchors.fill: parent
            spacing: 0

            // ── 工具行 ──────────────────────────────────────────────────
            // 搜索与账号在全局顶部条上；这一行只放"这一页特有的入口"。
            Item {
                id: toolbarRow
                width: parent.width
                height: 44

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    ComicButton {
                        text: qsTr("推荐")
                        tone: "paper"
                        onClicked: {
                            page.browseMode = "playlist"
                            Online.closeCollection()
                            Online.loadCollections("playlist")
                        }
                    }

                    ComicButton {
                        text: qsTr("榜单")
                        tone: "paper"
                        onClicked: {
                            page.browseMode = "chart"
                            Online.closeCollection()
                            Online.loadCollections("chart")
                        }
                    }

                    ComicButton {
                        text: qsTr("私人 FM")
                        tone: Online.fmMode ? "accent" : "paper"
                        onClicked: Online.startPersonalFm()
                    }

                    // 诊断入口：默认收起，点开才取快照（查两次 COUNT，不能进轮询）。
                    Rectangle {
                        id: diagChip
                        anchors.verticalCenter: parent.verticalCenter
                        width: 56
                        height: 24
                        radius: 12
                        color: diagSheet.visible ? Theme.ink : "transparent"
                        border.color: Theme.stroke
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("诊断")
                            color: diagSheet.visible ? Theme.paperHigh : Theme.inkSoft
                            font.family: Theme.fontText
                            font.pixelSize: Theme.textSmall
                        }

                        TapHandler {
                            onTapped: {
                                diagSheet.visible = !diagSheet.visible
                                if (diagSheet.visible)
                                    Online.refreshDiagnostics()
                            }
                        }
                    }
                }
            }

            // ── 表头 ───────────────────────────────────────────────────
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

            // ── 结果列表 ───────────────────────────────────────────────
            Item {
                width: parent.width
                height: parent.height - toolbarRow.height - headerRow.height - page.statusHeight

                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true
                    model: Online.results
                    spacing: 0

                    delegate: TrackRow {
                        cols: page.cols
                        rowHeight: page.rowHeight
                        onActivated: Online.play(index)
                        // 右键收藏：TrackRow 只负责喊一声，确认与发送在 ViewModel 里。
                        onFavoriteRequested: Online.requestFavorite(index)
                    }

                    // 空态：加载中 / 出错 / 真的没有结果，三种文案分开写。
                    // 合成一句"暂无数据"会让用户不知道要不要重试。
                    Text {
                        anchors.centerIn: parent
                        width: Math.min(parent.width * 0.7, 420)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: Online.results.count === 0
                        text: Online.errorText.length > 0
                              ? Online.errorText
                              : (Online.busy ? qsTr("正在连网易云…") : Online.statusText)
                        color: Online.errorText.length > 0 ? Theme.ink : Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textBody
                    }
                }
            }

            // ── 状态栏 ─────────────────────────────────────────────────
            Item {
                id: statusRow
                width: parent.width
                height: page.statusHeight

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: Theme.screentone
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    // 离线时这页是"上次搜到的结果"：说清楚，别让人以为是新数据。
                    Rectangle {
                        visible: Online.cachedResult
                        width: cachedLabel.implicitWidth + 12
                        height: 18
                        radius: 3
                        color: Theme.accent

                        Text {
                            id: cachedLabel
                            anchors.centerIn: parent
                            text: qsTr("离线·上次结果")
                            color: Theme.onAccent
                            font.family: Theme.fontText
                            font.pixelSize: Theme.textSmall - 2
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Online.busy ? qsTr("处理中…") : Online.statusText
                        color: Theme.inkSoft
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                    }
                }

                ComicButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    visible: Online.hasMore && !Online.busy
                    text: qsTr("加载更多")
                    onClicked: Online.loadMore()
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    visible: Online.retryable && !Online.busy
                    text: qsTr("重试")
                    color: Theme.accent
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall

                    TapHandler {
                        onTapped: Online.retry()
                    }
                }
            }
        }
    }

    // ── 诊断面板 ──────────────────────────────────────────────────
    // 展开时展示降级留痕；关着的时候一个查询都不发（面板自己管 Timer）。
    OnlineDiagPanel { id: diagSheet }

    // ── 浏览层：推荐歌单 / 榜单 / 新歌 / 我的 ──────────────────────
    OnlineBrowseLayer { id: browseLayer; anchors.fill: parent; mode: page.browseMode }

    // ── 卡片详情：与搜索页共用 Online.results ──────────────────────
    CollectionPage {
        anchors.fill: parent
        visible: Online.collectionActive
    }

    // ── 收藏确认 ──────────────────────────────────────────────────
    FavoriteConfirm { }

} // Item
