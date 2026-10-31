import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as MonzeroComponents

// The simple way in: sell or buy XMZ for another coin. The trader picks a direction, a coin and
// an XMZ amount; the card finds the best offer that can fill it and shows, before anything else,
// what they send, what they receive and the 1% project fee. Starting the trade is the page's job
// (startRequested); the card never touches funds.
Rectangle {
    id: card

    // Offers from the verified market snapshot: {pair, side, priceAtomic, amountAtomic}.
    // pair "BTC/XMZ"; side "SELL" sells the coin for XMZ, "BUY" buys the coin with XMZ;
    // price is XMZ per one coin in XMZ atomic units (11 decimals); amount is in coin atomic units.
    property var offers: []
    property var coins: ["BTC", "LTC", "BCH", "BNB"]
    property var coinDecimals: ({ "BTC": 8, "LTC": 8, "BCH": 8, "BNB": 18 })
    property real xmzBalance: -1          // unlocked XMZ; -1 when unknown
    property real feePercent: 1
    property bool canStart: false
    property string startBlockedReason: ""

    property string direction: "sell"     // "sell" XMZ for the coin, or "buy" XMZ with it
    property string coin: coins.length ? coins[0] : ""
    property alias amountText: amountInput.text
    property bool reviewing: false
    // On the exchange screen the pair tabs choose the coin; the card then hides its own picker
    property bool showCoinPicker: true
    readonly property var quote: quoteFor(direction, coin, Number(amountInput.text), offers, coinDecimals[coin], feePercent)

    signal startRequested(var quote)

    color: MonzeroComponents.Style.surfaceRaised
    radius: 8
    border.color: MonzeroComponents.Style.accentMuted
    implicitHeight: column.implicitHeight + 40

    function normalizedPair(pair) { return String(pair).replace(/\s+/g, "").toUpperCase() }

    // The best single offer that can fill `xmz` (one offer per trade: atomic swaps are 1:1).
    // Returns {ok, reason, ...}; reasons are user-facing.
    function quoteFor(dir, coinCode, xmz, offerList, decimals, fee) {
        if (!coinCode) return { ok: false, reason: qsTr("Choose a coin.") }
        if (!(xmz > 0) || !isFinite(xmz)) return { ok: false, reason: "", empty: true }
        if (!(decimals >= 0)) return { ok: false, reason: qsTr("This market is not available.") }
        var wantSide = dir === "sell" ? "SELL" : "BUY"
        var pair = coinCode + "/XMZ"
        var best = null, largest = 0, count = 0
        for (var i = 0; i < (offerList || []).length; ++i) {
            var o = offerList[i]
            if (normalizedPair(o.pair) !== pair || String(o.side).toUpperCase() !== wantSide) continue
            var price = Number(o.priceAtomic) / 1e11                   // XMZ per coin
            var available = Number(o.amountAtomic) / Math.pow(10, decimals)
            if (!(price > 0) || !(available > 0)) continue
            count++
            largest = Math.max(largest, available * price)          // XMZ this offer covers
            if (xmz / price > available) continue
            // Selling XMZ: the lowest price gives the most coin. Buying: the highest costs least.
            if (!best || (dir === "sell" ? price < best.price : price > best.price)) best = { offer: o, price: price }
        }
        if (!count) return { ok: false, reason: qsTr("No offers for %1 right now.").arg(coinCode), noOffers: true }
        if (!best) return { ok: false, reason: qsTr("No single offer is that large. The largest covers %1 XMZ.").arg(group(trim(largest, 4))) }
        var gross = xmz / best.price
        var projectFee = gross * fee / 100
        return {
            ok: true, direction: dir, coin: coinCode, xmz: xmz, price: best.price, offer: best.offer,
            gross: gross, fee: projectFee,
            // Selling XMZ: the fee comes out of the coin received. Buying: it is added to what is paid.
            receive: dir === "sell" ? gross - projectFee : xmz,
            send: dir === "sell" ? xmz : gross + projectFee
        }
    }

    function trim(n, places) {
        var s = Number(n).toFixed(places)
        return s.indexOf(".") < 0 ? s : s.replace(/0+$/, "").replace(/\.$/, "")
    }
    // Thousands separators for display only (never for text the user edits)
    function group(text) {
        var parts = String(text).split(".")
        var whole = parts[0], out = ""
        while (whole.length > 3) { out = "," + whole.slice(-3) + out; whole = whole.slice(0, -3) }
        parts[0] = whole + out
        return parts.join(".")
    }
    function show(n, code) { return group(trim(n, code === "XMZ" ? 6 : 8)) + " " + code }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // Direction: Buy / Sell tabs, green and red as on an exchange
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [ { k: "buy", t: qsTr("Buy XMZ"), c: MonzeroComponents.Style.green }, { k: "sell", t: qsTr("Sell XMZ"), c: "#E5677A" } ]
                delegate: Rectangle {
                    objectName: "direction_" + modelData.k
                    property bool active: card.direction === modelData.k
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    implicitHeight: 40
                    radius: 6
                    color: active ? modelData.c : "transparent"
                    border.color: active ? modelData.c : MonzeroComponents.Style.accentMuted
                    Text {
                        id: dirLabel; anchors.centerIn: parent; text: modelData.t
                        font.family: MonzeroComponents.Style.fontBold.name; font.pixelSize: 15
                        color: parent.active ? "#062224" : MonzeroComponents.Style.defaultFontColor
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { card.direction = modelData.k; card.reviewing = false } }
                }
            }
        }

        // Coin
        MonzeroComponents.TextPlain { themeTransition: false;
            visible: card.showCoinPicker
            text: card.direction === "sell" ? qsTr("Receive") : qsTr("Pay with")
            color: MonzeroComponents.Style.dimmedFontColor; font.pixelSize: 13
        }
        Flow {
            visible: card.showCoinPicker
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: card.coins
                delegate: Rectangle {
                    objectName: "coin_" + modelData
                    property bool active: card.coin === modelData
                    width: 72; height: 34; radius: 6
                    color: active ? MonzeroComponents.Style.accentMuted : "transparent"
                    border.color: active ? MonzeroComponents.Style.accent : MonzeroComponents.Style.accentMuted
                    Text {
                        anchors.centerIn: parent; text: modelData
                        font.family: MonzeroComponents.Style.fontMedium.name; font.pixelSize: 14
                        color: MonzeroComponents.Style.defaultFontColor
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { card.coin = modelData; card.reviewing = false } }
                }
            }
        }

        // Amount
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            MonzeroComponents.LineEdit {
                id: amountInput
                objectName: "amountInput"
                Layout.fillWidth: true
                labelText: card.direction === "sell" ? qsTr("XMZ to sell") : qsTr("XMZ to buy")
                placeholderText: "0.0"
                validator: RegExpValidator { regExp: /^\d{0,12}(\.\d{0,11})?$/ }
                error: card.direction === "sell" && card.xmzBalance >= 0 && Number(text) > card.xmzBalance
                onTextChanged: card.reviewing = false
            }
            MonzeroComponents.StandardButton {
                objectName: "maxButton"
                Layout.alignment: Qt.AlignBottom
                visible: card.direction === "sell" && card.xmzBalance > 0
                small: true; primary: false
                text: qsTr("Max")
                onClicked: amountInput.text = card.trim(card.xmzBalance, 11)
            }
        }
        MonzeroComponents.TextPlain { themeTransition: false;
            visible: card.direction === "sell" && card.xmzBalance >= 0
            text: card.xmzBalance >= 0 && Number(amountInput.text) > card.xmzBalance
                ? qsTr("More than your available %1.").arg(card.show(card.xmzBalance, "XMZ"))
                : qsTr("Available: %1").arg(card.show(Math.max(card.xmzBalance, 0), "XMZ"))
            color: Number(amountInput.text) > card.xmzBalance ? MonzeroComponents.Style.errorColor : MonzeroComponents.Style.dimmedFontColor
            font.pixelSize: 13
        }

        // Quote
        Rectangle {
            objectName: "quoteBox"
            Layout.fillWidth: true
            visible: !card.quote.empty
            color: "transparent"; radius: 6
            border.color: MonzeroComponents.Style.accentMuted
            implicitHeight: quoteColumn.implicitHeight + 24
            ColumnLayout {
                id: quoteColumn
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                spacing: 6
                MonzeroComponents.TextPlain { themeTransition: false;
                    objectName: "quoteProblem"
                    visible: !card.quote.ok
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: card.quote.reason || ""
                    color: MonzeroComponents.Style.warningColor
                }
                Repeater {
                    model: card.quote.ok ? [
                        { k: qsTr("You send"), v: card.show(card.quote.send, card.direction === "sell" ? "XMZ" : card.coin), strong: false },
                        { k: qsTr("You receive"), v: card.show(card.quote.receive, card.direction === "sell" ? card.coin : "XMZ"), strong: true },
                        { k: qsTr("Rate"), v: qsTr("1 %1 = %2 XMZ").arg(card.coin).arg(card.group(card.trim(card.quote.price, 4))), strong: false },
                        { k: qsTr("Project fee (%1%)").arg(card.feePercent), v: card.show(card.quote.fee, card.coin), strong: false }
                    ] : []
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        MonzeroComponents.TextPlain { themeTransition: false; text: modelData.k; color: MonzeroComponents.Style.dimmedFontColor; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                        MonzeroComponents.TextPlain { themeTransition: false; color: MonzeroComponents.Style.defaultFontColor;
                            objectName: "quote_" + index
                            text: modelData.v; font.pixelSize: modelData.strong ? 18 : 14
                            font.family: modelData.strong ? MonzeroComponents.Style.fontBold.name : MonzeroComponents.Style.fontRegular.name
                        }
                    }
                }
                MonzeroComponents.TextPlain { themeTransition: false;
                    visible: card.quote.ok === true
                    Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
                    color: MonzeroComponents.Style.dimmedFontColor
                    text: qsTr("Network fees are paid on top by each side's wallet. The project fee goes to the Monzero project's donation address as part of the swap; nobody holds your coins.")
                }
            }
        }

        // Review, then start
        MonzeroComponents.StandardButton {
            objectName: "reviewButton"
            visible: !card.reviewing
            enabled: card.quote.ok === true && !amountInput.error
            text: qsTr("Review trade")
            onClicked: card.reviewing = true
        }
        ColumnLayout {
            objectName: "reviewPanel"
            visible: card.reviewing && card.quote.ok === true
            Layout.fillWidth: true
            spacing: 8
            MonzeroComponents.TextPlain { themeTransition: false; color: MonzeroComponents.Style.defaultFontColor;
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: qsTr("How this trade stays safe:") + "\n"
                    + qsTr("• It is an atomic swap: either both sides get paid or both get their coins back.") + "\n"
                    + qsTr("• Your coins move only after you approve, and only into the swap itself, never to the other trader or to Monzero.") + "\n"
                    + qsTr("• If the other side stops, your coins come back to you automatically after the refund time.")
            }
            RowLayout {
                spacing: 10
                MonzeroComponents.StandardButton {
                    objectName: "startButton"
                    enabled: card.canStart && card.quote.ok === true
                    text: card.direction === "sell" ? qsTr("Start: sell %1").arg(card.show(card.quote.xmz || 0, "XMZ"))
                                                    : qsTr("Start: buy %1").arg(card.show(card.quote.xmz || 0, "XMZ"))
                    onClicked: card.startRequested(card.quote)
                }
                MonzeroComponents.StandardButton {
                    primary: false
                    text: qsTr("Change")
                    onClicked: card.reviewing = false
                }
            }
            MonzeroComponents.TextPlain { themeTransition: false;
                objectName: "startBlocked"
                visible: !card.canStart && card.startBlockedReason !== ""
                Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 13
                color: MonzeroComponents.Style.warningColor
                text: card.startBlockedReason
            }
        }
    }
}
