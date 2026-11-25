import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
// Selling mined XMZ: what counts as mined, the batch moved to the trading wallet, and sales only
// to buyers at or above the minimum price. A stand-in wallet, pool list and market.
Item {
    id: root
    property var translationManager: translations
    QtObject { id: translations; property string emptyString: "" }
    // SAMPLE data, not real transactions
    property var currentWallet: QtObject {
        property bool viewOnly: false
        property var history: QtObject {
            property var list: []
            function received(since) { return list }
        }
        property real unlocked: 5000e11
        function address(a, b) { return "Mz" + "A".repeat(95) }
        function unlockedBalance() { return unlocked }
    }
    property var appWindow: QtObject {
        property bool walletSynced: true
        property var dexAutoTx: null
        property var sends: []
        property var lastDone: null
        function dexAutoSend(address, atoms, done) { sends.push([address, atoms]); lastDone = done; return true }
    }
    QtObject {
        id: settings
        property bool dexAutoSell: false
        property string dexAutoSellCoin: "SOL"
        property int dexAutoSellPercent: 100
        property string dexAutoSellMinUsd: "0.001"
        property int dexAutoSellBatch: 500
        property bool dexAutoSellPool: true
        property int dexAutoSellSince: 0
        property string dexAutoSellState: "{}"
    }
    QtObject {
        id: feed
        property var data: ({ poolPayments: [{ txid: "b".repeat(64), amount: 400e11 }], usd: { SOL: 200 } })
        function poolPayments(a) { return true }
        function usdPrices() { return true }
    }
    QtObject {
        id: engine
        property var exchangeFeed: feed
        property int pendingTakes: 0
        property var takes: []
        property var dexWalletStatus: ({ state: "ready", keysReady: true, address: "Mt" + "C".repeat(95), unlocked: "0", tradeAccount: "0x00000000000000000000000000000000000000a1" })
        // Bids: 0.000004 SOL/XMZ = $0.0008 (below the minimum) and 0.000006 SOL/XMZ = $0.0012
        property var evmMarketStatus: ({ SOL: { configured: true, listings: [
            { id: "0x" + "1".repeat(64), pair: "SOL/XMZ", side: "buy-xmz", valueWei: String(4000 * 1000), xmzAtoms: String(1000e11), minXmzAtoms: String(100e11), maker: "0xb0", expiresAt: 9e9 },
            { id: "0x" + "2".repeat(64), pair: "SOL/XMZ", side: "buy-xmz", valueWei: String(6000 * 1000), xmzAtoms: String(1000e11), minXmzAtoms: String(100e11), maker: "0xb1", expiresAt: 9e9 } ] } })
        function takeEvmOffers(list) { takes.push(list); return true }
    }
    M.DexAutoSell { id: auto; dex: engine; settings: settings }

    TestCase {
        name: "DexAutoSell"
        function test_mined_xmz_is_moved_then_sold_only_at_the_minimum_price_or_better() {
            currentWallet.history.list = [
                { hash: "a".repeat(64), atoms: String(300e11), coinbase: true, confirmations: 70 },     // a block reward, spendable
                { hash: "b".repeat(64), atoms: String(400e11), coinbase: false, confirmations: 12 },    // a pool payout
                { hash: "c".repeat(64), atoms: String(1000e11), coinbase: false, confirmations: 30 },   // someone else's transfer: not mined
                { hash: "d".repeat(64), atoms: String(250e11), coinbase: true, confirmations: 20 } ]    // a reward not spendable yet
            auto.enable(true)
            verify(settings.dexAutoSellSince > 0)
            // 300 + 400 mined and spendable: a batch of 700 moves to the trading wallet
            compare(appWindow.sends.length, 1)
            compare(appWindow.sends[0][0], engine.dexWalletStatus.address)
            compare(appWindow.sends[0][1], 700e11)
            appWindow.lastDone(true, ["e".repeat(64)], "")
            var st = auto.state()
            compare(st.owed, 0); compare(st.moved, 700e11)
            // Not unlocked in the trading wallet yet: nothing sold
            auto.step()
            compare(engine.takes.length, 0)
            var w = JSON.parse(JSON.stringify(engine.dexWalletStatus)); w.unlocked = "700"; engine.dexWalletStatus = w
            auto.step()
            compare(engine.takes.length, 1)
            // Only the $0.0012 buyer: 700/1.01 - 2 = 691.06… XMZ
            compare(engine.takes[0].length, 1)
            compare(engine.takes[0][0].id, "0x" + "2".repeat(64))
            st = auto.state()
            compare(st.moved, 0)
            verify(st.sold > 690e11 && st.sold < 692e11)
            // Counted once: the same transfers again move nothing
            appWindow.sends = []
            auto.step()
            compare(appWindow.sends.length, 0)
            auto.enable(false)
        }
        function test_cannot_turn_on_without_a_minimum_price() {
            settings.dexAutoSellMinUsd = ""
            auto.enable(true)
            verify(!settings.dexAutoSell)
            settings.dexAutoSellMinUsd = "0.001"
        }
    }
}
