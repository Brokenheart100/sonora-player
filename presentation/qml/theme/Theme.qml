pragma Singleton
import QtQuick

// 全局设计令牌 —— 唯一真源（ARCHITECTURE.md §10.2）。
//
// 规矩（违反其中任何一条，漫画风就会散掉）：
//  · 线宽只有 stroke / strokeBold 两档；
//  · 硬阴影偏移全局只有一个值，hover/press 靠"收窄"而不是变模糊；
//  · accent 只用于"当前播放 / 当前选中"，accentAlt 只用于"可交互焦点"；
//  · 正文永远用 fontText，fontDisplay 只给标题与拟声词。
QtObject {
    // ── 颜色：黑白 + 两个强调色 ───────────────────────────────────────────
    readonly property color ink: "#12100E"   // 线稿 / 文字
    readonly property color inkSoft: "#3A3630"   // 次要文字
    readonly property color paper: "#F4EFE4"   // 纸底
    readonly property color paperAlt: "#E7DFCE"   // 次级纸底
    readonly property color paperHigh: "#FFFCF4"   // 面板底（比纸再亮一点）
    readonly property color screentone: "#C9C2B4"   // 网点灰
    readonly property color accent: "#D93A3A"   // 朱红：只用于"当前播放"
    readonly property color accentAlt: "#2E5F8A"   // 靛蓝：只用于"可交互焦点"

    // ── 线宽：全局只允许两档 ─────────────────────────────────────────────
    readonly property real stroke: 2.5
    readonly property real strokeBold: 4.0

    // ── 硬阴影：一个偏移贯穿全局 ─────────────────────────────────────────
    readonly property real shadowX: 4
    readonly property real shadowY: 4
    readonly property real shadowXPress: 2
    readonly property real shadowYPress: 2

    // ── 排版 ─────────────────────────────────────────────────────────────
    readonly property string fontDisplay: "ZCOOL KuaiLe"
    readonly property string fontText: "Microsoft YaHei UI"
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
