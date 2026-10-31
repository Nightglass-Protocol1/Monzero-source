// Copyright (c) 2014-2024, The Monzero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import QtQuick 2.9
import QtQuick.Layouts 1.1
import "../components" as MonzeroComponents

Rectangle {
    id: root
    color: "transparent"
    property alias miningHeight: mainLayout.height
    property alias stopMiningEnabled: stopButton.enabled
    property int threads: 1
    property bool busy: false
    property bool statusKnown: false
    property bool statusPending: false
    property int statusGeneration: 0
    property string activeAddress: ""
    property string errorText: ""
    readonly property bool localConnected: currentWallet !== undefined && currentWallet !== null && !persistentSettings.useRemoteNode && !appWindow.disconnected && appWindow.daemonStartStopInProgress === 0
    readonly property bool ready: localConnected && appWindow.daemonSynced

    onLocalConnectedChanged: { statusGeneration++; statusKnown = false; appWindow.isMining = false; appWindow.miningHashRate = 0; if (localConnected) { statusText.text = qsTr("Checking mining status…"); update(); } else statusText.text = qsTr("Mining status unknown — local node disconnected. Mining may still be running."); }

    ColumnLayout {
        id: mainLayout
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20; topMargin: 0 }
        spacing: 18
        MonzeroComponents.Label { text: qsTr("Mine Monzero"); fontSize: 24 }
        MonzeroComponents.TextPlain {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Use your computer's CPU to mine XMZ into this wallet. Choose your CPU threads, then click Start mining. Solo mining rewards depend on finding a block; payouts are not guaranteed.")
        }
        MonzeroComponents.WarningBox {
            Layout.fillWidth: true
            visible: !root.ready
            text: persistentSettings.useRemoteNode
                ? qsTr("Mining needs a local node. Open Node settings, select Local node and let it synchronize first.")
                : !root.localConnected
                    ? qsTr("Connect to your local node before mining. You can start it from Node settings.")
                    : qsTr("Your local node is synchronizing. Mining will be available when synchronization finishes.")
        }
        MonzeroComponents.StandardButton {
            visible: !root.ready
            text: qsTr("Open Node settings")
            onClicked: {
                middlePanel.settingsView.settingsStateViewState = "Node";
                appWindow.showPageRequest("Settings");
            }
        }
        MonzeroComponents.Label { text: qsTr("CPU threads"); fontSize: 16 }
        RowLayout {
            spacing: 14
            MonzeroComponents.StandardButton {
                objectName: "reduceMiningThreads"
                text: "−"; small: true; primary: false
                enabled: !root.busy && !appWindow.isMining && root.threads > 1
                onClicked: root.threads--
            }
            MonzeroComponents.TextPlain { text: root.threads; Layout.minimumWidth: 35; horizontalAlignment: Text.AlignHCenter }
            MonzeroComponents.StandardButton {
                text: "+"; small: true; primary: false
                enabled: !root.busy && !appWindow.isMining && root.threads < Math.max(1, idealThreadCount)
                onClicked: root.threads++
            }
            MonzeroComponents.StandardButton {
                text: qsTr("Use half"); small: true; primary: false
                enabled: !root.busy && !appWindow.isMining
                onClicked: root.threads = Math.max(1, Math.floor(idealThreadCount / 2))
            }
        }
        MonzeroComponents.TextPlain {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: qsTr("Using fewer threads leaves more CPU capacity for other apps. Mining uses electricity and can make your computer warm. Keep laptops plugged in while mining.")
        }
        RowLayout {
            spacing: 16
            MonzeroComponents.StandardButton {
                id: startButton
                objectName: "startMiningButton"
                text: root.busy ? qsTr("Please wait…") : qsTr("Start mining")
                enabled: root.ready && root.statusKnown && !appWindow.isMining && !root.busy
                onClicked: {
                    if (!root.ready || !root.statusKnown || appWindow.isMining || root.busy || !currentWallet) return;
                    root.errorText = "";
                    root.busy = true;
                    root.statusKnown = false;
                    root.statusGeneration++;
                    // No background or remote mining: an explicit local action only.
                    var wallet = currentWallet;
                    var endpoint = appWindow.localDaemonAddress;
                    walletManager.localMiningCommandAsync(endpoint, wallet.address(0, 0), root.threads, true, function(ok, reason) {
                        root.busy = false;
                        if (currentWallet !== wallet || appWindow.localDaemonAddress !== endpoint || persistentSettings.useRemoteNode) { root.update(); return; }
                        if (!ok) root.errorText = root.commandError(reason, true);
                        root.update();
                    });
                }
            }
            MonzeroComponents.StandardButton {
                id: stopButton
                objectName: "stopMiningButton"
                text: qsTr("Stop mining")
                enabled: root.localConnected && root.statusKnown && appWindow.isMining && !root.busy
                onClicked: {
                    if (!root.localConnected || !root.statusKnown || !appWindow.isMining || root.busy) return;
                    root.errorText = "";
                    root.busy = true;
                    root.statusKnown = false;
                    root.statusGeneration++;
                    var wallet = currentWallet;
                    var endpoint = appWindow.localDaemonAddress;
                    walletManager.localMiningCommandAsync(endpoint, "", 0, false, function(ok, reason) {
                        root.busy = false;
                        if (currentWallet !== wallet || appWindow.localDaemonAddress !== endpoint || persistentSettings.useRemoteNode) { root.update(); return; }
                        if (!ok) root.errorText = root.commandError(reason, false);
                        root.update();
                    });
                }
            }
        }
        MonzeroComponents.TextPlain {
            id: statusText
            objectName: "miningStatusText"
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: qsTr("Checking mining status…")
        }
        MonzeroComponents.TextPlain {
            Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
            visible: root.statusKnown && appWindow.isMining && root.activeAddress !== ""
            text: qsTr("Mining reward address: %1").arg(root.activeAddress)
        }
        MonzeroComponents.WarningBox { Layout.fillWidth: true; visible: root.errorText !== ""; text: root.errorText }
        MonzeroComponents.TextPlain {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: qsTr("Starting mining uses this wallet's primary address. Rewards unlock after 60 blocks. The local node can keep mining when the GUI closes; use Stop mining before closing if you want it to stop.")
        }
    }
    function commandError(reason, starting) {
        if (reason === "already-mining") return qsTr("The node reports that mining is already running. Checking its current status and reward address.");
        if (reason === "node-busy") return qsTr("The node is busy. Let it finish synchronizing before starting mining.");
        if (reason === "too-many-threads") return qsTr("The node rejected the thread count. Select fewer CPU threads.");
        if (reason === "invalid-request") return qsTr("Mining requires a valid local node endpoint and wallet address. Check Node settings.");
        if (reason === "scheduler-stopping") return qsTr("The wallet is shutting down and could not send the mining command. Check the node status when you reopen the wallet.");
        if (reason === "start-failed") return qsTr("The node could not start its miner. Check the node console, available memory and CPU thread count.");
        if (reason === "invalid-address") return qsTr("The node rejected the mining reward address. Check that wallet and node use the same network and a primary address.");
        if (reason === "node-rejected") return qsTr("The local node rejected the mining command. Check its console for the reason and ensure local RPC allows mining.");
        return starting
            ? qsTr("The node did not confirm mining startup. It may still be initializing RandomX or mining. Wait for a status reading before retrying; check the node console, available memory and that the local RPC endpoint allows mining. Try one CPU thread when connected.")
            : qsTr("The node did not confirm that mining stopped. Mining may still be running. Check the node console before closing the wallet.");
    }
    function update() {
        if (!root.localConnected || root.busy || root.statusPending) return;
        root.statusPending = true;
        var generation = root.statusGeneration;
        var endpoint = appWindow.localDaemonAddress;
        var wallet = currentWallet;
        walletManager.localMiningStatusAsync(endpoint, function(ok, mining, rate, address) {
            root.statusPending = false;
            if (generation !== root.statusGeneration || !root.localConnected || currentWallet !== wallet || appWindow.localDaemonAddress !== endpoint) { if (root.localConnected) root.update(); return; }
            root.statusKnown = ok;
            if (!ok) {
                appWindow.isMining = false;
                appWindow.miningHashRate = 0;
                statusText.text = qsTr("Mining status unavailable. Check your local node connection.");
                return;
            }
            root.activeAddress = address || "";
            appWindow.isMining = mining;
            appWindow.miningHashRate = rate;
            statusText.text = mining ? (rate > 0 ? qsTr("Mining at %1 H/s").arg(rate) : qsTr("Mining enabled — waiting for a hashrate reading")) : qsTr("Not mining");
        });
    }
    Timer {
        interval: middlePanel.advancedView.state === "Mining" && middlePanel.state === "Advanced" ? 3000 : 15000
        repeat: true
        running: root.localConnected
        onTriggered: root.update()
    }
    Component.onCompleted: update()
}
