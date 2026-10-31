import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as MonzeroComponents

// One trade in plain words: what is being swapped, four steps with the current one marked, what is
// happening now, when a refund becomes possible, and the single approval the trade needs.
// `trade` is the status the engine reports (configured, state, snapshot{role, stage, done, offer,
// deadlines}, canApprove, busy, commitment). The card only asks: approve() and refresh().
Rectangle {
    id: card

    property var trade: ({})
    readonly property var t: trade || ({})
    property string coin: "BNB"
    property int coinDecimals: 18
    property string errorText: ""
    readonly property var snapshot: t.snapshot || ({})
    readonly property bool buyer: snapshot.role === "owner"     // pays the coin, receives XMZ
    readonly property var progress: stepFor(snapshot.role, snapshot.stage) || ({ step: 0, refund: false, now: "" })
    property bool confirmed: false

    signal approve(string commitment)
    signal refresh()

    color: MonzeroComponents.Style.surfaceRaised
    radius: 8
    border.color: progress.refund ? MonzeroComponents.Style.warningColor : MonzeroComponents.Style.accentMuted
    implicitHeight: column.implicitHeight + 40
    onTradeChanged: if (!trade || trade.canApprove !== true) confirmed = false

    // {step 0..4 (4 = swapped), refund, now, finished}: where the trade is, in the trader's words.
    // A refunded or cancelled trade stops at the step it reached.
    function stepFor(role, stage) {
        var owner = {
            "Start": [1, false, qsTr("Waiting for your approval to lock your %1.").arg(coin)],
            "LockSent": [1, false, qsTr("Locking your %1 in the swap…").arg(coin)],
            "Locked": [2, false, qsTr("Your %1 is locked. Waiting for the seller to lock their XMZ.").arg(coin)],
            "ReadySent": [3, false, qsTr("The seller's XMZ is locked. Releasing the trade…")],
            "Ready": [3, false, qsTr("The seller's XMZ is locked. Waiting for them to collect the %1, which hands you the XMZ.").arg(coin)],
            "Sweeping": [3, false, qsTr("The seller collected. Moving your XMZ into your wallet…")],
            "Done(Swapped)": [4, false, qsTr("Done. The XMZ is in your wallet.")],
            "RefundSent": [2, true, qsTr("The trade did not finish in time. Refunding your %1…").arg(coin)],
            "Done(Refunded)": [2, true, qsTr("Refunded. Your %1 is back in your account.").arg(coin), true],
            "Done(Aborted)": [1, true, qsTr("Cancelled before anything was locked. Nothing moved."), true]
        }
        var claimer = {
            "WaitingForEvmLock": [1, false, qsTr("Waiting for the buyer to lock their %1.").arg(coin)],
            "XmzLockSent": [2, false, qsTr("The buyer's %1 is locked. Locking your XMZ…").arg(coin)],
            "XmzLocked": [2, false, qsTr("Your XMZ is locked. Waiting for the buyer to confirm it.")],
            "ClaimSent": [3, false, qsTr("Collecting your %1…").arg(coin)],
            "Done(Swapped)": [4, false, qsTr("Done. The %1 is in your account.").arg(coin)],
            "WaitingForRefund": [3, true, qsTr("Too close to the deadline to collect safely. Your XMZ comes back after the refund time.")],
            "SweepingBack": [3, true, qsTr("The buyer took their refund. Taking your XMZ back…")],
            "Done(Refunded)": [3, true, qsTr("Refunded. Your XMZ is back in your wallet."), true],
            "Done(Aborted)": [1, true, qsTr("Cancelled before anything was locked. Nothing moved."), true]
        }
        var row = (role === "owner" ? owner : claimer)[stage]
        if (!row) return { step: 0, refund: false, now: stage ? qsTr("Working on it (%1).").arg(stage) : qsTr("Loading the trade…") }
        return { step: row[0], refund: row[1], now: row[2], finished: row[0] === 4 || row[3] === true }
    }

    function atomic(text, decimals) {
        var n = Number(text) / Math.pow(10, decimals)
        if (!isFinite(n)) return "?"
        var s = n.toFixed(Math.min(decimals, 8))
        s = s.indexOf(".") < 0 ? s : s.replace(/0+$/, "").replace(/\.$/, "")
        var parts = s.split(".")
        var whole = parts[0], out = ""
        while (whole.length > 3) { out = "," + whole.slice(-3) + out; whole = whole.slice(0, -3) }
        parts[0] = whole + out
        return parts.join(".")
    }
    function when(seconds) {
        var t = Number(seconds)
        if (!(t > 0)) return ""
        return Qt.formatDateTime(new Date(t * 1000), "d MMM, HH:mm")
    }
    readonly property string coinAmount: snapshot.offer ? atomic(snapshot.offer.value, coinDecimals) + " " + coin : ""
    readonly property string xmzAmount: snapshot.offer ? atomic(snapshot.offer.xmzAmount, 11) + " XMZ" : ""

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            MonzeroComponents.Label {
                objectName: "tradeTitle"
                fontSize: 20
                text: card.progress.step === 4 ? (card.buyer ? qsTr("Bought %1").arg(card.xmzAmount) : qsTr("Sold %1").arg(card.xmzAmount))
                    : card.buyer ? qsTr("Buying %1").arg(card.xmzAmount) : qsTr("Selling %1").arg(card.xmzAmount)
            }
            Item { Layout.fillWidth: true }
            MonzeroComponents.StandardButton {
                small: true; primary: false
                enabled: card.t.busy !== true
                text: card.t.busy === true ? qsTr("Updating…") : qsTr("Refresh")
                onClicked: card.refresh()
            }
        }
        MonzeroComponents.TextPlain { themeTransition: false;
            objectName: "tradeSummary"
            Layout.fillWidth: true; wrapMode: Text.Wrap
            color: MonzeroComponents.Style.dimmedFontColor
            text: (card.progress.step === 4 ? qsTr("You paid %1 and received %2.") : qsTr("You pay %1 and receive %2."))
                .arg(card.buyer ? card.coinAmount : card.xmzAmount).arg(card.buyer ? card.xmzAmount : card.coinAmount)
        }

        // Four steps
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: card.buyer
                    ? [qsTr("Agreed"), qsTr("You lock %1").arg(card.coin), qsTr("Seller locks XMZ"), qsTr("XMZ arrives")]
                    : [qsTr("Agreed"), qsTr("Buyer locks %1").arg(card.coin), qsTr("You lock XMZ"), qsTr("%1 arrives").arg(card.coin)]
                delegate: ColumnLayout {
                    objectName: "step_" + index
                    // done: before the current step; current: the step in progress
                    property bool done: index < card.progress.step
                    property bool current: index === card.progress.step && !card.progress.finished
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1                                  // four equal columns
                    spacing: 6
                    Rectangle {
                        Layout.fillWidth: true; height: 6; radius: 3
                        color: parent.done ? MonzeroComponents.Style.accent
                             : parent.current ? (card.progress.refund ? MonzeroComponents.Style.warningColor : MonzeroComponents.Style.accentMuted)
                             : MonzeroComponents.Style.moneroGrey
                    }
                    MonzeroComponents.TextPlain { themeTransition: false;
                        Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
                        text: (parent.done ? "✓ " : "") + modelData
                        color: parent.done || parent.current ? MonzeroComponents.Style.defaultFontColor : MonzeroComponents.Style.dimmedFontColor
                    }
                }
            }
        }

        MonzeroComponents.TextPlain { themeTransition: false;
            objectName: "tradeNow"
            Layout.fillWidth: true; wrapMode: Text.Wrap
            font.pixelSize: 16
            color: card.progress.refund ? MonzeroComponents.Style.warningColor : MonzeroComponents.Style.defaultFontColor
            text: card.progress.now
        }
        MonzeroComponents.TextPlain { themeTransition: false;
            objectName: "tradeRefundTime"
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 13
            color: MonzeroComponents.Style.dimmedFontColor
            visible: card.progress.step > 0 && !card.progress.finished && text !== ""
            text: {
                var d = card.snapshot.deadlines
                if (!d || !card.when(d.timeout2)) return ""
                return card.buyer ? qsTr("If the trade doesn't finish, your %1 can be refunded from %2.").arg(card.coin).arg(card.when(d.timeout2))
                                  : qsTr("The %1 must be collected before %2; after that your XMZ comes back.").arg(card.coin).arg(card.when(d.timeout2))
            }
        }

        // The one approval
        Rectangle {
            objectName: "approvalBox"
            Layout.fillWidth: true
            visible: card.t.canApprove === true
            color: "transparent"; radius: 6
            border.color: MonzeroComponents.Style.accent
            implicitHeight: approvalColumn.implicitHeight + 24
            ColumnLayout {
                id: approvalColumn
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                spacing: 10
                MonzeroComponents.TextPlain { themeTransition: false; color: MonzeroComponents.Style.defaultFontColor;
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Approve this trade: %1 goes into the swap and you get %2, or your %1 back after the refund time. After you approve, the app does the remaining steps by itself.")
                        .arg(card.buyer ? card.coinAmount : card.xmzAmount).arg(card.buyer ? card.xmzAmount : card.coinAmount)
                }
                MonzeroComponents.CheckBox {
                    objectName: "approvalConfirm"
                    text: qsTr("I checked the amounts")
                    toggleOnClick: false
                    checked: card.confirmed
                    onClicked: card.confirmed = !card.confirmed
                }
                MonzeroComponents.StandardButton {
                    objectName: "approveButton"
                    enabled: card.confirmed && card.t.busy !== true
                    text: qsTr("Approve trade")
                    onClicked: card.approve(card.t.commitment)
                }
            }
        }

        MonzeroComponents.TextPlain { themeTransition: false;
            visible: card.errorText !== ""
            Layout.fillWidth: true; wrapMode: Text.Wrap
            color: MonzeroComponents.Style.errorColor
            text: card.errorText
        }
    }
}
