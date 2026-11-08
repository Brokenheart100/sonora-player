pragma Singleton
import QtQuick
import Sonora.Presentation 1.0

// 全局设计令牌 —— 唯一真源（ARCHITECTURE.md §10.2）。
//
// 规矩（违反其中任何一条，漫画风就会散掉）：
//  · 线宽只有 stroke / strokeBold 两档；
//  · 硬阴影偏移全局只有一个值，hover/press 靠"收窄"而不是变模糊；
//  · accent 只用于"当前播放 / 当前选中"，accentAlt 只用于"可交互焦点"；
//  · 正文永远用 fontText，fontDisplay 只给标题与拟声词。
//
// 两套配色（浅色 = 暖纸，深色 = 夜墨）只换颜色，不换线宽/排版/动效：
// 漫画风的骨架是**线稿 + 网点 + 硬阴影**，深浅模式的区别只是"纸"和"墨"对调。
// 分叉点只有一个 —— `Theme.dark`，它来自 Appearance（跟随系统 / 手动三态）。
// QML 里不要再写死十六进制颜色，需要新颜色就往这里加一个令牌。
QtObject {
    readonly property bool dark: Appearance.dark

    // ── 颜色：黑白 + 两个强调色 ───────────────────────────────────────────
    readonly property color ink: dark ? "#EDE6D6" : "#12100E"   // 线稿 / 文字
    readonly property color inkSoft: dark ? "#A9A08D" : "#3A3630"   // 次要文字
    readonly property color paper: dark ? "#141210" : "#F4EFE4"   // 纸底
    readonly property color paperAlt: dark ? "#1D1A16" : "#E7DFCE"   // 次级纸底
    readonly property color paperHigh: dark ? "#24201A" : "#FFFCF4"   // 面板底
    readonly property color screentone: dark ? "#3B372F" : "#C9C2B4"   // 网点灰
    readonly property color accent: dark ? "#E75B52" : "#D93A3A"   // 朱红：只用于"当前播放"
    readonly property color accentAlt: dark ? "#7FA8CC" : "#2E5F8A"   // 靛蓝：只用于"可交互焦点"

    // ── 派生的功能性颜色（页面里不要再另写十六进制）─────────────────────
    readonly property color shadow: dark ? "#000000" : "#12100E"   // 硬阴影：深色下用纯黑，不然会发光
    readonly property color halftoneInk: dark ? "#2A2721" : "#B9B1A2"   // 背景网点
    readonly property color grooveA: dark ? "#6A6152" : "#4A453D"   // 唱片纹路（奇数圈）
    readonly property color grooveB: dark ? "#464034" : "#2B2822"   // 唱片纹路（偶数圈）
    readonly property color scrim: "#33000000"   // 弹出面板遮罩，两套配色通用
    readonly property color onAccent: dark ? "#FFF3EE" : "#FFE3DC"   // 朱红底上的文字

    // ── 线宽：全局只允许两档 ─────────────────────────────────────────────
    readonly property real stroke: 2.5
    readonly property real strokeBold: 4.0

    // ── 硬阴影：一个偏移贯穿全局 ─────────────────────────────────────────
    readonly property real shadowX: 4
    readonly property real shadowY: 4
    readonly property real shadowXPress: 2
    readonly property real shadowYPress: 2

    // ── 排版 ─────────────────────────────────────────────────────────────
    readonly property string fontDisplay: Fonts.display
    readonly property string fontText: Fonts.text
    readonly property int textBody: 14
    readonly property int textSmall: 12
    readonly property int textTitle: 22
    readonly property int textSfx: 40

    // ── 网点默认参数（dot.frag 的 uniform）────────────────────────────────
    readonly property real dotSize: 3.0
    readonly property real dotAngle: 45
    readonly property real dotDensity: 0.55

    // ── 动效时长 ─────────────────────────────────────────────────────────
    readonly property int animFast: 120
    readonly property int animBase: 220
    readonly property int animSpring: 380
}
