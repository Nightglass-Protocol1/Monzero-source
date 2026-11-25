import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as M
import "dexExchange.js" as X

// The simple Swap screen: "buy (or sell) this much XMZ with this coin", one button. It takes the
// best offers by itself (as many as the amount needs), waits for a deposit when the trade account
// is short (one address, one amount, fees included) and starts once it arrives. The order book,
// limit orders and partial-fill settings stay on the advanced exchange.
Rectangle {
    id: sw
    property var ex                       // the DexExchange this screen belongs to
    property string side: "buy"           // "buy" XMZ with the coin, or "sell" XMZ for it
    // form: choosing; deposit: waiting for coin to arrive; moving: waiting for XMZ to arrive and
    // unlock in the trading wallet; started: the trades are under way
    property string stage: "form"
    property string note: ""
    property real quoted: 0               // the coin the user agreed to (to notice a price change)
    property int startedTrades: 0
    readonly property string coin: ex.coin
    readonly property real atoms: {
        var t = String(amountField.text).replace(/,/g, "").trim()
        return /^[0-9]+(\.[0-9]{0,11})?$/.test(t) ? Math.round(Number(t) * 1e11) : 0
    }
    readonly property var plan: X.planSwap(ex.book, side, atoms)
    readonly property var reserve: X.feeReserve(coin)
    readonly property bool token: coin === "USDT" || coin === "USDT-BSC"
    // What the trade account needs for a buy: the price (and, for USDT, the chain's coin for fees)
    readonly property real needCoin: plan.coin + (token ? 0 : plan.legs.length * reserve.amount)
    readonly property real needGas: token ? plan.legs.length * reserve.amount : 0
    readonly property real haveCoin: ex.balances[coin] !== undefined ? ex.balances[coin] : -1
    readonly property real haveGas: token ? (ex.balances[reserve.coin] !== undefined ? ex.balances[reserve.coin] : -1) : 0
    readonly property real shortCoin: Math.max(0, needCoin - Math.max(0, haveCoin))
    readonly property real shortGas: Math.max(0, needGas - Math.max(0, haveGas))
    // What the trading wallet needs for a sell (XMZ, fees included)
    readonly property real needXmz: X.sellNeed(plan.atoms, plan.legs.length) / 1e11
    readonly property real shortXmz: Math.max(0, needXmz - Math.max(0, ex.xmzBalance))
    readonly property bool funded: side === "buy" ? haveCoin >= 0 && shortCoin <= 0 && shortGas <= 0 : ex.xmzBalance >= 0 && shortXmz <= 0

    radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
    implicitHeight: col.implicitHeight + 32

    function decimals() { return ex.decimalsOf(coin) }
    function coinText(v) { return X.plain(v, Math.min(decimals(), 6)) }
    function reset() { stage = "form"; note = ""; quoted = 0 }

    // Take the planned offers now (the plan is made again from the current book first)
    function start() {
        var p = X.planSwap(ex.book, side, atoms)
        if (!p.legs.length) { note = qsTr("No offers match right now. Try a smaller amount or another coin."); stage = "form"; return }
        // The price moved up (a buy) or down (a sell) by more than 3% while waiting: ask again
        if (quoted > 0 && (side === "buy" ? p.coin > quoted * 1.03 : p.coin < quoted * 0.97)) {
            note = qsTr("The price changed while waiting. Check the new amount and press Swap again.")
            stage = "form"; return
        }
        var takes = p.legs.map(function (l) { return { id: l.row.id, xmz: l.atoms === l.row.atoms ? "" : X.atomsText(l.atoms) } })
        if (!ex.dex.takeEvmOffers(takes)) { note = qsTr("The swap could not start now. Try again in a moment."); stage = "form"; return }
        startedTrades = takes.length; stage = "started"; note = ""
        ex.bottomTab = "orders"
    }
    function swap() {
        note = ""
        if (ex.blockedReason !== "") { note = ex.blockedReason; return }
        if (!plan.legs.length) { note = qsTr("No offers match this amount right now."); return }
        quoted = plan.coin
        if (funded) { start(); return }
        stage = side === "buy" ? "deposit" : "moving"
        ex.loadBalance(coin); if (token) ex.loadBalance(reserve.coin)
    }
    // While waiting: look at the balance every 15 s and start as soon as it is enough
    Timer {
        interval: 15000; repeat: true
        running: sw.visible && (sw.stage === "deposit" || sw.stage === "moving")
        onTriggered: {
            sw.ex.loadBalance(sw.coin); if (sw.token) sw.ex.loadBalance(sw.reserve.coin)
            if (sw.funded) sw.start()
        }
    }
    onFundedChanged: if (funded && (stage === "deposit" || stage === "moving")) start()

    ColumnLayout {
        id: col
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16
        spacing: 12

        Text { text: qsTr("Swap"); font.pixelSize: 22; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }

        // Ready to trade? Three checks, each with what to do about it
        ColumnLayout {
            objectName: "swapChecklist"
            Layout.fillWidth: true; spacing: 4
            readonly property bool synced: typeof appWindow === "undefined" || appWindow.walletSynced === undefined || appWindow.walletSynced === true
            readonly property bool trading: sw.ex.keysReady && sw.ex.wallet.state === "ready"
            visible: sw.stage === "form" && (!synced || !trading)
            Repeater {
                model: [
                    { ok: parent.synced, t: parent.synced ? qsTr("Wallet synced") : qsTr("Your wallet is still syncing: balances show once it catches up (it connects to Monzero's node by itself).") },
                    { ok: parent.trading, t: parent.trading ? qsTr("Trading ready")
                          : !sw.ex.keysReady ? qsTr("Trading is not set up yet: use the button above.") : qsTr("The trading wallet is starting…") }
                ]
                delegate: Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13
                    text: (modelData.ok ? "✓ " : "○ ") + modelData.t
                    color: modelData.ok ? M.Style.green : M.Style.warningColor
                }
            }
        }

        // ---- the form
        ColumnLayout {
            Layout.fillWidth: true; spacing: 12
            visible: sw.stage === "form"
            RowLayout {
                spacing: 8
                Repeater {
                    model: [{ k: "buy", t: qsTr("Buy XMZ") }, { k: "sell", t: qsTr("Sell XMZ") }]
                    delegate: Rectangle {
                        objectName: "swapSide_" + modelData.k
                        width: 130; height: 38; radius: 6
                        color: sw.side === modelData.k ? (modelData.k === "buy" ? M.Style.green : "#E5677A") : "transparent"
                        border.color: sw.side === modelData.k ? "transparent" : M.Style.accentMuted
                        Text { anchors.centerIn: parent; text: modelData.t; font.pixelSize: 14; font.family: M.Style.fontMedium.name
                               color: sw.side === modelData.k ? "white" : M.Style.defaultFontColor }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { sw.side = modelData.k; sw.note = "" } }
                    }
                }
            }
            Text { text: sw.side === "buy" ? qsTr("Pay with") : qsTr("Get paid in"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
            Flow {
                Layout.fillWidth: true; spacing: 8
                Repeater {
                    model: sw.ex.allCoins.filter(function (c) { return (sw.ex.markets[c] || {}).configured === true })
                    delegate: Rectangle {
                        objectName: "swapCoin_" + modelData
                        width: 110; height: 34; radius: 6
                        color: sw.coin === modelData ? M.Style.accentMuted : "transparent"
                        border.color: sw.coin === modelData ? M.Style.accent : M.Style.accentMuted
                        Text { anchors.centerIn: parent; text: sw.ex.label(modelData); font.pixelSize: 13; color: M.Style.defaultFontColor }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { sw.ex.pick(modelData); sw.note = "" } }
                    }
                }
            }
            Text { text: qsTr("How much XMZ?"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
            M.LineEdit {
                id: amountField
                objectName: "swapAmount"
                Layout.fillWidth: true; Layout.maximumWidth: 360
                fontSize: 18; inputPaddingTop: 8; inputPaddingBottom: 8
                placeholderText: qsTr("e.g. 1000"); placeholderFontSize: 16
                onTextChanged: sw.note = ""
            }
            // The quote, in words
            Text {
                objectName: "swapQuote"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 15; color: M.Style.defaultFontColor
                visible: sw.atoms > 0
                text: !sw.plan.legs.length ? qsTr("Nobody is offering that right now on %1. Try a smaller amount or another coin.").arg(sw.ex.label(sw.coin))
                    : (sw.side === "buy" ? qsTr("You pay about %1 %2 and get %3 XMZ.") : qsTr("You give %3 XMZ and get about %1 %2."))
                        .arg(sw.coinText(sw.plan.coin)).arg(sw.ex.label(sw.coin)).arg(X.amount(sw.plan.atoms / 1e11, 2))
                      + (sw.ex.coinUsd > 0 ? " (≈ " + X.usd(sw.plan.coin * sw.ex.coinUsd) + ")" : "")
            }
            Text {
                objectName: "swapFees"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                visible: sw.atoms > 0 && sw.plan.legs.length > 0
                text: (sw.plan.legs.length > 1 ? qsTr("Filled from %n offer(s), best prices first. ", "", sw.plan.legs.length) : "")
                    + (sw.side === "buy"
                       ? qsTr("Network fees: up to %1 %2 is kept aside for them; what is not used stays in your trade account.").arg(sw.coinText(sw.plan.legs.length * sw.reserve.amount)).arg(sw.reserve.coin)
                       : qsTr("Network fees: about %1 XMZ.").arg(X.amount(sw.needXmz - sw.plan.atoms / 1e11, 2)))
            }
            RowLayout {
                visible: sw.atoms > 0 && sw.plan.short && sw.plan.atoms > 0
                spacing: 10
                Text { text: qsTr("Only %1 XMZ is offered at the moment.").arg(X.amount(sw.plan.atoms / 1e11, 2)); font.pixelSize: 13; color: M.Style.warningColor }
                Text {
                    objectName: "swapTakeAvailable"
                    text: sw.side === "buy" ? qsTr("Buy that much") : qsTr("Sell that much"); font.pixelSize: 13; font.underline: true; color: M.Style.accent
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: amountField.text = String(sw.plan.atoms / 1e11) }
                }
            }
            M.StandardButton {
                objectName: "swapButton"
                text: qsTr("Swap")
                enabled: sw.atoms > 0 && sw.plan.legs.length > 0 && !sw.plan.short
                onClicked: sw.swap()
            }
        }

        // ---- waiting for the coin (a buy): one address, one amount
        RowLayout {
            Layout.fillWidth: true; spacing: 16
            visible: sw.stage === "deposit"
            Image {
                visible: typeof appWindow !== "undefined"
                Layout.preferredWidth: 150; Layout.preferredHeight: 150
                fillMode: Image.PreserveAspectFit; smooth: false
                source: sw.stage === "deposit" && sw.ex.coinAccount !== "" && typeof appWindow !== "undefined" ? "image://qrcode/" + sw.ex.coinAccount : ""
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Text {
                    objectName: "swapDeposit"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 15; color: M.Style.defaultFontColor
                    text: qsTr("Send %1 %2 to your trade account:").arg(sw.coinText(sw.shortCoin > 0 ? sw.shortCoin : 0)).arg(sw.ex.label(sw.coin))
                        + (sw.shortGas > 0 ? " " + qsTr("and %1 %2 for network fees").arg(sw.coinText(sw.shortGas)).arg(sw.reserve.coin) : "")
                }
                M.LineEdit { Layout.fillWidth: true; readOnly: true; copyButton: true; fontSize: 13; text: sw.ex.coinAccount }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                    text: qsTr("This address is yours: only this wallet can spend from it. Send from any wallet or exchange on the %1 network. The swap starts by itself as soon as the coins arrive.")
                        .arg(sw.ex.networkName(sw.ex.marketChain(sw.coin)))
                }
                Text { text: qsTr("Waiting for your %1…").arg(sw.ex.label(sw.coin)); font.pixelSize: 13; color: M.Style.accent }
                Text {
                    text: qsTr("Cancel"); font.pixelSize: 12; font.underline: true; color: M.Style.dimmedFontColor
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sw.reset() }
                }
            }
        }

        // ---- waiting for XMZ (a sell): moved in from the main balance, then 10 blocks to unlock
        ColumnLayout {
            Layout.fillWidth: true; spacing: 8
            visible: sw.stage === "moving"
            Text {
                objectName: "swapMove"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 15; color: M.Style.defaultFontColor
                text: qsTr("Your trading wallet needs %1 more XMZ for this swap.").arg(X.amount(sw.shortXmz, 2))
            }
            M.StandardButton {
                objectName: "swapMoveButton"
                text: qsTr("Move %1 XMZ from my wallet").arg(X.amount(Math.ceil(sw.shortXmz), 0))
                onClicked: {
                    if (typeof middlePanel === "undefined") return
                    middlePanel.state = "Transfer"
                    middlePanel.transferView.sendTo(sw.ex.wallet.address, "", qsTr("XMZ for a swap on the DEX"), String(Math.ceil(sw.shortXmz)))
                    if (typeof leftPanel !== "undefined") leftPanel.selectItem(middlePanel.state)
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                text: qsTr("After you send it, the XMZ needs about 20 minutes (10 blocks) before it can be used. Leave the wallet open: the swap starts by itself then.")
            }
            Text {
                text: qsTr("Cancel"); font.pixelSize: 12; font.underline: true; color: M.Style.dimmedFontColor
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sw.reset() }
            }
        }

        // ---- started
        ColumnLayout {
            Layout.fillWidth: true; spacing: 8
            visible: sw.stage === "started"
            Text {
                objectName: "swapStarted"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 15; color: M.Style.defaultFontColor
                text: qsTr("Your swap has started (%n trade(s)). Each one shows its progress under My orders below. Keep the wallet open until they finish.", "", sw.startedTrades)
            }
            M.StandardButton {
                objectName: "swapAgain"
                small: true; primary: false
                text: qsTr("New swap")
                onClicked: { amountField.text = ""; sw.reset() }
            }
        }

        Text {
            objectName: "swapNote"
            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.warningColor
            visible: text !== ""
            text: sw.note !== "" ? sw.note : (sw.ex.market.message || "")
        }
    }
}
