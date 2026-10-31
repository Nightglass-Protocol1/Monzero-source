import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id: panel
    objectName: "lightWalletRecoveryPanel"
    property var backend
    property string coin: ""
    property string previousCoin: ""
    property var walletStatus: ({})
    property string error: ""
    property bool mnemonic: walletStatus.recoveryEncoding === "bip39-english-v1"
    visible: walletStatus.walletBackupConfigured === true
    function clear() {
        if (password) password.text=""
        if (consent) consent.checked=false
        if (restoreSeed) restoreSeed.text=""
        if (restorePassphrase) restorePassphrase.text=""
        if (restorePassword) restorePassword.text=""
        if (restoreConfirmation) restoreConfirmation.text=""
        if (restoreConsent) restoreConsent.checked=false
    }
    function hide() {
        clear()
        if (backend && coin) backend.hideLightWalletRecovery(coin)
    }
    onMnemonicChanged: hide()
    onCoinChanged: {
        clear();error=""
        if (backend && previousCoin) backend.hideLightWalletRecovery(previousCoin)
        previousCoin=coin
    }
    onVisibleChanged: if (!visible) hide()
    onWalletStatusChanged: if (walletStatus.busy === true) clear()
    Connections {
        target: Qt.application
        onStateChanged: if (Qt.application.state !== Qt.ApplicationActive) panel.hide()
    }
    M.TextPlain { text:panel.mnemonic ? qsTr("Local recovery phrase") : qsTr("Local recovery key");font.bold:true;color:M.Style.defaultFontColor }
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText
        text:panel.mnemonic ? qsTr("Keep these BIP39 recovery words and any additional BIP39 passphrase together. The passphrase is separate from your local wallet password. Anyone with them can recover the wallet keys. Keep an encrypted backup for address records; active trades require their own recovery records.") : qsTr("This wallet uses a raw BIP32 seed, not a mnemonic phrase or an individual spending key. Anyone with it can recover its keys. Keep the encrypted backup too: the seed alone excludes address-allocation records and active trades. Do not enter it in a BIP39 phrase field.")
    }
    TextField {
        id:password;objectName:"lightRecoveryPassword";Layout.fillWidth:true
        placeholderText:qsTr("Wallet password");echoMode:TextInput.Password
        inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText;maximumLength:1024
    }
    CheckBox {
        id:consent;objectName:"lightRecoveryConsent";Layout.fillWidth:true
        text:qsTr("I understand that revealing this recovery material gives access to the wallet's keys.")
        contentItem:M.TextPlain { text:consent.text;color:M.Style.defaultFontColor;wrapMode:Text.WordWrap;leftPadding:consent.indicator.width+consent.spacing }
    }
    M.StandardButton {
        objectName:"lightRecoveryReveal";text:qsTr("Unlock and reveal for 30 seconds")
        enabled:panel.walletStatus.recoveryAvailable === true && consent.checked && password.text.length>0
        onClicked: {
            if (!enabled) return
            var secret=password.text;panel.clear();panel.error=""
            var ok=panel.backend.revealLightWalletRecovery(panel.coin,panel.walletStatus.configurationDigest,secret,true);secret=""
            if (!ok) panel.error=qsTr("Recovery reveal was refused. Review the wallet configuration and try again.")
        }
    }
    TextArea {
        objectName:"lightRecoverySeed";Layout.fillWidth:true;readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;textFormat:TextEdit.PlainText
        visible:panel.walletStatus.recoveryVisible === true
        text:visible ? panel.backend.lightWalletRecoverySeed(panel.coin) : ""
    }
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor
        visible:panel.mnemonic && panel.walletStatus.recoveryVisible === true
        text:privatePassphrase.text.length>0 ? qsTr("Additional BIP39 passphrase (copy exactly, including spaces):") : qsTr("No additional BIP39 passphrase.")
    }
    TextArea {
        id:privatePassphrase;objectName:"lightRecoveryPassphrase";Layout.fillWidth:true;readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;textFormat:TextEdit.PlainText
        visible:panel.mnemonic && panel.walletStatus.recoveryVisible === true
        text:visible ? panel.backend.lightWalletRecoveryPassphrase(panel.coin) : ""
    }
    M.StandardButton { objectName:"lightRecoveryHide";text:qsTr("Hide recovery seed now");visible:panel.walletStatus.recoveryVisible === true;onClicked:panel.hide() }
    M.TextPlain { Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText;text:panel.error;visible:text.length>0 }
    ColumnLayout {
        Layout.fillWidth:true
    M.TextPlain { text:qsTr("Restore a missing local vault");font.bold:true;color:M.Style.defaultFontColor }
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText
        text:panel.mnemonic ? qsTr("Enter the English BIP39 recovery words and any additional BIP39 passphrase for this saved account. The app checks the keys before saving a local encrypted vault. Existing vaults are never replaced. This does not restore lost address records or trades; retain your encrypted backup and trade records.") : qsTr("Use the 64-character raw recovery seed for this saved account. The app checks that the keys match before saving an encrypted vault on this device. Existing vaults are never replaced. This does not restore lost address records or trades; use your encrypted backup for those wallet records.")
    }
    TextField {
        id:restoreSeed;objectName:"lightRecoveryImportSeed";Layout.fillWidth:true
        placeholderText:panel.mnemonic ? qsTr("English BIP39 recovery words") : qsTr("Raw recovery seed (64 lowercase hexadecimal characters)")
        echoMode:TextInput.Password;maximumLength:panel.mnemonic?1024:64
        inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
    }
    TextField {
        id:restorePassphrase;objectName:"lightRecoveryImportPassphrase";Layout.fillWidth:true;visible:panel.mnemonic
        placeholderText:qsTr("Additional BIP39 passphrase, if used (otherwise leave blank)")
        echoMode:TextInput.Password;maximumLength:1024;inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
    }
    TextField {
        id:restorePassword;objectName:"lightRecoveryImportPassword";Layout.fillWidth:true
        placeholderText:qsTr("Password for the restored vault");echoMode:TextInput.Password;maximumLength:1024
        inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
    }
    TextField {
        id:restoreConfirmation;objectName:"lightRecoveryImportConfirmation";Layout.fillWidth:true
        placeholderText:qsTr("Confirm password");echoMode:TextInput.Password;maximumLength:1024
        inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
    }
    CheckBox {
        id:restoreConsent;objectName:"lightRecoveryImportConsent";Layout.fillWidth:true
        text:qsTr("Restore this account's missing vault on my device using the recovery material I entered.")
        contentItem:M.TextPlain {text:restoreConsent.text;color:M.Style.defaultFontColor;wrapMode:Text.WordWrap;leftPadding:restoreConsent.indicator.width+restoreConsent.spacing}
    }
    M.StandardButton {
        objectName:"lightRecoveryImport";text:qsTr("Restore local vault")
        enabled:panel.walletStatus.recoveryAvailable === true && restoreConsent.checked
            && (panel.mnemonic ? /^[a-z]+(?:\s+[a-z]+)*$/.test(restoreSeed.text.trim())
                && [12,15,18,21,24].indexOf(restoreSeed.text.trim().split(/\s+/).length)>=0
                : /^[0-9a-f]{64}$/.test(restoreSeed.text)) && restorePassword.text.length>0
            && restorePassword.text===restoreConfirmation.text
        onClicked: {
            if (!enabled) return
            var material=restoreSeed.text,additional=restorePassphrase.text,secret=restorePassword.text,confirmation=restoreConfirmation.text
            panel.clear();panel.error=""
            var ok=panel.mnemonic ? panel.backend.restoreLightWalletMnemonic(panel.coin,panel.walletStatus.configurationDigest,material,additional,secret,confirmation,true)
                : panel.backend.restoreLightWalletRecovery(panel.coin,panel.walletStatus.configurationDigest,material,secret,confirmation,true)
            material="";additional="";secret="";confirmation=""
            if (!ok) panel.error=qsTr("Restoration was refused. Check the account, recovery material and matching passwords.")
        }
    }
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText
        visible:panel.walletStatus.state === "wallet-key-restored"
        text:qsTr("The local vault was restored and its keys match this saved account. Address records and trade recovery were not changed.")
    }

    }
}
