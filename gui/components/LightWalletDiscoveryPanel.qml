import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id:panel
    objectName:"lightWalletDiscoveryPanel"
    property var backend
    property string coin:""
    property var walletStatus:({})
    property var discovery:walletStatus.importDiscovery || ({})
    property var review:discovery.review || ({})
    property string context:coin+":"+(walletStatus.creationDigest || "")
    property string error:""
    property bool submitted:false
    visible:walletStatus.walletImportPending === true && walletStatus.state !== "wallet-import-incomplete"
    spacing:10
    function amount(value) {
        var digits=String(value || "0")
        while(digits.length<9)digits="0"+digits
        var fraction=digits.slice(-8).replace(/0+$/,"")
        return digits.slice(0,-8)+(fraction.length ? "."+fraction : "")
    }
    onContextChanged:{if(receiveMinimum)receiveMinimum.value=0;if(changeMinimum)changeMinimum.value=0;if(consent)consent.checked=false;error="";submitted=false}
    onVisibleChanged:if(!visible && consent)consent.checked=false
    onWalletStatusChanged:if(submitted && walletStatus.busy !== true) {
        if(walletStatus.state === "unknown")error=qsTr("Discovery could not be verified. Check the connection and scan limits, then try again.")
        submitted=false
    }
    M.TextPlain {text:qsTr("Find your imported wallet's history");font.bold:true;color:M.Style.defaultFontColor}
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText
        text:qsTr("This checks both receiving and change addresses with your selected server. Your recovery words stay on this device; the server can see which addresses you query. If known, enter how many addresses your previous wallet allocated.")
    }
    M.TextPlain {text:qsTr("Previously allocated receiving addresses");color:M.Style.defaultFontColor}
    SpinBox {id:receiveMinimum;objectName:"lightDiscoveryReceiveMinimum";from:0;to:480;editable:true;enabled:panel.walletStatus.busy !== true;onValueChanged:if(consent)consent.checked=false}
    M.TextPlain {text:qsTr("Previously allocated change addresses");color:M.Style.defaultFontColor}
    SpinBox {id:changeMinimum;objectName:"lightDiscoveryChangeMinimum";from:0;to:480;editable:true;enabled:panel.walletStatus.busy !== true;onValueChanged:if(consent)consent.checked=false}
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText
        text:qsTr("This scan checks up to 500 addresses per branch and stops after 20 addresses with no reported history beyond the known range. It cannot prove that all funds were found. Wallets with larger histories need a wider recovery scan.")
    }
    CheckBox {
        id:consent;objectName:"lightDiscoveryConsent";Layout.fillWidth:true
        text:qsTr("I approve checking this account with %1.").arg(panel.walletStatus.creation && panel.walletStatus.creation.endpoint ? panel.walletStatus.creation.endpoint.host : "")
        contentItem:M.TextPlain {text:consent.text;textFormat:Text.PlainText;color:M.Style.defaultFontColor;wrapMode:Text.WordWrap;leftPadding:consent.indicator.width+consent.spacing}
    }
    M.StandardButton {
        objectName:"lightDiscoveryStart";text:panel.walletStatus.busy ? qsTr("Checking history…") : qsTr("Check wallet history")
        enabled:panel.walletStatus.importDiscoveryAvailable === true && consent.checked
        onClicked:{
            if(!enabled)return
            panel.error="";panel.submitted=true
            if(!panel.backend.discoverLightWalletImport(panel.coin,receiveMinimum.value,changeMinimum.value,true)) {
                panel.submitted=false;panel.error=qsTr("Could not start discovery. Check the wallet settings and known address counts.")
            }
            consent.checked=false
        }
    }
    M.TextPlain {
        objectName:"lightDiscoveryResult";Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText
        visible:!!panel.discovery.reviewDigest
        text:qsTr("Observed confirmed balance: %1 %2 (provisional)\nChecked address ranges: %3 receiving, %4 change.\nRecovery backup and activation are still required before this account can receive or spend. Active trades are not recovered by this scan.")
            .arg(panel.amount(panel.review.observedConfirmedAtomic)).arg(panel.coin.toUpperCase())
            .arg(panel.review.coverage ? panel.review.coverage.allocatedAfter[0] : "")
            .arg(panel.review.coverage ? panel.review.coverage.allocatedAfter[1] : "")
    }
    M.TextPlain {Layout.fillWidth:true;visible:panel.error.length>0;text:panel.error;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText}
}
