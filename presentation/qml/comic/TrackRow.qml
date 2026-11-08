import QtQuick
import "../theme"

// 曲库列表的一行。
//
// 单独成文件不是为了复用（只有一处在用），是因为这一行**是有状态的**：
// 封面异步解码、加载失败要退回占位、hover 要变底色。这些分支混在 LibraryPage
// 的布局里，会让"五列怎么对齐"这件真正需要看清的事被淹没。
//
// 顶上那六个 required 属性就是它与模型的全部契约。ListView 会按 roleNames
// 里的名字自动绑定，所以 LibraryPage 那边只需要写 `TrackRow { }`——
// 多抄一遍 `required property string title` 是多余的，而且抄漏一个会在运行时
// 静默变成空串（QML 的 required 只在委托里生效，写错名字不报错）。
Item {
    id: row

    required property int index
    required property string title
    required property string artist
    required property string album
    required property string duration
    required property string cover

    // 列宽比例与行高由页面注入：表头和数据行必须共用一套数字，
    // 各自写一份就一定会漂。
    property var cols: [0.05, 0.42, 0.68, 0.85]
    property int rowHeight: 46

    // 行尾小标签（"试听" / "无版权"）。空串 = 不占位：曲库的行永远是空串，
    // 所以这一列对曲库页是零成本的。
    property string badge: ""

    // 右键 = 请求收藏（只有在线页会接这个信号；曲库行没人接，等于没有）。
    // 用右键而不是再加一行按钮：行高 46 没有多余空间，多一个按钮会把
    // "哪一列是曲名"这件事挤歪。
    signal favoriteRequested()

    // 行尾的「···」：只有在线详情页打开它（曲库页没有这些动作）。
    // 用信号而不是内联菜单：菜单要有唯一一份，不能每行各画一个。
    property bool showMenu: false
    signal menuRequested()

    // 在线详情页把它打开：Tab 能走进来，回车播放。
    // 曲库页保持关闭——那里列表几千行，Tab 一个个走过去还不如用搜索。
    property bool keyboardPlayable: false
    activeFocusOnTab: keyboardPlayable
    Keys.onReturnPressed: {
        if (row.keyboardPlayable) {
            row.activated()
        }
    }

    signal activated()

    width: parent ? parent.width : 0
    height: rowHeight

    Rectangle {
        anchors.fill: parent
        color: rowHover.hovered ? Theme.paperAlt : "transparent"
    }

    // 焦点环：键盘走到哪一行要看得见。用 accentAlt（"可交互焦点"专用色），
    // 不用 accent——accent 的含义是"正在播放"。
    Rectangle {
        anchors.fill: parent
        visible: row.activeFocus
        color: "transparent"
        border.width: 2
        border.color: Theme.accentAlt
    }

    // 双击播放。用 TapHandler 的 onDoubleTapped，而不是自己数点击次数：
    // 双击间隔、按下后移动的容差这些细节它都处理过了。
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: row.activated()
    }

    HoverHandler {
        id: rowHover
    }

    // 行间用一条浅浅的墨线，比斑马纹更"印刷"
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        height: 1
        color: Theme.screentone
        opacity: 0.7
    }

    Text {
        x: row.cols[0] * row.width
        anchors.verticalCenter: parent.verticalCenter
        text: (row.index + 1).toString().padStart(2, "0")
        color: Theme.inkSoft
        font.family: "Consolas"
        font.pixelSize: Theme.textSmall
    }

    // ── 封面 ────────────────────────────────────────────────────────────
    //
    // 缩略图加 1px 墨框：漫画分格里图片从来不是"浮"在纸上的，都有一圈线。
    Item {
        id: coverBox
        x: row.cols[1] * row.width
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28

        Rectangle {
            anchors.fill: parent
            color: Theme.paperAlt
            border.color: Theme.ink
            border.width: 1
            visible: coverImage.status !== Image.Ready

            Text {
                anchors.centerIn: parent
                text: "♪"
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: 13
            }
        }

        Image {
            id: coverImage
            anchors.fill: parent
            source: row.cover
            asynchronous: true
            cache: true
            clip: true
            fillMode: Image.PreserveAspectCrop
            // sourceSize 是关键：真实封面常常 1000×1000 起步，不设它的话
            // 每行都会解码一整张大图——两千行滚动时这就是卡顿的主因。
            // 56 是给 2 倍屏留的余量。
            sourceSize.width: 56
            sourceSize.height: 56
            // 加载失败（文件被删、格式不认识）时静默退回占位，不打印也不报错：
            // 封面缺一张不影响听歌。
            visible: status === Image.Ready
        }
    }

    // 弹性列用锚点而不是手算宽度：标题的右边界就是艺术家列的左边界，
    // 专辑的右边界就是时长列的左边界。手算 `width: row.width - 90 - x - 12`
    // 在列表宽度变化时会算错一截（实测专辑列被压成三个字），而锚点法
    // 无论窗口多宽都自动成立。
    Text {
        id: titleText
        anchors.left: coverBox.right
        anchors.leftMargin: 8
        anchors.right: artistText.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: row.title
        color: Theme.ink
        font.family: Theme.fontText
        font.pixelSize: Theme.textBody
        font.weight: Font.DemiBold
    }

    Text {
        id: artistText
        anchors.left: parent.left
        anchors.leftMargin: row.cols[2] * row.width
        anchors.right: albumText.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: row.artist
        color: Theme.inkSoft
        font.family: Theme.fontText
        font.pixelSize: Theme.textBody
    }

    Text {
        id: albumText
        anchors.left: parent.left
        anchors.leftMargin: row.cols[3] * row.width
        // 有标签时让位给标签，没标签时仍然是"贴到时长列左边"。
        anchors.right: badgeText.visible ? badgeText.left : durationText.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: row.album
        color: Theme.inkSoft
        font.family: Theme.fontText
        font.pixelSize: Theme.textBody
    }

    Text {
        id: badgeText
        anchors.right: durationText.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: row.badge.length > 0
        text: row.badge
        color: Theme.accent
        font.family: Theme.fontText
        font.pixelSize: Theme.textSmall
    }

    // 「···」：判定"用户想对这一行做点别的"。它自己不开菜单，只把行号喊出去。
    Item {
        id: menuButton
        visible: row.showMenu
        x: row.width - 30
        anchors.verticalCenter: parent.verticalCenter
        width: 24
        height: 24

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: menuHover.hovered ? Theme.paperAlt : "transparent"
            border.width: menuHover.hovered ? 1 : 0
            border.color: Theme.ink
        }

        Text {
            anchors.centerIn: parent
            text: "···"
            color: Theme.ink
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
            font.weight: Font.Bold
        }

        HoverHandler { id: menuHover }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: row.menuRequested()
        }
    }

    Text {
        id: durationText
        // 58 而不是 90：这一列只放 "5:10"，给它 90 逻辑像素等于从专辑列身上
        // 偷 32px——在 1180 宽的窗口里这不是小数目（列表只有约 927 逻辑像素宽，
        // 百分比 + 固定像素混着算，固定值吃掉的比重比看起来大得多）。
        // 有「···」时整列左移 30px，免得端头被按住。
        x: row.width - (row.showMenu ? 88 : 58)
        width: 50
        horizontalAlignment: Text.AlignRight
        anchors.verticalCenter: parent.verticalCenter
        text: row.duration
        color: Theme.inkSoft
        font.family: "Consolas"
        font.pixelSize: Theme.textSmall
    }
}
