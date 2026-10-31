import QtQuick 2.9
import QtTest 1.2
import "../../pages" as Pages

Item {
    width: 800; height: 700
    property int idealThreadCount: 8
    property var currentWallet: wallet
    property var walletManager: manager
    property var persistentSettings: settings
    property var appWindow: app
    property var middlePanel: panel
    property var translationManager: translations
    function releaseFocus() {}
    QtObject { id: otherWallet; function address(a, b) { return "other-primary-address"; } }
    QtObject { id: wallet; function address(a, b) { return "test-primary-address"; } }
    QtObject { id: translations; property string emptyString: "" }
    QtObject { id: settings; property bool useRemoteNode: false }
    QtObject {
        id: app
        property bool disconnected: false
        property bool daemonSynced: true
        property int daemonStartStopInProgress: 0
        property bool isMining: false
        property double miningHashRate: 0
        property bool themeTransition: false
        property string localDaemonAddress: "localhost:16175"
        property string page: ""
        function showPageRequest(value) { page = value; }
    }
    QtObject {
        id: panel
        property string state: "Advanced"
        property QtObject advancedView: QtObject { property string state: "Mining" }
        property QtObject settingsView: QtObject { property string settingsStateViewState: "" }
    }
    QtObject {
        id: manager
        property int starts: 0
        property int stops: 0
        property bool success: true
        property var lastStart: []
        property bool deferCommand: false
        property var commandCallback
        property bool statusSuccess: true
        property bool deferStatus: false
        property var statusCallback
        function localMiningStatusAsync(endpoint, callback) { if (deferStatus) { statusCallback = callback; return; } callback(statusSuccess, app.isMining, 123); }
        function localMiningCommandAsync(endpoint, address, threads, start, callback) {
            if (start) { starts++; lastStart = [address, threads, false, false]; }
            else stops++;
            if (deferCommand) { commandCallback = callback; return; }
            if (success) app.isMining = start;
            callback(success);
        }
    }
    Pages.Mining { id: mining; width: 780; height: 680 }
    TestCase {
        name: "MiningControls"
        when: windowShown
        function start() { return findChild(mining, "startMiningButton"); }
        function stop() { return findChild(mining, "stopMiningButton"); }
        function init() {
            currentWallet = wallet; settings.useRemoteNode = false; app.localDaemonAddress = "localhost:16175";
            app.disconnected = false; app.daemonSynced = true; app.daemonStartStopInProgress = 0;
            app.isMining = false; manager.success = true; manager.starts = 0; manager.stops = 0;
            manager.deferCommand = false; manager.deferStatus = false; manager.statusSuccess = true; mining.statusPending = false;
            mining.busy = false; mining.errorText = ""; mining.threads = 1; mining.update();
        }
        function initTestCase() { compare(idealThreadCount, 8); compare(mining.threads, 1); }
        function test_singleCpuHasOneThread() { compare(mining.threads, 1); verify(!findChild(mining, "reduceMiningThreads").enabled); }
        function test_explicitStartAndStop() {
            verify(start().enabled); compare(manager.starts, 0);
            start().clicked(); compare(manager.starts, 1);
            compare(manager.lastStart, ["test-primary-address", 1, false, false]);
            verify(!start().enabled); verify(stop().enabled);
            stop().clicked(); compare(manager.stops, 1); verify(start().enabled);
        }
        function test_remoteCannotMine() {
            settings.useRemoteNode = true; verify(!start().enabled); verify(!stop().enabled);
            start().clicked(); compare(manager.starts, 0);
        }
        function test_syncBlocksStartButAllowsStop() {
            app.daemonSynced = false; verify(!start().enabled);
            start().clicked(); compare(manager.starts, 0);
            app.isMining = true; mining.update(); verify(stop().enabled);
            stop().clicked(); compare(manager.stops, 1);
        }
        function test_disconnectedCannotStart() {
            app.disconnected = true; verify(!start().enabled);
            start().clicked(); compare(manager.starts, 0);
        }
        function test_closedWalletCannotStart() {
            currentWallet = null; verify(!start().enabled);
            start().clicked(); compare(manager.starts, 0);
        }
        function test_busyBlocksRepeatClick() {
            mining.busy = true; start().clicked(); stop().clicked();
            compare(manager.starts, 0); compare(manager.stops, 0);
        }
        function test_failureIsVisible() {
            manager.success = false; start().clicked();
            verify(mining.errorText.length > 0); verify(!app.isMining); verify(!mining.busy);
        }
        function test_connectionNeverStartsMining() {
            app.disconnected = true; app.disconnected = false;
            app.daemonSynced = false; app.daemonSynced = true;
            mining.update(); compare(manager.starts, 0);
        }
        function test_initializationKeepsControlsBusyUntilReply() {
            manager.deferCommand = true; start().clicked();
            verify(mining.busy); verify(!start().enabled); start().clicked(); compare(manager.starts, 1);
            manager.commandCallback(false); verify(!mining.busy); verify(mining.errorText.length > 0);
        }
        function test_unavailableStatusClearsStaleMiningBadge() {
            app.isMining = true; app.miningHashRate = 123;
            manager.statusSuccess = false; mining.update(); verify(!mining.statusKnown); verify(!start().enabled); verify(!app.isMining); compare(app.miningHashRate, 0);
        }
        function test_disconnectClearsDisplayedReading() {
            app.isMining = true; mining.update();
            verify(findChild(mining, "miningStatusText").text.indexOf("123") >= 0);
            app.disconnected = true;
            verify(findChild(mining, "miningStatusText").text.indexOf("unknown") >= 0);
            verify(findChild(mining, "miningStatusText").text.indexOf("123") < 0);
            verify(!mining.statusKnown); verify(!app.isMining);
        }
        function test_oldWalletCommandDoesNotShowErrorInNewWallet() {
            manager.deferCommand = true; start().clicked();
            currentWallet = otherWallet;
            manager.commandCallback(false, "no-response");
            verify(!mining.busy); compare(mining.errorText, "");
        }
        function test_oldNodeCommandDoesNotShowErrorForNewNode() {
            manager.deferCommand = true; start().clicked();
            app.localDaemonAddress = "localhost:6175";
            manager.commandCallback(false, "node-rejected");
            verify(!mining.busy); compare(mining.errorText, "");
        }
        function test_reconnectRefreshesAfterOldPendingReply() {
            manager.deferStatus = true; mining.update();
            var oldReply = manager.statusCallback;
            app.disconnected = true; app.disconnected = false;
            verify(findChild(mining, "miningStatusText").text.indexOf("Checking") >= 0);
            manager.deferStatus = false; oldReply(true, true, 999);
            verify(mining.statusKnown); verify(!mining.statusPending);
            verify(findChild(mining, "miningStatusText").text.indexOf("999") < 0);
            verify(findChild(mining, "miningStatusText").text.indexOf("disconnected") < 0);
        }
        function test_commandReportsActionableReason() {
            manager.deferCommand = true; start().clicked();
            manager.commandCallback(false, "too-many-threads");
            verify(mining.errorText.indexOf("fewer CPU threads") >= 0);
        }
        function test_timeoutDoesNotClaimMiningStopped() {
            app.isMining = true; mining.update();
            manager.deferCommand = true; stop().clicked();
            manager.commandCallback(false, "no-response");
            verify(mining.errorText.indexOf("may still be running") >= 0);
        }
        function test_startingNodeCannotMine() {
            app.daemonStartStopInProgress = 1; verify(!start().enabled);
            start().clicked(); compare(manager.starts, 0);
        }
    }
}
