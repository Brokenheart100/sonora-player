import QtQuick
import "../theme"

// 拟声字（onomatopoeia）——漫画里"咚！""啪！"那种贴在画面上又弹开的效果。
//
// **池化的**，不是每次 new 一个。QML 里动态创建对象是 M8 最容易被忽略的坑：
// 播一首歌弹三次字、切歌切十次就是三十个对象，再加上它们的动画，垃圾回收
// 会在最不该卡的那一帧来找你。这里固定 6 个 Text 循环复用，一个都不多。
//
// 用法：`sfx.burst("咚！", x, y)`。位置是**父坐标系**，调用方自己算。
Item {
    id: root

    property int poolSize: 6
    property color ink: Theme.ink
    property color accent: Theme.accent

    // 轮转游标。不找"空闲的那一个"——6 个够用，找空闲反而要遍历。
    property int cursor: 0

    function burst(text, atX, atY, big) {
        if (poolSize <= 0)
            return
        const item = pool.itemAt(cursor % poolSize)
        cursor = (cursor + 1) % poolSize
        if (!item)
            return
        item.text = text
        item.baseY = atY
        item.x = atX - item.width * 0.5
        item.y = atY
        item.font.pixelSize = big ? 46 : 34
        // 每个字歪一点点，但歪多少由 cursor 决定，同一个池位每次都不一样
        item.rotation = ((cursor % 3) - 1) * 6
        item.color = (cursor % 2 === 0) ? accent : ink
        item.start()
    }

    Repeater {
        id: pool
        model: root.poolSize

        Text {
            id: sfx

            property real baseY: 0

            opacity: 0
            visible: opacity > 0.02
            font.family: Theme.fontDisplay
            font.pixelSize: 34
            style: Text.Outline
            styleColor: Theme.paperHigh
            // 描白边：墨字压在深色封面上也认得出来（笔画外扩 3px）
            transformOrigin: Item.Center
            antialiasing: true
            // 屏幕外的不参与绘制
            layer.enabled: false

            // 池位上的方法：delegate 内部的 id（pop）外面看不见，
            // 所以由 delegate 自己开一个入口，调用方只认 start()。
            function start() {
                pop.restart()
            }

            SequentialAnimation {
                id: pop

                ParallelAnimation {
                    NumberAnimation {
                        target: sfx
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 80
                    }
                    NumberAnimation {
                        target: sfx
                        property: "scale"
                        from: 0.45
                        to: 1.14
                        duration: 130
                        easing.type: Easing.OutBack
                    }
                }
                ParallelAnimation {
                    NumberAnimation {
                        target: sfx
                        property: "scale"
                        to: 1.0
                        duration: 90
                    }
                    NumberAnimation {
                        target: sfx
                        property: "y"
                        to: sfx.baseY - 30
                        duration: 520
                        easing.type: Easing.OutQuad
                    }
                    NumberAnimation {
                        target: sfx
                        property: "opacity"
                        to: 0
                        duration: 520
                        easing.type: Easing.InQuad
                    }
                }
            }
        }
    }
}
