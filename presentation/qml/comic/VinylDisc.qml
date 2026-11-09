import QtQuick
import QtQuick.Effects
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 唱片：错位阴影 + 白盘 + 黑胶纹路 + 中心标签（有封面就圆形裁切，没封面回红标）。
// 抽成组件是因为这一块自己旋转、自己管封面回落，和页面别的部分没有耦合。
Item {
    id: root

    property real spinMs: 1800
        width: Math.min(parent.height - 24, 340)
        height: width
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.top: parent.top
        anchors.topMargin: 18

        // 错位的实心阴影，和 MangaPanel 同一套语言
        Rectangle {
            anchors.fill: plate
            x: plate.x + 10
            y: plate.y + 10
            radius: width / 2
            color: Theme.ink
            opacity: 0.28
        }

        MangaPanel {
            id: plate
            anchors.fill: parent
            cornerRadius: width / 2
            padding: 0
            shadow: false
            strokeWidth: 3
            fillColor: Theme.paperHigh
        }

        // 黑胶：一层圆 + 几圈纹路
        Item {
            id: disc
            anchors.fill: parent
            anchors.margins: 14

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.ink
            }

            // 纹路：同心的细圆环，靠 border 画，不引图片
            Repeater {
                model: 7
                Rectangle {
                    required property int index
                    anchors.centerIn: parent
                    width: disc.width - 26 - index * 18
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: index % 2 === 0 ? Theme.grooveA : Theme.grooveB
                }
            }

            // 中心标签：有封面就把封面裁成圆形贴上去（唱片中心本来就是圆的）；
            // 没封面才回到漫画里的老做法——红标上写歌名。
            Item {
                id: label
                anchors.centerIn: parent
                width: disc.width * 0.46
                height: width

                readonly property bool hasCover:
                    Player.coverUrl.length > 0 && coverImage.status === Image.Ready

                // 白圈：不管里面是封面还是红标，都压一道白边，才像一张唱片
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Theme.paperHigh
                }

                Item {
                    id: labelFace
                    anchors.fill: parent
                    anchors.margins: 4

                    // 封面本体是隐藏的：可见的那一份由下面的 MultiEffect 用圆形遮罩画出来
                    Image {
                        id: coverImage
                        anchors.fill: parent
                        source: Player.coverUrl
                        sourceSize: Qt.size(Math.round(width * 2), Math.round(height * 2))
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: true
                        visible: false
                    }

                    // 遮罩：只用 alpha 通道，颜色随便给个不透明的
                    Item {
                        id: coverMask
                        anchors.fill: parent
                        visible: false
                        layer.enabled: true

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: "white"
                        }
                    }

                    MultiEffect {
                        anchors.fill: parent
                        visible: label.hasCover
                        source: coverImage
                        maskEnabled: true
                        maskSource: coverMask
                    }

                    // 没封面时的红标 + 歌名
                    Rectangle {
                        anchors.fill: parent
                        visible: !label.hasCover
                        radius: width / 2
                        color: Theme.accent

                        Column {
                            anchors.centerIn: parent
                            width: parent.width * 0.78
                            spacing: 3

                            Text {
                                width: parent.width
                                text: Player.hasTrack ? Player.title : qsTr("未选曲")
                                color: Theme.paperHigh
                                font.family: Fonts.display
                                font.pixelSize: 16
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                elide: Text.ElideRight
                                maximumLineCount: 2
                            }
                            Text {
                                width: parent.width
                                text: Player.hasTrack ? Player.artist : ""
                                color: Theme.onAccent
                                font.family: Theme.fontText
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }
                    }

                    // 中心孔：永远在最上层，封面也压不住
                    Rectangle {
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.paperHigh
                    }
                }
            }
        }

        // 转速：只在播放时转。旋转用 NumberAnimation 循环，
        // 暂停时 stop() 会停在当前角度（不是"回到 0 度"）。
        RotationAnimation on rotation {
            running: Player.playing
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: spinMs
        }
}
