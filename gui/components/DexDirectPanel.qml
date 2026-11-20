import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as M
import "dexExchange.js" as X

// Coin-for-coin trades on the Exchange page: ETH, BNB and SOL swapped straight across, no XMZ
// leg. Each side locks its coin in the swap contract on its own chain and one secret releases
// both; if the other side does not follow through, each coin goes back to whoever locked it.
//
// Offers come from the relays through the pinned helper (DexEngine.directMarketStatus). Taking
// an offer agrees the keys; the taker then approves the exact terms before anything is locked.
// A maker's posted offer is its approval. Finished trades pay out to the user's own wallet.
ColumnLayout {
    id: dp
    property var dex: ({})
    readonly property var direct: (dex && dex.directMarketStatus) || ({})
    property var wallet: ({})
    property var balances: ({})            // coin -> trade account balance (coin)
    property var withdrawTo: ({})          // coin -> the user's own address (remembered)
    property var usd: ({})
    property real now: Date.now() / 1000
    property var networkName: function (chainId) { return "" }
    property int riskTick: 0
    signal addressUsed(string coin, string address)
    spacing: 12

    property string pair: ""
    property bool showPairs: true          // its own pair buttons (off when the page's pair row picks)
    readonly property string pairNow: pair !== "" && pairs.indexOf(pair) >= 0 ? pair : (pairs.length ? pairs[0] : "")
    readonly property var pairs: direct.pairs || []
    readonly property string base: pairNow.split("/")[0] || ""
    readonly property string quote: pairNow.split("/")[1] || ""
    property string localError: ""
    property string confirmId: ""          // the offer whose take is being confirmed
    property string giveCoin: ""           // the form: what we give ("" = the pair's base)
    readonly property string giving: giveCoin === base || giveCoin === quote ? giveCoin : base
    readonly property string getting: giving === base ? quote : base
    property int hours: 24

    function chainOf(coin) {
        var c = direct.chains || []
        for (var i = 0; i < c.length; ++i) if (c[i].coin === coin) return Number(c[i].chainId)
        return 0
    }
    // Solana's coin and its USDT share the Solana trade account; the EVM coins and BNB Chain's USDT the other
    function accountOf(coin) { return coin === "SOL" || coin === "USDT" ? (wallet.solTradeAccount || "") : (wallet.tradeAccount || "") }
    // Solana's USDT and BNB Chain's, named by their chains
    function label(coin) { return coin === "USDT" ? "USDT-SOL" : coin }
    function mine(l) { return String(l.maker || "").toLowerCase() === String(wallet.tradeAccount || "").toLowerCase() }
    // The pair's book: asks give the base (cheapest first), bids want it (dearest first)
    function side(which) {
        var out = (direct.listings || []).filter(function (l) { return l.pair === pairNow && l.side === which && l.expiresAt > now })
        out.sort(function (a, b) { return which === "sell" ? Number(a.price) - Number(b.price) : Number(b.price) - Number(a.price) })
        return out
    }
    readonly property var asks: side("sell")
    readonly property var bids: side("buy")
    readonly property var trades: {
        var order = { agreed: 0, open: 1, done: 2 }
        var out = (direct.mine || []).slice()
        out.sort(function (a, b) {
            var x = order[a.state] !== undefined ? order[a.state] : 3, y = order[b.state] !== undefined ? order[b.state] : 3
            return x - y || b.expiresAt - a.expiresAt
        })
        return out
    }
    readonly property int running: trades.filter(function (t) { return t.state === "agreed" || t.state === "open" }).length

    // What a trade is doing now, in the user's words
    function stageText(t) {
        var tr = t.trade || {}, s = tr.stage || "", leader = t.kind === "take"
        var give = label((t.gives || {}).coin || ""), get = label((t.wants || {}).coin || "")
        if (t.state === "open") return t.kind === "offer" ? qsTr("open · %1").arg(X.timeLeft(t.expiresAt, now)) : qsTr("waiting for the maker's answer")
        if (t.state === "cancelled") return qsTr("cancelled")
        if (t.state === "expired") return qsTr("expired, not taken")
        if (t.state === "refused") return qsTr("not matched: %1").arg(t.detail || qsTr("the maker refused"))
        if (s === "Done(Swapped)") return qsTr("swapped: you received %1").arg(get)
        if (s === "Done(Refunded)") return qsTr("refunded: your %1 came back").arg(give)
        if (s === "Done(Aborted)") return qsTr("stopped before anything was locked")
        if (s === "Done(Missed)") return qsTr("ended without a swap")
        if (leader && tr.approved !== true) return qsTr("matched: check the terms and approve")
        switch (s) {
        case "Start": case "LockSent": return leader ? qsTr("locking your %1…").arg(give) : qsTr("waiting for the taker to lock %1").arg(get)
        case "Locked": case "ReadySent": return leader ? qsTr("your %1 is locked").arg(give) : qsTr("your %1 is locked; waiting for the taker to collect it").arg(give)
        case "Waiting": return qsTr("your %1 is locked; waiting for the maker to lock %2").arg(give).arg(get)
        case "WaitingForA": return qsTr("waiting for the taker to lock %1").arg(get)
        case "Ready": return qsTr("your %1 is locked; waiting for the taker to collect it").arg(give)
        case "ClaimSent": case "Claiming": return qsTr("collecting your %1…").arg(get)
        case "GaveUp": return qsTr("the other side did not lock in time: your %1 comes back after its time limit").arg(give)
        case "RefundSent": return qsTr("taking your %1 back…").arg(give)
        }
        return s || qsTr("starting…")
    }
    // The coin a finished trade brought in: what we got, or our own back after a refund
    function payoutCoin(t) {
        var s = (t.trade || {}).stage || ""
        return s === "Done(Swapped)" ? (t.wants || {}).coin || "" : s === "Done(Refunded)" ? (t.gives || {}).coin || "" : ""
    }
    function take(l) {
        localError = ""
        if (confirmId !== l.id) { confirmId = l.id; return }
        // We give what the maker wants
        var refusal = typeof dex.directOrderRefusal === "function"
            ? dex.directOrderRefusal(Number(l.wants.chainId), l.wants.amount, Number(l.gives.chainId), l.gives.amount) : ""
        if (refusal) { localError = refusal; return }
        if (dex.takeDirectOffer(l.id)) confirmId = ""
        else if (!(direct.message || "")) localError = qsTr("The offer could not be taken now.")
    }

    // ---- intro and pairs
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.dimmedFontColor
        text: qsTr("Swap ETH, BNB and SOL straight across. Each side locks its coin in the swap contract on its own chain, and one secret releases both. If the other side does not follow through, your coin comes back to you after its time limit.")
    }
    Flow {
        Layout.fillWidth: true; spacing: 8
        visible: dp.showPairs
        Repeater {
            model: dp.pairs
            delegate: Rectangle {
                objectName: "directPair_" + modelData
                readonly property bool active: dp.pairNow === modelData
                width: 130; height: 40; radius: 6
                color: active ? M.Style.surfaceRaised : "transparent"
                border.color: active ? M.Style.accent : M.Style.accentMuted
                Text { anchors.centerIn: parent; text: modelData; font.pixelSize: 15; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { dp.pair = modelData; dp.confirmId = ""; dp.giveCoin = "" } }
            }
        }
    }
    Text {
        objectName: "directNotice"
        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
        visible: text !== ""
        color: dp.localError !== "" || dp.direct.message ? "#E5677A" : M.Style.warningColor
        text: dp.localError !== "" ? dp.localError
            : dp.direct.message ? dp.direct.message
            : (dp.direct.state === "failed" || dp.direct.state === "refused") && dp.direct.command === "directListings"
              ? qsTr("The offers could not be loaded from the relays: %1. Retrying…").arg(dp.direct.error || qsTr("no answer"))
            : (dp.direct.state === "failed" || dp.direct.state === "refused") && (dp.direct.command === "directPost" || dp.direct.command === "directTake" || dp.direct.command === "directPayout")
              ? qsTr("Not done: %1").arg(dp.direct.error || qsTr("no answer"))
            : ""
    }

    // ---- book and offer form
    GridLayout {
        Layout.fillWidth: true
        columns: dp.width >= 900 ? 2 : 1
        columnSpacing: 12; rowSpacing: 12

        Rectangle {
            Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
            implicitWidth: 400
            implicitHeight: bookCol.implicitHeight + 24
            radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
            ColumnLayout {
                id: bookCol
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                spacing: 6
                Text { text: qsTr("Offers · %1").arg(dp.pairNow); font.pixelSize: 15; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                RowLayout {
                    Layout.fillWidth: true
                    Repeater {
                        model: [[qsTr("Price (%1)").arg(dp.label(dp.quote)), 3], [dp.label(dp.base), 3], [dp.label(dp.quote), 3], [qsTr("Expires"), 2], ["", 3]]
                        delegate: Text { text: modelData[0]; font.pixelSize: 11; color: M.Style.dimmedFontColor; Layout.fillWidth: true; Layout.preferredWidth: modelData[1] }
                    }
                }
                Repeater {
                    model: [ { k: "sell", rows: dp.asks, label: qsTr("Selling %1").arg(dp.label(dp.base)), c: "#E5677A" },
                             { k: "buy", rows: dp.bids, label: qsTr("Buying %1").arg(dp.label(dp.base)), c: M.Style.green } ]
                    delegate: ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        readonly property var group: modelData
                        Text { text: group.label; font.pixelSize: 12; color: group.c; Layout.topMargin: 4 }
                        Repeater {
                            model: group.rows
                            delegate: ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                readonly property var l: modelData
                                readonly property bool confirming: dp.confirmId === l.id
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: l.price; font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: group.c; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                                    Text { text: l.base; font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                                    Text { text: l.quote; font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                                    Text { text: X.timeLeft(l.expiresAt, dp.now); font.pixelSize: 12; color: M.Style.dimmedFontColor; Layout.fillWidth: true; Layout.preferredWidth: 2 }
                                    Item {
                                        Layout.fillWidth: true; Layout.preferredWidth: 3; implicitHeight: 26
                                        Text { visible: dp.mine(l); anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: qsTr("yours"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
                                        M.StandardButton {
                                            objectName: "directTake_" + l.id
                                            visible: !dp.mine(l); anchors.right: parent.right; small: true
                                            primary: confirming
                                            text: confirming ? qsTr("Confirm") : group.k === "sell" ? qsTr("Buy") : qsTr("Sell")
                                            enabled: dp.direct.keys === true
                                            onClicked: dp.take(l)
                                        }
                                    }
                                }
                                Text {
                                    visible: confirming
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.defaultFontColor
                                    text: qsTr("You lock %1 %2 and receive %3 %4. Nothing is locked until you approve the trade below; if the maker then does not lock theirs, your %2 comes back after its time limit (about 9 hours).")
                                          .arg(l.wants.amount).arg(dp.label(l.wants.coin)).arg(l.gives.amount).arg(dp.label(l.gives.coin))
                                }
                                Text {
                                    visible: confirming
                                    text: qsTr("Back"); font.pixelSize: 12; color: M.Style.accent
                                    MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: dp.confirmId = "" }
                                }
                            }
                        }
                        Text { visible: group.rows.length === 0; text: qsTr("none"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
            implicitWidth: 400
            implicitHeight: formCol.implicitHeight + 24
            radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
            ColumnLayout {
                id: formCol
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                spacing: 8
                readonly property real giveAmount: Number(giveField.text)
                readonly property real getAmount: Number(getField.text)
                readonly property real giveBalance: dp.balances[dp.giving] !== undefined ? dp.balances[dp.giving] : -1
                readonly property string refusal: {
                    dp.riskTick
                    if (!(giveAmount > 0) || !(getAmount > 0) || typeof dp.dex.directOrderRefusal !== "function") return ""
                    return dp.dex.directOrderRefusal(dp.chainOf(dp.giving), giveField.text.trim(), dp.chainOf(dp.getting), getField.text.trim())
                }
                readonly property string problem: !(giveAmount > 0) || !(getAmount > 0) ? ""
                    : giveBalance >= 0 && giveAmount > giveBalance ? qsTr("Your %1 trade account holds %2 %1.").arg(dp.label(dp.giving)).arg(X.plain(giveBalance, 8))
                    : refusal
                Text { text: qsTr("Post an offer"); font.pixelSize: 15; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                Row {
                    spacing: 8
                    Repeater {
                        model: [dp.base, dp.quote]
                        delegate: Rectangle {
                            objectName: "directGive_" + modelData
                            readonly property bool active: dp.giving === modelData
                            width: 140; height: 32; radius: 5
                            color: active ? M.Style.accentMuted : "transparent"; border.color: M.Style.accentMuted
                            Text { anchors.centerIn: parent; text: qsTr("I give %1").arg(dp.label(modelData)); font.pixelSize: 13; color: M.Style.defaultFontColor }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: dp.giveCoin = modelData }
                        }
                    }
                }
                Text { text: qsTr("You give (%1)").arg(dp.label(dp.giving)); font.pixelSize: 12; color: M.Style.dimmedFontColor }
                M.LineEdit { id: giveField; objectName: "directGiveAmount"; Layout.fillWidth: true; fontSize: 14; inputPaddingTop: 6; inputPaddingBottom: 6; placeholderText: "0.0"; placeholderFontSize: 14 }
                Text { text: qsTr("You get (%1)").arg(dp.label(dp.getting)); font.pixelSize: 12; color: M.Style.dimmedFontColor }
                M.LineEdit { id: getField; objectName: "directGetAmount"; Layout.fillWidth: true; fontSize: 14; inputPaddingTop: 6; inputPaddingBottom: 6; placeholderText: "0.0"; placeholderFontSize: 14 }
                Text {
                    visible: formCol.giveAmount > 0 && formCol.getAmount > 0
                    font.pixelSize: 12; color: M.Style.dimmedFontColor
                    text: {
                        var b = dp.giving === dp.base ? formCol.giveAmount : formCol.getAmount, q = dp.giving === dp.base ? formCol.getAmount : formCol.giveAmount
                        return qsTr("Price: %1 %2 per %3").arg(X.price(q / b)).arg(dp.label(dp.quote)).arg(dp.label(dp.base))
                    }
                }
                Text { text: qsTr("Open for"); font.pixelSize: 12; color: M.Style.dimmedFontColor }
                Row {
                    spacing: 8
                    Repeater {
                        model: [[6, qsTr("6 hours")], [24, qsTr("1 day")], [168, qsTr("1 week")]]
                        delegate: Rectangle {
                            width: 90; height: 28; radius: 5
                            color: dp.hours === modelData[0] ? M.Style.accentMuted : "transparent"; border.color: M.Style.accentMuted
                            Text { anchors.centerIn: parent; text: modelData[1]; font.pixelSize: 12; color: M.Style.defaultFontColor }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: dp.hours = modelData[0] }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: "#E5677A"
                    visible: formCol.problem !== ""; text: formCol.problem
                }
                M.StandardButton {
                    objectName: "directPost"
                    Layout.fillWidth: true
                    text: qsTr("Post: give %1 for %2").arg(dp.label(dp.giving)).arg(dp.label(dp.getting))
                    enabled: dp.direct.keys === true && formCol.giveAmount > 0 && formCol.getAmount > 0 && formCol.problem === "" && dp.direct.busy !== true
                    onClicked: {
                        dp.localError = ""
                        if (dp.dex.postDirectOffer(dp.chainOf(dp.giving), giveField.text.trim(), dp.chainOf(dp.getting), getField.text.trim(), dp.hours)) { giveField.text = ""; getField.text = "" }
                        else if (!(dp.direct.message || "")) dp.localError = qsTr("The offer could not be posted now.")
                    }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11; color: M.Style.dimmedFontColor
                    text: qsTr("Posting is your approval: when someone takes the offer, the wallet locks your %1 once they have locked their %2, and collects their %2. Keep the wallet running while an offer is open, and keep a little of that chain's coin in its trade account for the network fee to collect it.").arg(dp.label(dp.giving)).arg(dp.label(dp.getting))
                }
            }
        }
    }

    // ---- my trades
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: tradesCol.implicitHeight + 24
        radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
        ColumnLayout {
            id: tradesCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
            spacing: 8
            Text { text: qsTr("My coin-for-coin trades") + (dp.running ? " (" + dp.running + ")" : ""); font.pixelSize: 15; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
            Repeater {
                model: dp.trades
                delegate: ColumnLayout {
                    objectName: "directTrade_" + index
                    Layout.fillWidth: true; spacing: 4
                    readonly property var t: modelData
                    readonly property var tr: t.trade || ({})
                    readonly property bool needsApproval: t.kind === "take" && t.state === "agreed" && tr.approved !== true && !!tr.commitment
                    readonly property string inCoin: dp.payoutCoin(t)
                    readonly property bool paid: !!(tr.payout && (tr.payout.tx || tr.payout.note))
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: t.pair; font.pixelSize: 13; color: M.Style.defaultFontColor; Layout.preferredWidth: 80 }
                        Text { text: qsTr("give %1 %2 → get %3 %4").arg(t.gives.amount).arg(dp.label(t.gives.coin)).arg(t.wants.amount).arg(dp.label(t.wants.coin))
                               font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; elide: Text.ElideRight }
                        Text {
                            objectName: "directCancel"
                            visible: t.state === "open"
                            text: t.kind === "offer" ? qsTr("Cancel") : qsTr("Stop waiting"); font.pixelSize: 13; color: "#E5677A"
                            MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                                        onClicked: dp.localError = dp.dex.cancelDirectOffer(t.folder) ? "" : qsTr("That could not be done now; try again in a few seconds.") }
                        }
                    }
                    Text {
                        objectName: "directStage"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                        color: tr.stage === "Done(Swapped)" ? M.Style.green : M.Style.dimmedFontColor
                        text: dp.stageText(t) + (t.detail && t.state === "agreed" ? " · " + t.detail : "")
                    }
                    // A trade waiting for coin in its account (the amount, plus the network fee)
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.warningColor
                        visible: !!tr.awaitingFunding
                        text: tr.awaitingFunding ? qsTr("Waiting for funds: %1.").arg(tr.awaitingFunding.text) : ""
                    }
                    // The taker's approval of exactly these terms
                    ColumnLayout {
                        visible: needsApproval
                        Layout.fillWidth: true; spacing: 6
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.defaultFontColor
                            text: qsTr("You lock %1 %2 on %3 and receive %4 %5 on %6. If the maker does not lock their %5 in time, your %2 comes back after about 9 hours. Approve before %7, or the trade lapses with nothing locked.")
                                  .arg(t.gives.amount).arg(dp.label(t.gives.coin)).arg(dp.networkName(Number(t.gives.chainId)) || dp.label(t.gives.coin))
                                  .arg(t.wants.amount).arg(dp.label(t.wants.coin)).arg(dp.networkName(Number(t.wants.chainId)) || dp.label(t.wants.coin))
                                  .arg(tr.lockDeadline ? Qt.formatDateTime(new Date((tr.lockDeadline - 3600) * 1000), "d MMM HH:mm") : "—")
                        }
                        M.StandardButton {
                            objectName: "directApprove"
                            small: true; text: qsTr("Approve and lock %1").arg(dp.label(t.gives.coin))
                            onClicked: dp.localError = dp.dex.approveDirectTrade(t.folder, tr.commitment) ? "" : (dp.direct.message || qsTr("The trade could not be approved now."))
                        }
                    }
                    // Finished: what it brought in goes to the user's own wallet
                    RowLayout {
                        visible: inCoin !== "" && !paid
                        Layout.fillWidth: true
                        M.LineEdit { id: payTo; objectName: "directPayoutAddress"; Layout.fillWidth: true; fontSize: 13; inputPaddingTop: 6; inputPaddingBottom: 6
                                     text: dp.withdrawTo[inCoin] || ""; placeholderText: inCoin === "SOL" || inCoin === "USDT" ? qsTr("Your Solana address") : qsTr("Your 0x… address"); placeholderFontSize: 13 }
                        M.StandardButton {
                            objectName: "directPayout"
                            small: true; text: qsTr("Send %1 to my wallet").arg(inCoin)
                            enabled: payTo.text.trim() !== ""
                            onClicked: {
                                var a = payTo.text.trim()
                                dp.addressUsed(inCoin, a)
                                dp.localError = dp.dex.payoutDirectTrade(t.folder, a) ? "" : (dp.direct.message || qsTr("The payout could not start now."))
                            }
                        }
                    }
                    Text {
                        visible: paid
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                        text: paid ? (tr.payout.tx ? qsTr("Sent to %1. Transaction: %2").arg(tr.payout.to).arg(tr.payout.tx) : qsTr("Sent to %1.").arg(tr.payout.to)) : ""
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: M.Style.accentMuted; opacity: 0.4 }
                }
            }
            Text {
                visible: dp.trades.length === 0
                text: qsTr("No coin-for-coin trades yet. Take an offer or post one above.")
                font.pixelSize: 13; color: M.Style.dimmedFontColor
            }
        }
    }

    // ---- the trade accounts these trades lock from and pay into
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: fundsCol.implicitHeight + 24
        radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
        ColumnLayout {
            id: fundsCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
            spacing: 6
            Text { text: qsTr("Trade accounts"); font.pixelSize: 15; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                text: qsTr("The coin you give is locked from this wallet's trade account on its chain, and the coin you get is collected there (each needs a little of its chain's coin for the network fee). These are the same accounts as the XMZ pairs use; the Funds tab of each XMZ pair sends coin back to your own wallet.")
            }
            Repeater {
                model: dp.direct.chains || []
                delegate: RowLayout {
                    Layout.fillWidth: true
                    Text { text: dp.label(modelData.coin); font.pixelSize: 13; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor; Layout.preferredWidth: 70 }
                    Text {
                        text: dp.balances[modelData.coin] !== undefined ? X.plain(dp.balances[modelData.coin], 8) + " " + dp.label(modelData.coin) : "—"
                        font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.preferredWidth: 160
                    }
                    M.LineEdit { Layout.fillWidth: true; readOnly: true; copyButton: true; fontSize: 12; text: dp.accountOf(modelData.coin); visible: text !== "" }
                }
            }
        }
    }
}
