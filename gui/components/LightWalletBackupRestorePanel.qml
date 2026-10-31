import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M
ColumnLayout {
    id:panel;objectName:"lightWalletBackupRestorePanel"
    property var backend
    property string coin:""
    property var walletStatus:({})
    property var preview:walletStatus.backupImportReview || ({})
    property var review:preview.review || ({})
    readonly property bool reviewReady:completeReview(preview)
    function completeReview(value) {
        var r=value && value.review;
        var c=r && r.configuration;
        var account=c && c.wallet && c.wallet.account;
        return !!(value && value.reviewDigest && r && r.network && r.configurationFile && r.backupDigest
            && account && typeof account.account === "number" && account.account >= 0 && Math.floor(account.account) === account.account
            && Array.isArray(r.allocated) && r.allocated.length === 2
            && r.allocated.every(function(n) { return typeof n === "number" && n >= 0 && Math.floor(n) === n; }));
    }
    function reviewSummary(value) {
        if (!completeReview(value)) return "";
        var r=value.review;
        return qsTr("Coin/network: %1 / %2\nAccount: %3\nSaved address ranges: %4 receiving, %5 change\nRestore configuration: %6\nBackup fingerprint: %7")
            .arg(coin.toUpperCase()).arg(r.network).arg(r.configuration.wallet.account.account)
            .arg(r.allocated[0]).arg(r.allocated[1]).arg(r.configurationFile).arg(r.backupDigest);
    }
    onPreviewChanged:clear()
    property var completed:walletStatus.backupRestoration || ({})
    property string context:coin+":"+((walletStatus.backupSetup || {}).setupDirectory || "")+":"+(preview.reviewDigest || "")
    property string error:""
    property bool submitted:false
    visible:walletStatus.backupSetupConfigured === true || completed.restored === true
    spacing:10
    function clear(){if(password)password.text="";if(consent)consent.checked=false;if(expiry)expiry.stop()}
    onContextChanged:{clear();error=""}
    onVisibleChanged:if(!visible)clear()
    onWalletStatusChanged:if(submitted && walletStatus.busy !== true) {
        if(walletStatus.state === "unknown")error=qsTr("Backup recovery could not be verified. Preview the backup again before retrying.")
        submitted=false
    }
    Connections {target:Qt.application;onStateChanged:if(Qt.application.state !== Qt.ApplicationActive)panel.clear()}
    Timer {id:expiry;interval:60000;onTriggered:panel.clear()}
    M.TextPlain {text:qsTr("Restore an encrypted local backup");font.bold:true;color:M.Style.defaultFontColor}
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;textFormat:Text.PlainText;color:M.Style.defaultFontColor
        text:panel.completed.restored === true ? qsTr("Wallet restored. Normal wallet controls are available. This historical receipt does not mean your current wallet is backed up. Active DEX trades are not recovered.")
            :panel.walletStatus.state === "backup-restore-invalid" ? qsTr("Saved restoration records could not be verified. The wallet remains unavailable.")
            :qsTr("Authenticate your backup locally, review the recovered account and destination, then approve restoration. Keys stay on this device. Backups cover keys and saved addresses, not active trades or complete history.")
    }
    M.TextPlain {
        objectName:"lightBackupRestoreReview";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText;color:M.Style.defaultFontColor
        visible:panel.reviewReady
        text:panel.reviewSummary(panel.preview)
    }
    TextField {
        id:password;objectName:"lightBackupRestorePassword";Layout.fillWidth:true;maximumLength:1024;echoMode:TextInput.Password
        visible:panel.walletStatus.backupSetupConfigured === true;enabled:panel.walletStatus.busy !== true
        placeholderText:qsTr("Backup encryption password");onTextChanged:{consent.checked=false;if(text.length)expiry.restart();else expiry.stop()}
    }
    M.StandardButton {
        objectName:"lightBackupRestorePreview";text:qsTr("Preview encrypted backup")
        visible:panel.walletStatus.backupSetupConfigured === true
        enabled:panel.walletStatus.backupImportPreviewAvailable === true && password.text.length>0
        onClicked:{if(!enabled)return;var secret=password.text;panel.clear();panel.error="";panel.submitted=true;
            if(!panel.backend.previewLightBackupImport(panel.coin,secret)){panel.submitted=false;panel.error=qsTr("Could not start backup preview.")}secret=""}
    }
    CheckBox {id:consent;objectName:"lightBackupRestoreConsent";visible:panel.reviewReady;text:qsTr("Restore this backup into the reviewed local folders.")}
    M.StandardButton {
        objectName:"lightBackupRestoreApply";text:qsTr("Restore reviewed backup")
        visible:panel.reviewReady;enabled:panel.reviewReady && panel.walletStatus.backupImportAvailable === true && consent.checked && password.text.length>0
        onClicked:{if(!enabled)return;var secret=password.text;var digest=panel.preview.reviewDigest;panel.clear();panel.error="";panel.submitted=true;
            if(!panel.backend.importLightBackup(panel.coin,digest,secret,true)){panel.submitted=false;panel.error=qsTr("Could not start restoration. Preview the backup again.")}secret=""}
    }
    M.TextPlain {Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;text:panel.error;visible:text.length>0}
}
