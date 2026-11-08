import QtQuick
import "theme"
import "comic"
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
    title: qsTr("Sonora Music")
    color: Theme.paper

    Shell {
        anchors.fill: parent
    }

    // 首次启动引导：盖住整个窗口（连侧栏一起），让用户先选一个音乐文件夹。
    // 它自己的 visible 挂在 Library.needsSetup 上，平时完全不存在。
    FirstRunCard {
        anchors.fill: parent
    }
}
