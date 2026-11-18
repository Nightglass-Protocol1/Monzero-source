import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as M
import "dexExchange.js" as X

// Buy or sell XMZ, as on an exchange. Market: take the best offer that fills the amount now (one
// offer makes one swap). Limit: post your own offer at your price for others to take. Every order
// is shown again in plain words before anything is sent; the swap itself still waits for the
// trade's approval, and coins stay yours until it completes or refunds.
Rectangle {
    id: form
    property string coin: "ETH"
    property int coinDecimals: 18
    property var book: ({ asks: [], bids: [], bestAsk: 0, bestBid: 0 })
    property real coinUsd: 0
    property real coinBalance: -1          // the trade account's coin (-1: not known yet)
    property real xmzBalance: -1           // the trading wallet's unlocked XMZ (-1: not known yet)
    property string blockedReason: ""      // why no order can be placed now ("" when it can)
    // function(coinText, xmzText) -> why the operator caps refuse an order of that size ("" when allowed)
    property var limitCheck: null
    property int limitTick: 0              // bumped when the caps document changes, to check again
    property bool busy: false
    property string message: ""            // the engine's last answer
    property string side: "buy"            // "buy" or "sell" XMZ
    property string type: "market"        // "market" or "limit"
    property string pickedId: ""           // an offer picked in the book (market orders take only it)
    property bool reviewing: false

    signal takeRequested(string listingId, string xmz)           // xmz "" = all of the offer
    signal postRequested(string side, string coinAmount, string xmz, int hours, string minXmz, bool renew)
    signal clearPick()

    readonly property bool buying: side === "buy"
    readonly property color sideColor: buying ? M.Style.green : "#E5677A"
    readonly property real amountAtoms: X.xmzAtoms(amountInput.text)
    readonly property var plan: type === "market" ? X.planMarket(book, side, amountAtoms, pickedId) : null
    readonly property real limitPrice: Number(priceInput.text)
    readonly property string limitCoin: amountAtoms > 0 && limitPrice > 0 ? X.limitCoin(limitPrice, amountAtoms, coinDecimals) : ""
    readonly property real minXmz: partialToggle.on ? Number(minInput.text || X.defaultMin(amountAtoms)) : 0
    // How long a limit order stays open: a preset, or custom (an amount in hours or days)
    property int presetHours: 6
    property bool customLife: false
    property string customUnit: "d"
    readonly property int hours: customLife ? X.customHours(lifeInput.text, customUnit) : presetHours

    // What the order pays and gets, for the button and the review
    readonly property real payCoin: type === "market" ? (plan && plan.ok ? plan.coin : 0) : Number(limitCoin || 0)
    readonly property string problem: {
        if (blockedReason !== "") return blockedReason
        if (amountInput.text.trim() === "") return qsTr("Enter the XMZ amount")
        if (amountAtoms <= 0) return qsTr("Check the XMZ amount")
        if (type === "market") {
            if (!plan || !plan.ok) return ""
            if (buying && coinBalance >= 0 && plan.coin > coinBalance) return ""   // funded during the trade: a note, not a block
            if (!buying && xmzBalance >= 0 && plan.atoms / 1e11 > xmzBalance) return qsTr("Not enough XMZ in your trading wallet")
            return capProblem(X.plain(plan.coin, coinDecimals), X.atomsText(plan.atoms))
        }
        if (!(limitPrice > 0)) return qsTr("Enter your price")
        if (hours < 1) return qsTr("An offer stays open from 1 hour to 31 days")
        if (limitCoin === "" || Number(limitCoin) <= 0) return qsTr("The total is too small")
        if (partialToggle.on && (!(minXmz >= 1) || minXmz * 1e11 > amountAtoms)) return qsTr("The smallest part must be at least 1 XMZ and at most the amount")
        if (!buying && xmzBalance >= 0 && amountAtoms / 1e11 > xmzBalance) return qsTr("Not enough XMZ in your trading wallet")
        return capProblem(limitCoin, X.atomsText(amountAtoms))
    }
    function capProblem(coinText, xmzText) {
        limitTick   // re-check when the caps document changes
        if (typeof limitCheck !== "function" || !coinText || !xmzText) return ""
        return limitCheck(coinText, xmzText) || ""
    }
    readonly property bool ready: problem === "" && (type === "limit" || (plan && plan.ok))
    readonly property var summary: summaryRows()
    readonly property string note: matchNote()
    readonly property bool suggestionShown: type === "market" && !!plan && !plan.ok && !!plan.suggestion && amountInput.text.trim() !== ""

    function setAmount(atoms) { amountInput.text = X.atomsText(atoms) }
    function fillFrom(row) {
        // A picked offer: trade against it now, for all of it or the part you choose
        side = row.side === "sell-xmz" ? "buy" : "sell"
        type = "market"
        setAmount(row.atoms)
        priceInput.text = X.plain(row.price, 12)
        reviewing = false
    }
    function percent(p) {
        if (buying) {
            var price = type === "limit" ? limitPrice : book.bestAsk
            if (!(coinBalance > 0) || !(price > 0)) return
            setAmount(Math.floor(coinBalance * p / price * 1e11 / 1e9) * 1e9)      // to 0.01 XMZ, down
        } else {
            if (!(xmzBalance > 0)) return
            setAmount(Math.floor(xmzBalance * p * 1e11 / 1e9) * 1e9)
        }
    }
    onSideChanged: { reviewing = false; if (type === "limit") priceInput.text = defaultPrice() }
    onTypeChanged: { reviewing = false; if (type === "limit" && priceInput.text === "") priceInput.text = defaultPrice() }
    onCoinChanged: { amountInput.text = ""; priceInput.text = ""; reviewing = false }
    function defaultPrice() {
        // From `side` itself: in a change handler the bindings that follow it may not be updated yet
        var p = side === "buy" ? (book.bestBid || book.bestAsk) : (book.bestAsk || book.bestBid)
        return p > 0 ? X.plain(p, 12) : ""
    }

    color: M.Style.surfaceRaised
    radius: 8
    border.color: M.Style.accentMuted
    implicitHeight: col.implicitHeight + 28

    ColumnLayout {
        id: col
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 14
        spacing: 10

        // Buy | Sell
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [ { k: "buy", t: qsTr("Buy XMZ"), c: M.Style.green }, { k: "sell", t: qsTr("Sell XMZ"), c: "#E5677A" } ]
                delegate: Rectangle {
                    objectName: "side_" + modelData.k
                    Layout.fillWidth: true; implicitHeight: 38
                    radius: 6
                    color: form.side === modelData.k ? modelData.c : "transparent"
                    border.color: form.side === modelData.k ? modelData.c : M.Style.accentMuted
                    Text { anchors.centerIn: parent; text: modelData.t; font.pixelSize: 15; font.family: M.Style.fontMedium.name
                           color: form.side === modelData.k ? "#062224" : M.Style.defaultFontColor }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: form.side = modelData.k }
                }
            }
        }
        // Market | Limit
        RowLayout {
            spacing: 18
            Repeater {
                model: [ { k: "market", t: qsTr("Market") }, { k: "limit", t: qsTr("Limit") } ]
                delegate: Column {
                    objectName: "type_" + modelData.k
                    spacing: 3
                    Text { text: modelData.t; font.pixelSize: 14; color: form.type === modelData.k ? M.Style.defaultFontColor : M.Style.dimmedFontColor
                           MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: form.type = modelData.k } }
                    Rectangle { width: parent.width; height: 2; color: form.type === modelData.k ? M.Style.accent : "transparent" }
                }
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
            text: form.type === "market"
                  ? (form.buying ? qsTr("Buy now from the best offer selling XMZ.") : qsTr("Sell now to the best offer buying XMZ."))
                  : (form.buying ? qsTr("Post an offer to buy XMZ at your price. It waits in the book until someone takes it.")
                                 : qsTr("Post an offer to sell XMZ at your price. It waits in the book until someone takes it."))
        }

        // Price (limit only)
        ColumnLayout {
            Layout.fillWidth: true; spacing: 4
            visible: form.type === "limit"
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Price, %1 per XMZ").arg(form.coin); font.pixelSize: 12; color: M.Style.dimmedFontColor; Layout.fillWidth: true }
                Text {
                    text: form.coinUsd > 0 && form.limitPrice > 0 ? "≈ " + X.usd(form.limitPrice * form.coinUsd) + qsTr(" per XMZ") : ""
                    font.pixelSize: 12; color: M.Style.dimmedFontColor
                }
            }
            M.LineEdit {
                id: priceInput; objectName: "limitPrice"
                Layout.fillWidth: true; fontSize: 15; inputPaddingTop: 8; inputPaddingBottom: 8
                placeholderText: form.coin; placeholderFontSize: 15
                validator: RegExpValidator { regExp: /^[0-9]{0,6}(\.[0-9]{0,18})?$/ }
                onTextChanged: form.reviewing = false
            }
            RowLayout {
                spacing: 6
                Repeater {
                    model: [ { t: qsTr("Best bid"), p: form.book.bestBid }, { t: qsTr("Best ask"), p: form.book.bestAsk } ]
                    delegate: Rectangle {
                        visible: modelData.p > 0
                        width: bt.implicitWidth + 14; height: 22; radius: 4; color: "transparent"; border.color: M.Style.accentMuted
                        Text { id: bt; anchors.centerIn: parent; text: modelData.t + " " + X.price(modelData.p); font.pixelSize: 11; color: M.Style.dimmedFontColor }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: priceInput.text = X.plain(modelData.p, 12) }
                    }
                }
            }
        }

        // Amount
        ColumnLayout {
            Layout.fillWidth: true; spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Amount, XMZ"); font.pixelSize: 12; color: M.Style.dimmedFontColor; Layout.fillWidth: true }
                Text {
                    objectName: "available"
                    font.pixelSize: 12; color: M.Style.dimmedFontColor
                    text: form.buying ? qsTr("Available: %1 %2").arg(form.coinBalance >= 0 ? X.amount(form.coinBalance, 6) : "…").arg(form.coin)
                                      : qsTr("Available: %1 XMZ").arg(form.xmzBalance >= 0 ? X.amount(form.xmzBalance, 2) : "…")
                }
            }
            M.LineEdit {
                id: amountInput; objectName: "orderAmount"
                Layout.fillWidth: true; fontSize: 15; inputPaddingTop: 8; inputPaddingBottom: 8
                placeholderText: "XMZ"; placeholderFontSize: 15
                validator: RegExpValidator { regExp: /^[0-9]{0,9}(\.[0-9]{0,11})?$/ }
                onTextChanged: form.reviewing = false
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Repeater {
                    model: [0.25, 0.5, 0.75, 1]
                    delegate: Rectangle {
                        Layout.fillWidth: true; implicitHeight: 24; radius: 4; color: "transparent"; border.color: M.Style.accentMuted
                        Text { anchors.centerIn: parent; text: modelData === 1 ? qsTr("Max") : (modelData * 100) + "%"; font.pixelSize: 11; color: M.Style.dimmedFontColor }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: form.percent(modelData) }
                    }
                }
            }
        }

        // Partial fills and lifetime (limit only)
        ColumnLayout {
            Layout.fillWidth: true; spacing: 6
            visible: form.type === "limit"
            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    id: partialToggle; objectName: "partialToggle"
                    property bool on: true
                    width: 34; height: 18; radius: 9
                    color: on ? M.Style.accentMuted : "#20383C"
                    Rectangle { width: 14; height: 14; radius: 7; y: 2; x: partialToggle.on ? 18 : 2; color: partialToggle.on ? M.Style.accent : M.Style.dimmedFontColor }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: partialToggle.on = !partialToggle.on }
                }
                Text { text: qsTr("Allow partial fills"); font.pixelSize: 13; color: M.Style.defaultFontColor; Layout.fillWidth: true }
                Text { visible: partialToggle.on; text: qsTr("from"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
                M.LineEdit {
                    id: minInput; objectName: "minPart"
                    visible: partialToggle.on
                    Layout.preferredWidth: 90; Layout.maximumHeight: 34; fontSize: 13; inputPaddingTop: 4; inputPaddingBottom: 4
                    placeholderText: form.amountAtoms > 0 ? String(X.defaultMin(form.amountAtoms)) : "1"; placeholderFontSize: 13
                    validator: RegExpValidator { regExp: /^[0-9]{0,9}(\.[0-9]{0,11})?$/ }
                }
                Text { visible: partialToggle.on; text: "XMZ"; font.pixelSize: 12; color: M.Style.dimmedFontColor }
            }
            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    id: renewToggle; objectName: "renewToggle"
                    property bool on: false
                    width: 34; height: 18; radius: 9
                    color: on ? M.Style.accentMuted : "#20383C"
                    Rectangle { width: 14; height: 14; radius: 7; y: 2; x: renewToggle.on ? 18 : 2; color: renewToggle.on ? M.Style.accent : M.Style.dimmedFontColor }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { form.reviewing = false; renewToggle.on = !renewToggle.on } }
                }
                Text {
                    text: qsTr("Keep open: renew automatically while this wallet runs"); font.pixelSize: 13; color: M.Style.defaultFontColor
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
            }
            Text { text: qsTr("Open for"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [ { h: 1, t: qsTr("1 h") }, { h: 6, t: qsTr("6 h") }, { h: 24, t: qsTr("1 day") },
                             { h: 168, t: qsTr("1 week") }, { h: 744, t: qsTr("1 month") }, { h: 0, t: qsTr("Custom") } ]
                    delegate: Rectangle {
                        objectName: "life_" + modelData.h
                        readonly property bool active: modelData.h === 0 ? form.customLife : !form.customLife && form.presetHours === modelData.h
                        width: lifeText.implicitWidth + 16; height: 24; radius: 4
                        color: active ? M.Style.accentMuted : "transparent"; border.color: M.Style.accentMuted
                        Text { id: lifeText; anchors.centerIn: parent; text: modelData.t; font.pixelSize: 11; color: M.Style.defaultFontColor }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: { form.reviewing = false; if (modelData.h === 0) form.customLife = true; else { form.customLife = false; form.presetHours = modelData.h } }
                        }
                    }
                }
            }
            RowLayout {
                visible: form.customLife
                Layout.fillWidth: true; spacing: 6
                M.LineEdit {
                    id: lifeInput; objectName: "customLife"
                    Layout.preferredWidth: 90; Layout.maximumHeight: 34; fontSize: 13; inputPaddingTop: 4; inputPaddingBottom: 4
                    placeholderText: form.customUnit === "d" ? "14" : "48"; placeholderFontSize: 13
                    validator: RegExpValidator { regExp: /^[0-9]{0,4}(\.[0-9]{0,2})?$/ }
                    onTextChanged: form.reviewing = false
                }
                Repeater {
                    model: [ { u: "h", t: qsTr("hours") }, { u: "d", t: qsTr("days") } ]
                    delegate: Rectangle {
                        objectName: "lifeUnit_" + modelData.u
                        width: unitText.implicitWidth + 16; height: 24; radius: 4
                        color: form.customUnit === modelData.u ? M.Style.accentMuted : "transparent"; border.color: M.Style.accentMuted
                        Text { id: unitText; anchors.centerIn: parent; text: modelData.t; font.pixelSize: 11; color: M.Style.defaultFontColor }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { form.customUnit = modelData.u; form.reviewing = false } }
                    }
                }
                Text { text: qsTr("up to 31 days"); font.pixelSize: 11; color: M.Style.dimmedFontColor; Layout.fillWidth: true }
            }
        }

        // The match (market) or the total (limit)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: summary.implicitHeight + 16
            visible: form.summary.length > 0 || form.note !== "" || form.suggestionShown || (form.pickedId !== "" && form.type === "market")
            radius: 6; color: "#0B1E22"
            ColumnLayout {
                id: summary
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    visible: form.pickedId !== "" && form.type === "market"
                    Text { text: qsTr("Trading against the offer you picked"); font.pixelSize: 11; color: M.Style.accent; Layout.fillWidth: true }
                    Text { text: qsTr("any offer ×"); font.pixelSize: 11; color: M.Style.dimmedFontColor
                           MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: form.clearPick() } }
                }
                Repeater {
                    model: form.summary
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        Text { text: modelData[0]; font.pixelSize: 12; color: M.Style.dimmedFontColor; Layout.fillWidth: true }
                        Text { text: modelData[1]; font.pixelSize: 13; color: M.Style.defaultFontColor; font.family: M.Style.fontMonoRegular.name; horizontalAlignment: Text.AlignRight }
                    }
                }
                Text {
                    objectName: "matchNote"
                    Layout.fillWidth: true; Layout.preferredWidth: summary.width; wrapMode: Text.WordWrap; font.pixelSize: 12
                    color: M.Style.warningColor
                    visible: text !== ""
                    text: form.note
                }
                Text {
                    Layout.fillWidth: true; Layout.preferredWidth: summary.width; wrapMode: Text.WordWrap; font.pixelSize: 12
                    color: M.Style.accent
                    visible: form.suggestionShown
                    text: visible ? qsTr("Use %1 XMZ").arg(X.amount(form.plan.suggestion.atoms / 1e11, 4)) : ""
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: form.setAmount(form.plan.suggestion.atoms) }
                }
            }
        }

        // Review, then send
        ColumnLayout {
            Layout.fillWidth: true; spacing: 8
            visible: form.reviewing
            Text {
                objectName: "reviewText"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.defaultFontColor
                text: form.reviewText()
            }
        }
        Rectangle {
            objectName: "orderButton"
            Layout.fillWidth: true; implicitHeight: 44; radius: 6
            readonly property bool active: form.ready && !form.busy
            color: active ? form.sideColor : "#20383C"
            Text {
                anchors.centerIn: parent; width: parent.width - 20; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                font.pixelSize: 15; font.family: M.Style.fontMedium.name
                color: parent.active ? "#062224" : M.Style.dimmedFontColor
                text: form.busy ? qsTr("Working…")
                    : form.problem !== "" ? form.problem
                    : form.reviewing ? qsTr("Confirm")
                    : form.type === "market" ? (form.buying ? qsTr("Buy %1 XMZ").arg(X.amount(form.plan && form.plan.ok ? form.plan.atoms / 1e11 : 0, 4)) : qsTr("Sell %1 XMZ").arg(X.amount(form.plan && form.plan.ok ? form.plan.atoms / 1e11 : 0, 4)))
                    : (form.buying ? qsTr("Post buy order") : qsTr("Post sell order"))
            }
            MouseArea {
                anchors.fill: parent; enabled: parent.active; cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (!form.reviewing) { form.reviewing = true; return }
                    form.reviewing = false
                    if (form.type === "market") {
                        var all = form.plan.atoms === form.plan.row.atoms
                        form.takeRequested(form.plan.row.id, all ? "" : X.atomsText(form.plan.atoms))
                    } else {
                        form.postRequested(form.buying ? "buy-xmz" : "sell-xmz", form.limitCoin, X.atomsText(form.amountAtoms), form.hours,
                                           partialToggle.on ? String(form.minXmz) : "", renewToggle.on)
                    }
                }
            }
        }
        Text {
            visible: form.reviewing
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Back"); font.pixelSize: 13; color: M.Style.dimmedFontColor
            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: form.reviewing = false }
        }
        Text {
            objectName: "orderMessage"
            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
            color: M.Style.warningColor
            visible: form.message !== ""
            text: form.message
        }
    }

    function usdText(coinAmount) { return form.coinUsd > 0 && coinAmount > 0 ? "  ≈ " + X.usd(coinAmount * form.coinUsd) : "" }
    function summaryRows() {
        if (type === "market") {
            if (!plan || !plan.ok) return []
            return [[qsTr("Price"), X.price(plan.price) + " " + coin],
                    [buying ? qsTr("You pay") : qsTr("You receive"), X.plain(plan.coin, 8) + " " + coin + usdText(plan.coin)],
                    [buying ? qsTr("You receive") : qsTr("You pay"), X.amount(plan.atoms / 1e11, 4) + " XMZ"]]
        }
        if (limitCoin === "") return []
        var rows = [[buying ? qsTr("You pay") : qsTr("You receive"), limitCoin + " " + coin + usdText(Number(limitCoin))]]
        if (partialToggle.on) rows.push([qsTr("Partial fills"), qsTr("from %1 XMZ").arg(X.amount(minXmz, 4))])
        if (renewToggle.on) rows.push([qsTr("Keep open"), qsTr("renewed at this price while the wallet runs")])
        if (hours > 0) rows.push([qsTr("Open until"), Qt.formatDateTime(new Date(Date.now() + hours * 3600000), "d MMM yyyy HH:mm")])
        return rows
    }
    function matchNote() {
        if (type === "market") {
            if (!plan || amountInput.text.trim() === "") return ""
            if (plan.ok) {
                if (buying && coinBalance >= 0 && plan.coin > coinBalance)
                    return qsTr("Your trade account holds %1 %2. The swap asks for the rest before it locks anything.").arg(X.plain(coinBalance, 8)).arg(coin)
                return ""
            }
            if (plan.reason === "empty") return buying ? qsTr("No one is selling XMZ for %1 now. Post a limit order instead.").arg(coin)
                                                       : qsTr("No one is buying XMZ with %1 now. Post a limit order instead.").arg(coin)
            if (plan.reason === "gone") return qsTr("The offer you picked is no longer in the book.")
            if (plan.reason === "amount") return ""
            var s = plan.suggestion
            return s.row.partial ? qsTr("The best offer allows %1 to %2 XMZ, leaving nothing or at least %1.").arg(X.amount(s.row.min, 2)).arg(X.amount(s.row.xmz, 2))
                                 : qsTr("The best offer is %1 XMZ at %2 %3 and is taken whole.").arg(X.amount(s.row.xmz, 2)).arg(X.price(s.row.price)).arg(coin)
        }
        if (limitPrice > 0) {
            // Prices as typed against prices from wei: equal within rounding
            var eps = limitPrice * 1e-9
            if (buying && book.bestAsk > 0 && limitPrice >= book.bestAsk - eps) return qsTr("Someone already sells at %1 %2 or less: a market order buys now.").arg(X.price(book.bestAsk)).arg(coin)
            if (!buying && book.bestBid > 0 && limitPrice <= book.bestBid + eps) return qsTr("Someone already buys at %1 %2 or more: a market order sells now.").arg(X.price(book.bestBid)).arg(coin)
        }
        return ""
    }
    function reviewText() {
        if (type === "market" && plan && plan.ok) {
            return (buying ? qsTr("You buy %1 XMZ for %2 %3.") : qsTr("You sell %1 XMZ for %2 %3."))
                       .arg(X.amount(plan.atoms / 1e11, 4)).arg(X.plain(plan.coin, 8)).arg(coin)
                 + " " + qsTr("The app agrees the swap with the offer's maker; you then approve the lock in the trade panel below. Network fees are extra.")
        }
        if (type === "limit" && limitCoin !== "") {
            return (buying ? qsTr("Your offer: buy %1 XMZ for %2 %3, open for %4.") : qsTr("Your offer: sell %1 XMZ for %2 %3, open for %4."))
                       .arg(X.amount(amountAtoms / 1e11, 4)).arg(limitCoin).arg(coin).arg(X.lifetimeText(hours))
                 + (hours > 24 ? " " + qsTr("Keep this wallet running when you can: a taker is answered only while it runs.") : "")
                 + (partialToggle.on ? " " + qsTr("Others may take any part from %1 XMZ.").arg(X.amount(minXmz, 4)) : "")
                 + " " + qsTr("You can cancel it while it is open.")
        }
        return ""
    }
}
