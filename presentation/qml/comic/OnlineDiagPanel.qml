import QtQuick
import "../comic"
import "../theme"
import Sonora.Presentation 1.0

// 在线页的诊断面板：展开时展示降级留痕（连续失败、令牌余量、缓存占用、最近错误）。
// 抽成组件是因为它和主列表没有一行共用逻辑，只在"想排查"的时候才展开。
    MangaPanel {
        id: root
        visible: false
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.top: parent.top
        anchors.topMargin: 60
        width: 320
        height: diagColumn.implicitHeight + 56
        padding: 14
        fillColor: Theme.paperHigh

        Timer {
            running: root.visible
            interval: 2000
            repeat: true
            onTriggered: {
                Online.refreshDiagnostics()
            }
        }

        Column {
            id: diagColumn
            width: parent.width
            spacing: 6

            Text {
                text: qsTr("在线诊断")
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.textBody
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    var d = Online.diagnostics
                    if (!d || d.provider === undefined)
                        return qsTr("暂无数据")
                    return qsTr("连续失败 %1 次 · 令牌 %2/%3")
                        .arg(d.consecutiveFailures).arg(d.tokensLeft.toFixed(1)).arg(d.tokenBurst)
                }
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    var d = Online.diagnostics
                    if (!d || d.cacheRows === undefined)
                        return ""
                    return qsTr("搜索缓存 %1/%2 行 · %3/%4 KB（LRU）")
                        .arg(d.cacheRows).arg(d.capRows)
                        .arg(Math.round(d.cacheBytes / 1024)).arg(Math.round(d.capBytes / 1024))
                }
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: {
                    var g = Online.diagnostics.degradations
                    if (!g || g.totalErrors === undefined)
                        return ""
                    return qsTr("降级：离线 %1 · 超时 %2 · 限流 %3 · 未登录 %4 · 上游 %5 · 地址过期 %6 · 缓存命中 %7")
                        .arg(g.offline).arg(g.timeout).arg(g.rateLimited).arg(g.unauthenticated)
                        .arg(g.upstream).arg(g.urlExpired).arg(g.cacheHits)
                }
                color: Theme.inkSoft
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.screentone
            }

            Text {
                text: qsTr("最近错误")
                color: Theme.ink
                font.family: Theme.fontText
                font.pixelSize: Theme.textSmall
            }

            Repeater {
                model: {
                    var d = Online.diagnostics
                    return (d && d.recentErrors) ? d.recentErrors : []
                }

                delegate: Text {
                    required property var modelData
                    width: diagColumn.width
                    wrapMode: Text.WordWrap
                    // "搜索 · 请求太频繁，稍后再试"
                    text: modelData.where + " · " + modelData.message
                    color: Theme.accent
                    font.family: Theme.fontText
                    font.pixelSize: Theme.textSmall
                }
            }
        }
    }
