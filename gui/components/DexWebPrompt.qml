import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.1

import "." as M

// What the wallet asks the user when the DEX web page is in use: accept the page's connection
// (compare the code), and approve each trade from the terms the wallet itself holds. The page
// cannot answer either question.
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
                M.StandardButton { objectName: "dexWebApproveLater"; text: qsTr("Not now"); primary: false; onClicked: { prompt.dismissedTrade = prompt.trade.commitment; approveDialog.close() } }
                M.StandardButton { objectName: "dexWebApprove"; text: qsTr("Approve"); onClicked: prompt.dex.approveEvmTrade(prompt.trade.commitment) }
            }
        }
    }
}
