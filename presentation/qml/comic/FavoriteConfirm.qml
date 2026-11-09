import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 收藏确认：遮罩 + 一句提示 + 两个按钮。
// 不做乐观更新，所以"确认"之后失败时界面留在原地并显示原因。
Item {
    id: root
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        visible: Online.favoritePromptVisible
        color: Theme.ink
        opacity: 0.42
        z: 40

        MouseArea {
            anchors.fill: parent
            onClicked: Online.cancelFavorite()
        }
    }

    MangaPanel {
        anchors.centerIn: parent
        visible: Online.favoritePromptVisible
        z: 41
        width: Math.min(460, parent.width - 96)
        height: 196

        Column {
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 16

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: Online.favoritePromptText
                color: Theme.ink
                font.family: Theme.fontText
                font.pixelSize: Theme.textBody
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 14

                ComicButton {
                    text: qsTr("收藏")
                    strong: true
                    onClicked: Online.confirmFavorite()
                }

                ComicButton {
                    text: qsTr("取消")
                    tone: "paper"
                    onClicked: Online.cancelFavorite()
                }
            }
        }
    }
}
