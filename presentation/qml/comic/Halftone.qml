import QtQuick
import "../theme"

// 网点背景（ARCHITECTURE.md §10.3）。
//
// shader 由 `xmake qsb` 从 assets/shaders/dot.frag 编出，产物 dot.frag.qsb 入 qrc。
// **属性声明顺序必须和 dot.frag 里 uniform 块的顺序逐字对应**——Qt 按声明顺序
// 往 UBO 里塞数据，顺序错了不报错，只会画出一片错色。
//
// 整页最多两个全屏 shader（§10.4 第 4 条）。这里是唯一的一个。
//
// 注意：`live` 是 ShaderEffectSource 的属性，ShaderEffect 上写它会**整页加载失败**
// （报 "Cannot assign to non-existent property live"），而宿主程序只会打印
// "QQmlApplicationEngine 加载失败" 就退出。别再往这里加它。
ShaderEffect {
    id: root

    property real  dotSize: Theme.dotSize
    property real  angle: Theme.dotAngle
    property real  density: Theme.dotDensity
    property color ink: Theme.screentone
    property color paper: Theme.paper

    fragmentShader: "qrc:/shaders/dot.frag.qsb"
}
