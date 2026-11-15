import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
// The in-app exchange (Linux) against a stand-in engine: the book it builds from the engine's
// listings, and the exact engine calls behind Buy, Sell and Post. No network (fetchEnabled off).
Item {
    id: root
    width: 1400; height: 1500
    property var appWindow: app
    property var translationManager: translations
    QtObject { id: app; property bool themeTransition: false; function showStatusMessage(a, b) {} }
    QtObject { id: translations; property string emptyString: "" }

    readonly property real now: Date.now() / 1000
    // SAMPLE offers, not market data
    function listing(id, side, wei, xmz, min, maker) {
        var l = { id: id, pair: "SOL/XMZ", side: side, chainId: 501000101, maker: maker || "0x00000000000000000000000000000000000000b0",
                  valueWei: String(wei), xmzAtoms: String(xmz * 1e11), expiresAt: now + 7200 }
        if (min) l.minXmzAtoms = String(min * 1e11)
        return l
    }
    QtObject {
        id: engine
        property var calls: []
        property var dexWalletStatus: ({ state: "ready", balance: "2500", unlocked: "2400", address: "MZtest", keysReady: true, keysExist: true,
                                         tradeAccount: "0x00000000000000000000000000000000000000a1", solTradeAccount: "11111111111111111111111111111112" })
        property var evmTradeStatus: ({ configured: false })
        property var evmMarketStatus: ({
            SOL: { configured: true, busy: false, coin: "SOL", chainId: 501000101, relays: [], mine: [], message: "",
                   listings: [ listing("0xa1", "sell-xmz", 215039000, 1500), listing("0xa2", "sell-xmz", 300000000, 2000, 100),
                               listing("0xa3", "sell-xmz", 400000000, 1000, 0, "0x00000000000000000000000000000000000000a1"),
                               listing("0xb1", "buy-xmz", 180000000, 1500, 100), listing("0xb2", "buy-xmz", 50000000, 500) ] },
            ETH: { configured: true, busy: false, coin: "ETH", chainId: 1, relays: [], mine: [], listings: [] } })
        function refreshEvmOffers(pair) { return true }
        function takeEvmOffer(id, xmz) { calls.push(["take", id, xmz]); return true }
        function postEvmOffer(chainId, side, coin, xmz, hours, min) { calls.push(["post", chainId, side, coin, xmz, hours, min]); return true }
        function cancelEvmOffer(folder) { calls.push(["cancel", folder]); return true }
    }
    M.DexExchange { id: ex; width: root.width; dex: engine; fetchEnabled: false }

    function find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) { var f = find(item.children[i], name); if (f) return f }
        return null
    }
    function press(name) {
        var o = find(ex, name)
        for (var i = 0; i < o.children.length; ++i) if (o.children[i].clicked !== undefined) { o.children[i].clicked(null); return }
        throw new Error("no button " + name)
    }

    TestCase {
        name: "DexExchange"
        when: windowShown
        function init() { engine.calls = []; ex.pick("SOL"); var f = find(ex, "exchangeForm"); f.clearPick(); f.reviewing = false }

        function test_book() {
            compare(ex.book.asks.map(function (r) { return r.id }).join(), "0xa1,0xa2,0xa3")
            compare(ex.book.bids.map(function (r) { return r.id }).join(), "0xb1,0xb2")
            verify(ex.book.asks[2].mine)
            fuzzyCompare(ex.book.bestAsk, 0.215039 / 1500, 1e-15)
        }
        function test_market_buy_takes_the_whole_best_offer_after_review() {
            var f = find(ex, "exchangeForm")
            f.side = "buy"; f.type = "market"
            find(ex, "orderAmount").text = "1500"
            press("orderButton")
            compare(engine.calls.length, 0)          // first the review
            verify(f.reviewing)
            press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["take", "0xa1", ""]]))
        }
        function test_market_buy_of_a_part_goes_to_the_partial_offer() {
            var f = find(ex, "exchangeForm")
            f.side = "buy"; f.type = "market"
            find(ex, "orderAmount").text = "500"
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["take", "0xa2", "500"]]))
        }
        function test_market_sell_part_of_a_bid() {
            var f = find(ex, "exchangeForm")
            f.side = "sell"; f.type = "market"
            find(ex, "orderAmount").text = "300"
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["take", "0xb1", "300"]]))
        }
        function test_an_amount_no_offer_allows_is_not_sent() {
            var f = find(ex, "exchangeForm")
            f.side = "buy"; f.type = "market"
            find(ex, "orderAmount").text = "1950"      // 0xa2 would keep 50 < its 100 minimum; 0xa1 is whole
            verify(!f.ready)
            press("orderButton")
            compare(engine.calls.length, 0)
        }
        function test_picked_offer_is_the_one_taken() {
            var book = find(ex, "exchangeBook")
            book.picked(ex.book.asks[1])
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["take", "0xa2", ""]]))
        }
        function test_limit_buy_posts_an_offer() {
            var f = find(ex, "exchangeForm")
            f.type = "limit"; f.side = "buy"
            find(ex, "limitPrice").text = "0.0001"
            find(ex, "orderAmount").text = "1000"
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["post", 501000101, "buy-xmz", "0.1", "1000", 6, "100"]]))
        }
        function test_selling_more_than_the_trading_wallet_holds_is_blocked() {
            var f = find(ex, "exchangeForm")
            f.type = "limit"; f.side = "sell"
            find(ex, "limitPrice").text = "0.0002"
            find(ex, "orderAmount").text = "5000"
            verify(!f.ready)
        }
        function test_locked_keys_block_orders() {
            var w = JSON.parse(JSON.stringify(engine.dexWalletStatus)); w.keysReady = false; engine.dexWalletStatus = w
            var f = find(ex, "exchangeForm")
            f.side = "buy"; f.type = "market"
            find(ex, "orderAmount").text = "1500"
            verify(!f.ready)
            verify(find(ex, "unlockCard").visible)
            w.keysReady = true; engine.dexWalletStatus = w
            verify(f.ready)
        }
        function test_market_orders_wait_for_the_running_trade() {
            engine.evmTradeStatus = { configured: true, snapshot: { done: false, role: "owner", stage: "Locked", offer: { chainId: 501000101, value: "1", xmzAmount: "1" } } }
            var f = find(ex, "exchangeForm")
            f.side = "buy"; f.type = "market"
            find(ex, "orderAmount").text = "1500"
            verify(!f.ready)
            engine.evmTradeStatus = { configured: false }
            verify(f.ready)
        }
    }
}
