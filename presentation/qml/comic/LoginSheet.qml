import QtQuick
import "../theme"
import Sonora.Presentation 1.0

// 登录面板：全局一份，挂在 Shell 里。
// 两种进路：扫码（默认，不用碰 cookie）与粘贴 Cookie（扫码不通时的后路）。
// 二维码用 Canvas 按格画：明文接口不给二维码图，本地也不引图片编码器。
//
// 为什么从 OnlinePage 挪到这里：登录入口现在是顶部条上的头像，
// 任何一页都该能点开，而面板跟着"在线"那一页走就会在别的页点不出来。
Item {
    id: root

    visible: false
    property bool qrMode: true

    // 打开即默认落在扫码上：少一步点击，也少一次"cookie 从哪来"的解释。
    function open() {
        qrMode = true;
        visible = true;
        if (!Online.signedIn)
            Online.startQrLogin();
    }

    function close() {
        visible = false;
        if (!Online.signedIn)
            Online.cancelQrLogin();
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
    }

    MangaPanel {
        anchors.centerIn: parent
        width: 560
        height: Online.signedIn ? 210 : 520

        Column {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            Text {
                text: Online.signedIn ? qsTr("已登录") : (root.qrMode ? qsTr("扫码登录") : qsTr("粘贴网易云 Cookie"))
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textTitle
            }

            // ---- 已登录：只给退出 ----
            Text {
                visible: Online.signedIn
                text: Online.accountText
                color: Theme.accent
                font.family: Theme.fontText
                font.pixelSize: Theme.textBody
            }

            Text {
                visible: Online.signedIn
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("退出后本地密文会被删除，下次要重新登录。")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            // ---- 未登录：两个入口 ----
            Row {
                visible: !Online.signedIn
                spacing: 10

                ComicButton {
                    text: qsTr("扫码")
                    tone: root.qrMode ? "accent" : "paper"
                    onClicked: {
                        root.qrMode = true;
                        Online.startQrLogin();
                    }
                }

                ComicButton {
                    text: qsTr("粘贴 Cookie")
                    tone: root.qrMode ? "paper" : "accent"
                    onClicked: {
                        root.qrMode = false;
                        Online.cancelQrLogin();
                    }
                }
            }

            // ---- 二维码 ----
            Item {
                visible: !Online.signedIn && root.qrMode
                width: 240
                height: 240

                Canvas {
                    id: qrCanvas
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d");
                        var size = Online.qrSize;
                        ctx.reset();
                        ctx.fillStyle = "white";
                        ctx.fillRect(0, 0, width, height);
                        if (size <= 0)
                            return;
                        var cell = width / size;
                        ctx.fillStyle = "#111111";
                        for (var row = 0; row < size; ++row) {
                            for (var col = 0; col < size; ++col) {
                                if (Online.qrCell(row, col))
                                    ctx.fillRect(col * cell, row * cell, Math.ceil(cell), Math.ceil(cell));
                            }
                        }
                    }
                }

                Connections {
                    target: Online
                    function onQrChanged() {
                        qrCanvas.requestPaint();
                    }
                }
            }

            Text {
                visible: !Online.signedIn && root.qrMode
                width: parent.width
                wrapMode: Text.WordWrap
                text: Online.qrMessage.length > 0 ? Online.qrMessage : qsTr("用网易云音乐手机端扫码")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            // ---- 粘贴框 ----
            Rectangle {
                visible: !Online.signedIn && !root.qrMode
                width: parent.width
                height: 150
                color: Theme.paperHigh
                border.color: Theme.stroke
                border.width: 1

                TextEdit {
                    id: cookieInput
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    color: Theme.ink
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                    Text {
                        anchors.fill: parent
                        visible: cookieInput.text.length === 0 && !cookieInput.activeFocus
                        text: qsTr("浏览器登录 music.163.com → F12 → Network → 任选一个请求，复制请求头里的整段 Cookie（只要有 MUSIC_U 就行）")
                        color: Theme.inkSoft
                        wrapMode: Text.WordWrap
                        font.family: Theme.fontText
                        font.pixelSize: Theme.textSmall
                    }
                }
            }

            Text {
                visible: !Online.signedIn
                width: parent.width
                wrapMode: Text.WordWrap
                text: Online.signingIn ? qsTr("正在核对…") : Online.loginError
                color: Theme.accent
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Row {
                spacing: 10

                ComicButton {
                    visible: !Online.signedIn && root.qrMode
                    text: qsTr("刷新二维码")
                    tone: "paper"
                    onClicked: Online.startQrLogin()
                }

                ComicButton {
                    visible: !Online.signedIn && !root.qrMode
                    text: Online.signingIn ? qsTr("请稍候") : qsTr("登录")
                    tone: "accent"
                    onClicked: Online.signIn(cookieInput.text)
                }

                ComicButton {
                    visible: Online.signedIn
                    text: qsTr("退出登录")
                    tone: "ink"
                    onClicked: {
                        Online.signOut();
                        root.visible = false;
                    }
                }

                ComicButton {
                    text: qsTr("关闭")
                    tone: "paper"
                    onClicked: root.close()
                }
            }
        }
    }

    // 登录成功就自动收起，并停掉扫码轮询。
    Connections {
        target: Online
        function onStatusChanged() {
            if (Online.signedIn && root.visible) {
                root.visible = false;
                Online.cancelQrLogin();
            }
        }
    }
}
