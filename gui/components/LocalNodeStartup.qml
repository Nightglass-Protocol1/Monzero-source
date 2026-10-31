import QtQuick 2.9

// One automatic startup check per wallet/node selection. Delayed callbacks from
// an old wallet must never launch a node for the wallet opened afterwards.
QtObject {
    id: root
    property var wallet: null
    property var manager
    property bool useRemoteNode: false
    property bool enabled: true
    property bool startInProgress: false
    property var nettype
    property string dataDir: ""
    property string flags: ""
    property bool pending: false
    property bool attempted: false
    property int generation: 0
    signal startRequested(string flags)
    function reset() { generation++; pending = false; attempted = false; }
    onWalletChanged: reset()
    onNettypeChanged: reset()
    onDataDirChanged: reset()
    onUseRemoteNodeChanged: reset()
    function checkDisconnected() {
        if (!enabled || !wallet || !wallet.disconnected || useRemoteNode || startInProgress || pending || attempted) return;
        attempted = true;
        pending = true;
        var requestGeneration = generation;
        manager.runningAsync(nettype, dataDir, function(running) {
            if (requestGeneration !== generation) return;
            pending = false;
            if (enabled && wallet && wallet.disconnected && !useRemoteNode && !startInProgress && !running) startRequested(flags);
        });
    }
}
