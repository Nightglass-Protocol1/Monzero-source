import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id: panel
    property var backend
    property var funding: ({})
    property var formatAmount
    property var parseAmount
    // Swap chain shown in labels: "LTC" (default) or "BTC".
    property string coin: "LTC"
    property string coinName: coin === "BTC" ? "Bitcoin" : "Litecoin"
    function chainText(text) {
        return coin === "LTC" ? text : text.replace(/Litecoin/g, coinName).replace(/\bLTC\b/g, coin)
    }
    property string message: ""
    property var terms: funding.terms || ({})
    property var payment: funding.retainedPayment || ({})
    property var review: funding.review || ({})
    property string reviewKind: funding.reviewOperation || ""
    property string reviewKey: (review.commitment || "") + ":" + reviewKind
    property bool idle: funding.configured === true && funding.busy !== true && funding.valid !== false
    property bool hasPayment: !!payment.address
    spacing: 10
    onReviewKeyChanged: clearApproval()
    onVisibleChanged: if (!visible) clearApproval()
    function clearApproval() { if (approval) approval.checked = false; if (password) password.text = "" }
    function started(ok) { message = ok ? "" : qsTr("This action could not start. Check the selected wallet and trade status.") }
    function amount(value) { return formatAmount ? formatAmount(value) : "—" }

    M.TextPlain { Layout.fillWidth: true; text: panel.chainText(qsTr("Fund this trade from your local Litecoin wallet · regtest")); color: M.Style.defaultFontColor; wrapMode: Text.WordWrap }
    M.StandardButton {
        objectName: "lightDexSelectWallet"
        visible: panel.funding.configured !== true
        enabled: panel.funding.selectionAvailable === true
        text: panel.chainText(qsTr("Use local Litecoin wallet"))
        onClicked: panel.started(panel.backend.useLitecoinLightWallet())
    }
    M.TextPlain {
        Layout.fillWidth: true; visible: panel.funding.configured !== true && panel.funding.selectionAvailable !== true
        text: panel.chainText(qsTr("Open your Litecoin regtest wallet and the private trade to select local funding."))
        color: M.Style.defaultFontColor; wrapMode: Text.WordWrap
    }
    M.TextPlain {
        Layout.fillWidth: true; visible: panel.funding.configured === true
        text: qsTr("The trade must refund to an address already saved in this local wallet. If the address differs, agree a new quote before funding.")
        color: M.Style.defaultFontColor; wrapMode: Text.WordWrap
    }
    M.StandardButton {
        objectName: "lightDexTerms"
        visible: panel.funding.configured === true; enabled: panel.idle
        text: qsTr("Review negotiated funding details")
        onClicked: panel.started(panel.backend.queryLitecoinLightTerms())
    }
    M.TextPlain {
        objectName: "lightDexTermsSummary"; Layout.fillWidth: true
        visible: !!panel.terms.contractAddress; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor
        text: visible ? qsTr("Swap amount: ") + panel.amount(panel.terms.amountAtomic) + " " + panel.coin + "\n"
            + qsTr("Maximum network fee: ") + panel.amount(panel.terms.maximumFeeAtomic) + " " + panel.coin + "\n"
            + qsTr("Contract: ") + panel.terms.contractAddress + "\n" + qsTr("Refund address: ") + panel.terms.refundAddress
            + "\n" + qsTr("Required lock confirmations: ") + panel.terms.lockConfirmations : ""
    }
    TextField {
        id: fee; objectName: "lightDexFee"; Layout.fillWidth: true
        visible: !!panel.terms.contractAddress && !panel.hasPayment
        enabled: panel.idle; placeholderText: panel.chainText(qsTr("Network fee in LTC"))
    }
    M.StandardButton {
        objectName: "lightDexPrepare"; visible: !panel.hasPayment && !!panel.terms.contractAddress
        enabled: panel.idle && panel.funding.prepareAvailable === true && panel.funding.peerReady === true && !!panel.parseAmount && panel.parseAmount(fee.text) !== ""
        text: qsTr("Reserve funding and exchange recovery")
        onClicked: panel.started(panel.backend.prepareLitecoinLightFunding(panel.parseAmount(fee.text), 1))
    }
    M.StandardButton {
        objectName: "lightDexReopen"; visible: panel.hasPayment
        enabled: panel.idle && panel.funding.prepareAvailable === true && panel.funding.peerReady === true
        text: qsTr("Reopen funding reservation")
        onClicked: panel.started(panel.backend.reopenLitecoinLightFunding())
    }
    M.StandardButton {
        objectName: "lightDexResumeRecovery"
        visible: panel.hasPayment && panel.funding.recoveryComplete !== true
        enabled: panel.idle && panel.funding.peerReady === true
        text: qsTr("Resume saved recovery exchange")
        onClicked: panel.started(panel.backend.resumeLitecoinLightRecovery())
    }
    RowLayout {
        visible: panel.hasPayment
        M.StandardButton { objectName: "lightDexReviewSign"; enabled: panel.idle; text: qsTr("Review signing"); onClicked: panel.started(panel.backend.reviewLitecoinLightSignature()) }
        M.StandardButton { objectName: "lightDexReviewSend"; enabled: panel.idle; text: qsTr("Review sending"); onClicked: panel.started(panel.backend.reviewLitecoinLightSend()) }
    }
    M.TextPlain {
        objectName: "lightDexReviewSummary"; Layout.fillWidth: true
        visible: !!panel.review.commitment; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor
        text: visible ? (panel.reviewKind === "review" ? qsTr("Review local signing") : panel.reviewKind === "retry-review" ? qsTr("Review sending the same saved transaction again") : qsTr("Review sending"))
            + "\n" + qsTr("Amount: ") + panel.amount(panel.payment.amountAtomic) + " " + panel.coin + " · " + qsTr("Fee: ") + panel.amount(panel.payment.feeAtomic) + " " + panel.coin + ""
            + "\n" + qsTr("Contract: ") + panel.payment.address + "\n" + qsTr("Transaction: ") + panel.review.details.txid
            + (panel.review.details.endpoint ? "\n" + qsTr("Broadcast server: ") + panel.review.details.endpoint.host + ":" + panel.review.details.endpoint.port : "") : ""
    }
    TextField {
        id: password; objectName: "lightDexPassword"; Layout.fillWidth: true
        visible: !!panel.review.commitment && panel.reviewKind === "review"
        enabled: panel.idle; placeholderText: qsTr("Local wallet password"); echoMode: TextInput.Password
        inputMethodHints: Qt.ImhHiddenText | Qt.ImhNoPredictiveText
        onVisibleChanged: if (!visible) text = ""
    }
    M.CheckBox {
        id: approval; objectName: "lightDexApproval"
        visible: !!panel.review.commitment; enabled: panel.idle
        text: panel.reviewKind === "review" ? qsTr("I approve signing this transaction locally.") : qsTr("I approve sending this exact transaction.")
    }
    M.StandardButton {
        objectName: "lightDexApprove"; visible: !!panel.review.commitment
        enabled: panel.idle && approval.checked && (panel.reviewKind !== "review" || password.text.length > 0)
        text: panel.reviewKind === "review" ? qsTr("Sign locally") : panel.reviewKind === "retry-review" ? qsTr("Retry saved transaction") : qsTr("Send transaction")
        onClicked: {
            var commitment = panel.review.commitment
            var secret = password.text
            approval.checked = false; password.text = ""
            if (panel.reviewKind === "review") panel.started(panel.backend.signLitecoinLightFunding(commitment, secret))
            else if (panel.reviewKind === "send-review") panel.started(panel.backend.sendLitecoinLightFunding(commitment))
            else if (panel.reviewKind === "retry-review") panel.started(panel.backend.retryLitecoinLightFunding(commitment))
            secret = ""
        }
    }
    RowLayout {
        visible: panel.hasPayment
        M.StandardButton { objectName: "lightDexObserve"; enabled: panel.idle; text: qsTr("Check confirmation"); onClicked: panel.started(panel.backend.observeLitecoinLightFunding()) }
        M.StandardButton { objectName: "lightDexReviewRetry"; enabled: panel.idle; text: qsTr("Review retry"); onClicked: panel.started(panel.backend.reviewLitecoinLightRetry()) }
    }
    M.TextPlain {
        objectName: "lightDexOutcome"; Layout.fillWidth: true; visible: panel.funding.configured === true
        textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor
        text: {
            var result = panel.funding.result || {}
            var observed = result.observation || {}
            if (panel.funding.busy) return qsTr("Working on the saved trade…")
            if (panel.message) return panel.message
            if (panel.funding.valid === false) return qsTr("The funding context does not match the selected trade.")
            if (panel.funding.state === "outcome-unconfirmed") return qsTr("The last action could not be verified. Check the saved transaction before trying again.")
            if (observed.position === "included") return qsTr("Funding confirmed: ") + observed.confirmations + "\n" + (result.txid || "")
            if (observed.position === "reported-pending") return qsTr("Funding reported pending; awaiting confirmation.")
            if (observed.position === "not-reported") return qsTr("The server has not reported this transaction. This does not establish that it was never sent.")
            if (panel.funding.operation === "sign" && result.signed) return qsTr("Signed locally. Review sending when ready.")
            if (result.broadcastStatus === "server-acknowledged") return qsTr("The server acknowledged funding. Check for confirmation.")
            if (panel.hasPayment) return qsTr("The exact payment is saved. Signing requires saved recovery data from both sides.")
            return qsTr("Review the negotiated details and network fee before reserving funds.")
        }
    }
}
