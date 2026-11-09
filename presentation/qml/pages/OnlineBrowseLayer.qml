import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 在线页的浏览层：推荐歌单 / 榜单 / 新歌速递 / 我的 的卡片墙。
// 整层覆盖搜索页（行高与列宽是两套布局），所以单独成一个页面组件。
    Item {
        id: browseLayer

        // "" 不显示；"playlist" 推荐歌单；"chart" 榜单；"mine" 我的歌单。
        property string mode: ""
        anchors.fill: parent
        visible: mode.length > 0 && !Online.collectionActive

        Rectangle {
            anchors.fill: parent
            color: Theme.paperHigh
        }

        Item {
            id: browseBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 50

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.screentone
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                ComicButton {
                    text: qsTr("推荐")
                    tone: mode === "playlist" ? "accent" : "paper"
                    onClicked: {
                        mode = "playlist"
                        Online.loadCollections("playlist")
                    }
                }

                ComicButton {
                    text: qsTr("榜单")
                    tone: mode === "chart" ? "accent" : "paper"
                    onClicked: {
                        mode = "chart"
                        Online.loadCollections("chart")
                    }
                }

                ComicButton {
                    text: qsTr("新歌速递")
                    tone: "paper"
                    onClicked: Online.openCollection("newsongs", "latest", qsTr("新歌速递"))
                }

                ComicButton {
                    text: qsTr("每日推荐")
                    tone: "paper"
                    onClicked: Online.openDailyRecommend()
                }

                // N4：私人 FM。追加式队列，播到末尾会自动再来要一批。
                ComicButton {
                    text: qsTr("私人 FM")
                    tone: Online.fmMode ? "accent" : "paper"
                    onClicked: {
                        mode = ""
                        Online.closeCollection()
                        Online.startPersonalFm()
                    }
                }

                ComicButton {
                    text: qsTr("我的")
                    tone: mode === "mine" ? "accent" : "paper"
                    onClicked: {
                        mode = "mine"
                        Online.closeCollection()
                        Online.loadMyPlaylists()
                    }
                }
            }

            ComicButton {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("返回")
                tone: "paper"
                onClicked: {
                    mode = ""
                    Online.closeCollection()
                }
            }
        }

        GridView {
            id: cardGrid
            visible: mode !== "mine"
            anchors.top: browseBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            anchors.topMargin: 20
            clip: true
            cellWidth: 188
            cellHeight: 248
            model: Online.collections

            delegate: CollectionCard {
                onActivated: Online.openCollection(mode, collectionId, name)
            }
        }

        // "我的歌单"：登录后从 playlist[] 拉下来的，卡片样式与推荐/榜单一致。
        GridView {
            id: mineGrid
            visible: mode === "mine" && Online.myPlaylists.length > 0
            anchors.top: browseBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            clip: true
            cellWidth: 188
            cellHeight: 248
            model: Online.myPlaylists

            delegate: Item {
                width: 168
                height: 228

                Rectangle {
                    anchors.fill: parent
                    color: Theme.paperHigh
                    border.width: 3
                    border.color: Theme.ink
                }

                Image {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 10
                    height: 140
                    source: modelData.cover
                    sourceSize: Qt.size(320, 320)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }

                Text {
                    anchors.top: parent.top
                    anchors.topMargin: 158
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 10
                    text: modelData.name
                    elide: Text.ElideRight
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: 16
                }

                Text {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 10
                    text: modelData.trackCount > 0 ? qsTr("%n 首", "", modelData.trackCount) : ""
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: 13
                }

                TapHandler {
                    onTapped: Online.openCollection("playlist", modelData.playlistId, modelData.name)
                }
            }
        }

        Text {
            id: mineEmpty
            anchors.centerIn: parent
            visible: mode === "mine" && Online.myPlaylists.length === 0
            text: Online.signedIn ? qsTr("还没有歌单") : qsTr("登录后可见")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: 18
        }

        Text {
            anchors.centerIn: cardGrid
            width: Math.min(parent.width * 0.7, 420)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: Online.collections.count === 0
            text: Online.collections.status.length > 0 ? Online.collections.status : qsTr("正在拉取…")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
        }
    }
