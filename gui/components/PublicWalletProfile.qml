import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2
import "." as M

ColumnLayout {
    id: root
    property var wallet: null
    property bool busy: false
    property int generation: 0
    property var pending: null
    // Optional transport supplied by the offline QML test harness.
    property var transport: null
    property string notice: "Private by default. No information is sent until you choose Publish or Stop sharing."
    readonly property string genesis: "FRJ2nMKxwd94nsgTLnjpJ77d3gZ2X5NxdUkTcFXuoNaWSQBNp5syt7dcwVsGPYP6vE1hyuLQcVgTtHKkNBUhpJoENAhsryR"
    spacing: 12
    Layout.fillWidth: true
    Component.onDestruction: generation++
    onWalletChanged: {
        generation++;
        pending = null; busy = false;
        mode.currentIndex = 0; publicName.text = "";
        notice = "Private by default. Opening this panel does not contact the directory. Existing public profiles remain online until Stop sharing is confirmed.";
        if (wallet) {
            var saved = wallet.getCacheAttribute("monzero.publicProfile.mode");
            mode.currentIndex = saved === "balance" ? 2 : saved === "address" ? 1 : 0;
            publicName.text = wallet.getCacheAttribute("monzero.publicProfile.name");
        }
    }
    function request(action, body, callback) {
        if (transport) { transport(action, body, callback); return; }
        var g = generation;
        pending = wallet.requestPublicProfile(action, body, function(status, response) {
            if (g !== generation) return;
            pending = null;
            if (status !== 200) { busy = false; notice = "Update not confirmed. The public profile may still be visible. Retry or check the directory."; return; }
            try { callback(JSON.parse(response)); }
            catch (e) { busy = false; notice = "Invalid directory response. No privacy change confirmed."; }
        });
        if (!pending) { busy = false; notice = "Cannot start this request. Check the wallet network and proxy settings."; }
    }

    function submit(hide) {
        if (!wallet || busy || wallet.nettype !== 0) return;
        var selected = hide ? "private" : mode.currentIndex === 2 ? "balance" : "address";
        var snapshot = wallet.publicProfileSnapshot();
        if (selected === "balance" && !snapshot.synchronized) { notice = "Wait for the wallet and node to synchronize before sharing a balance."; return; }
        var name = hide ? "" : publicName.text.trim();
        if (!/^[A-Za-z0-9 _.-]{0,40}$/.test(name)) { notice = "Use up to 40 letters, numbers, spaces, dots, underscores or hyphens for the public name."; return; }
        if (snapshot.address === genesis) {
            if (hide || selected !== "balance") { notice = "GenesisWallet1 is permanently public by project policy."; return; }
            name = "GenesisWallet1";
        }
        var payload = {address:snapshot.address,mode:selected,name:name,balance:selected === "balance" ? snapshot.balance : "-",height:selected === "balance" ? snapshot.height : "-"};
        busy = true; notice = "Preparing your signed disclosure…";
        request("challenge",payload,function(c) {
            if (!/^[0-9a-f]{64}$/.test(c.nonce) || !Number.isInteger(c.expires) || !Number.isInteger(c.revision) || c.revision < 0 || c.expires < Date.now()/1000 - 60 || c.expires > Date.now()/1000 + 600) throw Error("Invalid challenge");
            var message = ["Monzero public wallet profile v1","Origin:https://monzero.org","Address:"+payload.address,"Mode:"+payload.mode,"Balance:"+payload.balance,"Height:"+payload.height,"Name:"+payload.name,"Nonce:"+c.nonce,"Expires:"+c.expires,"Revision:"+c.revision].join("\n");
            var signature = wallet.signMessage(message);
            if (!signature || signature.indexOf("SigV2") !== 0) { busy = false; notice = "This wallet could not sign the disclosure. A spend-capable wallet is required."; return; }
            request("update",{nonce:c.nonce,signature:signature},function(result) {
                if (result.ok !== true || result.mode !== selected) throw Error("Unconfirmed update");
                wallet.setCacheAttribute("monzero.publicProfile.mode",selected);
                wallet.setCacheAttribute("monzero.publicProfile.name",name);
                wallet.storeAsync(function(ok) {});
                busy = false; mode.currentIndex = selected === "balance" ? 2 : selected === "address" ? 1 : 0;
                notice = selected === "private" ? "Stop sharing confirmed. Your public profile has been removed. Previously copied information cannot be erased." : "Published. This is a wallet-reported snapshot, not a verified balance. Press Publish again when you want to update it.";
            });
        });
    }
    M.TextPlain { text: qsTr("Public wallet profile"); font.pixelSize: 20; font.bold: true }
    M.TextPlain { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Choose whether to disclose your primary address and total wallet balance across all accounts. Balances are snapshots; they are not updated automatically. No private keys are shared.") }
    ComboBox { id: mode; Layout.fillWidth: true; enabled: !root.busy; palette.button: M.Style.inputBoxBackground; palette.buttonText: M.Style.defaultFontColor; palette.base: M.Style.inputBoxBackground; palette.text: M.Style.defaultFontColor; palette.highlight: M.Style.accentMuted; model: [qsTr("Private"),qsTr("Share address"),qsTr("Share address and balance")] }
    TextField { id: publicName; Layout.fillWidth: true; enabled: !root.busy; maximumLength: 40; placeholderText: qsTr("Public name (optional)"); color: M.Style.defaultFontColor; background: Rectangle { color:M.Style.inputBoxBackground; border.color:M.Style.inputBorderColorInActive; radius:4 } }
    M.TextPlain { Layout.fillWidth:true; wrapMode:Text.Wrap; text:qsTr("Public information can be copied. Stop sharing removes your ordinary listing and stops future disclosure, but cannot erase copies. Selecting Private alone does not remove an existing listing.") }
    RowLayout {
        M.StandardButton { text:qsTr("Publish / update snapshot"); enabled:!!root.wallet && root.wallet.nettype === 0 && !root.busy && mode.currentIndex > 0; onClicked:root.submit(false) }
        M.StandardButton { text:qsTr("Stop sharing"); enabled:!!root.wallet && root.wallet.nettype === 0 && !root.busy; onClicked:root.submit(true) }
    }
    M.TextPlain { Layout.fillWidth:true; wrapMode:Text.Wrap; text:root.notice }
}
