import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
// Coin-for-coin trades on the Exchange page against a stand-in engine: the tab, the book it
// builds from the engine's direct listings, and the exact engine calls behind Buy/Sell, Post,
// Approve, Cancel and the payout. No network (fetchEnabled off).
Item {
    id: root
    width: 1400; height: 2000
    property var appWindow: app
    property var translationManager: translations
    QtObject { id: app; property bool themeTransition: false; function showStatusMessage(a, b) {} }
    QtObject { id: translations; property string emptyString: "" }
    function releaseFocus() {}

    readonly property real now: Date.now() / 1000
    readonly property string me: "0x00000000000000000000000000000000000000a1"
    // SAMPLE offers, not market data. gives/wants as the engine shows them; side: of the pair's base
    function offer(id, pair, side, gives, wants, price, base, quote, maker) {
        return { id: id, pair: pair, side: side, maker: maker || "0x00000000000000000000000000000000000000b0", price: price, base: base, quote: quote,
                 gives: gives, wants: wants, expiresAt: now + 7200, relays: 2 }
    }
    function coin(c, amount) { return { chainId: c === "ETH" ? 11155111 : c === "BNB" ? 97 : 501000102, coin: c, amount: amount, units: "1" } }
    QtObject {
        id: engine
        property var calls: []
        property string refusal: ""
        property var dexWalletStatus: ({ state: "ready", balance: "0", unlocked: "0", address: "MZtest", keysReady: true, keysExist: true,
                                         tradeAccount: root.me, solTradeAccount: "11111111111111111111111111111112" })
        property var evmTradeStatus: ({ configured: false })
        property var evmMarketStatus: ({ ETH: { configured: true, busy: false, coin: "ETH", chainId: 11155111, relays: [], mine: [], listings: [] } })
        property var directMarketStatus: ({
            configured: true, keys: true, busy: false, state: "ok", message: "",
            pairs: ["BNB/ETH", "BNB/SOL", "ETH/SOL"],
            chains: [{ chainId: 97, coin: "BNB", decimals: 18 }, { chainId: 11155111, coin: "ETH", decimals: 18 }, { chainId: 501000102, coin: "SOL", decimals: 9 }],
            listings: [
                offer("0xa1", "BNB/ETH", "sell", coin("BNB", "0.5"), coin("ETH", "0.012"), "0.024", "0.5", "0.012"),
                offer("0xa2", "BNB/ETH", "sell", coin("BNB", "1"), coin("ETH", "0.02"), "0.02", "1", "0.02"),
                offer("0xa3", "BNB/ETH", "sell", coin("BNB", "2"), coin("ETH", "0.05"), "0.025", "2", "0.05", root.me),
                offer("0xb1", "BNB/ETH", "buy", coin("ETH", "0.018"), coin("BNB", "1"), "0.018", "1", "0.018"),
                offer("0xb2", "BNB/ETH", "buy", coin("ETH", "0.019"), coin("BNB", "1"), "0.019", "1", "0.019"),
                offer("0xc1", "ETH/SOL", "sell", coin("ETH", "0.1"), coin("SOL", "2"), "20", "0.1", "2") ],
            mine: [
                { folder: "/t/take-1", kind: "take", state: "agreed", pair: "BNB/ETH", gives: coin("ETH", "0.02"), wants: coin("BNB", "1"), expiresAt: now + 3600,
                  trade: { stage: "Start", approved: false, commitment: "0x" + "c".repeat(64), lockDeadline: now + 7200 } },
                { folder: "/t/take-2", kind: "take", state: "done", pair: "ETH/SOL", gives: coin("ETH", "0.1"), wants: coin("SOL", "2"), expiresAt: now - 10,
                  trade: { stage: "Done(Swapped)", approved: true, done: true } },
                { folder: "/t/offer-1", kind: "offer", state: "open", pair: "BNB/ETH", gives: coin("BNB", "2"), wants: coin("ETH", "0.05"), expiresAt: now + 7200, trade: {} } ] })
        property real capCoin: 1000
        signal dexRiskChanged()
        function evmOrderRefusal(chainId, coin, xmz) { return "" }
        function refreshEvmOffers(pair) { return true }
        function refreshDirectOffers() { calls.push(["refresh"]); return true }
        function directOrderRefusal(g, ga, w, wa) { return refusal }
        function takeDirectOffer(id) { calls.push(["take", id]); return true }
        function postDirectOffer(g, ga, w, wa, hours) { calls.push(["post", g, ga, w, wa, hours]); return true }
        function cancelDirectOffer(folder) { calls.push(["cancel", folder]); return true }
        function approveDirectTrade(folder, commitment) { calls.push(["approve", folder, commitment]); return true }
        function payoutDirectTrade(folder, address) { calls.push(["payout", folder, address]); return true }
    }
    M.DexExchange { id: ex; width: root.width; dex: engine; fetchEnabled: false }

    function find(item, name) {
        if (item.objectName === name) return item
        for (var i = 0; i < item.children.length; ++i) { var f = find(item.children[i], name); if (f) return f }
        return null
    }
    function findAll(item, name, out) {
        out = out || []
        if (item.objectName === name) out.push(item)
        for (var i = 0; i < item.children.length; ++i) findAll(item.children[i], name, out)
        return out
    }
    function click(o) {
        if (o.clicked !== undefined && typeof o.clicked === "function") { o.clicked(); return }
        for (var i = 0; i < o.children.length; ++i) if (o.children[i].clicked !== undefined) { o.children[i].clicked(null); return }
        throw new Error("no button " + o.objectName)
    }
    function panel() { return find(ex, "directPanel") }
    // The one of several same-named controls (one per row) that is on screen
    function shown(name) {
        var all = findAll(ex, name).filter(function (o) { return o.visible })
        if (all.length !== 1) throw new Error(all.length + " visible " + name)
        return all[0]
    }

    TestCase {
        name: "DexDirect"
        when: windowShown
        function init() {
            engine.calls = []; engine.refusal = ""
            ex.pick("ETH"); ex.pick("DIRECT")
            var p = panel(); p.pair = ""; p.confirmId = ""; p.giveCoin = ""; p.localError = ""
            ex.balances = { ETH: 1, BNB: 1, SOL: 10 }
            find(ex, "directGiveAmount").text = ""; find(ex, "directGetAmount").text = ""
        }
        function test_the_tab_shows_the_panel_and_hides_the_xmz_market() {
            verify(find(ex, "pair_DIRECT").visible)
            verify(ex.directMode)
            verify(panel().visible)
            verify(!find(ex, "exchangeBook").visible)
            compare(engine.calls[0][0], "refresh")
            ex.pick("ETH")
            verify(!panel().visible)
        }
        function test_book_asks_cheapest_first_bids_dearest_first() {
            var p = panel()
            compare(p.pairNow, "BNB/ETH")
            compare(p.asks.map(function (l) { return l.id }).join(), "0xa2,0xa1,0xa3")
            compare(p.bids.map(function (l) { return l.id }).join(), "0xb2,0xb1")
            verify(p.mine(p.asks[2]))
            p.pair = "ETH/SOL"
            compare(p.asks.map(function (l) { return l.id }).join(), "0xc1")
        }
        function test_taking_asks_for_confirmation_first() {
            var b = find(ex, "directTake_0xa1")
            click(b)
            compare(engine.calls.filter(function (c) { return c[0] === "take" }).length, 0)
            compare(panel().confirmId, "0xa1")
            click(b)
            compare(JSON.stringify(engine.calls.filter(function (c) { return c[0] === "take" })), JSON.stringify([["take", "0xa1"]]))
        }
        function test_a_refused_take_is_not_sent() {
            engine.refusal = "This order is above the per-trade limit of 0.01 ETH while the DEX is opening. Post a smaller order."
            var b = find(ex, "directTake_0xa2")
            click(b); click(b)
            compare(engine.calls.filter(function (c) { return c[0] === "take" }).length, 0)
            compare(panel().localError, engine.refusal)
        }
        function test_own_offer_cannot_be_taken() {
            verify(!find(ex, "directTake_0xa3").visible)
        }
        function test_post_gives_the_chosen_coin() {
            find(ex, "directGiveAmount").text = "0.5"; find(ex, "directGetAmount").text = "0.01"
            click(find(ex, "directPost"))
            click(find(ex, "directGive_ETH"))
            find(ex, "directGiveAmount").text = "0.02"; find(ex, "directGetAmount").text = "1"
            click(find(ex, "directPost"))
            compare(JSON.stringify(engine.calls.filter(function (c) { return c[0] === "post" })),
                    JSON.stringify([["post", 97, "0.5", 11155111, "0.01", 24], ["post", 11155111, "0.02", 97, "1", 24]]))
        }
        function test_giving_more_than_the_account_holds_is_blocked() {
            ex.balances = { ETH: 1, BNB: 0.1, SOL: 10 }
            find(ex, "directGiveAmount").text = "0.5"; find(ex, "directGetAmount").text = "0.01"
            verify(!find(ex, "directPost").enabled)
        }
        function test_the_limit_refusal_blocks_posting() {
            engine.refusal = "New trades are paused by the operator."
            find(ex, "directGiveAmount").text = "0.5"; find(ex, "directGetAmount").text = "0.011"
            verify(!find(ex, "directPost").enabled)
        }
        function test_the_taker_approves_exactly_the_shown_terms() {
            click(shown("directApprove"))
            compare(JSON.stringify(engine.calls.filter(function (c) { return c[0] === "approve" })), JSON.stringify([["approve", "/t/take-1", "0x" + "c".repeat(64)]]))
        }
        function test_a_finished_swap_pays_out_to_the_address_given() {
            var addr = shown("directPayoutAddress")
            addr.text = "7xKXtg2CW87d97TXJSDpbD5jBkheTqA83TZRuJosgAsU"
            click(shown("directPayout"))
            compare(JSON.stringify(engine.calls.filter(function (c) { return c[0] === "payout" })), JSON.stringify([["payout", "/t/take-2", "7xKXtg2CW87d97TXJSDpbD5jBkheTqA83TZRuJosgAsU"]]))
            compare(ex.withdrawTo.SOL, "7xKXtg2CW87d97TXJSDpbD5jBkheTqA83TZRuJosgAsU")   // remembered for next time
        }
        function test_open_offer_can_be_cancelled() {
            click(shown("directCancel"))
            compare(JSON.stringify(engine.calls.filter(function (c) { return c[0] === "cancel" })), JSON.stringify([["cancel", "/t/offer-1"]]))
        }
        function test_stages_read_as_words() {
            var p = panel(), t = p.trades
            compare(t.map(function (x) { return x.folder }).join(), "/t/take-1,/t/offer-1,/t/take-2")   // running first
            compare(p.stageText(t[0]), "matched: check the terms and approve")
            compare(p.stageText(t[2]), "swapped: you received SOL")
            compare(p.payoutCoin(t[2]), "SOL")
            var waiting = { kind: "take", state: "agreed", gives: coin("ETH", "1"), wants: coin("BNB", "1"), trade: { stage: "Waiting", approved: true } }
            compare(p.stageText(waiting), "your ETH is locked; waiting for the maker to lock BNB")
            var maker = { kind: "offer", state: "agreed", gives: coin("BNB", "1"), wants: coin("ETH", "1"), trade: { stage: "WaitingForA" } }
            compare(p.stageText(maker), "waiting for the taker to lock ETH")
            var refunded = { kind: "offer", state: "done", gives: coin("BNB", "1"), wants: coin("ETH", "1"), trade: { stage: "Done(Refunded)" } }
            compare(p.stageText(refunded), "refunded: your BNB came back")
            compare(p.payoutCoin(refunded), "BNB")
        }
    }
}
