import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as MonzeroComponents

// Optional: connect your own wallet for one coin by its address (MetaMask for BNB; any Bitcoin,
// Litecoin or Bitcoin Cash wallet). Only the address is kept: no keys, no access.
//  - BNB: trades can be funded from it, and what a trade pays out is sent back to it.
//  - BTC / LTC / BCH: it becomes the trade's destination: where the coin you receive is paid when
//    you sell XMZ, and where your coin is refunded if a trade you pay into does not complete.
// The app still runs every swap step and refund itself. The engine checks every destination
// against the trade's network again when a trade is set up.
Rectangle {
    id: wallet
    property string coin: "BNB"
    property string address: ""                 // the connected address, "" when none
    signal connectRequested(string address)
    signal disconnectRequested()
    property bool editing: false
    readonly property bool valid: validAddress(coin, input.text.trim())

    color: MonzeroComponents.Style.surfaceRaised
    radius: 8
    border.color: MonzeroComponents.Style.accentMuted
    implicitHeight: col.implicitHeight + 24

    // Address formats the swap engine accepts as destinations (mainnet and test networks). Litecoin
    // destinations must be native SegWit (ltc1/tltc1/rltc1): the Litecoin swap pays only those.
    function validAddress(c, a) {
        var bech32 = "[02-9ac-hj-np-z]"
        var base58 = "[1-9A-HJ-NP-Za-km-z]"
        if (c === "BNB" || c === "ETH") return /^0x[0-9a-fA-F]{40}$/.test(a)
        if (c === "BTC") return new RegExp("^(bc1|tb1|bcrt1)" + bech32 + "{11,71}$").test(a)
                             || new RegExp("^[13mn2]" + base58 + "{25,34}$").test(a)
        if (c === "LTC") return new RegExp("^(ltc1|tltc1|rltc1)" + bech32 + "{11,71}$").test(a)
        if (c === "BCH") return new RegExp("^((bitcoincash|bchtest|bchreg):)?[qp]" + bech32 + "{41}$").test(a)
        return false
    }
    function walletName(c) {
        return c === "BNB" || c === "ETH" ? qsTr("MetaMask") : c === "BTC" ? qsTr("Bitcoin wallet")
             : c === "LTC" ? qsTr("Litecoin wallet") : c === "BCH" ? qsTr("Bitcoin Cash wallet") : c
    }
    function placeholder(c) {
        return c === "BNB" || c === "ETH" ? qsTr("Your MetaMask address (0x…)") : c === "BTC" ? qsTr("Your Bitcoin address (bc1…)")
             : c === "LTC" ? qsTr("Your Litecoin address (ltc1…)") : qsTr("Your Bitcoin Cash address (bitcoincash:q…)")
    }
    function shortAddress(a) {
        var plain = a.indexOf(":") >= 0 ? a.slice(a.indexOf(":") + 1) : a
        return plain.length > 14 ? plain.slice(0, 7) + "…" + plain.slice(-5) : plain
    }

    ColumnLayout {
        id: col
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            MonzeroComponents.TextPlain {
                themeTransition: false; color: MonzeroComponents.Style.defaultFontColor; font.pixelSize: 14
                text: wallet.coin
                font.family: MonzeroComponents.Style.fontBold.name
            }
            MonzeroComponents.TextPlain {
                themeTransition: false; color: MonzeroComponents.Style.dimmedFontColor; font.pixelSize: 12
                Layout.fillWidth: true
                text: wallet.address ? qsTr("%1 connected").arg(wallet.walletName(wallet.coin)) : qsTr("%1 (optional)").arg(wallet.walletName(wallet.coin))
            }
            MonzeroComponents.TextPlain {
                objectName: "walletAddress"
                visible: wallet.address !== "" && !wallet.editing
                themeTransition: false; color: MonzeroComponents.Style.accent; font.pixelSize: 12
                text: wallet.shortAddress(wallet.address)
            }
        }
        MonzeroComponents.TextPlain {
            visible: wallet.editing || wallet.address === ""
            themeTransition: false; color: MonzeroComponents.Style.dimmedFontColor; font.pixelSize: 11
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: wallet.coin === "BNB" || wallet.coin === "ETH"
                ? qsTr("Fund BNB trades from MetaMask and receive BNB back to it. The app still runs the swap and any refund automatically.")
                : qsTr("%1 you receive is paid straight to this wallet, and if a trade you pay %1 into does not complete, it is refunded here.").arg(wallet.coin)
                  + (wallet.coin === "LTC" ? " " + qsTr("Use a native SegWit address (ltc1…).") : "")
        }
        RowLayout {
            visible: wallet.editing
            Layout.fillWidth: true
            MonzeroComponents.LineEdit {
                id: input
                objectName: "walletInput"
                Layout.fillWidth: true
                placeholderText: wallet.placeholder(wallet.coin)
                fontSize: 13
                error: text !== "" && !wallet.valid
            }
            MonzeroComponents.StandardButton {
                objectName: "walletSave"
                small: true
                enabled: wallet.valid
                text: qsTr("Connect")
                onClicked: { wallet.connectRequested(input.text.trim()); wallet.editing = false }
            }
        }
        RowLayout {
            visible: !wallet.editing
            MonzeroComponents.StandardButton {
                objectName: "walletConnect"
                small: true; primary: wallet.address === ""
                text: wallet.address ? qsTr("Change") : qsTr("Connect")
                onClicked: { input.text = wallet.address; wallet.editing = true }
            }
            MonzeroComponents.StandardButton {
                objectName: "walletDisconnect"
                visible: wallet.address !== ""
                small: true; primary: false
                text: qsTr("Disconnect")
                onClicked: wallet.disconnectRequested()
            }
        }
    }
}
