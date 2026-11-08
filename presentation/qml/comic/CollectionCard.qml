import QtQuick
import "../theme"

// 在线"卡片"：一张封面 + 一行名字 + 一行副标题（推荐歌单 / 榜单共用）。
//
// 五个 required 就是它与 `OnlineCollectionModel` 的全部契约，role 名在
// `OnlineViewModel.cpp` 的 roleNames() 里逐字对应——对不上不会报错，
// 只会静默绑定成空串，所以两边改名必须一起改。
Item {
    id: card

    required property string collectionId
    required property string name
    required property string cover
    required property string subtitle
    required property int trackCount

    signal activated()

    // Tab 能走进卡片、回车能打开——"进歌单"这一步不该只有鼠标能做。
    activeFocusOnTab: true
    Keys.onReturnPressed: card.activated()

    width: 168
    height: 228

    Rectangle {
        anchors.fill: parent
        visible: card.activeFocus
        radius: 12
        color: "transparent"
        border.width: 3
        border.color: Theme.accentAlt
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        clip: true
        radius: 10
        color: Theme.paperHigh
        border.width: 2
        border.color: cardHover.hovered ? Theme.accent : Theme.ink
    }

    Image {
        id: art
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        height: width
        source: card.cover
        // 必须给 sourceSize，否则一张 1200px 的图会按原尺寸解码驻留显存。
        sourceSize: Qt.size(320, 320)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: status === Image.Ready
    }

    Rectangle {
        anchors.fill: art
        visible: art.status !== Image.Ready
        color: Theme.paperAlt

        Text {
            anchors.centerIn: parent
            text: "♪"
            color: Theme.inkSoft
            font.family: Theme.fontDisplay
            font.pixelSize: 40
        }
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: art.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 8
        text: card.name
        color: Theme.ink
        font.family: Theme.fontText
        font.pixelSize: 13
        elide: Text.ElideRight
        maximumLineCount: 2
        wrapMode: Text.Wrap
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.bottomMargin: 10
        text: card.subtitle
        color: Theme.inkSoft
        font.family: Theme.fontText
        font.pixelSize: 11
        elide: Text.ElideRight
    }

    TapHandler {
        onTapped: card.activated()
    }

    HoverHandler {
        id: cardHover
    }
}
