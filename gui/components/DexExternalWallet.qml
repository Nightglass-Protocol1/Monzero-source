import QtQuick 2.9
import QtQuick.Layouts 1.1

import "." as MonzeroComponents

// Optional: connect your own EVM wallet (MetaMask) by its address. BNB trades can then be funded
// from it, and what a trade pays out goes back to it. The app still runs every swap step and
// refund itself, so nothing waits on the phone. Only the address is kept; no keys, no access.
Rectangle {
    id: wallet
    property string address: ""                 // the connected address, "" when none
    signal connectRequested(string address)
    signal disconnectRequested()
    property bool editing: false
    readonly property bool valid: /^0x[0-9a-fA-F]{40}$/.test(input.text.trim())

    color: MonzeroComponents.Style.surfaceRaised
    radius: 8
    border.color: MonzeroComponents.Style.accentMuted
    implicitHeight: col.implicitHeight + 28

    function shortAddress(a) { return a.length > 12 ? a.slice(0, 6) + "…" + a.slice(-4) : a }

    ColumnLayout {
        id: col
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            MonzeroComponents.TextPlain {
                themeTransition: false; color: MonzeroComponents.Style.defaultFontColor; font.pixelSize: 14
                Layout.fillWidth: true
                text: wallet.address ? qsTr("MetaMask connected") : qsTr("MetaMask (optional)")
            }
            MonzeroComponents.TextPlain {
                objectName: "walletAddress"
                visible: wallet.address !== "" && !wallet.editing
                themeTransition: false; color: MonzeroComponents.Style.accent; font.pixelSize: 13
                text: wallet.shortAddress(wallet.address)
            }
        }
        MonzeroComponents.TextPlain {
            themeTransition: false; color: MonzeroComponents.Style.dimmedFontColor; font.pixelSize: 12
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: wallet.address
                ? qsTr("BNB trades can be funded from this wallet, and BNB you receive or get refunded can be sent back to it.")
                : qsTr("Fund BNB trades from MetaMask and receive BNB back to it. The app still runs the swap and any refund automatically.")
        }
        RowLayout {
            visible: wallet.editing
            Layout.fillWidth: true
            MonzeroComponents.LineEdit {
                id: input
                objectName: "walletInput"
                Layout.fillWidth: true
                placeholderText: qsTr("Your MetaMask address (0x…)")
                fontSize: 14
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
        MonzeroComponents.TextPlain {
            visible: wallet.editing
            themeTransition: false; color: MonzeroComponents.Style.dimmedFontColor; font.pixelSize: 11
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: qsTr("In MetaMask, tap your account name to copy its address, then paste it here. Use the BNB Smart Chain network in MetaMask.")
        }
        RowLayout {
            visible: !wallet.editing
            MonzeroComponents.StandardButton {
                objectName: "walletConnect"
                small: true; primary: wallet.address === ""
                text: wallet.address ? qsTr("Change") : qsTr("Connect MetaMask")
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
