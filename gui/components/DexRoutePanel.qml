import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as M
import "dexExchange.js" as X

// XMR for XMZ on the Exchange page: no swap joins the two chains directly (neither has timed
// locks), so the pair runs as two atomic swaps through one coin both trade against (ETH, BNB,
// SOL or USDT): XMZ -> coin -> XMR to buy XMR, XMR -> coin -> XMZ to sell it.
//
// The quote comes from both order books. Step 1 takes the offer that buys what you give; once that
// swap is done and the coin is in your trade account, step 2 takes the offer that sells what you
// want, with that coin. Each step is an ordinary trade: approved before anything is locked, and
// refunded if the other side does not follow through. Between the steps you hold the coin.
ColumnLayout {
    id: rp
    property var dex: ({})
    property var xmzMarkets: ({})          // coin -> XMZ market (DexEngine.evmMarketStatus)
    property var xmrMarkets: ({})          // coin -> XMR market (DexEngine.xmrMarketStatus)
    property var trade: ({})               // DexEngine.evmTradeStatus
    property var coins: []
    property string myAccount: ""
    property real now: Date.now() / 1000
    property bool keysReady: false
    spacing: 12

    // Kept back from step 1's coin for step 2's network fees
    readonly property real feeReserve: 0.02
    property string direction: "buy"       // "buy": XMZ -> XMR; "sell": XMR -> XMZ
    property string via: ""
    property int step: 0                   // 0 not started, 1 first swap taken, 2 second swap taken
    property real stepOneCoin: 0           // the coin step 1 brings in
    property string stepOneId: ""          // the offer step 1 took (its trade runs in "take-<id>")
    property string message: ""

    function decimalsOf(c) { return c === "USDT" ? 6 : c === "SOL" ? 9 : 18 }
    function label(c) { return c === "USDT" ? "USDT-SOL" : c }
    readonly property var viaCoins: coins.filter(function (c) {
        return (xmzMarkets[c] || {}).configured === true && (xmrMarkets[c] || {}).configured === true
    })
    readonly property string viaNow: via !== "" && viaCoins.indexOf(via) >= 0 ? via : (viaCoins.length ? viaCoins[0] : "")
    readonly property string gives: direction === "buy" ? "XMZ" : "XMR"
    readonly property string gets: direction === "buy" ? "XMR" : "XMZ"
    readonly property var bookXmz: X.book((xmzMarkets[viaNow] || {}).listings || [], viaNow, decimalsOf(viaNow), myAccount, now, "XMZ")
    readonly property var bookXmr: X.book((xmrMarkets[viaNow] || {}).listings || [], viaNow, decimalsOf(viaNow), myAccount, now, "XMR")
    // Step 1 sells what you give to an offer buying it; step 2 spends the coin on what you get
    readonly property real amountAtoms: X.xmzAtoms(amountInput.text, X.placesOf(gives))
    readonly property var planOne: amountAtoms > 0 ? X.planMarket(direction === "buy" ? bookXmz : bookXmr, "sell", amountAtoms) : null
    readonly property real budget: step >= 1 ? stepOneCoin * (1 - feeReserve) : (planOne && planOne.ok ? planOne.coin * (1 - feeReserve) : 0)
    readonly property var planTwo: X.planSpend(direction === "buy" ? bookXmr : bookXmz, budget)
    readonly property var running: trade.snapshot || ({})
    readonly property bool tradeRunning: trade.configured === true && running.done !== true
    // Step 1's own trade (not an earlier one still shown) finished with the swap made
    readonly property bool stepOneTrade: stepOneId !== "" && String(trade.tradeDir || "").indexOf("/take-" + stepOneId.substring(2, 18)) >= 0
    readonly property bool stepOneDone: step === 1 && stepOneTrade && running.done === true && running.stage === "Done(Swapped)"
    readonly property bool stepOneFailed: step === 1 && stepOneTrade && running.done === true && running.stage !== "Done(Swapped)"

    function refresh() {
        if (viaNow === "" || !dex) return
        if (typeof dex.refreshEvmOffers === "function") dex.refreshEvmOffers(viaNow + "/XMZ")
        if (typeof dex.refreshXmrOffers === "function") dex.refreshXmrOffers(viaNow)
    }
    onViaNowChanged: refresh()
    Timer { interval: 20000; running: rp.visible; repeat: true; triggeredOnStart: true; onTriggered: rp.refresh() }

    function amountOf(plan, base) { return plan.atoms === plan.row.atoms ? "" : X.atomsText(plan.atoms, X.placesOf(base)) }
    function startOne() {
        message = ""
        if (!planOne || !planOne.ok) return
        if (dex.takeEvmOffer(planOne.row.id, amountOf(planOne, gives))) { stepOneCoin = planOne.coin; stepOneId = planOne.row.id; step = 1 }
        else message = qsTr("The first swap could not start now.")
    }
    function startTwo() {
        message = ""
        if (!planTwo.ok) return
        if (dex.takeEvmOffer(planTwo.row.id, amountOf(planTwo, gets))) step = 2
        else message = qsTr("The second swap could not start now.")
    }
    function reset() { step = 0; stepOneCoin = 0; stepOneId = ""; message = ""; amountInput.text = "" }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: col.implicitHeight + 28
        radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
        ColumnLayout {
            id: col
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
            spacing: 10
            Text { text: qsTr("XMR / XMZ · two swaps"); font.pixelSize: 18; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                text: qsTr("XMR and XMZ cannot be swapped in one step, so this runs as two atomic swaps through a coin both trade against. Each swap is approved before anything is locked and refunded if the other side does not follow through. Between the two you hold the coin in your trade account.")
            }
            Text {
                objectName: "routeUnavailable"
                visible: rp.viaCoins.length === 0
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.warningColor
                text: qsTr("No coin has both an XMZ and an XMR market running in this wallet yet.")
            }
            RowLayout {
                visible: rp.viaCoins.length > 0
                spacing: 8
                Repeater {
                    model: [ { k: "buy", t: qsTr("Buy XMR with XMZ") }, { k: "sell", t: qsTr("Sell XMR for XMZ") } ]
                    delegate: M.StandardButton {
                        objectName: "routeSide_" + modelData.k
                        small: true; primary: rp.direction === modelData.k; text: modelData.t
                        enabled: rp.step === 0
                        onClicked: rp.direction = modelData.k
                    }
                }
            }
            RowLayout {
                visible: rp.viaCoins.length > 0
                spacing: 8
                Text { text: qsTr("Through"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
                Repeater {
                    model: rp.viaCoins
                    delegate: M.StandardButton {
                        objectName: "routeVia_" + modelData
                        small: true; primary: rp.viaNow === modelData; text: rp.label(modelData)
                        enabled: rp.step === 0
                        onClicked: rp.via = modelData
                    }
                }
            }
            RowLayout {
                visible: rp.viaCoins.length > 0
                spacing: 8
                Text { text: qsTr("You give, %1").arg(rp.gives); font.pixelSize: 12; color: M.Style.dimmedFontColor }
                M.LineEdit {
                    id: amountInput; objectName: "routeAmount"
                    Layout.fillWidth: true; fontSize: 14; inputPaddingTop: 6; inputPaddingBottom: 6
                    placeholderText: rp.gives; placeholderFontSize: 14
                    readOnly: rp.step > 0
                }
            }
            // The quote: both swaps, from the books as they are now
            Text {
                objectName: "routeStepOne"
                visible: rp.viaCoins.length > 0 && amountInput.text.trim() !== ""
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.defaultFontColor
                text: !rp.planOne ? qsTr("Check the amount.")
                    : rp.planOne.ok ? qsTr("1. Sell %1 %2 for %3 %4").arg(X.amount(rp.planOne.atoms / Math.pow(10, X.placesOf(rp.gives)), 4)).arg(rp.gives)
                                        .arg(X.plain(rp.planOne.coin, 8)).arg(rp.label(rp.viaNow))
                    : rp.planOne.reason === "empty" ? qsTr("1. No one is buying %1 with %2 now.").arg(rp.gives).arg(rp.label(rp.viaNow))
                    : qsTr("1. No offer buys exactly that much %1; the nearest is %2 %1.").arg(rp.gives)
                          .arg(rp.planOne.suggestion ? X.amount(rp.planOne.suggestion.atoms / Math.pow(10, X.placesOf(rp.gives)), 4) : "?")
            }
            Text {
                objectName: "routeStepTwo"
                visible: rp.viaCoins.length > 0 && (rp.step > 0 || (rp.planOne && rp.planOne.ok))
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.defaultFontColor
                text: rp.planTwo.ok ? qsTr("2. Buy %1 %2 with %3 %4 (%5% of step 1's coin is kept for network fees)")
                                          .arg(X.amount(rp.planTwo.atoms / Math.pow(10, X.placesOf(rp.gets)), 6)).arg(rp.gets)
                                          .arg(X.plain(rp.planTwo.coin, 8)).arg(rp.label(rp.viaNow)).arg(rp.feeReserve * 100)
                    : rp.planTwo.reason === "empty" ? qsTr("2. No one is selling %1 for %2 now.").arg(rp.gets).arg(rp.label(rp.viaNow))
                    : qsTr("2. No %1 offer fits %2 %3 now.").arg(rp.gets).arg(X.plain(rp.budget, 8)).arg(rp.label(rp.viaNow))
            }
            Text {
                objectName: "routeState"
                visible: text !== ""
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                text: rp.stepOneFailed ? qsTr("Step 1 did not swap: what you gave comes back to you (refund). Start a new route when you like.")
                    : rp.step === 1 && !rp.stepOneDone ? qsTr("Step 1 is running: approve it in the trade card above when asked, and keep the wallet open until it is done.")
                    : rp.stepOneDone ? qsTr("Step 1 is done. The %1 is in your trade account: start step 2.").arg(rp.label(rp.viaNow))
                    : rp.step === 2 ? qsTr("Step 2 is running: approve it in the trade card above when asked. The %1 arrives in your trading wallet.").arg(rp.gets)
                    : ""
            }
            Text { visible: rp.message !== ""; text: rp.message; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.warningColor }
            RowLayout {
                visible: rp.viaCoins.length > 0
                spacing: 8
                M.StandardButton {
                    objectName: "routeStartOne"
                    visible: rp.step === 0
                    primary: true
                    text: qsTr("Start step 1")
                    enabled: rp.keysReady && !rp.tradeRunning && !!rp.planOne && rp.planOne.ok && rp.planTwo.ok
                    onClicked: rp.startOne()
                }
                M.StandardButton {
                    objectName: "routeStartTwo"
                    visible: rp.step === 1
                    primary: true
                    text: qsTr("Start step 2")
                    enabled: rp.stepOneDone && rp.planTwo.ok
                    onClicked: rp.startTwo()
                }
                M.StandardButton {
                    objectName: "routeReset"
                    visible: rp.step > 0
                    small: true
                    text: qsTr("New route")
                    enabled: !rp.tradeRunning || rp.stepOneFailed
                    onClicked: rp.reset()
                }
            }
        }
    }
}
