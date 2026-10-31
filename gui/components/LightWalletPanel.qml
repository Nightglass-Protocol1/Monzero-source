import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id: panel
    objectName: "multiCoinLightWalletPanel"
    property var backend
    property string coin: ""
    property var walletStatus: ({})
    property var wallet: walletStatus.wallet || ({})
    property var settings: walletStatus.creation || ({})
    property string error: ""
    property string context: coin + ":" + (walletStatus.network || "") + ":" + (walletStatus.creationDigest || "") + ":" + (wallet.backupDigest || "") + ":" + (walletStatus.backupDirectory || "") + ":" + JSON.stringify(walletStatus.restoreDirectories || {})
    visible: walletStatus.configured === true
    spacing: 10
    function clearApproval() {
        if (password) password.text = ""
        if (confirmation) confirmation.text = ""
        if (consent) consent.checked = false
    }
    function accepted(ok) { if (!ok) error = qsTr("The request was not accepted. Review the current wallet state before retrying.") }
    onContextChanged: { clearApproval(); if (backupDirectory) backupDirectory.text = ""; error = "" }
    onVisibleChanged: if (!visible) clearApproval()
    onWalletStatusChanged: if (walletStatus.busy === true) clearApproval()


    M.TextPlain { text: panel.coin.toUpperCase() + qsTr(" local wallet — development preview"); font.bold: true; color: M.Style.defaultFontColor }
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;color:M.Style.defaultFontColor;textFormat:Text.PlainText
        text:qsTr("Wallet folder: %1\nUse this folder to reopen the wallet after restarting the app.").arg(panel.walletStatus.walletFolder || "")
    }
    M.TextPlain {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; textFormat: Text.PlainText
        text: qsTr("Keys and encrypted backups stay on your device. Monzero does not keep a recovery copy. Chain servers can observe wallet queries. Wallet transfers and DEX swaps have separate approvals.")
    }
    M.TextPlain {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: M.Style.defaultFontColor; textFormat: Text.PlainText
        text: qsTr("Network: %1 · State: %2").arg(panel.walletStatus.network || "").arg(panel.walletStatus.state || "")
            + ((panel.coin === "bch" || panel.coin === "doge") ? qsTr(" · Isolated test network only") : "")
    }
    M.TextPlain {
        objectName:"multiCoinWalletRecoveryFormat";Layout.fillWidth:true;wrapMode:Text.WordWrap
        color:M.Style.defaultFontColor;textFormat:Text.PlainText;visible:panel.walletStatus.setupConfigured === true
        text:panel.settings.seedEncoding === "bip39-english-v1"
            ? qsTr("Recovery format: 24 English BIP39 words, with no additional BIP39 passphrase.")
            : qsTr("Recovery format: raw BIP32 seed. This wallet does not use recovery words.")
    }
    M.TextPlain {
        objectName: "multiCoinSetupDetails"; visible: panel.walletStatus.setupConfigured === true
        Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor; textFormat: Text.PlainText
        text: qsTr("Review local wallet setup\nWallet ID: %1\nKey vault: %2\nAddress records: %3\nBackup folder: %4\nSetup records: %5\nPayment records: %6\nHeader cache: %7\nAccount: %8\nChain server: %9")
            .arg(panel.settings.walletId || "").arg(panel.settings.vaultDirectory || "")
            .arg(panel.settings.addressBookDirectory || "").arg(panel.settings.backupDirectory || "")
            .arg(panel.settings.setupDirectory || "").arg(panel.settings.spendDirectory || "")
            .arg(panel.settings.cacheDirectory || "").arg(panel.settings.accountIndex || 0)
            .arg(panel.settings.endpoint ? panel.settings.endpoint.host + ":" + panel.settings.endpoint.port : qsTr("Not configured"))
    }
    TextField {
        id: password; objectName: "multiCoinWalletPassword"; Layout.fillWidth: true
        visible: panel.walletStatus.walletCreateAvailable === true || panel.walletStatus.walletBackupAvailable === true
        placeholderText: qsTr("Wallet password"); echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText; maximumLength: 1024
    }
    TextField {
        id: confirmation; objectName: "multiCoinWalletConfirmation"; Layout.fillWidth: true
        visible: panel.walletStatus.walletCreateAvailable === true
        placeholderText: qsTr("Confirm wallet password"); echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText; maximumLength: 1024
    }
    CheckBox {
        id: consent; objectName: "multiCoinWalletConsent"; Layout.fillWidth: true
        visible: panel.walletStatus.walletCreateAvailable === true || panel.walletStatus.walletRestoreAvailable === true
        text: panel.walletStatus.setupConfigured === true ? qsTr("I approve creating or resuming this coin's wallet in the folders above.") : qsTr("I approve restoring the verified backup to the local folders below.")
        contentItem: M.TextPlain { text: consent.text; color: M.Style.defaultFontColor; wrapMode: Text.WordWrap; leftPadding: consent.indicator.width + consent.spacing }
    }
    M.StandardButton {
        objectName: "multiCoinWalletCreate"; text: qsTr("Create or resume local wallet")
        visible: panel.walletStatus.walletCreateAvailable === true
        enabled: visible && panel.walletStatus.busy !== true && consent.checked && password.text.length > 0 && password.text === confirmation.text
        onClicked: {
            if (!enabled) return
            var secret = password.text, repeat = confirmation.text; panel.clearApproval(); panel.error = ""
            panel.accepted(panel.backend.createLightWallet(panel.coin, panel.walletStatus.creationDigest, secret, repeat)); secret = ""; repeat = ""
        }
    }
    M.StandardButton {
        text: qsTr("Review saved wallet"); visible: panel.walletStatus.walletCreatedReviewAvailable === true
        enabled: panel.walletStatus.busy !== true
        onClicked: panel.accepted(panel.backend.reviewCreatedLightWallet(panel.coin))
    }
    M.TextPlain {
        Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor; textFormat: Text.PlainText
        visible: panel.walletStatus.walletCreatedOpenAvailable === true
        text: qsTr("Saved local configuration: %1\nConfiguration digest: %2\nReviewing a saved wallet does not reverify its backup.").arg(panel.wallet.configurationFile || "").arg(panel.wallet.configurationDigest || "")
    }
    M.StandardButton {
        objectName: "multiCoinWalletOpen"; text: qsTr("Open reviewed wallet"); visible: panel.walletStatus.walletCreatedOpenAvailable === true
        enabled: panel.walletStatus.busy !== true
        onClicked: panel.accepted(panel.backend.openCreatedLightWallet(panel.coin, panel.wallet.configurationDigest))
    }
    M.StandardButton {
        objectName: "multiCoinWalletReceive"; text: qsTr("Show receive address"); visible: panel.walletStatus.walletReceiveAvailable === true
        onClicked: panel.accepted(panel.backend.receiveLightWallet(panel.coin))
    }
    TextArea {
        objectName: "multiCoinWalletAddress"; Layout.fillWidth: true; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
        visible: panel.walletStatus.state === "wallet-address"; text: panel.wallet.address || ""
    }
    TextField {
        id: backupDirectory; objectName: "multiCoinBackupDirectory"; Layout.fillWidth: true
        visible: panel.walletStatus.walletBackupConfigured === true
        placeholderText: qsTr("Local encrypted backup folder (full path)")
        onTextChanged: consent.checked = false
    }
    RowLayout {
        Layout.fillWidth: true; visible: panel.walletStatus.walletBackupConfigured === true
        M.StandardButton {
            objectName: "multiCoinBackupSave"; text: qsTr("Save backup")
            enabled: panel.walletStatus.walletBackupAvailable === true && password.text.length > 0 && backupDirectory.text.length > 0
            onClicked: { if (!enabled) return; var secret=password.text; panel.clearApproval(); panel.accepted(panel.backend.backupLightWallet(panel.coin,backupDirectory.text,secret,true)); secret="" }
        }
        M.StandardButton {
            objectName: "multiCoinBackupVerify"; text: qsTr("Verify backup")
            enabled: panel.walletStatus.walletBackupAvailable === true && password.text.length > 0 && backupDirectory.text.length > 0
            onClicked: { if (!enabled) return; var secret=password.text; panel.clearApproval(); panel.accepted(panel.backend.backupLightWallet(panel.coin,backupDirectory.text,secret,false)); secret="" }
        }
    }
    M.TextPlain {
        Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: M.Style.defaultFontColor; textFormat: Text.PlainText
        visible: panel.walletStatus.walletRestoreAvailable === true
        text: qsTr("Verified backup: %1\nDigest: %2\nRestore key vault: %3\nRestore address records: %4\nBackup covers keys and addresses, not active DEX trades.")
            .arg(panel.walletStatus.backupDirectory || "").arg(panel.wallet.backupDigest || "")
            .arg((panel.walletStatus.restoreDirectories || {}).vault || "").arg((panel.walletStatus.restoreDirectories || {}).addressBook || "")
    }
    M.StandardButton {
        objectName: "multiCoinWalletRestore"; text: qsTr("Restore verified backup")
        visible: panel.walletStatus.walletRestoreAvailable === true
        enabled: visible && consent.checked && password.text.length > 0 && backupDirectory.text === panel.walletStatus.backupDirectory
        onClicked: { if (!enabled) return; var secret=password.text; panel.clearApproval(); panel.accepted(panel.backend.restoreLightWallet(panel.coin,backupDirectory.text,panel.wallet.backupDigest,secret)); secret="" }
    }
    M.StandardButton {
        text: qsTr("Synchronize chain headers"); visible: panel.walletStatus.syncAvailable === true
        onClicked: panel.accepted(panel.backend.synchronizeLightWallet(panel.coin,10000))
    }
    M.StandardButton {
        text: qsTr("Refresh provisional balance"); visible: panel.walletStatus.walletRefreshAvailable === true
        onClicked: panel.accepted(panel.backend.refreshLightWallet(panel.coin))
    }
    M.TextPlain {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor
        visible: panel.walletStatus.state === "wallet-checked"
        text: qsTr("Observed confirmed amount (atomic units): %1\nThis is a provisional observation, not a guarantee of spendability.").arg(panel.wallet.observedConfirmedAtomic || "0")
    }
    M.LightWalletRecoveryPanel {
        Layout.fillWidth: true
        backend: panel.backend
        coin: panel.coin
        walletStatus: panel.walletStatus
    }
    M.LightWalletPaymentPanel {
        Layout.fillWidth: true
        backend: panel.backend
        coin: panel.coin
        walletStatus: panel.walletStatus
    }
    M.StandardButton {
        text: qsTr("Cancel pending request"); visible: panel.walletStatus.busy === true
        onClicked: { panel.clearApproval(); panel.backend.cancelLightWallet(panel.coin) }
    }
    M.TextPlain { Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: M.Style.defaultFontColor; text: panel.error; visible: text.length > 0 }
}
