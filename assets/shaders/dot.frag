#version 440
// 网点（screentone）片元着色器 —— ARCHITECTURE.md §10.3 的 Halftone。
//
// 写的是 Qt 的 "custom shader" 格式，用 `xmake qsb` 编成 dot.frag.qsb 放进 qrc，
// QML 侧用 ShaderEffect { fragmentShader: "qrc:/shaders/dot.frag.qsb" }。
//
// 两条硬约束（改这个文件前先读）：
//  1. uniform 块的成员顺序必须和 Halftone.qml 里属性声明的顺序**逐字对应**，
//     Qt 是照属性声明顺序往 UBO 里塞数据的，顺序错了不会报错，只会画错。
//  2. 网点只做背景与阴影，绝不压在文字下面（§10.6 第 2 条）——所以这里输出
//     的是不透明底色 + 网点，不输出 alpha 合成。

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float dotSize;      // 一个网点格子的边长（像素）
    float angle;        // 网屏角度（度），印刷里常用 45°
    float density;      // 0..1，网点半径占比
    vec4 ink;           // 网点颜色
    vec4 paper;         // 底色
} ubuf;

layout(location = 0) out vec4 fragColor;

void main()
{
    // 用屏幕坐标而不是 uv：网点要"贴在纸上"，不跟着元素缩放而变形
    vec2 p = gl_FragCoord.xy / max(ubuf.dotSize, 0.5);

    float a = radians(ubuf.angle);
    float c = cos(a);
    float s = sin(a);
    mat2 rot = mat2(c, -s, s, c);
    vec2 r = rot * p;

    // 每个格子里一个圆点：离格心越近越黑
    vec2 cell = fract(r) - vec2(0.5);
    float d = length(cell);

    // uDensity 决定半径；0.06 是抗锯齿过渡带
    float radius = 0.5 * clamp(ubuf.density, 0.0, 1.0);
    float mask = 1.0 - smoothstep(radius - 0.06, radius + 0.06, d);

    // 网点最浓也只到 55%，否则纸底的呼吸感就没了
    vec3 rgb = mix(ubuf.paper.rgb, ubuf.ink.rgb, mask * 0.55);
    fragColor = vec4(rgb, 1.0) * ubuf.qt_Opacity;
}
