// Copyright (c) 2026, The Monzero Project
// Distributed under the BSD 3-Clause license.
import QtQuick 2.9
import QtQuick.Layouts 1.1
import "." as M

// The wallet's DEX page, kept simple: trading happens on the DEX web page; here the user makes
// (or unlocks) the trading keys, opens the website, sees what is connected and how much XMZ is
// ready to trade. Offers, prices and charts live on the website. Approving a trade and accepting
// a connection are asked by DexWebPrompt, on top of whatever page is open.
ColumnLayout {
    id: card
    property var dex: null
    property var web: null
    property string tradeCoin: "ETH"
    property string site: "https://monzero.org/dex/"
    signal fullScreenRequested()

    readonly property var w: dex ? (dex.dexWalletStatus || ({})) : ({})
    readonly property var link: web ? (web.connection || ({})) : ({})
    readonly property var trade: dex ? (dex.evmTradeStatus || ({})) : ({})
    readonly property bool keysReady: w.keysReady === true
    readonly property bool connected: (link.account || "") !== ""
    property string tradeError: ""
    property string moveNote: ""
    spacing: 14

    function mainAddress() {
        return typeof appWindow !== "undefined" && appWindow.currentWallet ? appWindow.currentWallet.address(0, 0) : ""
    }

    M.Label { fontSize: 25; text: qsTr("Monzero DEX") }
    M.TextPlain {
        objectName: "dexConnectIntro"
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.dimmedFontColor; themeTransition: false
        text: qsTr("You trade on the DEX website. This wallet keeps your trading keys, moves the XMZ, and asks you to approve each trade. Keep it open while you trade.")
    }

    // ---- 1. Trading keys
    Rectangle {
        Layout.fillWidth: true; radius: 8; color: "transparent"; border.color: M.Style.accentMuted
        Layout.preferredHeight: keysColumn.implicitHeight + 28
        ColumnLayout {
            id: keysColumn
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
            spacing: 8
            M.TextPlain {
                objectName: "dexConnectKeysTitle"
                themeTransition: false; font.bold: true; font.pixelSize: 16; color: M.Style.defaultFontColor
                text: (card.keysReady ? "✓ " : "1. ") + (card.keysReady ? qsTr("Trading keys unlocked")
                    : card.w.keysExist === true ? qsTr("Unlock your trading keys") : qsTr("Create your trading keys"))
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.dimmedFontColor; themeTransition: false
                visible: !card.keysReady
                text: card.w.keysBusy === true ? qsTr("Working… this can take a minute.")
                    : card.w.keysExist === true ? qsTr("Enter your trading password.")
                    : qsTr("Choose a password. The keys are made and kept encrypted on this device. Nobody can restore the password for you.")
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !card.keysReady
                M.LineEdit { id: password; objectName: "dexConnectPassword"; Layout.fillWidth: true; password: true
                    placeholderText: card.w.keysExist === true ? qsTr("Trading password") : qsTr("New trading password") }
                M.LineEdit { id: repeat; objectName: "dexConnectPasswordRepeat"; Layout.fillWidth: true; password: true
                    visible: card.w.keysExist !== true; placeholderText: qsTr("Repeat the password") }
                M.StandardButton {
                    objectName: "dexConnectKeys"
                    text: card.w.keysBusy === true ? qsTr("Working…") : card.w.keysExist === true ? qsTr("Unlock") : qsTr("Create trading keys")
                    enabled: card.w.keysBusy !== true && password.text !== "" && (card.w.keysExist === true || password.text === repeat.text)
                    onClicked: { card.dex.setUpTradingKeys(password.text); password.text = ""; repeat.text = "" }
                }
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.errorColor; themeTransition: false
                visible: (card.w.keysMessage || "") !== ""
                text: card.w.keysMessage || ""
            }
        }
    }

    // ---- 2. The website
    Rectangle {
        Layout.fillWidth: true; radius: 8; color: "transparent"; border.color: M.Style.accentMuted
        Layout.preferredHeight: siteColumn.implicitHeight + 28
        opacity: card.keysReady ? 1 : 0.5
        ColumnLayout {
            id: siteColumn
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
            spacing: 8
            M.TextPlain {
                objectName: "dexConnectSiteTitle"
                themeTransition: false; font.bold: true; font.pixelSize: 16; color: M.Style.defaultFontColor
                text: card.connected ? "✓ " + qsTr("Connected to the DEX website") : "2. " + qsTr("Open the DEX website and connect")
            }
            M.TextPlain {
                objectName: "dexConnectSiteStatus"
                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: M.Style.dimmedFontColor; themeTransition: false
                text: card.connected ? qsTr("%1 · MetaMask account %2").arg(card.link.origin || "").arg(card.link.account || "")
                    : qsTr("On the website press Connect. A code appears there and here: if they match, accept it here.")
            }
            RowLayout {
                M.StandardButton {
                    objectName: "dexConnectOpenSite"
                    text: qsTr("Open the DEX website")
                    enabled: card.keysReady
                    onClicked: Qt.openUrlExternally(card.site)
                }
                M.StandardButton {
                    objectName: "dexConnectDisconnect"
                    primary: false
                    visible: card.connected
                    text: qsTr("Disconnect")
                    onClicked: card.web.disconnectPage()
                }
            }
        }
    }

    // ---- 3. XMZ ready to trade
    Rectangle {
        Layout.fillWidth: true; radius: 8; color: "transparent"; border.color: M.Style.accentMuted
        Layout.preferredHeight: xmzColumn.implicitHeight + 28
        visible: card.w.state !== undefined && card.w.state !== "unconfigured"
        ColumnLayout {
            id: xmzColumn
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
            spacing: 8
            M.TextPlain {
                themeTransition: false; font.bold: true; font.pixelSize: 16; color: M.Style.defaultFontColor
                text: qsTr("XMZ for trading")
            }
            M.TextPlain {
                objectName: "dexConnectBalance"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; themeTransition: false
                text: card.w.state === "ready" ? qsTr("%1 XMZ ready (%2 in total). XMZ you buy arrives here.").arg(card.w.unlocked).arg(card.w.balance)
                    : card.w.state === "failed" ? qsTr("The trading wallet is not running: %1").arg(card.w.error || "")
                    : qsTr("Starting the trading wallet…")
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.dimmedFontColor; themeTransition: false
                text: qsTr("To sell XMZ, first move it here from your main balance. To take XMZ out, move it back.")
            }
            RowLayout {
                visible: card.w.state === "ready"
                M.StandardButton {
                    objectName: "dexConnectAddXmz"
                    text: qsTr("Add XMZ…")
                    enabled: (card.w.address || "") !== ""
                    // Opens the wallet's own Send page with the trading address filled in; the user
                    // types the amount and confirms there as for any payment
                    onClicked: middlePanel.sendTo(card.w.address, "", qsTr("XMZ for trading on the DEX"))
                }
                M.StandardButton {
                    objectName: "dexConnectReturnXmz"
                    primary: false
                    text: qsTr("Move all back to my main balance")
                    enabled: card.w.busy !== true && Number(card.w.unlocked) > 0 && card.mainAddress() !== ""
                    onClicked: card.moveNote = card.dex.sendFromDexWallet(card.mainAddress(), "")
                        ? qsTr("Sending it back; it shows in your main balance after a few minutes.") : qsTr("That could not start now. Try again in a moment.")
                }
            }
            M.TextPlain {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.dimmedFontColor; themeTransition: false
                visible: card.moveNote !== ""
                text: card.moveNote
            }
        }
    }

    // ---- The trade in progress (approval is also asked in a prompt)
    M.DexTradeProgress {
        objectName: "dexConnectTrade"
        Layout.fillWidth: true
        visible: card.trade.configured === true
        trade: card.trade
        coin: card.tradeCoin
        errorText: card.tradeError
        externalWallet: card.link.account || ""
        onApprove: card.tradeError = card.dex.approveEvmTrade(commitment) ? "" : qsTr("Approval could not start.")
        onRefresh: card.tradeError = card.dex.refreshEvmTrade() ? "" : qsTr("Refresh could not start.")
        onPayoutRequested: card.tradeError = card.dex.payoutEvmTrade(card.link.account || "") ? "" : qsTr("The payout could not start.")
    }

    M.TextPlain {
        objectName: "dexConnectFullScreen"
        themeTransition: false; color: M.Style.dimmedFontColor; font.pixelSize: 13
        text: qsTr("Trade inside the wallet instead (advanced)") + " ›"
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: card.fullScreenRequested() }
    }
}
