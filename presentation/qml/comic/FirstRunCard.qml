import QtQuick
import QtQuick.Dialogs
import "../theme"
import Sonora.Presentation 1.0

// 首次启动的引导卡（M9）。
//
// 为什么需要它：装好第一次打开时，库里空空如也，用户看到的是一个空列表和一句
// "共 0 首"——他不知道该把歌放哪儿、也不知道这软件要扫文件夹。这一张卡只做一件事：
// 让他选一个目录，选完立刻开始导入。
//
// 它盖在整个窗口上（连侧栏一起），因为第一次打开时侧栏本来也没什么可点的。
// `Library.needsSetup` 由宿主在启动时置位：既没有命令行参数、也没记过目录时为真。
Item {
    id: root

    visible: Library.needsSetup

    // 半透明墨色：底下仍然透出一点点界面，用户知道窗口不是死了。
    Rectangle {
        anchors.fill: parent
        color: Theme.ink
        opacity: 0.55
        // 点遮罩不关闭：这是"必须先做一件事"的卡，不是普通弹窗。
        MouseArea { anchors.fill: parent }
    }

    MangaPanel {
        id: card
        anchors.centerIn: parent
        width: Math.min(560, root.width - 96)
        height: body.height + 96
        strokeWidth: Theme.strokeBold

        Column {
            id: body
            x: 28
            y: 28
            width: card.width - 56
            spacing: 14

            Text {
                width: parent.width
                text: qsTr("先把歌放进来")
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: 34
            }

            Text {
                width: parent.width
                text: qsTr("选一个装着音乐文件的文件夹，Sonora 会把里面的歌扫进曲库。\n以后再打开，它会自己记住这个位置。")
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textBody
                lineHeight: 1.4
                wrapMode: Text.WordWrap
            }

            // 选中的目录先亮出来再导入：导入是后台线程，用户得知道自己选的是哪儿。
            Text {
                width: parent.width
                text: Library.status
                visible: text.length > 0
                color: Theme.accentAlt
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
                elide: Text.ElideMiddle
            }

            Row {
                spacing: 12

                ComicButton {
                    text: qsTr("选文件夹…")
                    strong: true
                    onClicked: { console.log("M9CLICK choose"); folderPicker.open() }
                }

                ComicButton {
                    text: qsTr("以后再说")
                    tone: "paper"
                    onClicked: { console.log("M9CLICK dismiss"); Library.dismissSetup() }
                }
            }
        }
    }

    FolderDialog {
        id: folderPicker
        title: qsTr("选择音乐文件夹")
        onAccepted: Library.chooseFolder(selectedFolder)
    }
}
