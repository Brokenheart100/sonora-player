import QtQuick
import "theme"
import Sonora.Presentation 1.0

// 窗口很薄：布局全在 Shell.qml，视觉全在 theme/ 与 comic/，页面在 pages/。
// **import Sonora.Presentation 1.0 不能删**——Library 单例就从这里来的，删掉不会报错，
// 只会让所有绑定悄悄变成 undefined。
Window {
    id: window
    width: 1180
    height: 800
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: "Sonora Music"
    color: Theme.paper

    Shell {
        anchors.fill: parent
    }
}
