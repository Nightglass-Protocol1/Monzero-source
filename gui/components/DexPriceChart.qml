import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as M
import "dexExchange.js" as X

// The price chart. While the DEX is young most intervals hold one swap or none, so by default
// each completed swap is a dot whose area follows its XMZ amount; once intervals hold several
// swaps it shows candles (the chips switch either way). Prices are in USD across every pair (each
// swap at its coin's USD price then) or in the pair's own coin. NoirTrade's XMZ/USDT market is an
// amber step line, the best offers now are dashed lines, volume is below. A swap too small to
// matter (under 0.1% of the volume shown) does not stretch the price axis: it is an arrow at the
// edge with its price.
Rectangle {
    id: chart
    property string coin: "ETH"
    property var coins: ["ETH"]              // the pairs shown in USD
    property var trades: ({})                // coin -> completed swaps (as from X.trades)
    property var usd: ({})                   // coin -> USD price now
    property var usdHistory: ({})            // coin -> [[seconds, usd], …] hourly, recent days
    property var noir: null                  // NoirTrade's market: {last, stale, trades: [[ms, USDT per XMZ], …]}
    property int interval: 3600
    property real bestBid: 0                 // in the pair's coin
    property real bestAsk: 0
    property bool loading: false
    property real now: Date.now() / 1000
    property string unit: "USD"              // "USD" or "coin"
    property string kind: "auto"             // "auto", "trades" or "candles"
    signal intervalPicked(int seconds)

    readonly property bool inUsd: unit === "USD"
    readonly property string coinName: coin === "USDT-BSC" ? "USDT" : coin
    readonly property real coinUsd: X.isUsdt(coin) ? 1 : (usd[coin] || 0)
    readonly property int span: 120          // intervals across the chart, at most
    readonly property var points: X.chartPoints(trades, inUsd ? coins : [coin], inUsd ? "USD" : coin, usdHistory, usd)
    readonly property var noirPts: X.noirPoints(noir)
    // From the first swap or NoirTrade trade in the last `span` intervals, to the current one
    readonly property real tEnd: Math.floor(now / interval) * interval + interval
    readonly property real tStart: {
        var limit = tEnd - span * interval, first = Infinity
        for (var i = 0; i < points.length; ++i) if (points[i].t >= limit) { first = points[i].t; break }
        for (var j = 0; j < noirPts.length; ++j) if (noirPts[j].t >= limit) { first = Math.min(first, noirPts[j].t); break }
        return isFinite(first) ? Math.max(limit, Math.floor(first / interval) * interval - interval) : limit
    }
    readonly property var shown: points.filter(function (p) { return p.t >= tStart })
    readonly property var candles: X.candlesOf(shown, interval)
    readonly property string drawn: kind === "auto" ? X.autoKind(shown, candles) : kind
    readonly property var noirLine: inUsd || coinUsd > 0 ? X.noirSteps(noirPts, inUsd ? "USD" : coin, coin, usdHistory, usd[coin], interval, tStart, now) : []
    readonly property real bid: bestBid > 0 ? (inUsd ? bestBid * coinUsd : bestBid) : 0
    readonly property real ask: bestAsk > 0 ? (inUsd ? bestAsk * coinUsd : bestAsk) : 0
    readonly property color up: M.Style.green
    readonly property color down: "#E5677A"
    readonly property color noirColor: "#E8B04B"
    property int hover: -1                   // the swap (or candle) under the mouse
    property real hoverT: -1                 // the time under the mouse

    color: M.Style.surfaceRaised
    radius: 8
    border.color: M.Style.accentMuted
    implicitHeight: 420

    onShownChanged: canvas.requestPaint()
    onDrawnChanged: { hover = -1; canvas.requestPaint() }
    onNoirLineChanged: canvas.requestPaint()
    onBidChanged: canvas.requestPaint()
    onAskChanged: canvas.requestPaint()
    onHoverChanged: canvas.requestPaint()
    onHoverTChanged: canvas.requestPaint()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                text: chart.hoverT >= 0 ? chart.readout()
                      : chart.inUsd ? qsTr("Price of 1 XMZ in USD · all pairs") : qsTr("Price of 1 XMZ in %1").arg(chart.coinName)
                color: M.Style.dimmedFontColor; font.pixelSize: 12; font.family: M.Style.fontMonoRegular.name
                Layout.fillWidth: true; elide: Text.ElideRight
            }
            Row {
                visible: chart.noirLine.length > 0
                spacing: 5
                Rectangle { width: 14; height: 2; color: chart.noirColor; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: chart.noir && chart.noir.stale ? qsTr("NoirTrade (old)") : qsTr("NoirTrade")
                    color: M.Style.dimmedFontColor; font.pixelSize: 12
                    M.Tooltip { id: noirTip; anchors.fill: parent; text: chart.inUsd ? qsTr("The last trade on NoirTrade (XMZ/USDT)")
                                                                                   : qsTr("The last trade on NoirTrade (XMZ/USDT), in %1 at its USD price then").arg(chart.coinName) }
                    MouseArea { anchors.fill: parent; hoverEnabled: true; onEntered: noirTip.tooltipPopup.open(); onExited: noirTip.tooltipPopup.close() }
                }
            }
            Item { width: 4; height: 1 }
            Repeater {
                model: [ { k: "USD", t: "USD" }, { k: "coin", t: chart.coinName } ]
                delegate: chart.chip
            }
            Item { width: 4; height: 1 }
            Repeater {
                model: [ { k: "trades", t: qsTr("Trades") }, { k: "candles", t: qsTr("Candles") } ]
                delegate: chart.chip
            }
            Item { width: 4; height: 1 }
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
                // Where the last paint put things, for the mouse
                property real plotW: 10
                property var dots: []        // [{x, y, i}] swaps (or candles) on screen
                onPaint: chart.paint(getContext("2d"), width, height)
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onPositionChanged: {
                    if (mouse.x > canvas.plotW) { chart.hoverT = -1; chart.hover = -1; return }
                    chart.hoverT = chart.tStart + mouse.x / canvas.plotW * (chart.tEnd - chart.tStart)
                    var best = -1, bestD = 14 * 14
                    for (var i = 0; i < canvas.dots.length; ++i) {
                        var d = canvas.dots[i], dx = d.x - mouse.x, dy = chart.drawn === "candles" ? 0 : d.y - mouse.y
                        if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = d.i }
                    }
                    chart.hover = best
                }
                onExited: { chart.hover = -1; chart.hoverT = -1 }
            }
            Rectangle {
                anchors.fill: emptyText; anchors.margins: -6
                visible: emptyText.visible; radius: 4; color: chart.color; opacity: 0.85
            }
            Text {
                id: emptyText
                anchors.horizontalCenter: parent.horizontalCenter
                y: chart.noirLine.length ? 8 : (parent.height - height) / 2
                visible: chart.shown.length === 0
                width: parent.width * 0.8
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                color: M.Style.dimmedFontColor; font.pixelSize: 13
                text: chart.loading ? qsTr("Loading the chart…")
                      : (chart.inUsd ? qsTr("No completed swaps in this time range yet.") : qsTr("No completed %1/XMZ swaps in this time range yet.").arg(chart.coinName))
                        + (chart.noirLine.length ? " " + qsTr("The amber line is NoirTrade.") : "")
                        + (chart.bid > 0 || chart.ask > 0 ? " " + qsTr("The dashed lines are the best offers now.") : "")
            }
        }
    }

    property Component chip: Rectangle {
        readonly property bool on: modelData.k === "USD" ? chart.inUsd : modelData.k === "coin" ? !chart.inUsd : chart.drawn === modelData.k
        width: chipText.implicitWidth + 16; height: 24; radius: 4
        color: on ? M.Style.accentMuted : "transparent"
        border.color: M.Style.accentMuted
        Text { id: chipText; anchors.centerIn: parent; text: modelData.t; font.pixelSize: 12; color: M.Style.defaultFontColor }
        MouseArea {
            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (modelData.k === "USD" || modelData.k === "coin") chart.unit = modelData.k
                else chart.kind = modelData.k
            }
        }
    }

    function money(p) { return chart.inUsd ? "$" + X.price(p) : X.price(p) + " " + chart.coinName }
    function noirAt(t) {
        var p = null
        for (var i = 0; i < noirLine.length && noirLine[i].t <= t; ++i) p = noirLine[i].p
        return p
    }
    function readout() {
        var fmt = chart.interval >= 86400 ? "d MMM yyyy" : "d MMM HH:mm"
        var n = noirAt(chart.hoverT), noirText = n !== null ? "  " + qsTr("NoirTrade %1").arg(money(n)) : ""
        if (chart.drawn === "candles" && chart.hover >= 0 && chart.hover < chart.candles.length) {
            var c = chart.candles[chart.hover], change = c.o > 0 ? (c.c - c.o) / c.o * 100 : 0
            return Qt.formatDateTime(new Date(c.t * 1000), fmt) + "  O " + X.price(c.o) + "  H " + X.price(c.h) + "  L " + X.price(c.l)
                 + "  C " + X.price(c.c) + "  " + (change >= 0 ? "+" : "") + change.toFixed(2) + "%  "
                 + qsTr("%n swap(s)", "", c.n) + ", " + qsTr("%1 XMZ").arg(X.amount(c.v, 0)) + noirText
        }
        if (chart.drawn === "trades" && chart.hover >= 0 && chart.hover < chart.shown.length) {
            var s = chart.shown[chart.hover]
            return Qt.formatDateTime(new Date(s.t * 1000), "d MMM HH:mm") + "  " + qsTr("%1 XMZ at %2").arg(X.amount(s.xmz, 2)).arg(money(s.p))
                 + (chart.inUsd ? "  (" + (s.coin === "USDT-BSC" ? "USDT on BNB Chain" : s.coin) + ")" : "") + noirText
        }
        return Qt.formatDateTime(new Date(chart.hoverT * 1000), fmt) + noirText
    }

    function paint(ctx, w, h) {
        ctx.reset()
        var axis = 92, xAxis = 20
        var plotW = Math.max(10, w - axis), plotH = Math.max(10, h - xAxis)
        var priceTop = 8, priceBottom = plotH * 0.78, volTop = plotH * 0.84
        canvas.plotW = plotW
        ctx.font = "11px sans-serif"
        ctx.strokeStyle = "#1E3A3F"; ctx.lineWidth = 1
        for (var g = 0; g <= 4; ++g) {
            var gy = Math.round(priceTop + (priceBottom - priceTop) * g / 4) + 0.5
            ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(plotW, gy); ctx.stroke()
        }
        var t0 = chart.tStart, t1 = chart.tEnd
        var tx = function (t) { return (t - t0) / (t1 - t0) * plotW }
        var pts = chart.shown, cds = chart.candles, nl = chart.noirLine, candleMode = chart.drawn === "candles"
        var items = []
        if (candleMode) for (var a = 0; a < cds.length; ++a) items.push({ lo: cds[a].l, hi: cds[a].h, v: cds[a].v, t: cds[a].t + chart.interval / 2 })
        else for (var b = 0; b < pts.length; ++b) items.push({ lo: pts[b].p, hi: pts[b].p, v: pts[b].xmz, t: pts[b].t })
        var extras = [chart.bid, chart.ask]
        for (var e = 0; e < nl.length; ++e) extras.push(nl[e].p)
        var range = X.axisRange(items, extras)
        canvas.dots = []
        if (!range) return
        var hi = range.hi, lo = range.lo
        var pad = (hi - lo) * 0.1 || hi * 0.05
        hi += pad; lo = Math.max(0, lo - pad)
        var y = function (p) { return priceTop + (hi - p) / (hi - lo) * (priceBottom - priceTop) }

        ctx.fillStyle = "#7FA3A6"
        for (var k = 0; k <= 4; ++k) {
            var tick = hi - (hi - lo) * k / 4
            ctx.fillText(tick > 0 ? X.price(tick) : "0", plotW + 6, priceTop + (priceBottom - priceTop) * k / 4 + 4)
        }

        // Volume per interval, below
        var slotW = Math.max(2, chart.interval / (t1 - t0) * plotW * 0.7), vmax = 0
        for (var v = 0; v < cds.length; ++v) vmax = Math.max(vmax, cds[v].v)
        ctx.fillStyle = M.Style.accent; ctx.globalAlpha = 0.3
        for (var vv = 0; vv < cds.length; ++vv) {
            if (!(vmax > 0)) break
            var vh = Math.max(1, cds[vv].v / vmax * (plotH - volTop))
            ctx.fillRect(tx(cds[vv].t + chart.interval / 2) - slotW / 2, plotH - vh, slotW, vh)
        }
        ctx.globalAlpha = 1

        // NoirTrade: steps from each interval's last trade, carried to now
        if (nl.length) {
            ctx.strokeStyle = chart.noirColor; ctx.lineWidth = 1.5; ctx.beginPath()
            for (var q = 0; q < nl.length; ++q) {
                var qx = Math.max(0, tx(nl[q].t)), qy = y(nl[q].p)
                if (q === 0) ctx.moveTo(qx, qy); else { ctx.lineTo(qx, y(nl[q - 1].p)); ctx.lineTo(qx, qy) }
            }
            ctx.lineTo(plotW, y(nl[nl.length - 1].p)); ctx.stroke()
        }

        // The swaps, cut at the price area's edges
        ctx.save(); ctx.beginPath(); ctx.rect(0, priceTop, plotW, priceBottom - priceTop); ctx.clip()
        var dots = []
        if (candleMode) {
            for (var c = 0; c < cds.length; ++c) {
                var cd = cds[c], cx = tx(cd.t + chart.interval / 2), color = cd.c >= cd.o ? chart.up : chart.down
                ctx.strokeStyle = color; ctx.fillStyle = color; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(Math.round(cx) + 0.5, y(cd.h)); ctx.lineTo(Math.round(cx) + 0.5, y(cd.l)); ctx.stroke()
                ctx.fillRect(cx - slotW / 2, y(Math.max(cd.o, cd.c)), slotW, Math.max(1, Math.abs(y(cd.o) - y(cd.c))))
                dots.push({ x: cx, y: y(cd.c), i: c })
            }
        } else {
            var big = 0
            for (var m = 0; m < pts.length; ++m) big = Math.max(big, pts[m].xmz)
            for (var d = 0; d < pts.length; ++d) {
                var pt = pts[d], px = tx(pt.t), py = y(pt.p), r = 3 + 9 * Math.sqrt(pt.xmz / big)
                ctx.beginPath(); ctx.arc(px, py, r, 0, 2 * Math.PI)
                ctx.globalAlpha = 0.55; ctx.fillStyle = M.Style.accent; ctx.fill(); ctx.globalAlpha = 1
                ctx.lineWidth = chart.hover === d ? 2 : 1; ctx.strokeStyle = chart.hover === d ? "#FFFFFF" : M.Style.accent; ctx.stroke()
                dots.push({ x: px, y: py, i: d })
            }
        }
        ctx.restore()
        canvas.dots = dots

        // A swap off the scale: an arrow at the edge with its price
        ctx.fillStyle = "#9EB6BD"
        for (var o = 0; o < range.off.length; ++o) {
            var os = range.off[o], above = os.hi > range.hi, ox = tx(os.t), ey = above ? priceTop + 2 : priceBottom - 2, dir = above ? 1 : -1
            ctx.beginPath(); ctx.moveTo(ox, ey); ctx.lineTo(ox - 5, ey + dir * 7); ctx.lineTo(ox + 5, ey + dir * 7); ctx.closePath(); ctx.fill()
            var label = X.price(above ? os.hi : os.lo) + " · " + X.amount(os.v, 2) + " XMZ"
            var lw = ctx.measureText(label).width
            ctx.fillText(label, Math.max(0, Math.min(plotW - lw, ox - lw / 2)), ey + dir * 19)
        }

        // Best offers now: dashed lines
        var tags = []
        var offers = [{ p: chart.ask, color: chart.down }, { p: chart.bid, color: chart.up }]
        for (var n = 0; n < offers.length; ++n) {
            if (!(offers[n].p > 0)) continue
            var my = y(offers[n].p)
            ctx.strokeStyle = offers[n].color; ctx.lineWidth = 1
            ctx.setLineDash([5, 4]); ctx.beginPath(); ctx.moveTo(0, my); ctx.lineTo(plotW, my); ctx.stroke(); ctx.setLineDash([])
            tags.push({ y: my, color: offers[n].color, p: offers[n].p })
        }
        if (pts.length) tags.push({ y: y(pts[pts.length - 1].p), color: M.Style.accent, p: pts[pts.length - 1].p })
        if (nl.length) tags.push({ y: y(nl[nl.length - 1].p), color: chart.noirColor, p: nl[nl.length - 1].p })
        // Price tags on the axis, moved apart so none hides another
        tags.sort(function (p1, p2) { return p1.y - p2.y })
        for (var tg = 1; tg < tags.length; ++tg) if (tags[tg].y < tags[tg - 1].y + 19) tags[tg].y = tags[tg - 1].y + 19
        for (var tt = 0; tt < tags.length; ++tt) {
            ctx.globalAlpha = 0.9; ctx.fillStyle = tags[tt].color
            ctx.fillRect(plotW + 2, tags[tt].y - 9, axis - 4, 18); ctx.globalAlpha = 1
            ctx.fillStyle = "#062224"; ctx.fillText(X.price(tags[tt].p), plotW + 6, tags[tt].y + 4)
        }

        // Crosshair
        if (chart.hoverT >= t0) {
            var hx = Math.round(tx(chart.hoverT)) + 0.5
            ctx.strokeStyle = "rgba(255,255,255,0.25)"; ctx.setLineDash([2, 3])
            ctx.beginPath(); ctx.moveTo(hx, 0); ctx.lineTo(hx, plotH); ctx.stroke(); ctx.setLineDash([])
        }
        // Time axis
        ctx.fillStyle = "#7FA3A6"
        var fmt = t1 - t0 > 4 * 86400 ? "d MMM" : "d MMM HH:mm"
        var ticks = Math.max(2, Math.min(6, Math.floor(plotW / 140)))
        for (var ti = 0; ti < ticks; ++ti) {
            var at = t0 + (t1 - t0) * ti / (ticks - 1)
            var label2 = Qt.formatDateTime(new Date(at * 1000), fmt), w2 = ctx.measureText(label2).width
            ctx.fillText(label2, Math.min(plotW - w2, Math.max(0, tx(at) - w2 / 2)), h - 4)
        }
    }
}
