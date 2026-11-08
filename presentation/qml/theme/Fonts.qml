pragma Singleton
import QtQuick
import Sonora.Presentation 1.0

// 字体装载。打包字体只有一个：站酷快乐体（标题 / 拟声词）。
// 正文用系统自带的微软雅黑，零体积（ARCHITECTURE.md §10.5）。
//
// 打包进去的是**裁切版**（84 KB，源字体 1479 KB）：tools/subset_fonts.py 只保留
// 所有 .qml 里出现过的字。加了新文案要重跑一次，否则新字掉回系统字体。
// 源字体留在 assets/fonts/ 下，别删——script 拿它做原料。
//
// 找不到打包字体时不要报错也不要崩——直接回落微软雅黑，界面依然成形。
// 这条回落是刻意的：字体属于"锦上添花"，不该阻断启动。
//
// 正文按语言换字族：英文用 Segoe UI（Windows 上最稳的拉丁正文），
// 中文用微软雅黑。站酷快乐体两套都带拉丁字形，标题不换。
QtObject {
    readonly property FontLoader displayLoader: FontLoader {
        source: "qrc:/fonts/ZCOOLKuaiLe-Subset.ttf"
    }

    readonly property bool displayReady: displayLoader.status === FontLoader.Ready
                                         && displayLoader.name.length > 0

    readonly property bool english: Appearance.language === "en"

    readonly property string display: displayReady ? displayLoader.name
                                                    : (english ? "Segoe UI" : "Microsoft YaHei UI")
    readonly property string text: english ? "Segoe UI" : "Microsoft YaHei UI"
}
