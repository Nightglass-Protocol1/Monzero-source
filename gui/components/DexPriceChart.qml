import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as M
import "dexExchange.js" as X

// The pair's price chart: candles (or a line) of completed swaps from the relays, with volume
// below, the best bid and ask as dashed lines and a crosshair that reads out each candle. With no
// completed swaps it still shows where the offers stand.
Rectangle {
    id: chart
    property string coin: "ETH"
    property var candles: []                 // relay candles, ascending
    property int interval: 3600
    property real bestBid: 0
    property real bestAsk: 0
    property real coinUsd: 0                 // the coin's USD price, for the read-out (0: unknown)
    property string mode: "candles"          // "candles" or "line"
    property bool loading: false
    property real now: Date.now() / 1000
    signal intervalPicked(int seconds)

    readonly property color up: M.Style.green
    readonly property color down: "#E5677A"
    readonly property var shown: X.chartCandles(candles, interval, now, 160)
    property int hover: -1

    color: M.Style.surfaceRaised
    radius: 8
    border.color: M.Style.accentMuted
    implicitHeight: 420

    onShownChanged: canvas.requestPaint()
    onModeChanged: canvas.requestPaint()
    onBestBidChanged: canvas.requestPaint()
    onBestAskChanged: canvas.requestPaint()
    onHoverChanged: canvas.requestPaint()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: chart.hover >= 0 && chart.hover < chart.shown.length ? chart.readout(chart.shown[chart.hover])
                      : qsTr("Price of 1 XMZ in %1").arg(chart.coin)
                color: M.Style.dimmedFontColor; font.pixelSize: 12; font.family: M.Style.fontMonoRegular.name
                Layout.fillWidth: true; elide: Text.ElideRight
            }
            Repeater {
                model: [ { k: "candles", t: qsTr("Candles") }, { k: "line", t: qsTr("Line") } ]
                delegate: chart.chip
            }
            Item { width: 8; height: 1 }
            Repeater {
                model: [ { s: 3600, t: "1H" }, { s: 14400, t: "4H" }, { s: 86400, t: "1D" } ]
                delegate: Rectangle {
                    width: 40; height: 24; radius: 4
                    color: chart.interval === modelData.s ? M.Style.accentMuted : "transparent"
                    border.color: M.Style.accentMuted
                    Text { anchors.centerIn: parent; text: modelData.t; font.pixelSize: 12; color: M.Style.defaultFontColor }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: chart.intervalPicked(modelData.s) }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Canvas {
                id: canvas
                objectName: "priceCanvas"
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                property real slot: 8
                property real x0: 0
                onPaint: chart.paint(getContext("2d"), width, height)
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onPositionChanged: {
                    var i = Math.floor((mouse.x - canvas.x0) / canvas.slot)
                    chart.hover = i >= 0 && i < chart.shown.length ? i : -1
                }
                onExited: chart.hover = -1
            }
            Text {
                anchors.centerIn: parent
                visible: chart.shown.length === 0
                width: parent.width * 0.8
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                color: M.Style.dimmedFontColor; font.pixelSize: 13
                text: chart.loading ? qsTr("Loading the chart…")
                      : qsTr("No completed %1/XMZ swaps yet. The chart starts with the first one; the dashed lines show the best offers now.").arg(chart.coin)
            }
        }
    }

    property Component chip: Rectangle {
        width: chipText.implicitWidth + 16; height: 24; radius: 4
        color: chart.mode === modelData.k ? M.Style.accentMuted : "transparent"
        border.color: M.Style.accentMuted
        Text { id: chipText; anchors.centerIn: parent; text: modelData.t; font.pixelSize: 12; color: M.Style.defaultFontColor }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: chart.mode = modelData.k }
    }

    function readout(c) {
        var when = Qt.formatDateTime(new Date(c.t * 1000), chart.interval >= 86400 ? "d MMM yyyy" : "d MMM HH:mm")
        var change = c.o > 0 ? (c.c - c.o) / c.o * 100 : 0
        var text = when + "  O " + X.price(c.o) + "  H " + X.price(c.h) + "  L " + X.price(c.l) + "  C " + X.price(c.c)
                 + "  " + (change >= 0 ? "+" : "") + change.toFixed(2) + "%"
        if (c.v > 0) text += "  " + qsTr("Vol %1 XMZ").arg(X.amount(c.v, 0))
        if (chart.coinUsd > 0) text += "  ≈ " + X.usd(c.c * chart.coinUsd)
        return text
    }

    function paint(ctx, w, h) {
        ctx.reset()
        var axis = 92, xAxis = 20
        var plotW = Math.max(10, w - axis), plotH = Math.max(10, h - xAxis)
        var priceTop = 8, priceBottom = plotH * 0.78, volTop = plotH * 0.84
        ctx.font = "11px sans-serif"
        ctx.strokeStyle = "#1E3A3F"; ctx.lineWidth = 1
        for (var g = 0; g <= 4; ++g) {
            var gy = Math.round(priceTop + (priceBottom - priceTop) * g / 4) + 0.5
            ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(plotW, gy); ctx.stroke()
        }
        var cs = chart.shown
        // The price range: candles and the best offers, so the offers' lines are always on screen
        var hi = 0, lo = Infinity, vmax = 0
        for (var i = 0; i < cs.length; ++i) { hi = Math.max(hi, cs[i].h); lo = Math.min(lo, cs[i].l); vmax = Math.max(vmax, cs[i].v) }
        var marks = []
        if (chart.bestAsk > 0) marks.push({ p: chart.bestAsk, color: chart.down, label: qsTr("ask") })
        if (chart.bestBid > 0) marks.push({ p: chart.bestBid, color: chart.up, label: qsTr("bid") })
        for (var m = 0; m < marks.length; ++m) { hi = Math.max(hi, marks[m].p); lo = Math.min(lo, marks[m].p) }
        if (!(hi > 0) || !isFinite(lo)) return
        var pad = (hi - lo) * 0.1 || hi * 0.05
        hi += pad; lo = Math.max(0, lo - pad)
        var y = function (p) { return priceTop + (hi - p) / (hi - lo) * (priceBottom - priceTop) }

        ctx.fillStyle = "#7FA3A6"
        for (var a = 0; a <= 4; ++a) {
            var tick = hi - (hi - lo) * a / 4
            ctx.fillText(tick > 0 ? X.price(tick) : "0", plotW + 6, priceTop + (priceBottom - priceTop) * a / 4 + 4)
        }

        var slot = cs.length ? Math.max(4, Math.min(16, plotW / Math.max(cs.length, 40))) : 8
        var count = Math.min(cs.length, Math.floor(plotW / slot))
        var first = cs.length - count
        var x0 = plotW - count * slot
        canvas.slot = slot; canvas.x0 = x0 - first * slot

        if (count > 0 && chart.mode === "line") {
            ctx.beginPath()
            for (var j = first; j < cs.length; ++j) {
                var lx = x0 + (j - first) * slot + slot / 2
                if (j === first) ctx.moveTo(lx, y(cs[j].c)); else ctx.lineTo(lx, y(cs[j].c))
            }
            ctx.strokeStyle = M.Style.accent; ctx.lineWidth = 2; ctx.stroke()
            ctx.lineTo(x0 + (cs.length - 1 - first) * slot + slot / 2, priceBottom); ctx.lineTo(x0 + slot / 2, priceBottom); ctx.closePath()
            ctx.fillStyle = "rgba(41,214,203,0.10)"; ctx.fill()
        }
        for (var k = first; k < cs.length; ++k) {
            var c = cs[k], rising = c.c >= c.o
            var color = c.flat ? "#3B5A5E" : rising ? chart.up : chart.down
            var cx = x0 + (k - first) * slot + slot / 2
            ctx.strokeStyle = color; ctx.fillStyle = color; ctx.lineWidth = 1
            if (chart.mode === "candles") {
                ctx.beginPath(); ctx.moveTo(Math.round(cx) + 0.5, y(c.h)); ctx.lineTo(Math.round(cx) + 0.5, y(c.l)); ctx.stroke()
                var top = y(Math.max(c.o, c.c)), body = Math.max(1, Math.abs(y(c.o) - y(c.c)))
                ctx.fillRect(Math.round(cx - slot * 0.35), top, Math.max(2, Math.round(slot * 0.7)), body)
            }
            if (vmax > 0 && c.v > 0) {
                var vh = c.v / vmax * (plotH - volTop)
                ctx.globalAlpha = 0.35
                ctx.fillRect(Math.round(cx - slot * 0.35), plotH - vh, Math.max(2, Math.round(slot * 0.7)), vh)
                ctx.globalAlpha = 1
            }
        }
        // Best offers now: dashed lines with their price on the axis
        for (var n = 0; n < marks.length; ++n) {
            var my = y(marks[n].p)
            ctx.strokeStyle = marks[n].color; ctx.lineWidth = 1
            ctx.setLineDash([5, 4]); ctx.beginPath(); ctx.moveTo(0, my); ctx.lineTo(plotW, my); ctx.stroke(); ctx.setLineDash([])
            ctx.globalAlpha = 0.85; ctx.fillStyle = marks[n].color
            ctx.fillRect(plotW + 2, my - 9, axis - 4, 18); ctx.globalAlpha = 1
            ctx.fillStyle = "#062224"; ctx.fillText(X.price(marks[n].p), plotW + 6, my + 4)
        }
        // The last trade price
        if (count > 0) {
            var last = cs[cs.length - 1], ly = y(last.c)
            ctx.fillStyle = M.Style.accent
            ctx.fillRect(plotW + 2, ly - 9, axis - 4, 18)
            ctx.fillStyle = "#062224"; ctx.fillText(X.price(last.c), plotW + 6, ly + 4)
        }
        // Crosshair
        if (chart.hover >= first && chart.hover < cs.length) {
            var hx = Math.round(x0 + (chart.hover - first) * slot + slot / 2) + 0.5
            ctx.strokeStyle = "rgba(255,255,255,0.25)"; ctx.setLineDash([2, 3])
            ctx.beginPath(); ctx.moveTo(hx, 0); ctx.lineTo(hx, plotH); ctx.stroke(); ctx.setLineDash([])
        }
        // Time axis
        if (count > 0) {
            ctx.fillStyle = "#7FA3A6"
            var fmt = chart.interval >= 86400 ? "d MMM" : "d MMM HH:mm"
            var ticks = Math.max(2, Math.min(6, Math.floor(plotW / 140)))
            for (var t = 0; t < ticks; ++t) {
                var idx = first + Math.round((count - 1) * t / Math.max(1, ticks - 1))
                var tx = Math.min(plotW - 70, Math.max(0, x0 + (idx - first) * slot - 30))
                ctx.fillText(Qt.formatDateTime(new Date(cs[idx].t * 1000), fmt), tx, h - 4)
            }
        }
    }
}
