import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id:panel
    objectName:"lightWalletImportRecoveryPanel"
    property var backend
    property string coin:""
    property var walletStatus:({})
    property string context:coin+":"+(walletStatus.creationDigest || "")
    property string error:""
    property bool submitted:false
    property var choices:makeChoices()
    property string digest:selection && selection.currentIndex>=0 && selection.currentIndex<choices.length ? choices[selection.currentIndex].digest : ""
    property string backupPath:walletStatus.creation && digest.length ? walletStatus.creation.setupDirectory+"/import-recovery-"+digest+"/backup" : ""
    visible:walletStatus.walletImportPending === true && choices.length>0
    spacing:10
    function makeChoices() {
        var items=[],seen={},latest=walletStatus.importDiscovery || ({})
        if(latest.reviewDigest) {items.push({digest:latest.reviewDigest,label:qsTr("Latest scan · block %1").arg(latest.review.coverage.height)});seen[latest.reviewDigest]=true}
        var saved=walletStatus.importRecoveryRequests || []
        for(var i=0;i<saved.length;i++)if(!seen[saved[i].reviewDigest])items.push({digest:saved[i].reviewDigest,label:qsTr("Saved recovery · block %1 · %2").arg(saved[i].height).arg(saved[i].reviewDigest.slice(0,8))})
        return items
    }
    function clear() {if(password)password.text="";if(consent)consent.checked=false;if(expiry)expiry.stop()}
    onDigestChanged:clear()
    onContextChanged:{clear();error="";submitted=false}
    onVisibleChanged:if(!visible)clear()
    onWalletStatusChanged:if(submitted && walletStatus.busy !== true) {
        if(walletStatus.state === "unknown")error=qsTr("Recovery could not be verified. Check your password and retry the saved recovery. If the scan changed before address recovery began, check wallet history again.")
        submitted=false
    }
    Connections {target:Qt.application;onStateChanged:if(Qt.application.state !== Qt.ApplicationActive)panel.clear()}
    Timer {id:expiry;interval:60000;onTriggered:panel.clear()}
    M.TextPlain {text:qsTr("Restore address records and verify backup");font.bold:true;color:M.Style.defaultFontColor}
    ComboBox {id:selection;objectName:"lightImportRecoverySelection";Layout.fillWidth:true;model:panel.choices;textRole:"label";enabled:panel.walletStatus.busy !== true}
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
        text:qsTr("Local encrypted backup: %1\nA saved review lets you continue after restarting. Your password is never saved with the review. This step does not activate spending or recover active trades.").arg(panel.backupPath)
    }
    TextField {
        id:password;objectName:"lightImportRecoveryPassword";Layout.fillWidth:true;maximumLength:1024;echoMode:TextInput.Password
        placeholderText:qsTr("Password for this local wallet");enabled:panel.walletStatus.busy !== true
        inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
        onTextChanged:{if(consent)consent.checked=false;if(expiry)expiry.restart()}
    }
    CheckBox {
        id:consent;objectName:"lightImportRecoveryConsent";Layout.fillWidth:true
        text:qsTr("I approve retaining these address ranges and creating or verifying their encrypted backup.")
        contentItem:M.TextPlain {text:consent.text;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor;leftPadding:consent.indicator.width+consent.spacing}
    }
    M.StandardButton {
        objectName:"lightImportRecoveryStart";text:qsTr("Recover or verify saved recovery")
        enabled:panel.walletStatus.importRecoveryAvailable === true && panel.digest.length>0 && password.text.length>0 && consent.checked
        onClicked:{
            if(!enabled)return
            panel.error="";panel.submitted=true
            var started=panel.backend.recoverLightWalletImport(panel.coin,panel.digest,password.text,true)
            panel.clear()
            if(!started) {panel.submitted=false;panel.error=qsTr("Could not start recovery. Check the selected review and wallet folder.")}
        }
    }
    M.TextPlain {
        objectName:"lightImportRecoveryResult";Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
        visible:!!panel.walletStatus.importRecovery && panel.walletStatus.importRecovery.backupVerified === true
        text:qsTr("Address records and encrypted backup verified. Wallet activation is still required. Complete history and active-trade recovery have not been established.")
    }
    M.TextPlain {Layout.fillWidth:true;visible:panel.error.length>0;text:panel.error;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor}
}
