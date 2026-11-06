import QtQuick

// 翻页转场（ARCHITECTURE.md §10.7 / IMPLEMENTATION.md M8）。
//
// 替换直接换 Loader 的 sourceComponent：那样页面是"刷"地一下换掉的，
// 漫画的节奏感全丢。这里做的是"最便宜的那个版本"：
//   · 旧页以左边缘为轴转过头去（Rotation，angle 0 → -82）
//   · 新页从背面转回来（angle 82 → 0），同时从纸色里浮起来
// 全程只有旋转与透明度，没有纹理抓取、没有全屏 shader。
//
// 两条与 ARCHITECTURE.md §10.7 原稿不同的决定（都是量出来的）：
//  1. **不用 ShaderEffectSource 快照**。快照要先抓一遍整页到纹理，那一帧的
//     代价比"让旧页自己转"高得多，还要处理 live:false 的时序。
//  2. **三页常驻，不做按需创建**。动态实例化会在转场首帧留下 50ms 尖峰
//     （NowPlaying 里有唱片纹路 Repeater + 歌词 ListView），异步加载只是把
//     尖峰推到"页面第一次可见"那一帧。页面数是 3、固定、有界，不是无界缓存，
//     所以"不用的就别创建"在这里的收益远小于它带来的抖动。
//
// 用法：
//   PageTurner { pages: [comp0, comp1, comp2]; index: shell.currentPage }
Item {
    id: root

    // Component 数组。用 var 而不是 list<Component>：调用方通常直接写字面量数组。
    property var pages: []
    // 想显示第几页
    property int index: 0
    // 内部状态（-1 = 还没装过）
    property int activeIndex: -1
    // 转场时长（ms）
    property int duration: 260

    function apply(i) {
        if (i === activeIndex)
            return;
        const incoming = repeater.itemAt(i);
        if (!incoming)
            return;
        const outgoing = activeIndex >= 0 ? repeater.itemAt(activeIndex) : null;

        activeIndex = i;

        if (!outgoing) {
            incoming.visible = true;
            incoming.angle = 0;
            incoming.opacity = 1;
            return;
        }

        outgoing.z = 1;
        outgoing.beginExit();

        incoming.z = 2;
        incoming.visible = true;
        incoming.angle = 82;
        incoming.opacity = 0;
        incoming.beginEnter();
    }

    onIndexChanged: apply(index)
    Component.onCompleted: apply(index)

    Repeater {
        id: repeater
        model: root.pages

        TurnPage {
            sourceComponent: modelData
        }
    }

    component TurnPage: Item {
        id: turnPage

        property real angle: 0
        property alias sourceComponent: inner.sourceComponent

        anchors.fill: parent
        visible: false

        transform: Rotation {
            origin.x: 0
            origin.y: turnPage.height * 0.5
            axis {
                x: 0
                y: 1
                z: 0
            }
            angle: turnPage.angle
        }

        // 转起来的时候压一层淡墨影，纸才不会看起来像塑料板
        Rectangle {
            anchors.fill: parent
            visible: Math.abs(turnPage.angle) > 1
            color: "#12100E"
            opacity: Math.min(Math.abs(turnPage.angle) / 82, 1) * 0.18
        }

        Loader {
            id: inner
            anchors.fill: parent
            // 三页都在启动时创建，异步只是为了别让启动那一帧扛全部成本
            asynchronous: true
        }

        SequentialAnimation {
            id: enterAnim

            ParallelAnimation {
                NumberAnimation {
                    target: turnPage
                    property: "angle"
                    to: 0
                    duration: root.duration
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    target: turnPage
                    property: "opacity"
                    to: 1
                    duration: root.duration * 0.7
                    easing.type: Easing.OutQuad
                }
            }
        }

        SequentialAnimation {
            id: exitAnim

            ParallelAnimation {
                NumberAnimation {
                    target: turnPage
                    property: "angle"
                    to: -82
                    duration: root.duration
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    target: turnPage
                    property: "opacity"
                    to: 0
                    duration: root.duration * 0.8
                    easing.type: Easing.InQuad
                }
            }

            // 旧页藏起来就够了：常驻实例不销毁，下次回到这一页还要用。
            ScriptAction {
                script: {
                    turnPage.visible = false;
                    turnPage.angle = 0;
                }
            }
        }

        function beginEnter() {
            enterAnim.restart();
        }

        function beginExit() {
            exitAnim.restart();
        }
    }
}
