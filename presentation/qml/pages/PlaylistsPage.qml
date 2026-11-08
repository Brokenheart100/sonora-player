import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 歌单页：列表 ↔ 详情两种形态，用一个 opened 属性切换。
//
// 为什么不拆成两页：两页意味着 PageTurner 里多一个 index，而"返回"要在
// 两个 index 之间来回猜上一页是哪。歌单是一个"抽屉"，不是一个目的地。
Item {
    id: page

    readonly property int rowHeight: 46

    // 新建歌单的输入框内容。空串时按钮置灰，不让用户点了才知道不行。
    property string draftName: ""
    property int armedRemove: -1      // 哪个歌单正等着"再点一次才删"
    // "从曲库挑一首"的浮层。曲库列表直接用 Library 这个 model，
    // 不复制一份 DTO 过来——它本来就是 QAbstractListModel。
    property bool adding: false

    // ── 列表形态 ──────────────────────────────────────────────────────
    Column {
        anchors.fill: parent
        spacing: 10
        visible: !Playlists.opened

        Row {
            width: parent.width
            spacing: 10

            Text {
                text: qsTr("歌单")
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: 34
            }

            Item { width: parent.width - 260; height: 1 }
        }

        // 新建：一条输入 + 一个按钮。娱乐产品不需要模态框，一行就够。
        Row {
            width: parent.width
            spacing: 10

            Rectangle {
                width: parent.width - 160
                height: 38
                color: Theme.paperHigh
                border.width: Theme.stroke
                border.color: Theme.ink
                radius: 6

                TextInput {
                    id: nameInput
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                    selectByMouse: true
                    clip: true
                    text: page.draftName
                    onTextChanged: page.draftName = text
                    Keys.onReturnPressed: {
                        if (page.draftName.trim().length > 0) {
                            Playlists.createPlaylist(page.draftName)
                            page.draftName = ""
                        }
                    }
                }

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    visible: page.draftName.length === 0 && !nameInput.activeFocus
                    text: qsTr("新歌单的名字")
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }
            }

            ComicButton {
                width: 140
                height: 38
                text: qsTr("建一个")
                tone: page.draftName.trim().length > 0 ? "accent" : "paper"
                strong: page.draftName.trim().length > 0
                enabled: page.draftName.trim().length > 0
                onClicked: {
                    Playlists.createPlaylist(page.draftName)
                    page.draftName = ""
                }
            }
        }

        Text {
            width: parent.width
            text: Playlists.status
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textSmall
        }

        ListView {
            id: list
            width: parent.width
            height: parent.height - 150
            clip: true
            model: Playlists.playlists
            spacing: 6

            delegate: Item {
                required property var modelData
                required property int index

                width: list.width
                height: 56

                Rectangle {
                    anchors.fill: parent
                    color: rowMouse.containsMouse ? Theme.paperHigh : "transparent"
                    radius: 6
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                    elide: Text.ElideRight
                    width: parent.width - 220
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14 + parent.width - 220
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 首").arg(modelData.trackCount)
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }

                ComicButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 84
                    height: 30
                    text: page.armedRemove === modelData.playlistId ? qsTr("确认删") : qsTr("删除")
                    tone: page.armedRemove === modelData.playlistId ? "accent" : "paper"
                    onClicked: {
                        if (page.armedRemove === modelData.playlistId) {
                            page.armedRemove = -1
                            Playlists.removePlaylist(modelData.playlistId)
                        } else {
                            // 删歌单是不可逆的：第一次点只把它变成"确认删"。
                            page.armedRemove = modelData.playlistId
                        }
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    anchors.rightMargin: 100
                    hoverEnabled: true
                    onClicked: {
                        page.armedRemove = -1
                        Playlists.openPlaylist(modelData.playlistId, modelData.name)
                    }
                }
            }
        }

        Text {
            width: parent.width
            visible: Playlists.playlists.length === 0
            text: qsTr("还没有歌单。上面输入一个名字，回车。")
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
        }
    }

    // ── 详情形态 ──────────────────────────────────────────────────────
    Column {
        anchors.fill: parent
        spacing: 8
        visible: Playlists.opened

        Row {
            width: parent.width
            spacing: 10

            ComicButton {
                width: 96
                height: 36
                text: qsTr("返回")
                onClicked: Playlists.closePlaylist()
            }

            Text {
                text: Playlists.currentName
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: 28
                elide: Text.ElideRight
                width: parent.width - 260
                verticalAlignment: Text.AlignVCenter
                height: 36
            }

            ComicButton {
                width: 84
                height: 36
                text: qsTr("加歌")
                onClicked: page.adding = true
            }

            ComicButton {
                width: 112
                height: 36
                text: qsTr("播放全部")
                tone: "accent"
                strong: true
                onClicked: Playlists.playAll()
            }
        }

        Text {
            width: parent.width
            text: Playlists.status
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textSmall
        }

        // ── 列名与分隔线 ────────────────────────────────────────────
        // 详情页原来是"标题直接堆在列表上"，没有列名也没有一条横线，
        // 和曲库页摆在一起就像少了表头的一格。这里补上同样的表头语言：
        // 列名 + 一条粗墨线，把"标题区"和"歌曲区"切开。
        Item {
            width: parent.width
            height: 26

            Text {
                x: 48
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("歌曲")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
                font.letterSpacing: 1
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 96
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("时长")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
                font.letterSpacing: 1
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.stroke
                color: Theme.ink
            }
        }

        ListView {
            id: trackList
            width: parent.width
            height: parent.height - 126
            clip: true
            model: Playlists.entries
            spacing: 2

            delegate: Item {
                required property var modelData
                required property int index

                width: trackList.width
                height: page.rowHeight
                opacity: modelData.available ? 1.0 : 0.45

                Rectangle {
                    anchors.fill: parent
                    color: trackMouse.containsMouse && modelData.available ? Theme.paperHigh : "transparent"
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    text: (index + 1).toString()
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 48
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 300
                    text: modelData.title + (modelData.available ? "" : qsTr("（不可用）"))
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textBody
                    elide: Text.ElideRight
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 96
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.duration
                    color: Theme.inkSoft
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }

                ComicButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 72
                    height: 28
                    text: qsTr("移出")
                    onClicked: Playlists.removeTrack(modelData.trackId)
                }

                MouseArea {
                    id: trackMouse
                    anchors.fill: parent
                    anchors.rightMargin: 88
                    hoverEnabled: true
                    onClicked: {
                        if (modelData.available) {
                            Playlists.playEntry(index)
                        }
                    }
                }
            }
        }
    }

    // ── 从曲库挑一首 ──────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        visible: page.adding
        color: Theme.ink
        opacity: 0.35

        MouseArea {
            anchors.fill: parent
            onClicked: page.adding = false
        }
    }

    MangaPanel {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 620)
        height: Math.min(parent.height - 80, 520)
        padding: 16
        visible: page.adding

        Column {
            anchors.fill: parent
            spacing: 8

            Row {
                width: parent.width
                spacing: 10

                Text {
                    text: qsTr("从曲库挑一首")
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: 24
                }

                Item { width: parent.width - 220; height: 1 }

                ComicButton {
                    width: 84
                    height: 30
                    text: qsTr("关闭")
                    onClicked: page.adding = false
                }
            }

            ListView {
                id: picker
                width: parent.width
                height: parent.height - 52
                clip: true
                model: Library

                delegate: Item {
                    required property int index
                    required property string title
                    required property string artist

                    width: picker.width
                    height: 40

                    Rectangle {
                        anchors.fill: parent
                        color: pickMouse.containsMouse ? Theme.paperHigh : "transparent"
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 24
                        text: title + (artist.length > 0 ? " — " + artist : "")
                        color: Theme.ink
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textBody
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        id: pickMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        // 加完就收起浮层：连续加三首是少数场景，多留一步反而乱。
                        onClicked: {
                            Playlists.addFromLibrary(index)
                            page.adding = false
                        }
                    }
                }
            }
        }
    }
}
