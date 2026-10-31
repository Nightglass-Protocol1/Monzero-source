import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as MonzeroComponents

// One market at a glance, from verified data only: best prices, a chart (order-book depth, or the
// price of completed trades when the snapshot carries them) and the order book. Clicking an offer
// asks the page to fill the trade card with it (offerPicked); nothing else happens on a click.
Rectangle {
    id: panel

    // Same offer format as DexTradeCard: {pair, side, priceAtomic, amountAtomic}; price = XMZ per
    // coin (11 decimals), amount in coin atoms. SELL offers sell the coin (asks), BUY offers buy it.
    property var offers: []
    // Completed trades, oldest first: {time (unix seconds), priceAtomic}. Empty: no price chart.
    property var trades: []
    property string coin: "BTC"
    property int coinDecimals: 8
    property string chartMode: "depth"          // "depth" or "price"
    property int bookRows: 8

    readonly property var book: buildBook(offers, coin, coinDecimals)
    readonly property real bestAsk: book.asks.length ? book.asks[0].price : 0
    readonly property real bestBid: book.bids.length ? book.bids[0].price : 0

    signal offerPicked(var offer)

    color: MonzeroComponents.Style.surfaceRaised
    radius: 8
    border.color: MonzeroComponents.Style.accentMuted
    implicitHeight: column.implicitHeight + 40

    readonly property color askColor: "#E5677A"
    readonly property color bidColor: MonzeroComponents.Style.green

    // {asks: ascending price, bids: descending price}, each row {price, amount (coin), xmz, total, offer}
    function buildBook(list, coinCode, decimals) {
        var asks = [], bids = []
        var pair = coinCode + "/XMZ"
        for (var i = 0; i < (list || []).length; ++i) {
            var o = list[i]
            if (String(o.pair).replace(/\s+/g, "").toUpperCase() !== pair) continue
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
    function priceText(p) { return fmt(p, p >= 100 ? 2 : 6) }

    onBookChanged: chart.requestPaint()
    onTradesChanged: chart.requestPaint()
    onChartModeChanged: chart.requestPaint()

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // Header: market and best prices
        RowLayout {
            Layout.fillWidth: true
            spacing: 24
            MonzeroComponents.Label { fontSize: 20; text: panel.coin + " / XMZ" }
            Repeater {
                model: [
                    { k: qsTr("Best ask"), v: panel.priceText(panel.bestAsk), c: panel.askColor, n: "bestAsk" },
                    { k: qsTr("Best bid"), v: panel.priceText(panel.bestBid), c: panel.bidColor, n: "bestBid" },
                    { k: qsTr("Spread"), v: panel.bestAsk > 0 && panel.bestBid > 0
                          ? panel.fmt((panel.bestAsk - panel.bestBid) / panel.bestAsk * 100, 2) + "%" : "—",
                      c: MonzeroComponents.Style.defaultFontColor, n: "spread" }
                ]
                delegate: ColumnLayout {
                    spacing: 2
                    MonzeroComponents.TextPlain { themeTransition: false; text: modelData.k; font.pixelSize: 12; color: MonzeroComponents.Style.dimmedFontColor }
                    MonzeroComponents.TextPlain { themeTransition: false; objectName: modelData.n; text: modelData.v; font.pixelSize: 15; color: modelData.c }
                }
            }
            Item { Layout.fillWidth: true }
        }
        MonzeroComponents.TextPlain { themeTransition: false;
            text: qsTr("Prices in XMZ per %1").arg(panel.coin)
            font.pixelSize: 12; color: MonzeroComponents.Style.dimmedFontColor
        }

        // Chart
        RowLayout {
            spacing: 8
            Repeater {
                model: [ { k: "depth", t: qsTr("Depth") }, { k: "price", t: qsTr("Price") } ]
                delegate: Rectangle {
                    objectName: "chartMode_" + modelData.k
                    property bool active: panel.chartMode === modelData.k
                    implicitWidth: modeText.implicitWidth + 24; implicitHeight: 28; radius: 14
                    color: active ? MonzeroComponents.Style.accentMuted : "transparent"
                    border.color: MonzeroComponents.Style.accentMuted
                    Text {
                        id: modeText; anchors.centerIn: parent; text: modelData.t
                        font.family: MonzeroComponents.Style.fontMedium.name; font.pixelSize: 13
                        color: MonzeroComponents.Style.defaultFontColor
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.chartMode = modelData.k }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            Canvas {
                id: chart
                objectName: "chart"
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var w = width, h = height, pad = 26
                    ctx.strokeStyle = "#1E3A3F"; ctx.lineWidth = 1
                    for (var g = 0; g <= 4; ++g) {
                        var gy = pad / 2 + (h - pad * 1.5) * g / 4
                        ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke()
                    }
                    ctx.font = "11px sans-serif"; ctx.fillStyle = "#7FA3A6"
                    if (panel.chartMode === "price") {
                        var pts = []
                        for (var i = 0; i < panel.trades.length; ++i) {
                            var p = Number(panel.trades[i].priceAtomic) / 1e11, t = Number(panel.trades[i].time)
                            if (p > 0 && t > 0) pts.push({ t: t, p: p })
                        }
                        if (pts.length < 2) return
                        var t0 = pts[0].t, t1 = pts[pts.length - 1].t
                        var lo = pts[0].p, hi = pts[0].p
                        for (var a = 1; a < pts.length; ++a) { lo = Math.min(lo, pts[a].p); hi = Math.max(hi, pts[a].p) }
                        if (hi === lo) { hi *= 1.01; lo *= 0.99 }
                        var x = function (tt) { return t1 === t0 ? w / 2 : (tt - t0) / (t1 - t0) * (w - 60) }
                        var y = function (pp) { return pad / 2 + (hi - pp) / (hi - lo) * (h - pad * 1.5) }
                        ctx.strokeStyle = MonzeroComponents.Style.accent; ctx.lineWidth = 2
                        ctx.beginPath(); ctx.moveTo(x(pts[0].t), y(pts[0].p))
                        for (var b = 1; b < pts.length; ++b) ctx.lineTo(x(pts[b].t), y(pts[b].p))
                        ctx.stroke()
                        ctx.fillStyle = "#7FA3A6"
                        ctx.fillText(panel.priceText(hi), w - 56, pad / 2 + 10)
                        ctx.fillText(panel.priceText(lo), w - 56, h - pad)
                        ctx.fillText(Qt.formatDateTime(new Date(t0 * 1000), "d MMM HH:mm"), 2, h - 8)
                        ctx.fillText(Qt.formatDateTime(new Date(t1 * 1000), "d MMM HH:mm"), w - 140, h - 8)
                        return
                    }
                    // Depth: cumulative coin amount; bids rise to the left of the mid price, asks to the right
                    var asks = panel.book.asks, bids = panel.book.bids
                    if (!asks.length && !bids.length) return
                    var minP = bids.length ? bids[bids.length - 1].price : asks[0].price
                    var maxP = asks.length ? asks[asks.length - 1].price : bids[0].price
                    var span = Math.max(maxP - minP, maxP * 0.02)
                    minP -= span * 0.05; maxP += span * 0.05
                    var maxTotal = Math.max(asks.length ? asks[asks.length - 1].total : 0, bids.length ? bids[bids.length - 1].total : 0)
                    var px = function (pp) { return (pp - minP) / (maxP - minP) * w }
                    var py = function (tt) { return h - pad - tt / maxTotal * (h - pad * 1.5) }
                    var side = function (rows, color, fill) {
                        if (!rows.length) return
                        ctx.beginPath(); ctx.moveTo(px(rows[0].price), h - pad)
                        var prev = 0
                        for (var r = 0; r < rows.length; ++r) {
                            ctx.lineTo(px(rows[r].price), py(prev)); ctx.lineTo(px(rows[r].price), py(rows[r].total)); prev = rows[r].total
                        }
                        ctx.lineTo(rows === asks ? w : 0, py(prev)); ctx.lineTo(rows === asks ? w : 0, h - pad); ctx.closePath()
                        ctx.fillStyle = fill; ctx.fill(); ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.stroke()
                    }
                    side(bids, panel.bidColor, "rgba(85,214,167,0.18)")
                    side(asks, panel.askColor, "rgba(229,103,122,0.18)")
                    ctx.fillStyle = "#7FA3A6"
                    ctx.fillText(panel.priceText(minP), 2, h - 8)
                    ctx.fillText(panel.priceText(maxP), w - 60, h - 8)
                    ctx.fillText(panel.fmt(maxTotal, 4) + " " + panel.coin, 2, pad / 2 + 10)
                }
            }
            MonzeroComponents.TextPlain { themeTransition: false;
                objectName: "chartEmpty"
                anchors.centerIn: parent
                visible: panel.chartMode === "price" ? panel.trades.length < 2 : !(panel.book.asks.length || panel.book.bids.length)
                text: panel.chartMode === "price" ? qsTr("No completed trades yet: the price chart starts with the first trades.")
                                                  : qsTr("No offers for %1 yet.").arg(panel.coin)
                color: MonzeroComponents.Style.dimmedFontColor
            }
        }

        // Order book: asks above (best nearest the middle), bids below
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: [qsTr("Price (XMZ)"), qsTr("Amount (%1)").arg(panel.coin), qsTr("Total (XMZ)")]
                delegate: MonzeroComponents.TextPlain { themeTransition: false;
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                    text: modelData; font.pixelSize: 12; color: MonzeroComponents.Style.dimmedFontColor
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
                implicitHeight: 24
                Rectangle {
                    anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                    width: rowItem.width * rowItem.share; color: rowItem.tint; opacity: 0.14; radius: 2
                }
                RowLayout {
                    anchors.fill: parent
                    MonzeroComponents.TextPlain { themeTransition: false; Layout.fillWidth: true; Layout.preferredWidth: 1; text: panel.priceText(rowItem.row.price); color: rowItem.tint; font.pixelSize: 13 }
                    MonzeroComponents.TextPlain { themeTransition: false; color: MonzeroComponents.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 1; horizontalAlignment: Text.AlignRight; text: panel.fmt(rowItem.row.amount, 8); font.pixelSize: 13 }
                    MonzeroComponents.TextPlain { themeTransition: false; color: MonzeroComponents.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 1; horizontalAlignment: Text.AlignRight; text: panel.fmt(rowItem.row.xmz, 4); font.pixelSize: 13 }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.offerPicked(rowItem.row.offer) }
            }
        }
        ColumnLayout {
            objectName: "asks"
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                // Furthest ask first, so the best ask sits just above the middle line
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
            MonzeroComponents.TextPlain { themeTransition: false; color: MonzeroComponents.Style.defaultFontColor;
                objectName: "midPrice"
                text: panel.bestAsk > 0 && panel.bestBid > 0 ? qsTr("Mid %1 XMZ").arg(panel.priceText((panel.bestAsk + panel.bestBid) / 2)) : ""
                font.pixelSize: 14
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
        MonzeroComponents.TextPlain { themeTransition: false;
            visible: panel.book.asks.length + panel.book.bids.length > 0
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
            color: MonzeroComponents.Style.dimmedFontColor
            text: qsTr("Red: offers selling %1 for XMZ (you sell XMZ). Green: offers buying %1 with XMZ (you buy XMZ). Click an offer to fill the trade form.").arg(panel.coin)
        }
    }
}
