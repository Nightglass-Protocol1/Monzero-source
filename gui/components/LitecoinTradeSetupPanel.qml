import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id: panel
    property var backend
    property var appStatus: ({})
    property string market: ""
    // Swap chain shown in labels: "LTC" (default) or "BTC".
    property string coin: "LTC"
    property string coinName: coin === "BTC" ? "Bitcoin" : "Litecoin"
    function chainText(text) {
        return coin === "LTC" ? text : text.replace(/Litecoin/g, coinName).replace(/\bLTC\b/g, coin)
    }
    property var review: ({})
    property string notice: ""
    readonly property bool ready: !!review && review.signaturesVerified === true
        && typeof review.commitment === "string" && /^[0-9a-f]{64}$/.test(review.commitment)
        && !!review.terms && review.appPath === path.text
    visible: market === coin && appStatus.configured !== true
    spacing: 10
    function clearReview() { review=({}); consent.checked=false; expiry.stop() }
    function reviewSetup() {
        clearReview(); notice=""
        review=backend.reviewLitecoinTradeSetup(path.text)
        if (ready) expiry.restart()
        else notice=qsTr("This setup could not be verified. Check the local file and selected network.")
    }
    function openSetup() {
        if (!ready || !consent.checked) return
        var commitment=review.commitment
        clearReview()
        if (!backend.importLitecoinTradeSetup(path.text,commitment,true))
            notice=qsTr("The setup could not be opened. Review it again. If it appears under Your trades, keep its folder for recovery.")
    }
    onMarketChanged: clearReview()
    onBackendChanged: clearReview()
    onVisibleChanged: if (!visible) clearReview()
    Timer { id:expiry; interval:120000; onTriggered:panel.clearReview() }
    M.TextPlain { text:panel.chainText(qsTr("Open a prepared LTC/XMZ trade")); font.bold:true; color:M.Style.defaultFontColor }
    M.TextPlain {
        Layout.fillWidth:true; wrapMode:Text.WordWrap; color:M.Style.defaultFontColor
        text:qsTr("Select the private app.json prepared on this device. Exchange only public peer cards and signed quotes with your trading partner, never this setup folder. Opening a saved trade may reconnect to the partner and run its configured helper; sending funds requires a separate approval.")
    }
    TextField {
        id:path; objectName:"tradeSetupPath"; Layout.fillWidth:true; maximumLength:2048
        placeholderText:qsTr("Full path to your local app.json")
        onTextChanged:panel.clearReview()
    }
    M.StandardButton {
        objectName:"tradeSetupReview"; text:qsTr("Review trade setup"); enabled:path.text.length>0
        onClicked:if(enabled)panel.reviewSetup()
    }
    M.TextPlain {
        objectName:"tradeSetupDetails"; Layout.fillWidth:true; wrapMode:Text.WrapAnywhere; textFormat:Text.PlainText
        visible:panel.ready; color:M.Style.defaultFontColor
        text: {
            if (!panel.ready) return ""
            var r=panel.review, t=r.terms
            return qsTr("Network: %1\nTrade: %2\nYour role: %3\nPartner signing key: %4\nPartner certificate fingerprint: %5\nHelper: %6\nHelper fingerprint: %7\n\nBoth quote signatures verified. This does not verify the partner's real-world identity or current chain state.")
                .arg(r.network).arg(r.tradeId).arg(r.role).arg(r.peerKey).arg(r.peerCertificateSha256).arg(r.helper).arg(r.helperDigest)
                + "\n\n" + qsTr("Signed terms (amounts in atomic units):") + "\n" + JSON.stringify(t,null,2)
        }
    }
    CheckBox {
        id:consent; objectName:"tradeSetupConsent"; visible:panel.ready
        text:qsTr("I reviewed this local setup and trust the displayed helper and partner fingerprints.")
    }
    M.StandardButton {
        objectName:"tradeSetupOpen"; text:qsTr("Open and remember this trade")
        visible:panel.ready; enabled:panel.ready && consent.checked
        onClicked:if(enabled)panel.openSetup()
    }
    M.TextPlain { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:M.Style.defaultFontColor; text:panel.notice; visible:text.length>0 }
}
