import QtQuick
import QtQuick.Effects
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 「正在播放」页（M6）。
//
// 这里刻意**不做真封面**：封面字节只在导入时被 `ICoverStore` 落过一次盘，
// 而播放队列里的 `QueueEntry` 只带了标题/艺术家（播放只需要这两样）。
// 要把封面接上来，得让 `PlayQueue` 认识 coverPath——那是"播放器持有曲库字段"，
// 违反"两个模块只通过契约说话"。所以这一页画的是唱片本身：转起来、有歌名。
//
// 转不转由 `Player.playing` 决定：暂停时唱片停在原地，比"永远在转"诚实。
Item {
    id: page

    // 唱片转速：33⅓ 转/分 = 每圈 1.8 秒。这里慢一点，视觉上不晃眼。
    readonly property int spinMs: 5400

    // ── 集中线：只在换歌那一瞬间闪一下 ──────────────────────────────────
    // 用 Canvas 画，不用 `assets/shaders/lines.frag`——自定义 UBO 里的 `vec2`
    // 到不了 shader，实测渲染出来是一团同心红光晕而不是射线。
    //
    // 注意这里动画的是 `flash` 而不是 `opacity`：NumberAnimation 一旦作用在
    // `opacity` 上，就会把这个属性上的绑定打断，动画结束后永远停在 0——
    // 表现为"射线再也不出现"。留一个 `flash` 中间量，`opacity` 的绑定才活得下来。
    Canvas {
        id: burstLayer
        anchors.fill: parent
        opacity: flash
        visible: flash > 0.01

        // 0 = 不显示；换歌时由 burstAnim 推到 0.42 再回落
        property real flash: 0

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();

            const cx = record.x + record.width / 2;
            const cy = record.y + record.height / 2;
            const r0 = record.width * 0.50;   // 正好贴住唱片外缘
            const r1 = record.width * 0.95;

            // 文案 + 进度条那块是禁区：射线要么绕开，要么提前收笔。
            // 让它穿字而过，这一屏就会像"渲染坏了"而不是"有冲击力"。
            const keep = { x: -20, y: record.y + record.height + 4,
                           w: lyricsPanel.x - 8, h: page.height };

            // 射线进入禁区的最小参数 t（slab 法），进不去就是 Infinity
            function enterT(ox, oy, dx, dy) {
                let lo = 0, hi = 1e9;
                const ax = [[ox, dx, keep.x, keep.x + keep.w],
                            [oy, dy, keep.y, keep.y + keep.h]];
                for (let k = 0; k < 2; ++k) {
                    const o = ax[k][0], d = ax[k][1], a = ax[k][2], b = ax[k][3];
                    if (Math.abs(d) < 1e-6) {
                        if (o < a || o > b) return Infinity;
                        continue;
                    }
                    let t0 = (a - o) / d, t1 = (b - o) / d;
                    if (t0 > t1) { const s = t0; t0 = t1; t1 = s; }
                    lo = Math.max(lo, t0);
                    hi = Math.min(hi, t1);
                    if (lo > hi) return Infinity;
                }
                return lo > 0 ? lo : Infinity;
            }

            // 页面边界：唱片贴在左上角，往左/往上的射线本来就没地方走，
            // 与其让它们在半空被硬切成小残桩，不如按"还能走多远"收笔。
            const cxMin = -burstLayer.x, cxMax = page.width - burstLayer.x;
            const cyMin = -burstLayer.y, cyMax = page.height - burstLayer.y;

            function exitT(ox, oy, dx, dy) {
                let t = Infinity;
                if (dx > 1e-6) t = Math.min(t, (cxMax - ox) / dx);
                else if (dx < -1e-6) t = Math.min(t, (cxMin - ox) / dx);
                if (dy > 1e-6) t = Math.min(t, (cyMax - oy) / dy);
                else if (dy < -1e-6) t = Math.min(t, (cyMin - oy) / dy);
                return t;
            }

            ctx.fillStyle = Theme.ink;
            for (let i = 0; i < 28; ++i) {
                const a = (i * (360 / 28) + (i % 3 - 1) * 2.5) * Math.PI / 180;
                const ca = Math.cos(a), sa = Math.sin(a);

                // 唱臂压在唱片右上方：正对它的那几根从臂后露头，
                // 把起点推到臂外，省得在臂边上留一撮 8px 的碎屑
                // 唱臂那一小片角度干脆不画：起点被臂挡住、终点在臂外露出一撮，
                // 补丁式地"从臂后露头"总会在某个角度留下碎屑，不如留白。
                // `a` 落在 [0, 2π)，唱臂在右上（−41°..−13°）对应 5.56–6.02 rad
                if (a > 5.56 && a < 6.02)
                    continue;
                const start = r0 + (i % 4) * 2.5;
                let end = Math.min(r1, exitT(cx, cy, ca, sa) - 2);
                const hit = enterT(cx, cy, ca, sa);
                if (hit < end)
                    end = Math.max(start, hit - 8);
                if (end - start < 55)                   // 太短的残桩不如不画
                    continue;

                const w = 3.4 + (i % 3) * 1.8;          // 焦点处宽
                const tip = 0;                          // 收成真正的针尖：末端落在哪都不像被切断
                const px = -sa, py = ca;                // 法线

                ctx.beginPath();
                ctx.moveTo(cx + ca * start + px * w * 0.5, cy + sa * start + py * w * 0.5);
                ctx.lineTo(cx + ca * end + px * tip, cy + sa * end + py * tip);
                ctx.lineTo(cx + ca * end - px * tip, cy + sa * end - py * tip);
                ctx.lineTo(cx + ca * start - px * w * 0.5, cy + sa * start - py * w * 0.5);
                ctx.closePath();
                ctx.fill();
            }
        }
    }

    SequentialAnimation {
        id: burstAnim
        NumberAnimation {
            target: burstLayer
            property: "flash"
            from: 0
            to: 0.55
            duration: 110
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: burstLayer
            property: "flash"
            to: 0
            duration: 330
            easing.type: Easing.InQuad
        }
    }

    // ── 唱片 ────────────────────────────────────────────────────────────
    VinylDisc { id: record; spinMs: page.spinMs }

    // ── 唱针 ────────────────────────────────────────────────────────────
    Item {
        id: arm
        width: 96
        height: 150
        x: record.x + record.width - 46
        y: record.y + 6
        rotation: Player.playing ? 18 : 0

        Behavior on rotation {
            NumberAnimation { duration: Theme.animSpring; easing.type: Easing.OutBack }
        }

        Rectangle {
            width: 7
            height: 108
            x: 44
            y: 16
            radius: 3
            color: Theme.ink
        }
        Rectangle {
            width: 22
            height: 22
            x: 36
            y: 4
            radius: 11
            color: Theme.paperHigh
            border.width: 2
            border.color: Theme.ink
        }
        Rectangle {
            width: 30
            height: 16
            x: 33
            y: 116
            radius: 3
            color: Theme.accent
            border.width: 2
            border.color: Theme.ink
        }
    }

    // ── 看戏的小猫：跟着播放状态换表情 ──────────────────────────────────
    // 放在左下角：那里是页面上唯一不承载信息的空白，角色立绘在漫画里
    // 也就是干这个的——把空白填成"有人在看"。
    Mascot {
        anchors.left: parent.left
        anchors.leftMargin: 6
        anchors.bottom: parent.bottom
        width: 132
        height: 132
    }

    // ── 曲目信息与进度 ──────────────────────────────────────────────────
    Column {
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.right: lyricsPanel.left
        anchors.rightMargin: 28
        anchors.top: record.bottom
        anchors.topMargin: 26
        spacing: 10

        Text {
            width: parent.width
            text: Player.hasTrack ? Player.title : qsTr("还没有在放的东西")
            color: Theme.ink
            font.family: Fonts.display
            font.pixelSize: 30
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: {
                if (!Player.hasTrack) {
                    return qsTr("双击曲库里的任意一行开始")
                }
                const parts = []
                if (Player.artist.length > 0) parts.push(Player.artist)
                if (Player.queueText.length > 0) parts.push(Player.queueText)
                parts.push(Player.modeLabel)
                return parts.join("  ·  ")
            }
            color: Theme.inkSoft
            font.family: Theme.fontText
            font.pixelSize: Theme.textBody
            elide: Text.ElideRight
        }

        Row {
            spacing: 12

            Text {
                text: Player.positionText
                color: Theme.ink
                font.family: "Consolas"
                font.pixelSize: 18
            }
            Text {
                text: "/ " + Player.durationText
                color: Theme.inkSoft
                font.family: "Consolas"
                font.pixelSize: 15
            }
        }

        InkSlider {
            id: seek
            width: parent.width
            from: 0
            to: 1
            value: 0
            interactive: Player.hasTrack
            onCommitted: (v) => Player.seekFraction(v)
        }
    }

    // ── 歌词 ────────────────────────────────────────────────────────────
    // 整份加载、只推进"当前行"（见 LyricsModel.h 的两条说明）。
    // 当前行居中靠一段 contentY 动画：不用 Behavior on contentY，
    // 那会和用户拖动抢同一个属性。
    LyricsColumn { id: lyricsPanel }

    Connections {
        target: Player
        // 换歌 = 情绪上来的一瞬间，墨线从唱片四周放射出去。
        function onTrackChanged() {
            if (Player.hasTrack) {
                burstAnim.restart()
            }
        }
        function onPositionChanged() {
            if (!seek.pressed) {
                seek.value = Player.progress
            }
        }
    }

    Component.onCompleted: seek.value = Player.progress
}
