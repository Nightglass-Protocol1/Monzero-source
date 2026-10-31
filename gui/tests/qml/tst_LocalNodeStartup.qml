import QtQuick 2.9
import QtTest 1.2
import "../../components" as Components

TestCase {
    name: "LocalNodeStartup"
    QtObject { id: wallet; property bool disconnected: true }
    QtObject { id: otherWallet; property bool disconnected: true }
    QtObject {
        id: manager
        property var callbacks: []
        function runningAsync(nettype, dataDir, callback) { callbacks.push(callback); }
    }
    Components.LocalNodeStartup {
        id: startup
        wallet: wallet
        manager: manager
        nettype: 1
        flags: "--test-flag"
    }
    SignalSpy { id: launches; target: startup; signalName: "startRequested" }
    function init() {
        startup.wallet = wallet; wallet.disconnected = true; otherWallet.disconnected = true;
        startup.enabled = true; startup.useRemoteNode = false; startup.startInProgress = false;
        startup.nettype = 1; startup.dataDir = ""; startup.reset();
        manager.callbacks = []; launches.clear();
    }
    function test_startsImmediatelyAfterAbsentNodeCheck() {
        startup.checkDisconnected(); compare(manager.callbacks.length, 1); verify(startup.pending);
        manager.callbacks[0](false); compare(launches.count, 1); verify(!startup.pending);
        compare(launches.signalArguments[0][0], "--test-flag");
    }
    function test_existingNodeIsReused() {
        startup.checkDisconnected(); manager.callbacks[0](true); compare(launches.count, 0);
    }
    function test_connectingFirstDoesNotPreventLaterStart() {
        wallet.disconnected = false; startup.checkDisconnected(); compare(manager.callbacks.length, 0);
        wallet.disconnected = true; startup.checkDisconnected(); manager.callbacks[0](false);
        compare(launches.count, 1);
    }
    function test_repeatedStatusDoesNotLaunchTwice() {
        startup.checkDisconnected(); startup.checkDisconnected(); compare(manager.callbacks.length, 1);
        manager.callbacks[0](false); startup.checkDisconnected(); compare(manager.callbacks.length, 1);
        compare(launches.count, 1);
    }
    function test_remoteSelectionCancelsPendingStartup() {
        startup.checkDisconnected(); startup.useRemoteNode = true; manager.callbacks[0](false);
        startup.checkDisconnected(); compare(launches.count, 0); compare(manager.callbacks.length, 1);
    }
    function test_oldWalletCallbackCannotAffectNewCheck() {
        startup.checkDisconnected(); startup.wallet = otherWallet; startup.checkDisconnected();
        compare(manager.callbacks.length, 2); manager.callbacks[0](false);
        compare(launches.count, 0); verify(startup.pending);
        manager.callbacks[1](false); compare(launches.count, 1);
    }
    function test_closedWalletDoesNotStartNode() {
        startup.checkDisconnected(); startup.wallet = null; manager.callbacks[0](false); compare(launches.count, 0);
    }
    function test_connectedBeforeCheckCompletesDoesNotStartNode() {
        startup.checkDisconnected(); wallet.disconnected = false; manager.callbacks[0](false); compare(launches.count, 0);
    }
    function test_manualStartupInProgressDoesNotDuplicate() {
        startup.checkDisconnected(); startup.startInProgress = true; manager.callbacks[0](false); compare(launches.count, 0);
    }
    function test_simpleModeUsesItsExistingManager() {
        startup.enabled = false; startup.checkDisconnected(); compare(manager.callbacks.length, 0);
    }
    function test_networkChangeInvalidatesOldCheck() {
        startup.checkDisconnected(); startup.nettype = 2; manager.callbacks[0](false); compare(launches.count, 0);
    }
}
