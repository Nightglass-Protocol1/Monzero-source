import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as MonzeroComponents

// One market, laid out like an exchange: a stats bar (last price, 24 h change, high/low, volume,
// best bid/ask), a candlestick chart of completed trades (or order-book depth), and the order book
// beside the chart on wide screens. Everything comes from verified data only: with no trades the
// chart says so instead of drawing candles. Clicking an offer asks the page to fill the trade form
// with it (offerPicked); nothing else happens on a click.
Rectangle {
    id: panel

    // Same offer format as DexTradeCard: {pair, side, priceAtomic, amountAtomic}; price = XMZ per
    // coin (11 decimals), amount in coin atoms. SELL offers sell the coin (asks), BUY offers buy it.
    property var offers: []
    // Completed trades, any order: {pair, time (unix seconds), priceAtomic, amountAtomic?}.
    property var trades: []
    property string coin: "BTC"
    property int coinDecimals: 8
    property string chartMode: "candles"        // "candles" or "depth"
    property int candleSeconds: 3600            // 1H, 4H or 1D
    property int bookRows: 8
    property real now: Date.now() / 1000        // the page refreshes this; tests set it

    readonly property var book: buildBook(offers, coin, coinDecimals)
    readonly property var pairTrades: tradesFor(trades, coin)
    readonly property var candles: buildCandles(pairTrades, candleSeconds, coinDecimals)
    readonly property var stats: dayStats(pairTrades, now, coinDecimals)
    readonly property real bestAsk: book.asks.length ? book.asks[0].price : 0
    readonly property real bestBid: book.bids.length ? book.bids[0].price : 0
    readonly property bool wide: width > 860

    signal offerPicked(var offer)

    color: MonzeroComponents.Style.surfaceRaised
    radius: 8
    border.color: MonzeroComponents.Style.accentMuted
    implicitHeight: column.implicitHeight + 32

    readonly property color askColor: "#E5677A"
    readonly property color bidColor: MonzeroComponents.Style.green

    function samePair(p, coinCode) { return String(p).replace(/\s+/g, "").toUpperCase() === coinCode + "/XMZ" }

    // {asks: ascending price, bids: descending price}, each row {price, amount (coin), xmz, total, offer}
    function buildBook(list, coinCode, decimals) {
        var asks = [], bids = []
        for (var i = 0; i < (list || []).length; ++i) {
            var o = list[i]
            if (!samePair(o.pair, coinCode)) continue
            var price = Number(o.priceAtomic) / 1e11
            var amount = Number(o.amountAtomic) / Math.pow(10, decimals)
            if (!(price > 0) || !(amount > 0) || !isFinite(price) || !isFinite(amount)) continue
            var row = { price: price, amount: amount, xmz: price * amount, offer: o }
            var side = String(o.side).toUpperCase()
            if (side === "SELL") asks.push(row)
            else if (side === "BUY") bids.push(row)
        }
        asks.sort(function (a, b) { return a.price - b.price })
        bids.sort(function (a, b) { return b.price - a.price })
        var t = 0
        for (var j = 0; j < asks.length; ++j) { t += asks[j].amount; asks[j].total = t }
        t = 0
        for (var k = 0; k < bids.length; ++k) { t += bids[k].amount; bids[k].total = t }
        return { asks: asks, bids: bids }
    }

    // This pair's trades, oldest first, with valid prices
    function tradesFor(list, coinCode) {
        var out = []
        for (var i = 0; i < (list || []).length; ++i) {
            var t = list[i]
            if (t.pair !== undefined && !samePair(t.pair, coinCode)) continue
            var p = Number(t.priceAtomic) / 1e11, time = Number(t.time)
            if (p > 0 && time > 0 && isFinite(p)) out.push({ time: time, price: p, amountAtomic: t.amountAtomic })
        }
        out.sort(function (a, b) { return a.time - b.time })
        return out
    }

    // OHLC + volume (coin) per bucket; buckets without trades are left out, not invented
    function buildCandles(list, seconds, decimals) {
        var out = [], cur = null
        for (var i = 0; i < list.length; ++i) {
            var t = list[i], start = Math.floor(t.time / seconds) * seconds
            var v = Number(t.amountAtomic) / Math.pow(10, decimals)
            if (!cur || cur.t !== start) { cur = { t: start, o: t.price, h: t.price, l: t.price, c: t.price, v: 0 }; out.push(cur) }
            cur.h = Math.max(cur.h, t.price); cur.l = Math.min(cur.l, t.price); cur.c = t.price
            if (v > 0 && isFinite(v)) cur.v += v
        }
        return out
    }

    // Last price and the 24 h window ending at `at`
    function dayStats(list, at, decimals) {
        if (!list.length) return { last: 0, change: NaN, high: 0, low: 0, volume: 0 }
        var last = list[list.length - 1].price, first = 0, high = 0, low = 0, volume = 0
        for (var i = 0; i < list.length; ++i) {
            var t = list[i]
            if (t.time < at - 86400) continue
            if (!first) first = t.price
            high = Math.max(high, t.price); low = low ? Math.min(low, t.price) : t.price
            var v = Number(t.amountAtomic) / Math.pow(10, decimals)
            if (v > 0 && isFinite(v)) volume += v
        }
        return { last: last, change: first ? (last - first) / first * 100 : NaN, high: high, low: low, volume: volume }
    }

    // Thousands separators for display only (never for text the user edits)
    function group(text) {
        var parts = String(text).split(".")
        var whole = parts[0], out = ""
        while (whole.length > 3) { out = "," + whole.slice(-3) + out; whole = whole.slice(0, -3) }
        parts[0] = whole + out
        return parts.join(".")
    }
    function fmt(n, places) {
        if (!(n > 0)) return "—"
        var s = Number(n).toFixed(places)
        return group(s.indexOf(".") < 0 ? s : s.replace(/0+$/, "").replace(/\.$/, ""))
    }
    function priceText(p) { return fmt(p, p >= 100 ? 0 : 6) }

    onCandlesChanged: chart.requestPaint()
    onBookChanged: chart.requestPaint()
    onChartModeChanged: chart.requestPaint()

    ColumnLayout {
        id: column
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 16
        spacing: 12

        // Stats bar
        Flow {
            Layout.fillWidth: true
            spacing: 24
            ColumnLayout {
                spacing: 0
                MonzeroComponents.TextPlain { themeTransition: false; text: panel.coin + " / XMZ"; font.pixelSize: 18; color: MonzeroComponents.Style.defaultFontColor }
                MonzeroComponents.TextPlain { themeTransition: false; text: qsTr("XMZ per %1").arg(panel.coin); font.pixelSize: 11; color: MonzeroComponents.Style.dimmedFontColor }
            }
            Repeater {
                model: [
                    { k: qsTr("Last price"), n: "lastPrice", v: panel.priceText(panel.stats.last),
                      c: isNaN(panel.stats.change) ? MonzeroComponents.Style.defaultFontColor : panel.stats.change >= 0 ? panel.bidColor : panel.askColor },
                    { k: qsTr("24h change"), n: "change24h", v: isNaN(panel.stats.change) ? "—" : (panel.stats.change >= 0 ? "+" : "") + panel.stats.change.toFixed(2) + "%",
                      c: isNaN(panel.stats.change) ? MonzeroComponents.Style.defaultFontColor : panel.stats.change >= 0 ? panel.bidColor : panel.askColor },
                    { k: qsTr("24h high"), n: "high24h", v: panel.priceText(panel.stats.high), c: MonzeroComponents.Style.defaultFontColor },
                    { k: qsTr("24h low"), n: "low24h", v: panel.priceText(panel.stats.low), c: MonzeroComponents.Style.defaultFontColor },
                    { k: qsTr("24h volume (%1)").arg(panel.coin), n: "volume24h", v: panel.fmt(panel.stats.volume, 6), c: MonzeroComponents.Style.defaultFontColor },
                    { k: qsTr("Best bid"), n: "bestBid", v: panel.priceText(panel.bestBid), c: panel.bidColor },
                    { k: qsTr("Best ask"), n: "bestAsk", v: panel.priceText(panel.bestAsk), c: panel.askColor }
                ]
                delegate: ColumnLayout {
                    spacing: 2
                    MonzeroComponents.TextPlain { themeTransition: false; text: modelData.k; font.pixelSize: 11; color: MonzeroComponents.Style.dimmedFontColor }
                    MonzeroComponents.TextPlain { themeTransition: false; objectName: modelData.n; text: modelData.v; font.pixelSize: 14; color: modelData.c }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: MonzeroComponents.Style.accentMuted; opacity: 0.6 }

        GridLayout {
            Layout.fillWidth: true
            columns: panel.wide ? 2 : 1
            columnSpacing: 16; rowSpacing: 12

            // Chart
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8
                RowLayout {
                    spacing: 6
                    Repeater {
                        model: [ { k: "candles", t: qsTr("Candles") }, { k: "depth", t: qsTr("Depth") } ]
                        delegate: Rectangle {
                            objectName: "chartMode_" + modelData.k
                            property bool active: panel.chartMode === modelData.k
                            implicitWidth: modeText.implicitWidth + 22; implicitHeight: 26; radius: 13
                            color: active ? MonzeroComponents.Style.accentMuted : "transparent"
                            border.color: MonzeroComponents.Style.accentMuted
                            Text { id: modeText; anchors.centerIn: parent; text: modelData.t; font.family: MonzeroComponents.Style.fontMedium.name; font.pixelSize: 12; color: MonzeroComponents.Style.defaultFontColor }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.chartMode = modelData.k }
                        }
                    }
                    Item { Layout.fillWidth: true; implicitWidth: 16 }
                    Repeater {
                        model: panel.chartMode === "candles" ? [ { s: 3600, t: "1H" }, { s: 14400, t: "4H" }, { s: 86400, t: "1D" } ] : []
                        delegate: Text {
                            objectName: "timeframe_" + modelData.t
                            text: modelData.t
                            font.family: MonzeroComponents.Style.fontMedium.name; font.pixelSize: 12
                            color: panel.candleSeconds === modelData.s ? MonzeroComponents.Style.accent : MonzeroComponents.Style.dimmedFontColor
                            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: panel.candleSeconds = modelData.s }
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: panel.wide ? 380 : 260
                    Canvas {
                        id: chart
                        objectName: "chart"
                        anchors.fill: parent
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            var w = width, h = height, axis = 78, xAxis = 20
                            var plotW = w - axis, plotH = h - xAxis
                            ctx.font = "11px sans-serif"
                            var grid = function (top, bottom) {
                                ctx.strokeStyle = "#1E3A3F"; ctx.lineWidth = 1
                                for (var g = 0; g <= 4; ++g) {
                                    var gy = Math.round(top + (bottom - top) * g / 4) + 0.5
                                    ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(plotW, gy); ctx.stroke()
                                }
                            }
                            if (panel.chartMode === "candles") {
                                var cs = panel.candles
                                grid(6, plotH * 0.78)
                                if (!cs.length) return
                                // Show the latest candles that fit, at least 6 px each
                                var slot = Math.max(6, Math.min(18, plotW / Math.max(cs.length, 20)))
                                var count = Math.min(cs.length, Math.floor(plotW / slot))
                                var shown = cs.slice(cs.length - count)
                                var hi = shown[0].h, lo = shown[0].l, vmax = 0
                                for (var i = 0; i < shown.length; ++i) { hi = Math.max(hi, shown[i].h); lo = Math.min(lo, shown[i].l); vmax = Math.max(vmax, shown[i].v) }
                                var pad = (hi - lo) * 0.08 || hi * 0.01
                                hi += pad; lo -= pad
                                var priceTop = 6, priceBottom = plotH * 0.78, volTop = plotH * 0.82
                                var y = function (p) { return priceTop + (hi - p) / (hi - lo) * (priceBottom - priceTop) }
                                var x0 = plotW - shown.length * slot
                                for (var j = 0; j < shown.length; ++j) {
                                    var c = shown[j], up = c.c >= c.o
                                    var color = up ? panel.bidColor : panel.askColor
                                    var cx = x0 + j * slot + slot / 2
                                    ctx.strokeStyle = color; ctx.fillStyle = color; ctx.lineWidth = 1
                                    ctx.beginPath(); ctx.moveTo(Math.round(cx) + 0.5, y(c.h)); ctx.lineTo(Math.round(cx) + 0.5, y(c.l)); ctx.stroke()
                                    var top = y(Math.max(c.o, c.c)), body = Math.max(1, Math.abs(y(c.o) - y(c.c)))
                                    ctx.fillRect(Math.round(cx - slot * 0.35), top, Math.max(2, Math.round(slot * 0.7)), body)
                                    if (vmax > 0) {
                                        var vh = c.v / vmax * (plotH - volTop)
                                        ctx.globalAlpha = 0.35
                                        ctx.fillRect(Math.round(cx - slot * 0.35), plotH - vh, Math.max(2, Math.round(slot * 0.7)), vh)
                                        ctx.globalAlpha = 1
                                    }
                                }
                                // Price axis on the right and the last price marker
                                ctx.fillStyle = "#7FA3A6"
                                for (var a = 0; a <= 4; ++a) {
                                    var p = hi - (hi - lo) * a / 4
                                    ctx.fillText(panel.priceText(p), plotW + 6, priceTop + (priceBottom - priceTop) * a / 4 + 4)
                                }
                                var last = shown[shown.length - 1]
                                var ly = y(last.c)
                                ctx.strokeStyle = last.c >= last.o ? panel.bidColor : panel.askColor
                                ctx.setLineDash([4, 3]); ctx.beginPath(); ctx.moveTo(0, ly); ctx.lineTo(plotW, ly); ctx.stroke(); ctx.setLineDash([])
                                ctx.fillStyle = last.c >= last.o ? panel.bidColor : panel.askColor
                                ctx.fillRect(plotW + 2, ly - 9, axis - 4, 18)
                                ctx.fillStyle = "#062224"; ctx.fillText(panel.priceText(last.c), plotW + 6, ly + 4)
                                // Time axis: first, middle and last candle
                                ctx.fillStyle = "#7FA3A6"
                                var fmtT = panel.candleSeconds >= 86400 ? "d MMM" : "d MMM HH:mm"
                                var marks = [0, Math.floor(shown.length / 2), shown.length - 1]
                                for (var m = 0; m < marks.length; ++m) {
                                    var mx = Math.min(plotW - 80, Math.max(0, x0 + marks[m] * slot - 30))
                                    ctx.fillText(Qt.formatDateTime(new Date(shown[marks[m]].t * 1000), fmtT), mx, h - 4)
                                }
                                return
                            }
                            // Depth: cumulative coin amount; bids rise to the left of the mid price, asks to the right
                            grid(6, plotH)
                            var asks = panel.book.asks, bids = panel.book.bids
                            if (!asks.length && !bids.length) return
                            var minP = bids.length ? bids[bids.length - 1].price : asks[0].price
                            var maxP = asks.length ? asks[asks.length - 1].price : bids[0].price
                            var span = Math.max(maxP - minP, maxP * 0.02)
                            minP -= span * 0.05; maxP += span * 0.05
                            var maxTotal = Math.max(asks.length ? asks[asks.length - 1].total : 0, bids.length ? bids[bids.length - 1].total : 0)
                            var px = function (pp) { return (pp - minP) / (maxP - minP) * plotW }
                            var py = function (tt) { return plotH - tt / maxTotal * (plotH - 12) }
                            var side = function (rows, color, fill, toRight) {
                                if (!rows.length) return
                                ctx.beginPath(); ctx.moveTo(px(rows[0].price), plotH)
                                var prev = 0
                                for (var r = 0; r < rows.length; ++r) {
                                    ctx.lineTo(px(rows[r].price), py(prev)); ctx.lineTo(px(rows[r].price), py(rows[r].total)); prev = rows[r].total
                                }
                                ctx.lineTo(toRight ? plotW : 0, py(prev)); ctx.lineTo(toRight ? plotW : 0, plotH); ctx.closePath()
                                ctx.fillStyle = fill; ctx.fill(); ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.stroke()
                            }
                            side(bids, panel.bidColor, "rgba(85,214,167,0.18)", false)
                            side(asks, panel.askColor, "rgba(229,103,122,0.18)", true)
                            ctx.fillStyle = "#7FA3A6"
                            ctx.fillText(panel.priceText(minP), 2, h - 4)
                            ctx.fillText(panel.priceText(maxP), plotW - 70, h - 4)
                            ctx.fillText(panel.fmt(maxTotal, 4) + " " + panel.coin, plotW + 6, 16)
                        }
                    }
                    MonzeroComponents.TextPlain {
                        themeTransition: false
                        objectName: "chartEmpty"
                        anchors.centerIn: parent
                        width: parent.width * 0.8; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        visible: panel.chartMode === "candles" ? panel.candles.length === 0 : !(panel.book.asks.length || panel.book.bids.length)
                        text: panel.chartMode === "candles" ? qsTr("No completed %1/XMZ trades yet. Candles appear with the first trades.").arg(panel.coin)
                                                            : qsTr("No offers for %1 yet.").arg(panel.coin)
                        color: MonzeroComponents.Style.dimmedFontColor
                    }
                }
            }

            // Order book: asks above (best nearest the middle), bids below
            ColumnLayout {
                objectName: "orderBook"
                Layout.fillWidth: !panel.wide
                Layout.preferredWidth: panel.wide ? 300 : -1
                Layout.alignment: Qt.AlignTop
                spacing: 4
                MonzeroComponents.TextPlain { themeTransition: false; text: qsTr("Order book"); font.pixelSize: 14; color: MonzeroComponents.Style.defaultFontColor }
                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: [qsTr("Price (XMZ)"), qsTr("Amount (%1)").arg(panel.coin), qsTr("Total (XMZ)")]
                        delegate: MonzeroComponents.TextPlain {
                            themeTransition: false
                            Layout.fillWidth: true; Layout.preferredWidth: 1
                            horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                            text: modelData; font.pixelSize: 11; color: MonzeroComponents.Style.dimmedFontColor
                        }
                    }
                }
                Component {
                    id: bookRow
                    Item {
                        id: rowItem
                        property var row
                        property color tint
                        property real share: 0
                        Layout.fillWidth: true
                        implicitHeight: 22
                        Rectangle {
                            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                            width: rowItem.width * rowItem.share; color: rowItem.tint; opacity: 0.14; radius: 2
                        }
                        RowLayout {
                            anchors.fill: parent
                            MonzeroComponents.TextPlain { themeTransition: false; Layout.fillWidth: true; Layout.preferredWidth: 1; text: panel.priceText(rowItem.row.price); color: rowItem.tint; font.pixelSize: 12 }
                            MonzeroComponents.TextPlain { themeTransition: false; Layout.fillWidth: true; Layout.preferredWidth: 1; horizontalAlignment: Text.AlignRight; text: panel.fmt(rowItem.row.amount, 8); font.pixelSize: 12; color: MonzeroComponents.Style.defaultFontColor }
                            MonzeroComponents.TextPlain { themeTransition: false; Layout.fillWidth: true; Layout.preferredWidth: 1; horizontalAlignment: Text.AlignRight; text: panel.fmt(rowItem.row.xmz, 0); font.pixelSize: 12; color: MonzeroComponents.Style.defaultFontColor }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.offerPicked(rowItem.row.offer) }
                    }
                }
                ColumnLayout {
                    objectName: "asks"
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: panel.book.asks.slice(0, panel.bookRows).reverse()
                        delegate: Loader {
                            Layout.fillWidth: true
                            sourceComponent: bookRow
                            onLoaded: { item.row = modelData; item.tint = panel.askColor; item.share = modelData.total / panel.book.asks[Math.min(panel.bookRows, panel.book.asks.length) - 1].total }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4; Layout.bottomMargin: 4
                    MonzeroComponents.TextPlain {
                        themeTransition: false
                        objectName: "midPrice"
                        text: panel.bestAsk > 0 && panel.bestBid > 0 ? qsTr("Spread %1%").arg(((panel.bestAsk - panel.bestBid) / panel.bestAsk * 100).toFixed(2)) : qsTr("No offers yet")
                        font.pixelSize: 12; color: MonzeroComponents.Style.dimmedFontColor
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: MonzeroComponents.Style.accentMuted }
                }
                ColumnLayout {
                    objectName: "bids"
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: panel.book.bids.slice(0, panel.bookRows)
                        delegate: Loader {
                            Layout.fillWidth: true
                            sourceComponent: bookRow
                            onLoaded: { item.row = modelData; item.tint = panel.bidColor; item.share = modelData.total / panel.book.bids[Math.min(panel.bookRows, panel.book.bids.length) - 1].total }
                        }
                    }
                }
            }
        }
        MonzeroComponents.TextPlain {
            themeTransition: false
            visible: panel.book.asks.length + panel.book.bids.length > 0
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11
            color: MonzeroComponents.Style.dimmedFontColor
            text: qsTr("Red: offers selling %1 for XMZ (you sell XMZ). Green: offers buying %1 with XMZ (you buy XMZ). Click an offer to fill the trade form.").arg(panel.coin)
        }
    }
}
