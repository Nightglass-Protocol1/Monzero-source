import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Controls 2.0
import "." as M

ColumnLayout {
    id:panel
    objectName:"lightWalletImportPanel"
    property var backend
    property string coin:""
    property var walletStatus:({})
    property var review:walletStatus.importReview || ({})
    property var account:review.review || ({})
    property string context:coin+":"+(walletStatus.creationDigest || "")
    property bool completed:walletStatus.walletImportPending === true && walletStatus.state !== "wallet-import-incomplete"
    property int revision:0
    property int previewRevision:-1
    property string error:""
    visible:walletStatus.setupConfigured === true && walletStatus.creation && walletStatus.creation.seedEncoding === "bip39-english-v1"
    spacing:10
    function changedInput() {revision++;if(consent)consent.checked=false;if(expiry)expiry.restart()}
    function clear() {if(phrase)phrase.text="";if(passphrase)passphrase.text="";if(password)password.text="";if(confirmation)confirmation.text="";if(consent)consent.checked=false;previewRevision=-1;if(expiry)expiry.stop()}
    onContextChanged:{clear();error=""}
    onWalletStatusChanged:if(walletStatus.state === "unknown" && !panel.completed)error=qsTr("The import could not be verified. Check the recovery material and preview the account again.")
    onVisibleChanged:if(!visible)clear()
    Connections {target:Qt.application;onStateChanged:if(Qt.application.state !== Qt.ApplicationActive)panel.clear()}
    Timer {id:expiry;interval:60000;onTriggered:panel.clear()}
    M.TextPlain {text:qsTr("Import an existing wallet's recovery words");font.bold:true;color:M.Style.defaultFontColor}
    M.TextPlain {
        Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;textFormat:Text.PlainText
        text:panel.completed ? qsTr("Keys imported into an encrypted local vault. Complete history recovery, backup verification and activation before using this account. Payments, new receive addresses and DEX trading remain unavailable.")
            :qsTr("Preview the account derived from your English BIP39 words. Check the address and derivation against your original wallet. Import restores keys only; it does not recover address history or active trades. This preview address is not a new receiving allocation.")
    }
    ColumnLayout {
        Layout.fillWidth:true;visible:!panel.completed
        TextField {
            id:phrase;objectName:"lightImportPhrase";Layout.fillWidth:true;maximumLength:1024;echoMode:TextInput.Password
            placeholderText:qsTr("English BIP39 recovery words");enabled:panel.walletStatus.busy !== true
            inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText;onTextChanged:panel.changedInput()
        }
        TextField {
            id:passphrase;objectName:"lightImportPassphrase";Layout.fillWidth:true;maximumLength:1024;echoMode:TextInput.Password
            placeholderText:qsTr("Additional BIP39 passphrase, if used");enabled:panel.walletStatus.busy !== true
            inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText;onTextChanged:panel.changedInput()
        }
        M.StandardButton {
            objectName:"lightImportPreview";text:qsTr("Preview account")
            enabled:panel.walletStatus.walletImportAvailable === true && phrase.text.trim().length>0
            onClicked: {
                if(!enabled)return
                panel.error="";consent.checked=false;panel.previewRevision=panel.revision
                if(!panel.backend.previewLightWalletImport(panel.coin,panel.walletStatus.creationDigest,phrase.text,passphrase.text)) {
                    panel.previewRevision=-1;panel.error=qsTr("Could not preview this account. Check the recovery words and selected settings.")
                }
            }
        }
        M.TextPlain {
            objectName:"lightImportReview";Layout.fillWidth:true;wrapMode:Text.WrapAnywhere;color:M.Style.defaultFontColor;textFormat:Text.PlainText
            visible:!!panel.review.reviewDigest && panel.previewRevision === panel.revision
            text:qsTr("Account: %1 · Network: %2\nDerivation: %3\nAddress for identity review: %4\nAdditional BIP39 passphrase: %5")
                .arg(panel.account.account ? panel.account.account.account : "").arg(panel.account.network || "")
                .arg(panel.account.receivePath || "").arg(panel.account.receiveAddress || "")
                .arg(panel.account.additionalPassphraseUsed ? qsTr("Used") : qsTr("None"))
        }
        TextField {
            id:password;objectName:"lightImportPassword";Layout.fillWidth:true;maximumLength:1024;echoMode:TextInput.Password
            placeholderText:qsTr("Password for the imported local vault");enabled:panel.walletStatus.busy !== true
            inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText;onTextChanged:{consent.checked=false;expiry.restart()}
        }
        TextField {
            id:confirmation;objectName:"lightImportConfirmation";Layout.fillWidth:true;maximumLength:1024;echoMode:TextInput.Password
            placeholderText:qsTr("Confirm local vault password");enabled:panel.walletStatus.busy !== true
            inputMethodHints:Qt.ImhSensitiveData | Qt.ImhNoPredictiveText;onTextChanged:{consent.checked=false;expiry.restart()}
        }
        CheckBox {
            id:consent;objectName:"lightImportConsent";Layout.fillWidth:true
            text:qsTr("I checked this account and approve importing its keys locally. Discovery is still required.")
            contentItem:M.TextPlain {text:consent.text;color:M.Style.defaultFontColor;wrapMode:Text.WordWrap;leftPadding:consent.indicator.width+consent.spacing}
        }
        M.StandardButton {
            objectName:"lightImportCommit";text:qsTr("Import encrypted keys")
            enabled:panel.walletStatus.walletImportAvailable === true && !!panel.review.reviewDigest
                && panel.previewRevision === panel.revision && consent.checked && password.text.length>0 && password.text===confirmation.text
            onClicked:{
                if(!enabled)return
                var words=phrase.text,additional=passphrase.text,secret=password.text,repeat=confirmation.text,digest=panel.review.reviewDigest
                panel.clear();panel.error=""
                if(!panel.backend.importLightWalletMnemonic(panel.coin,digest,words,additional,secret,repeat,true))
                    panel.error=qsTr("Import was refused. Preview the account again before retrying.")
                words="";additional="";secret="";repeat=""
            }
        }
    }
    M.TextPlain {Layout.fillWidth:true;wrapMode:Text.WordWrap;color:M.Style.defaultFontColor;text:panel.error;visible:text.length>0}
}
