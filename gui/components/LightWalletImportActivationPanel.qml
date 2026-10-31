import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M
ColumnLayout {
    id:panel
    objectName:"lightWalletImportActivationPanel"
    property var backend
    property string coin:""
    property var walletStatus:({})
    property bool activated:walletStatus.importActivated === true
    property string context:coin+":"+(walletStatus.importActivationDigest || "")
    property string error:""
    property bool submitted:false
    visible:walletStatus.walletImportPending === true || activated
    spacing:10
    function clear(){if(password)password.text="";if(consent)consent.checked=false;if(expiry)expiry.stop()}
    onContextChanged:{clear();error=""}
    onVisibleChanged:if(!visible)clear()
    onWalletStatusChanged:if(submitted && walletStatus.busy !== true) {
        if(walletStatus.state === "unknown")error=qsTr("Activation could not be verified. Verify the recovery backup again, then retry activation.")
        submitted=false
    }
    Connections {target:Qt.application;onStateChanged:if(Qt.application.state !== Qt.ApplicationActive)panel.clear()}
    Timer {id:expiry;interval:60000;onTriggered:panel.clear()}
    M.TextPlain {text:panel.activated ? qsTr("Imported wallet enabled") : qsTr("Enable this recovered wallet");font.bold:true;color:M.Style.defaultFontColor}
    M.TextPlain {
        objectName:"lightImportActivationStatus";Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
        text:panel.activated ? qsTr("Normal wallet controls are available. DEX trading is not enabled. An earlier activation does not mean your current backup is up to date.")
            :panel.walletStatus.state === "wallet-import-activation-invalid" ? qsTr("Saved activation records could not be verified. This account remains unavailable.")
            :panel.walletStatus.state === "wallet-import-activation-incomplete" ? qsTr("Activation was interrupted. Verify the recovery backup again, then retry activation.")
            :qsTr("First verify the recovered address records and encrypted backup. Activation enables receiving and reviewed wallet payments; it does not enable DEX trading, establish complete history or recover active trades.")
    }
    ColumnLayout {
        Layout.fillWidth:true;visible:!panel.activated
        TextField {
            id:password;objectName:"lightImportActivationPassword";Layout.fillWidth:true;maximumLength:1024;echoMode:TextInput.Password
            placeholderText:qsTr("Confirm with your local wallet password");enabled:panel.walletStatus.importActivationAvailable === true
            inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            onTextChanged:{if(consent)consent.checked=false;if(expiry)expiry.restart()}
        }
        CheckBox {
            id:consent;objectName:"lightImportActivationConsent";Layout.fillWidth:true
            text:qsTr("I reviewed the recovered address ranges and approve enabling this wallet.")
            contentItem:M.TextPlain {text:consent.text;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor;leftPadding:consent.indicator.width+consent.spacing}
        }
        M.StandardButton {
            objectName:"lightImportActivationStart";text:qsTr("Enable recovered wallet")
            enabled:panel.walletStatus.importActivationAvailable === true && password.text.length>0 && consent.checked
            onClicked:{
                if(!enabled)return
                panel.error="";panel.submitted=true
                var started=panel.backend.activateLightWalletImport(panel.coin,panel.walletStatus.importActivationDigest,password.text,true)
                panel.clear()
                if(!started){panel.submitted=false;panel.error=qsTr("Could not start activation. Verify the recovery backup and review it again.")}
            }
        }
    }
    M.TextPlain {Layout.fillWidth:true;visible:panel.error.length>0;text:panel.error;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor}
}
