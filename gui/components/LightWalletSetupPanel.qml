import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id: panel
    objectName: "lightWalletSetupPanel"
    property var backend
    property string coin: ""
    property var walletStatus: ({})
    property string error: ""
    property var helperReview: ({})
    function focusCreation() { network.forceActiveFocus(); return network }
    function clearHelperReview() { helperReview=({});helperConsent.checked=false }
    visible: backend && backend.lightWalletSetupAvailable === true
        && ["ltc","btc","bch","doge"].indexOf(coin)>=0 && walletStatus.configured !== true
    spacing: 10
    onCoinChanged: { clearHelperReview(); accountIndex.value=0;network.currentIndex=0; consent.checked=false; error="" }
    onVisibleChanged: if (!visible) {consent.checked=false;clearHelperReview()}
    M.TextPlain {text:qsTr("Return to an existing local wallet");font.bold:true;color:M.Style.defaultFontColor}
    TextField {
        id:existing;objectName:"lightSetupExistingFolder";Layout.fillWidth:true;maximumLength:2048
        placeholderText:qsTr("Saved wallet folder (full path)")
        onTextChanged:panel.clearHelperReview()
    }
    M.StandardButton {
        objectName:"lightSetupOpenExisting";text:qsTr("Open saved wallet settings")
        enabled:existing.text.charAt(0)==="/"
        onClicked: {
            if (!enabled) return
            panel.error=""
            if (!panel.backend.openLocalLightWallet(panel.coin,existing.text))
                panel.error=qsTr("Could not open this wallet. Check the selected coin and saved folder. The wallet must use the trusted installed helper.")
        }
    }
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor
        text:qsTr("Opening settings keeps the wallet locked and does not connect to a server. This device remembers the folder for next time. If creation was interrupted, review the saved settings to resume it.")
    }
    M.StandardButton {
        objectName:"lightHelperReview";text:qsTr("Review app helper update")
        enabled:existing.text.charAt(0)==="/"
        onClicked: {
            if (!enabled) return
            panel.clearHelperReview();panel.error=""
            panel.helperReview=panel.backend.reviewLightWalletHelperUpgrade(panel.coin,existing.text)
            if (!panel.helperReview.commitment) panel.error=qsTr("No helper update could be reviewed. Check the coin, folder and installed helper.")
        }
    }
    M.TextPlain {
        objectName:"lightHelperDetails";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;textFormat:Text.PlainText
        visible:!!panel.helperReview.commitment;color:M.Style.defaultFontColor
        text:visible ? qsTr("Use this app's installed helper for the saved wallet. Keys and wallet records stay in their existing folders. This does not unlock the wallet or connect to a server.\nPrevious helper: %1\nPrevious fingerprint: %2\nInstalled helper: %3\nInstalled fingerprint: %4")
            .arg(panel.helperReview.previousHelper).arg(panel.helperReview.previousDigest).arg(panel.helperReview.helper).arg(panel.helperReview.helperDigest) : ""
    }
    CheckBox {
        id:helperConsent;objectName:"lightHelperConsent";visible:!!panel.helperReview.commitment
        text:qsTr("Use the reviewed installed helper for this wallet.")
    }
    M.StandardButton {
        objectName:"lightHelperApply";text:qsTr("Update helper and open settings")
        visible:!!panel.helperReview.commitment;enabled:visible && helperConsent.checked
        onClicked: {
            if (!enabled) return
            var commitment=panel.helperReview.commitment;panel.clearHelperReview()
            if (!panel.backend.upgradeLightWalletHelper(panel.coin,existing.text,commitment))
                panel.error=qsTr("The helper update could not be verified. Review the saved settings again.")
        }
    }
    M.TextPlain { text: qsTr("Set up or restore a %1 wallet on this device").arg(panel.coin.toUpperCase());font.bold:true;color:M.Style.defaultFontColor }
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor
        text:qsTr("Prepare local settings for a new 24-word wallet or an existing BIP39 wallet. Choose where to keep its encrypted files, then review the settings before creating or importing keys. Monzero does not hold your keys or a recovery copy.")
    }
    ComboBox {
        id:network;objectName:"lightSetupNetwork"
        model:panel.coin === "bch" || panel.coin === "doge" ? ["regtest"] : panel.coin === "btc" ? ["regtest","testnet4","signet","main"] : ["regtest","test","main"]
        onCurrentIndexChanged:consent.checked=false
    }
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor
        text:network.currentText === "regtest" ? qsTr("Isolated test network: coins have no real value.")
            :network.currentText === "test" || network.currentText === "testnet4" || network.currentText === "signet" ? qsTr("Public test network: test coins only.")
            :qsTr("Main network selected. Wallet support is a development preview; DEX trading is not enabled.")
    }
    M.TextPlain {text:qsTr("Account number (usually 0)");color:M.Style.defaultFontColor}
    SpinBox {id:accountIndex;objectName:"lightSetupAccountIndex";from:0;to:2147483647;value:0;editable:true;onValueChanged:consent.checked=false}
    TextField {
        id:directory;objectName:"lightSetupDirectory";Layout.fillWidth:true
        placeholderText:qsTr("Existing local folder (full path)");maximumLength:1024
        onTextChanged:consent.checked=false
    }
    M.TextPlain {Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;text:qsTr("A separate private wallet folder will be created inside this folder. Keep a separate encrypted backup on another device.")}
    TextField {
        id:host;objectName:"lightSetupHost";Layout.fillWidth:true;maximumLength:253
        placeholderText:qsTr("Chain server hostname (optional for offline setup)")
        onTextChanged:consent.checked=false
    }
    TextField {
        id:port;objectName:"lightSetupPort";Layout.fillWidth:true;maximumLength:5
        placeholderText:qsTr("TLS server port");visible:host.text.length>0
        onTextChanged:consent.checked=false
    }
    M.TextPlain {Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;text:qsTr("Chain servers can observe wallet queries. Preparing settings does not connect to a server or send coins.")}
    CheckBox {id:consent;objectName:"lightSetupConsent";text:qsTr("Prepare local settings for this coin and network.")}
    TextField {
        id:backupSource;objectName:"lightSetupBackupSource";Layout.fillWidth:true;maximumLength:1024
        placeholderText:qsTr("Encrypted backup folder (leave empty for a new wallet or recovery words)")
        onTextChanged:consent.checked=false
    }
    M.StandardButton {
        objectName:"lightSetupRestoreBackup";text:qsTr("Prepare encrypted backup recovery")
        enabled:consent.checked && backupSource.text.charAt(0)==="/" && directory.text.charAt(0)==="/"
            && (host.text.length===0 || (/^[0-9]+$/.test(port.text) && Number(port.text)>0 && Number(port.text)<=65535))
        onClicked: {
            if(!enabled)return
            consent.checked=false;panel.error=""
            if(!panel.backend.prepareLocalBackupRestore(panel.coin,network.currentText,directory.text,backupSource.text,host.text,host.text.length?Number(port.text):0))
                panel.error=qsTr("Could not prepare backup recovery. Check the private backup folder and the destination folder on this device.")
        }
    }
    M.StandardButton {
        objectName:"lightSetupPrepare";text:qsTr("Review wallet settings")
        enabled:consent.checked && backupSource.text.length===0 && directory.text.charAt(0)==="/" && network.currentText.length>0
            && (host.text.length===0 || (/^[0-9]+$/.test(port.text) && Number(port.text)>0 && Number(port.text)<=65535))
        onClicked: {
            if (!enabled) return
            consent.checked=false;panel.error=""
            if (!panel.backend.prepareLocalLightWallet(panel.coin,network.currentText,directory.text,host.text,host.text.length?Number(port.text):0,accountIndex.value))
                panel.error=qsTr("Could not prepare the wallet. Check that the folder belongs to you, the server settings are valid and the wallet helper is available.")
        }
    }
    M.TextPlain {Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;text:panel.error;visible:text.length>0}
}
