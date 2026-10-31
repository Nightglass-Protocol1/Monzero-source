import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id: panel
    objectName: "multiCoinPaymentPanel"
    property var backend
    property string coin: ""
    property var walletStatus: ({})
    property var wallet: walletStatus.wallet || ({})
    property var payment: ({})
    property var review: ({})
    property string requestId: ""
    property bool preparing: false
    property string error: ""
    readonly property bool busy: walletStatus.busy === true
    readonly property bool retained: requestId.length > 0 && !!review.signingCommitment
    readonly property bool formLocked: busy || preparing || requestId.length > 0
    visible: walletStatus.walletPrepareConfigured === true
    spacing: 10
    function parseAmount(text) {
        var match = /^(0|[1-9][0-9]{0,11})(?:\.([0-9]{1,8}))?$/.exec(text.trim())
        if (!match) return ""
        var fraction = match[2] || ""
        while (fraction.length < 8) fraction += "0"
        var result = (match[1] + fraction).replace(/^0+(?=[0-9])/, "")
        if (result.length > 20 || (result.length === 20 && result > "18446744073709551615")) return ""
        return result
    }
    function formatAmount(value) {
        if (typeof value !== "string" || !/^[0-9]+$/.test(value)) return "—"
        while (value.length < 9) value = "0" + value
        return value.slice(0,-8) + "." + value.slice(-8)
    }
    function clearConsent() {
        if (password) password.text=""
        if (signConsent) signConsent.checked=false
        if (sendConsent) sendConsent.checked=false
    }
    function reset() { clearConsent(); payment=({});review=({});requestId="";preparing=false;error="" }
    function accepted(ok) { if (!ok) { preparing=false;error=qsTr("Request refused. Review saved payment records before retrying.") } }
    onCoinChanged: { reset(); if (recipient) recipient.text="";if (amount) amount.text="";if (fee) fee.text="";if (minimumChange) minimumChange.text="" }
    onVisibleChanged: if (!visible) reset()
    onWalletStatusChanged: {
        clearConsent()
        if (preparing && walletStatus.state === "wallet-prepared" && walletStatus.coin === coin) {
            review=walletStatus.wallet || ({});requestId=walletStatus.requestId || "";preparing=false
        } else if (preparing && walletStatus.state === "unknown") {
            preparing=false;error=qsTr("The outcome is unknown. Load saved payment records; do not assume no action occurred.")
        }
    }
    M.TextPlain { text: qsTr("Send %1 from your local wallet").arg(panel.coin.toUpperCase()); font.bold:true;color:M.Style.defaultFontColor }
    M.TextPlain { Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;text:qsTr("This is a wallet transfer, not a DEX swap. Review the recipient and fee, then approve signing and sending separately. Saved payments remain available after restart.") }
    TextField { id:recipient;objectName:"multiCoinPaymentRecipient";Layout.fillWidth:true;enabled:!panel.formLocked;placeholderText:qsTr("Recipient address") }
    TextField { id:amount;objectName:"multiCoinPaymentAmount";Layout.fillWidth:true;enabled:!panel.formLocked;placeholderText:qsTr("Amount in %1").arg(panel.coin.toUpperCase());inputMethodHints:Qt.ImhFormattedNumbersOnly }
    TextField { id:fee;objectName:"multiCoinPaymentFee";Layout.fillWidth:true;enabled:!panel.formLocked;placeholderText:qsTr("Total network fee in %1").arg(panel.coin.toUpperCase());inputMethodHints:Qt.ImhFormattedNumbersOnly }
    TextField { id:minimumChange;objectName:"multiCoinMinimumChange";Layout.fillWidth:true;enabled:!panel.formLocked;placeholderText:qsTr("Minimum change output in %1").arg(panel.coin.toUpperCase());inputMethodHints:Qt.ImhFormattedNumbersOnly }
    M.StandardButton {
        objectName:"multiCoinPaymentPrepare";text:qsTr("Prepare payment for review")
        enabled:panel.walletStatus.walletPrepareAvailable === true && !panel.formLocked && recipient.text.length > 0
            && panel.parseAmount(amount.text) !== "" && panel.parseAmount(amount.text) !== "0"
            && panel.parseAmount(fee.text) !== "" && panel.parseAmount(minimumChange.text) !== "" && panel.parseAmount(minimumChange.text) !== "0"
        onClicked: {
            if (!enabled) return
            panel.payment={address:recipient.text,amountAtomic:panel.parseAmount(amount.text),feeAtomic:panel.parseAmount(fee.text),minimumChangeAtomic:panel.parseAmount(minimumChange.text),minimumConfirmations:1}
            panel.preparing=true;panel.clearConsent();panel.error=""
            panel.accepted(panel.backend.prepareLightPayment(panel.coin,panel.payment))
        }
    }
    M.StandardButton { text:qsTr("Load saved payments");enabled:panel.walletStatus.walletRequestsAvailable === true;onClicked:panel.accepted(panel.backend.loadLightPaymentRequests(panel.coin)) }
    Repeater {
        model:panel.walletStatus.paymentRequests || []
        delegate:M.StandardButton {
            text:qsTr("Review saved payment %1").arg(modelData.requestId || "")
            enabled:!panel.busy && !!modelData.review
            onClicked: {
                panel.clearConsent();panel.preparing=false;panel.payment=modelData.payment;panel.review=modelData.review;panel.requestId=modelData.requestId
            }
        }
    }
    M.TextPlain {
        objectName:"multiCoinPaymentReview";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor;visible:panel.retained
        text:qsTr("Recipient: %1\nAmount: %2 %3\nNetwork fee: %4 %3\nChange: %5 %3\nChange address: %6\nSigning commitment: %7")
            .arg(panel.payment.address || "").arg(panel.formatAmount(panel.review.paymentAtomic)).arg(panel.coin.toUpperCase())
            .arg(panel.formatAmount(panel.review.feeAtomic)).arg(panel.formatAmount(panel.review.changeAtomic))
            .arg(panel.review.changeAddress || qsTr("None")).arg(panel.review.signingCommitment || "")
    }
    M.StandardButton {
        text:qsTr("Recheck retained payment before signing");enabled:panel.retained && panel.walletStatus.walletReopenAvailable === true
        onClicked:panel.accepted(panel.backend.reopenLightPayment(panel.coin,panel.requestId,panel.payment,{signingCommitment:panel.review.signingCommitment,changeAddress:panel.review.changeAddress}))
    }
    TextField { id:password;objectName:"multiCoinPaymentPassword";Layout.fillWidth:true;visible:panel.retained && panel.walletStatus.walletSignAvailable === true;placeholderText:qsTr("Unlock wallet to sign");echoMode:TextInput.Password;inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText;maximumLength:1024 }
    CheckBox {
        id:signConsent;objectName:"multiCoinPaymentSignConsent";Layout.fillWidth:true;visible:password.visible
        text:qsTr("I approve signing the reviewed recipient, amount and fee.")
        contentItem:M.TextPlain { text:signConsent.text;color:M.Style.defaultFontColor;wrapMode:Text.WordWrap;leftPadding:signConsent.indicator.width+signConsent.spacing }
    }
    M.StandardButton {
        objectName:"multiCoinPaymentSign";text:qsTr("Sign reviewed payment");visible:password.visible
        enabled:visible && signConsent.checked && password.text.length > 0 && panel.wallet.signingCommitment === panel.review.signingCommitment
        onClicked: { if (!enabled) return;var secret=password.text;panel.clearConsent();panel.accepted(panel.backend.signLightPayment(panel.coin,panel.review.signingCommitment,secret));secret="" }
    }
    M.StandardButton {
        text:qsTr("Check saved signed payment and send status");enabled:panel.retained && panel.walletStatus.walletBroadcastStatusAvailable === true
        onClicked:panel.accepted(panel.backend.checkLightPaymentSend(panel.coin,panel.requestId,panel.payment,panel.review))
    }
    M.TextPlain {
        objectName:"multiCoinPaymentSendReview";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
        visible:panel.retained && !!panel.wallet.broadcastStatus
        text:qsTr("Final transaction ID: %1\nSend status: %2\nServer: %3\nConfirmation: %4\nA lost reply does not mean the transaction failed. Check its status before deciding whether to send the same saved transaction again.")
            .arg(panel.wallet.txid || "").arg(panel.wallet.broadcastStatus || "")
            .arg(panel.wallet.broadcastEndpoint ? panel.wallet.broadcastEndpoint.host+":"+panel.wallet.broadcastEndpoint.port : "")
            .arg(panel.wallet.confirmationStatus || qsTr("Not checked"))
    }
    CheckBox {
        id:sendConsent;objectName:"multiCoinPaymentSendConsent";Layout.fillWidth:true;visible:panel.retained && panel.walletStatus.walletBroadcastAvailable === true
        text:qsTr("I approve sending this exact saved transaction to the server shown above.")
        contentItem:M.TextPlain { text:sendConsent.text;color:M.Style.defaultFontColor;wrapMode:Text.WordWrap;leftPadding:sendConsent.indicator.width+sendConsent.spacing }
    }
    M.StandardButton {
        objectName:"multiCoinPaymentSend";text:qsTr("Send approved transaction");visible:sendConsent.visible
        enabled:visible && sendConsent.checked && panel.wallet.signingCommitment === panel.review.signingCommitment
        onClicked: { if (!enabled) return;panel.clearConsent();panel.accepted(panel.backend.sendLightPayment(panel.coin,panel.wallet.signingCommitment,panel.wallet.txid,panel.wallet.broadcastEndpoint)) }
    }
    M.StandardButton {
        text:qsTr("Check payment confirmation");enabled:panel.retained && panel.walletStatus.walletReconcileAvailable === true
        onClicked:panel.accepted(panel.backend.reconcileLightPayment(panel.coin,panel.requestId,panel.payment,panel.review))
    }
    M.StandardButton { text:qsTr("Clear this form (saved records are kept)");enabled:!panel.busy;onClicked:panel.reset() }
    M.TextPlain { Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor;text:panel.error;visible:text.length>0 }
}
