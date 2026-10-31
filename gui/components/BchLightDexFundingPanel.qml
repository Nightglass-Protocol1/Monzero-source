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
    property string message: ""
    property var terms: funding.terms || ({})
    property var payment: funding.retainedPayment || ({})
    property var review: funding.review || ({})
    property string reviewKind: funding.reviewOperation || ""
    property string reviewKey: (review.commitment || "") + ":" + reviewKind
    property bool idle: funding.configured === true && funding.busy !== true && funding.valid !== false && funding.contextReady === true
    property bool hasPayment: !!payment.address
    spacing: 10
    onReviewKeyChanged: clearApproval()
    onVisibleChanged: if (!visible) clearApproval()
    onIdleChanged: if (!idle) clearApproval()
    function clearApproval() { if (approval) approval.checked = false; if (password) password.text = "" }
    function started(ok) { message = ok ? "" : qsTr("This action could not start. Check the selected wallet and trade status.") }
    function amount(value) { return formatAmount ? formatAmount(value) : "—" }

    M.TextPlain { Layout.fillWidth: true; text: qsTr("Fund this trade from your local BCH wallet · regtest"); color: M.Style.defaultFontColor; wrapMode: Text.WordWrap }
    M.StandardButton {
        objectName: "bchLightDexSelectWallet"
        visible: panel.funding.configured !== true
        enabled: panel.funding.selectionAvailable === true
        text: qsTr("Use local BCH wallet")
        onClicked: panel.started(panel.backend.useBchLightWallet())
    }
    M.TextPlain {
        Layout.fillWidth: true; visible: panel.funding.configured !== true && panel.funding.selectionAvailable !== true
        text: qsTr("Open your BCH regtest wallet and the private trade to select local funding.")
        color: M.Style.defaultFontColor; wrapMode: Text.WordWrap
    }
    M.TextPlain {
        Layout.fillWidth: true; visible: panel.funding.configured === true
        text: qsTr("The trade must refund to an address already saved in this local wallet. If the address differs, agree a new quote before funding.")
        color: M.Style.defaultFontColor; wrapMode: Text.WordWrap
    }
    M.StandardButton {
        objectName: "bchLightDexSelectSource"
        visible: panel.funding.configured === true
        enabled: panel.idle
        text: panel.funding.walletSourceSelected === true ? qsTr("Recheck selected wallet") : qsTr("Confirm this wallet for the trade")
        onClicked: panel.started(panel.backend.selectBchLightFundingSource())
    }
    M.StandardButton {
        objectName: "bchLightDexTerms"
        visible: panel.funding.configured === true; enabled: panel.idle
        text: qsTr("Review negotiated funding details")
        onClicked: panel.started(panel.backend.queryBchLightTerms())
    }
    M.TextPlain {
        objectName: "bchLightDexTermsSummary"; Layout.fillWidth: true
        visible: !!panel.terms.contractAddress; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor
        text: visible ? qsTr("Swap amount: ") + panel.amount(panel.terms.amountAtomic) + " BCH\n"
            + qsTr("Maximum network fee: ") + panel.amount(panel.terms.maximumFeeAtomic) + " BCH\n"
            + qsTr("Contract: ") + panel.terms.contractAddress + "\n" + qsTr("Refund address: ") + panel.terms.refundAddress
            + "\n" + qsTr("Required lock confirmations: ") + panel.terms.lockConfirmations : ""
    }
    TextField {
        id: fee; objectName: "bchLightDexFee"; Layout.fillWidth: true
        visible: !!panel.terms.contractAddress && !panel.hasPayment
        enabled: panel.idle; placeholderText: qsTr("Network fee in BCH")
    }
    M.StandardButton {
        objectName: "bchLightDexPrepare"; visible: !panel.hasPayment && !!panel.terms.contractAddress
        enabled: panel.idle && panel.funding.prepareAvailable === true && !!panel.parseAmount && panel.parseAmount(fee.text) !== ""
        text: qsTr("Reserve BCH funding")
        onClicked: panel.started(panel.backend.prepareBchLightFunding(panel.parseAmount(fee.text), panel.terms.lockConfirmations))
    }
    M.StandardButton {
        objectName: "bchLightDexReopen"; visible: panel.hasPayment
        enabled: panel.idle && panel.funding.prepareAvailable === true
        text: qsTr("Reopen funding reservation")
        onClicked: panel.started(panel.backend.reopenBchLightFunding())
    }
    M.StandardButton {
        objectName: "bchLightDexReviewRecovery"
        visible: !!panel.funding.retainedTxid
        enabled: panel.idle
        text: qsTr("Review saved refund protection")
        onClicked: panel.started(panel.backend.reviewBchLightRecovery())
    }
    RowLayout {
        visible: panel.hasPayment
        M.StandardButton { objectName: "bchLightDexReviewSign"; enabled: panel.idle; text: qsTr("Review signing"); onClicked: panel.started(panel.backend.reviewBchLightSignature()) }
        M.StandardButton { objectName: "bchLightDexReviewSend"; enabled: panel.idle; text: qsTr("Review sending"); onClicked: panel.started(panel.backend.reviewBchLightSend()) }
    }
    M.TextPlain {
        objectName: "bchLightDexReviewSummary"; Layout.fillWidth: true
        visible: !!panel.review.commitment; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor
        text: visible ? (panel.reviewKind === "review" ? qsTr("Review local signing") : panel.reviewKind === "recovery-review" ? qsTr("Review retaining refund protection locally") : panel.reviewKind === "retry-review" ? qsTr("Review sending the same saved transaction again") : qsTr("Review sending"))
            + "\n" + qsTr("Amount: ") + panel.amount(panel.payment.amountAtomic) + " BCH · " + qsTr("Fee: ") + panel.amount(panel.payment.feeAtomic) + " BCH"
            + "\n" + qsTr("Contract: ") + panel.payment.address + "\n" + qsTr("Transaction: ") + (panel.review.details.txid || panel.review.details.unsignedTxid || panel.review.details.fundingTxid || panel.funding.retainedTxid || "")
            + (panel.reviewKind === "recovery-review" ? "\n" + qsTr("Cancellation transaction: ") + (panel.review.details.cancelTxid || "")
                + "\n" + qsTr("Refund transaction: ") + (panel.review.details.refundTxid || "")
                + "\n" + qsTr("Punishment transaction: ") + (panel.review.details.punishTxid || "") : "")
            + (panel.review.details.endpoint ? "\n" + qsTr("Broadcast server: ") + panel.review.details.endpoint.host + ":" + panel.review.details.endpoint.port : "") : ""
    }
    TextField {
        id: password; objectName: "bchLightDexPassword"; Layout.fillWidth: true
        visible: !!panel.review.commitment && panel.reviewKind === "review"
        enabled: panel.idle; placeholderText: qsTr("Local wallet password"); echoMode: TextInput.Password
        inputMethodHints: Qt.ImhHiddenText | Qt.ImhNoPredictiveText
        onVisibleChanged: if (!visible) text = ""
    }
    M.CheckBox {
        id: approval; objectName: "bchLightDexApproval"
        visible: !!panel.review.commitment; enabled: panel.idle
        text: panel.reviewKind === "review" ? qsTr("I approve signing this transaction locally.") : panel.reviewKind === "recovery-review" ? qsTr("I approve saving the reviewed refund protection locally.") : qsTr("I approve sending this exact transaction.")
    }
    M.StandardButton {
        objectName: "bchLightDexApprove"; visible: !!panel.review.commitment
        enabled: panel.idle && panel.funding.approvalAvailable === true && approval.checked && (panel.reviewKind !== "review" || password.text.length > 0)
        text: panel.reviewKind === "review" ? qsTr("Sign locally") : panel.reviewKind === "recovery-review" ? qsTr("Save refund protection") : panel.reviewKind === "retry-review" ? qsTr("Retry saved transaction") : qsTr("Send transaction")
        onClicked: {
            var commitment = panel.review.commitment
            var secret = password.text
            approval.checked = false; password.text = ""
            if (panel.reviewKind === "review") panel.started(panel.backend.signBchLightFunding(commitment, secret))
            else if (panel.reviewKind === "recovery-review") panel.started(panel.backend.retainBchLightRecovery(commitment))
            else if (panel.reviewKind === "send-review") panel.started(panel.backend.sendBchLightFunding(commitment))
            else if (panel.reviewKind === "retry-review") panel.started(panel.backend.retryBchLightFunding(commitment))
            secret = ""
        }
    }
    RowLayout {
        visible: panel.hasPayment
        M.StandardButton { objectName: "bchLightDexObserve"; enabled: panel.idle; text: qsTr("Check confirmation"); onClicked: panel.started(panel.backend.observeBchLightFunding()) }
        M.StandardButton { objectName: "bchLightDexReviewRetry"; enabled: panel.idle; text: qsTr("Review retry"); onClicked: panel.started(panel.backend.reviewBchLightRetry()) }
    }
    M.StandardButton {
        objectName: "bchLightDexReconcile"
        visible: !!panel.funding.retainedTxid; enabled: panel.idle
        text: qsTr("Recover saved broadcast acknowledgement")
        onClicked: panel.started(panel.backend.reconcileBchLightAcknowledgement())
    }
    M.TextPlain {
        objectName: "bchLightDexOutcome"; Layout.fillWidth: true; visible: panel.funding.configured === true
        textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor
        text: {
            var result = panel.funding.result || {}
            var observed = result.observation || {}
            if (panel.funding.busy) return qsTr("Working on the saved trade…")
            if (panel.message) return panel.message
            if (panel.funding.contextReady !== true) return qsTr("Check the saved BCH agreement and selected wallet before continuing.")
            if (panel.funding.valid === false) return qsTr("The funding context does not match the selected trade.")
            if (panel.funding.state === "outcome-unconfirmed") return qsTr("The last action could not be verified. Check the saved transaction before trying again.")
            if (observed.position === "included") return qsTr("Funding confirmed: ") + observed.confirmations + "\n" + (result.txid || "")
            if (observed.position === "reported-pending") return qsTr("Funding reported pending; awaiting confirmation.")
            if (observed.position === "not-reported") return qsTr("The server has not reported this transaction. This does not establish that it was never sent.")
            if (panel.funding.operation === "sign" && result.signed) return qsTr("Signed locally. Review and save refund protection before sending.")
            if (result.broadcastStatus === "server-acknowledged") return qsTr("The server acknowledged funding. Check for confirmation.")
            if (panel.hasPayment) return qsTr("The exact payment is saved. Before sending, review and save the authenticated refund protection.")
            return qsTr("Review the negotiated details and network fee before reserving funds.")
        }
    }
}
