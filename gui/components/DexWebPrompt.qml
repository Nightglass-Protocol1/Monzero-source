import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.1

import "." as M

// What the wallet asks the user when the DEX web page is in use: accept the page's connection
// (compare the code), the trading password (to make or unlock the trading keys), each trade's
// approval from the terms the wallet itself holds, and moving XMZ back out. The page cannot
// answer any of these.
Item {
    id: prompt
    anchors.fill: parent
    property var dex: null          // DexEngine
    property var web: null          // DexWebBridge
    readonly property var pairing: web ? (web.pairRequest || ({})) : ({})
    readonly property var link: web ? (web.connection || ({})) : ({})
    readonly property var trade: dex ? (dex.evmTradeStatus || ({})) : ({})
    readonly property var offer: (trade.snapshot || ({})).offer || ({})
    readonly property bool approvalWanted: (link.account || "") !== "" && trade.awaitingApproval === true && (trade.commitment || "") !== ""
    property string dismissedTrade: ""
    readonly property var wallet: dex ? (dex.dexWalletStatus || ({})) : ({})
    readonly property bool keysWanted: web ? web.keysWanted === true && wallet.keysReady !== true : false
    function mainAddress() {
        return typeof appWindow !== "undefined" && appWindow.currentWallet ? appWindow.currentWallet.address(0, 0) : ""
    }
    onKeysWantedChanged: if (keysWanted) keysDialog.open(); else keysDialog.close()
    Connections {
        target: prompt.web
        ignoreUnknownSignals: true
        // Adding XMZ is an ordinary payment from the main balance to the trading wallet: the wallet's
        // own Send page, filled in, where the user checks the amount and confirms as always
        onXmzAddRequested: {
            if (typeof middlePanel === "undefined" || (prompt.wallet.address || "") === "") return
            middlePanel.state = "Transfer"
            middlePanel.transferView.sendTo(prompt.wallet.address, "", qsTr("XMZ for trading on the DEX"), xmz)
            if (typeof leftPanel !== "undefined") leftPanel.selectItem(middlePanel.state)
        }
        onXmzReturnRequested: returnDialog.open()
    }

    function units(text, places) {
        var digits = String(text || "0")
        if (!/^[0-9]+$/.test(digits)) return "?"
        while (digits.length <= places) digits = "0" + digits
        var whole = digits.slice(0, digits.length - places), part = digits.slice(digits.length - places).replace(/0+$/, "")
        return part === "" ? whole : whole + "." + part
    }
    function coin(chainId) { var id = Number(chainId); return id === 56 || id === 97 ? "BNB" : "ETH" }
    function network(chainId) {
        var id = Number(chainId)
        return id === 1 ? qsTr("Ethereum Mainnet") : id === 56 ? qsTr("BNB Smart Chain") : id === 11155111 ? qsTr("Sepolia (test network)")
             : id === 97 ? qsTr("BNB Smart Chain Testnet") : qsTr("chain %1").arg(id)
    }

    onPairingChanged: if ((pairing.id || "") !== "") pairDialog.open(); else pairDialog.close()
    onApprovalWantedChanged: if (approvalWanted && dismissedTrade !== trade.commitment) approveDialog.open(); else if (!approvalWanted) approveDialog.close()

    Popup {
        id: pairDialog
        objectName: "dexWebPairDialog"
        modal: true; focus: true; closePolicy: Popup.NoAutoClose
        x: Math.round((prompt.width - width) / 2); y: Math.round((prompt.height - height) / 2)
        width: Math.min(560, prompt.width - 40); padding: 24
        background: Rectangle { color: "#202b36"; radius: 10; border.color: "#526b80" }
        contentItem: ColumnLayout {
            spacing: 14
            M.TextPlain { text: qsTr("Connect the DEX web page?"); font.bold: true; font.pixelSize: 20; color: "#f0f5fa" }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#f0f5fa"
                text: qsTr("%1 asks to use this wallet for trading.").arg(prompt.pairing.origin || "")
            }
            M.TextPlain {
                objectName: "dexWebPairCode"
                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 34; font.bold: true; color: "#f0f5fa"
                text: prompt.pairing.code || ""
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: "#f0f5fa"
                text: qsTr("Accept only if the page shows the same code.\n\nWhat you receive from trades is sent to this account:\n%1").arg(prompt.pairing.account || "")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#a9b8c6"
                text: qsTr("The page can show your trading balance and ask to post or take offers. It cannot start a trade: you approve each one here.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 12
                M.StandardButton { objectName: "dexWebPairRefuse"; text: qsTr("Refuse"); primary: false; onClicked: prompt.web.refusePairing(prompt.pairing.id) }
                M.StandardButton { objectName: "dexWebPairAccept"; text: qsTr("Accept"); onClicked: prompt.web.acceptPairing(prompt.pairing.id) }
            }
        }
    }

    Popup {
        id: approveDialog
        objectName: "dexWebApproveDialog"
        modal: true; focus: true; closePolicy: Popup.NoAutoClose
        x: Math.round((prompt.width - width) / 2); y: Math.round((prompt.height - height) / 2)
        width: Math.min(560, prompt.width - 40); padding: 24
        background: Rectangle { color: "#202b36"; radius: 10; border.color: "#526b80" }
        readonly property bool buying: (prompt.trade.snapshot || ({})).role === "owner"
        readonly property string coinText: prompt.units(prompt.offer.value, 18) + " " + prompt.coin(prompt.offer.chainId)
        readonly property string xmzText: prompt.units(prompt.offer.xmzAmount, 11) + " XMZ"
        contentItem: ColumnLayout {
            spacing: 14
            M.TextPlain { text: qsTr("Approve this trade?"); font.bold: true; font.pixelSize: 20; color: "#f0f5fa" }
            M.TextPlain {
                objectName: "dexWebApproveTerms"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 17; color: "#f0f5fa"
                text: approveDialog.buying
                    ? qsTr("You pay %1\nYou receive %2").arg(approveDialog.coinText).arg(approveDialog.xmzText)
                    : qsTr("You pay %1\nYou receive %2").arg(approveDialog.xmzText).arg(approveDialog.coinText)
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: "#a9b8c6"
                text: qsTr("Network: %1\nThese amounts are from the signed offer this wallet holds, not from the web page. Once approved the swap runs by itself; keep the wallet open until it is finished. If the other side stops, your funds are returned after the time limit.\n%2 left in the trade account afterwards goes to %3")
                    .arg(prompt.network(prompt.offer.chainId)).arg(prompt.coin(prompt.offer.chainId)).arg(prompt.link.account || "")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.errorColor
                visible: text !== ""; text: prompt.trade.startRefusal || ""
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 12
                M.StandardButton { objectName: "dexWebApproveCancel"; text: qsTr("Cancel this trade"); primary: false; onClicked: prompt.dex.abandonEvmTrade() }
                M.StandardButton { objectName: "dexWebApproveLater"; text: qsTr("Not now"); primary: false; onClicked: { prompt.dismissedTrade = prompt.trade.commitment; approveDialog.close() } }
                M.StandardButton { objectName: "dexWebApprove"; text: qsTr("Approve"); onClicked: prompt.dex.approveEvmTrade(prompt.trade.commitment) }
            }
        }
    }

    Popup {
        id: keysDialog
        objectName: "dexWebKeysDialog"
        modal: true; focus: true; closePolicy: Popup.NoAutoClose
        x: Math.round((prompt.width - width) / 2); y: Math.round((prompt.height - height) / 2)
        width: Math.min(560, prompt.width - 40); padding: 24
        background: Rectangle { color: "#202b36"; radius: 10; border.color: "#526b80" }
        readonly property bool fresh: prompt.wallet.keysExist !== true
        contentItem: ColumnLayout {
            spacing: 14
            M.TextPlain {
                objectName: "dexWebKeysTitle"
                text: keysDialog.fresh ? qsTr("Choose a trading password") : qsTr("Enter your trading password"); font.bold: true; font.pixelSize: 20; color: "#f0f5fa"
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#f0f5fa"
                text: prompt.wallet.keysBusy === true ? qsTr("Working… this can take a minute.")
                    : keysDialog.fresh ? qsTr("This makes your trading keys. They are kept encrypted on this device, and nobody can restore the password for you. You will be asked for it each time you start trading.")
                    : qsTr("It unlocks your trading keys until you close the wallet.")
            }
            M.LineEdit { id: keysPassword; objectName: "dexWebKeysPassword"; Layout.fillWidth: true; password: true
                placeholderText: keysDialog.fresh ? qsTr("New trading password") : qsTr("Trading password") }
            M.LineEdit { id: keysRepeat; objectName: "dexWebKeysRepeat"; Layout.fillWidth: true; password: true
                visible: keysDialog.fresh; placeholderText: qsTr("Repeat the password") }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.errorColor
                visible: text !== ""; text: prompt.wallet.keysMessage || ""
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 12
                M.StandardButton { objectName: "dexWebKeysLater"; text: qsTr("Not now"); primary: false
                    onClicked: { keysPassword.text = ""; keysRepeat.text = ""; prompt.web.dismissKeys() } }
                M.StandardButton {
                    objectName: "dexWebKeysGo"
                    text: prompt.wallet.keysBusy === true ? qsTr("Working…") : keysDialog.fresh ? qsTr("Create trading keys") : qsTr("Unlock")
                    enabled: prompt.wallet.keysBusy !== true && keysPassword.text !== "" && (!keysDialog.fresh || keysPassword.text === keysRepeat.text)
                    onClicked: { prompt.dex.setUpTradingKeys(keysPassword.text); keysPassword.text = ""; keysRepeat.text = "" }
                }
            }
        }
    }

    Popup {
        id: returnDialog
        objectName: "dexWebReturnDialog"
        modal: true; focus: true; closePolicy: Popup.NoAutoClose
        x: Math.round((prompt.width - width) / 2); y: Math.round((prompt.height - height) / 2)
        width: Math.min(560, prompt.width - 40); padding: 24
        background: Rectangle { color: "#202b36"; radius: 10; border.color: "#526b80" }
        property string note: ""
        onOpened: note = ""
        contentItem: ColumnLayout {
            spacing: 14
            M.TextPlain { text: qsTr("Move your trading XMZ back?"); font.bold: true; font.pixelSize: 20; color: "#f0f5fa" }
            M.TextPlain {
                objectName: "dexWebReturnText"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#f0f5fa"
                text: returnDialog.note !== "" ? returnDialog.note
                    : qsTr("%1 XMZ is ready in your trading wallet. All of it goes back to this wallet's main balance, minus the network fee.").arg(prompt.wallet.unlocked || "0")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 12
                M.StandardButton { objectName: "dexWebReturnClose"; text: returnDialog.note !== "" ? qsTr("Close") : qsTr("Cancel"); primary: false; onClicked: returnDialog.close() }
                M.StandardButton {
                    objectName: "dexWebReturnGo"; text: qsTr("Move it back")
                    visible: returnDialog.note === ""
                    enabled: prompt.wallet.busy !== true && Number(prompt.wallet.unlocked) > 0 && prompt.mainAddress() !== ""
                    onClicked: returnDialog.note = prompt.dex.sendFromDexWallet(prompt.mainAddress(), "")
                        ? qsTr("Sent. It shows in your main balance after a few minutes.") : qsTr("That could not start now. Try again in a moment.")
                }
            }
        }
    }

    // While a trade runs: one line at the bottom of the wallet, whatever page is open, so it is
    // clear that something is in progress and that the wallet has to stay open
    Rectangle {
        id: tradeBar
        objectName: "dexWebTradeBar"
        readonly property var snapshot: prompt.trade.snapshot || ({})
        readonly property bool running: prompt.trade.configured === true && snapshot.done !== true && (snapshot.stage || "") !== ""
            && (snapshot.approved === true || prompt.trade.awaitingApproval === true)
        readonly property var steps: snapshot.role === "owner" ? ["Start", "LockSent", "Locked", "ReadySent", "Ready", "Sweeping"]
            : ["WaitingForEvmLock", "XmzLockSent", "XmzLocked", "ClaimSent"]
        readonly property var texts: ({
            "Start": qsTr("waiting for your approval"), "LockSent": qsTr("locking your coin"), "Locked": qsTr("waiting for the seller's XMZ"),
            "ReadySent": qsTr("releasing the swap"), "Ready": qsTr("waiting for the seller to claim"), "Sweeping": qsTr("your XMZ is arriving"),
            "WaitingForEvmLock": qsTr("waiting for the buyer's coin"), "XmzLockSent": qsTr("locking your XMZ"), "XmzLocked": qsTr("waiting for the buyer to release"),
            "ClaimSent": qsTr("claiming your coin"), "RefundSent": qsTr("refunding your coin"), "WaitingForRefund": qsTr("waiting for the buyer's refund"),
            "SweepingBack": qsTr("taking your XMZ back") })
        readonly property var counted: {
            var p = snapshot.progress || ({})
            var c = snapshot.stage === "Sweeping" || snapshot.stage === "SweepingBack" ? p.xmzSweep : (snapshot.stage === "Locked" || snapshot.stage === "XmzLockSent") ? p.xmzLock : null
            return c && c.needed ? qsTr(" (%1 of %2 confirmations)").arg(c.confirmations).arg(c.needed) : ""
        }
        visible: running
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 34; color: "#16303a"; border.color: "#2f6f7a"
        M.TextPlain {
            objectName: "dexWebTradeBarText"
            anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 16; anchors.right: parent.right; anchors.rightMargin: 16
            elide: Text.ElideRight; color: "#f0f5fa"; font.pixelSize: 13; themeTransition: false
            text: {
                var at = tradeBar.steps.indexOf(tradeBar.snapshot.stage || "")
                var where = at >= 0 ? qsTr("step %1 of %2: ").arg(at + 1).arg(tradeBar.steps.length) : ""
                return qsTr("DEX trade in progress, %1%2%3. Keep the wallet open.").arg(where).arg(tradeBar.texts[tradeBar.snapshot.stage] || tradeBar.snapshot.stage || "").arg(tradeBar.counted)
            }
        }
    }
}
