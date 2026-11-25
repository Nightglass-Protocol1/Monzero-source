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
    QtObject { id: app; property bool themeTransition: false; property string walletPassword: ""; function showStatusMessage(a, b) {} }
    QtObject { id: translations; property string emptyString: "" }
    function releaseFocus() {}

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
        property var evmTrades: []
        function selectEvmTrade(dir) { calls.push(["select", dir]); return true }
        function abandonEvmTrade() { calls.push(["abandon"]); return true }
        function approveAllEvmTrades() { calls.push(["approveAll"]); return 1 }
        function setAutoApproveEvmTrades(on) { calls.push(["auto", on]) }
        property var evmMarketStatus: ({
            SOL: { configured: true, busy: false, coin: "SOL", chainId: 501000101, relays: [], mine: [], message: "",
                   listings: [ listing("0xa1", "sell-xmz", 215039000, 1500), listing("0xa2", "sell-xmz", 300000000, 2000, 100),
                               listing("0xa3", "sell-xmz", 400000000, 1000, 0, "0x00000000000000000000000000000000000000a1"),
                               listing("0xb1", "buy-xmz", 180000000, 1500, 100), listing("0xb2", "buy-xmz", 50000000, 500) ] },
            ETH: { configured: true, busy: false, coin: "ETH", chainId: 1, relays: [], mine: [], listings: [] },
            USDT: { configured: true, busy: false, coin: "USDT", chainId: 501000201, relays: [], mine: [], listings: [] },
            "USDT-BSC": { configured: true, busy: false, coin: "USDT-BSC", chainId: 100000056, relays: [], mine: [], listings: [] } })
        // The operator's per-trade cap in coins; the real engine reads it from the signed caps document
        property real capCoin: 1000
        signal dexRiskChanged()
        signal tradeHistoryExported(string path, int rows, string error)
        function exportTradeHistory(folder) { calls.push(["exportHistory", folder]); tradeHistoryExported(folder + "/monzero-dex-trades-2026-11-24.csv", 3, ""); return true }
        function evmOrderRefusal(chainId, coin, xmz) {
            return Number(coin) > capCoin ? "This order is above the per-trade limit of " + capCoin + " SOL while the DEX is opening. Post a smaller order." : ""
        }
        function refreshEvmOffers(pair) { return true }
        function takeEvmOffer(id, xmz) { calls.push(["take", id, xmz]); return true }
        function takeEvmOffers(list) { calls.push(["takes", JSON.stringify(list)]); return true }
        function setUpTradingKeys(pw) { calls.push(["keys", pw]); return true }
        function sendFromDexWallet(to, xmz) { calls.push(["home", to, xmz]); return true }
        function postEvmOffer(chainId, side, coin, xmz, hours, min, renew) { calls.push(["post", chainId, side, coin, xmz, hours, min].concat(renew ? ["renew"] : [])); return true }
        function cancelEvmOffer(folder) { calls.push(["cancel", folder]); return true }
        function approveTradingKeysHelper(commitment) { calls.push(["approveHelper", commitment]); return true }
        property var xmrWalletStatus: ({ coin: "xmr", state: "ready", balance: "1.5", unlocked: "1.25", address: "5" + "A".repeat(94), daemon: "31.70.193.31:38089", busy: false })
        function sendFromXmrWallet(address, amount) { calls.push(["sendXmr", address, amount]); return true }
        // Monero markets: none until a test gives them
        property var xmrMarketStatus: ({})
        function refreshXmrOffers(coin) { calls.push(["refreshXmr", coin]); return true }
        function postXmrOffer(chainId, side, coin, xmr, hours, min, renew) { calls.push(["postXmr", chainId, side, coin, xmr, hours, min].concat(renew ? ["renew"] : [])); return true }
        function xmrOrderRefusal(chainId, coin, xmr) { return Number(coin) > capCoin ? "This order is above the per-trade limit." : "" }
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
        function init() { ex.simple = false; engine.capCoin = 1000; engine.calls = []; ex.pick("SOL"); var f = find(ex, "exchangeForm"); f.clearPick(); f.reviewing = false }

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
        function test_limit_orders_can_stay_open_a_week_a_month_or_a_custom_time() {
            var f = find(ex, "exchangeForm")
            f.type = "limit"; f.side = "buy"
            find(ex, "limitPrice").text = "0.0001"
            find(ex, "orderAmount").text = "1000"
            press("life_168"); press("orderButton"); press("orderButton")
            press("life_744"); press("orderButton"); press("orderButton")
            press("life_0"); press("lifeUnit_d"); find(ex, "customLife").text = "10"
            press("orderButton"); press("orderButton")
            press("lifeUnit_h"); find(ex, "customLife").text = "36"
            press("orderButton"); press("orderButton")
            compare(engine.calls.map(function (c) { return c[5] }).join(), "168,744,240,36")
            find(ex, "customLife").text = "800"           // past 31 days
            verify(!f.ready)
            press("life_6"); verify(f.ready)
        }
        function test_usdt_is_its_own_market_with_six_decimals() {
            compare(ex.allCoins.join(), "ETH,BNB,SOL,USDT,USDT-BSC")
            compare(ex.label("USDT-BSC"), "USDT (BNB)")
            ex.pick("USDT")
            compare(ex.coinDecimals, 6)
            compare(ex.coinAccount, "11111111111111111111111111111112", "USDT sits in the Solana trade account")
            var f = find(ex, "exchangeForm")
            f.type = "limit"; f.side = "buy"
            find(ex, "limitPrice").text = "0.012"
            find(ex, "orderAmount").text = "1000"
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["post", 501000201, "buy-xmz", "12", "1000", 6, "100"]]))
            ex.pick("SOL")
        }
        function test_bnb_usdt_has_eighteen_decimals_and_the_evm_account() {
            ex.pick("USDT-BSC")
            compare(ex.coinDecimals, 18)
            compare(ex.coinAccount, "0x00000000000000000000000000000000000000a1")
            var f = find(ex, "exchangeForm")
            f.type = "limit"; f.side = "buy"
            find(ex, "limitPrice").text = "0.012"
            find(ex, "orderAmount").text = "1000"
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["post", 100000056, "buy-xmz", "12", "1000", 6, "100"]]))
            ex.pick("SOL")
        }
        function test_keep_open_posts_a_renewing_offer() {
            var f = find(ex, "exchangeForm")
            f.type = "limit"; f.side = "buy"
            find(ex, "limitPrice").text = "0.0001"
            find(ex, "orderAmount").text = "1000"
            press("renewToggle"); press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["post", 501000101, "buy-xmz", "0.1", "1000", 6, "100", "renew"]]))
            press("renewToggle")
        }
        function test_selling_more_than_the_trading_wallet_holds_is_blocked() {
            var f = find(ex, "exchangeForm")
            f.type = "limit"; f.side = "sell"
            find(ex, "limitPrice").text = "0.0002"
            find(ex, "orderAmount").text = "5000"
            verify(!f.ready)
        }
        function test_an_order_above_the_per_trade_cap_names_the_cap_before_posting() {
            var f = find(ex, "exchangeForm")
            f.type = "limit"; f.side = "sell"
            engine.capCoin = 0.3; engine.dexRiskChanged()
            find(ex, "limitPrice").text = "0.0002"
            find(ex, "orderAmount").text = "2000"        // 0.4 SOL
            verify(!f.ready)
            verify(f.problem.indexOf("0.3 SOL") >= 0)
            press("orderButton")
            compare(engine.calls.length, 0)
            engine.capCoin = 1; engine.dexRiskChanged()   // a new caps document arrives
            verify(f.ready)
            find(ex, "orderAmount").text = "1000"        // 0.2 SOL, within the cap
            engine.capCoin = 0.3; engine.dexRiskChanged()
            verify(f.ready)
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
        function test_keys_from_another_copy_ask_for_approval_first() {
            var w = JSON.parse(JSON.stringify(engine.dexWalletStatus))
            w.keysReady = false; w.keysExist = true
            w.keysHelperReview = { previousHelper: "/old/monzero-light/native-ltc-light", helper: "/new/monzero-light/native-ltc-light",
                                   previousDigest: "6b1c5ce3", helperDigest: "6b1c5ce3", commitment: "c0ffee" }
            engine.dexWalletStatus = w
            verify(find(ex, "keysHelperReview").visible)
            verify(!find(ex, "exchangePassword").visible)
            press("approveKeysHelper")
            compare(JSON.stringify(engine.calls), JSON.stringify([["approveHelper", "c0ffee"]]))
            delete w.keysHelperReview; w.keysReady = true; engine.dexWalletStatus = w
            verify(!find(ex, "unlockCard").visible)
        }
        function test_market_orders_do_not_wait_for_a_running_trade() {
            engine.evmTradeStatus = { configured: true, snapshot: { done: false, role: "owner", stage: "Locked", offer: { chainId: 501000101, value: "1", xmzAmount: "1" } } }
            var f = find(ex, "exchangeForm")
            f.side = "buy"; f.type = "market"
            find(ex, "orderAmount").text = "1500"
            verify(f.ready)
            engine.evmTradeStatus = { configured: false }
        }
        function test_swap_screen_buys_across_offers_in_one_click() {
            ex.simple = true
            var sw = find(ex, "swapScreen")
            verify(sw.visible)
            verify(!find(ex, "exchangeForm").visible)
            sw.side = "buy"
            // Enough SOL in the trade account: the swap starts at once, best offers first
            var b = JSON.parse(JSON.stringify(ex.balances)); b.SOL = 10; ex.balances = b
            find(ex, "swapAmount").text = "2500"
            // 1500 whole from 0xa1, then 1000 of the partial 0xa2
            compare(sw.plan.legs.map(function (l) { return l.row.id + ":" + l.atoms / 1e11 }).join(), "0xa1:1500,0xa2:1000")
            press("swapButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["takes", JSON.stringify([{ id: "0xa1", xmz: "" }, { id: "0xa2", xmz: "1000" }])]]))
            compare(sw.stage, "started")
            find(ex, "swapAgain").clicked(null)
            compare(sw.stage, "form")
        }
        function test_swap_waits_for_a_deposit_and_starts_when_it_arrives() {
            ex.simple = true
            var sw = find(ex, "swapScreen")
            sw.side = "buy"
            var b = JSON.parse(JSON.stringify(ex.balances)); b.SOL = 0; ex.balances = b
            find(ex, "swapAmount").text = "1500"
            press("swapButton")
            compare(sw.stage, "deposit")
            compare(engine.calls.length, 0)
            // Price 0.215039 SOL plus the fee reserve for one trade
            verify(find(ex, "swapDeposit").text.indexOf("0.219539 SOL") >= 0, find(ex, "swapDeposit").text)
            b = JSON.parse(JSON.stringify(ex.balances)); b.SOL = 0.3; ex.balances = b
            compare(sw.stage, "started")
            compare(engine.calls[0][0], "takes")
        }
        function test_swap_sell_asks_to_move_xmz_when_the_trading_wallet_is_short() {
            ex.simple = true
            var sw = find(ex, "swapScreen")
            sw.side = "sell"
            find(ex, "swapAmount").text = "1500"
            compare(sw.plan.legs.map(function (l) { return l.row.id }).join(), "0xb1")
            // The trading wallet holds 2400 XMZ: 1500 + 15 for fees fits
            press("swapButton")
            compare(sw.stage, "started")
            find(ex, "swapAgain").clicked(null)
            find(ex, "swapAmount").text = "1500"
            var w = JSON.parse(JSON.stringify(engine.dexWalletStatus)); w.unlocked = "100"; engine.dexWalletStatus = w
            engine.calls = []
            press("swapButton")
            compare(sw.stage, "moving")
            verify(find(ex, "swapMove").text.indexOf("1,415") >= 0, find(ex, "swapMove").text)
            w.unlocked = "2400"; engine.dexWalletStatus = w
            compare(sw.stage, "started")
            find(ex, "swapAgain").clicked(null)
        }
        function test_swap_says_when_not_enough_is_offered() {
            ex.simple = true
            var sw = find(ex, "swapScreen")
            sw.side = "buy"
            find(ex, "swapAmount").text = "100000"
            verify(sw.plan.short)
            verify(!find(ex, "swapButton").enabled)
            find(ex, "swapAmount").text = ""
        }
        function test_trading_sets_up_and_unlocks_with_the_wallet_password() {
            var w0 = engine.dexWalletStatus
            app.walletPassword = "wallet-pw"
            ex.triedWalletPassword = false
            var w = JSON.parse(JSON.stringify(w0)); w.keysReady = false; w.keysExist = false
            engine.dexWalletStatus = w
            verify(find(ex, "quickSetup").visible)
            press("quickSetupButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["keys", "wallet-pw"]]))
            // Keys that exist: tried once with the wallet password, by themselves
            engine.calls = []; ex.triedWalletPassword = false
            engine.dexWalletStatus = w0
            w = JSON.parse(JSON.stringify(w0)); w.keysReady = false; w.keysExist = true
            engine.dexWalletStatus = w
            compare(JSON.stringify(engine.calls), JSON.stringify([["keys", "wallet-pw"]]))
            verify(!find(ex, "quickSetup").visible)
            engine.dexWalletStatus = w0
            app.walletPassword = ""
        }
        function test_bought_xmz_moves_on_to_the_main_wallet_when_nothing_needs_it() {
            ex.homeAtoms = 0; ex.homeSeen = ({})
            engine.evmTrades = [ { tradeDir: "/t/a", tradeId: "0x1", role: "owner", stage: "Locked", done: false, xmzAmount: "800000000000000" } ]
            compare(ex.homeAtoms, 0)
            engine.evmTrades = [ { tradeDir: "/t/a", tradeId: "0x1", role: "owner", stage: "Done(Swapped)", done: true, approved: true, xmzAmount: "800000000000000" },
                                 { tradeDir: "/t/b", tradeId: "0x2", role: "claimer", stage: "Done(Swapped)", done: true, approved: true, xmzAmount: "50000000000000" } ]
            compare(ex.homeAtoms, 800000000000000)
            // The page test has no main wallet: nothing is sent, the amount waits
            ex.moveHome()
            compare(engine.calls.filter(function (c) { return c[0] === "home" }).length, 0)
            compare(ex.homeAtoms, 800000000000000)
            engine.evmTrades = []
            ex.homeAtoms = 0
        }
        function test_a_trade_can_be_cancelled_until_its_coins_are_locked() {
            engine.evmTrades = [
                { tradeDir: "/t/old", tradeId: "0x1", role: "owner", stage: "Start", done: false, approved: true, coin: "SOL", decimals: 9, value: "1", xmzAmount: "100000000000" },
                { tradeDir: "/t/locked", tradeId: "0x2", role: "owner", stage: "Locked", done: false, approved: true, coin: "SOL", decimals: 9, value: "1", xmzAmount: "100000000000" } ]
            ex.bottomTab = "orders"
            wait(50)
            var cancels = []
            ;(function walk(o) { if (o.objectName === "tradeRowCancel") cancels.push(o); for (var i = 0; i < o.children.length; ++i) walk(o.children[i]) })(ex)
            compare(cancels.length, 2)
            verify(cancels[0].visible)        // approved, but nothing locked yet
            verify(!cancels[1].visible)       // its coin is locked: it goes on to the swap or the refund
            engine.calls = []
            cancels[0].children[0].clicked(null)
            compare(JSON.stringify(engine.calls), JSON.stringify([["select", "/t/old"], ["abandon"]]))
            engine.evmTrades = []
        }
        function test_trades_are_rows_with_ticks_and_the_full_card_only_on_request() {
            engine.evmTrades = [
                { tradeDir: "/t/a", tradeId: "0x1", selected: true, role: "owner", stage: "Locked", done: false, awaitingApproval: false, awaitingFunding: false,
                  coin: "SOL", decimals: 9, value: "112000000", xmzAmount: "800000000000000" },
                { tradeDir: "/t/b", tradeId: "0x2", selected: false, role: "owner", stage: "Done(Swapped)", done: true, awaitingApproval: false, awaitingFunding: false,
                  coin: "SOL", decimals: 9, value: "70000000", xmzAmount: "500000000000000" } ]
            engine.evmTradeStatus = { configured: true, autoApprove: true, awaitingFunding: {}, startRefusal: "",
                                      snapshot: { done: false, role: "owner", stage: "Locked", offer: { chainId: 501000101, value: "112000000", xmzAmount: "800000000000000" } } }
            ex.bottomTab = "orders"
            wait(50)
            var rows = []
            ;(function walk(o) { if (o.objectName === "tradeRow") rows.push(o); for (var i = 0; i < o.children.length; ++i) walk(o.children[i]) })(ex)
            compare(rows.length, 2)
            // Running: agreed and locked ticked, the seller's lock under way, arrival to come
            var texts = []
            ;(function walk(o) { if (o.text !== undefined && /^[✓●○↩] /.test(o.text)) texts.push(o.text); for (var i = 0; i < o.children.length; ++i) walk(o.children[i]) })(rows[0])
            compare(texts.join("|"), "✓ Agreed|✓ You lock SOL|● Seller locks XMZ|○ XMZ arrives")
            texts = []
            ;(function walk(o) { if (o.text !== undefined && /^[✓●○↩] /.test(o.text)) texts.push(o.text); for (var i = 0; i < o.children.length; ++i) walk(o.children[i]) })(rows[1])
            compare(texts.join("|"), "✓ Agreed|✓ You lock SOL|✓ Seller locks XMZ|✓ XMZ arrives")
            // No banner for a trade that runs by itself; Details opens it
            verify(!find(ex, "exchangeTrade").visible)
            ex.tradeDetail = true
            verify(find(ex, "exchangeTrade").visible)
            ex.tradeDetail = false
            // A trade that needs the user (funds) shows in full by itself
            var t = JSON.parse(JSON.stringify(engine.evmTradeStatus)); t.awaitingFunding = { neededWei: "1", haveWei: "0", missingWei: "1" }
            engine.evmTradeStatus = t
            verify(find(ex, "exchangeTrade").visible)
            engine.evmTrades = []
            engine.evmTradeStatus = { configured: false }
        }
        function test_every_announced_pair_has_a_tile_and_the_ones_not_trading_say_coming_soon() {
            // No direct market in this wallet: all fifteen announced pairs show, each "coming soon"
            compare(ex.soonPairs.length, 15)
            var names = ["BNB/ETH", "ETH/USDT", "SOL/USDT-BSC", "ETH/XMR", "BNB/XMR", "SOL/XMR", "USDT/XMR", "USDT-BSC/XMR", "XMR/XMZ"]
            for (var i = 0; i < names.length; ++i) verify(find(ex, "pair_" + names[i]) !== null, names[i])
            compare(ex.directLabel("USDT/XMR"), "XMR / USDT (SOL)")
            compare(ex.directLabel("ETH/XMR"), "XMR / ETH")
            compare(ex.directLabel("ETH/USDT-BSC"), "ETH / USDT (BNB)")
            // Folded into one tile until opened; a picked one stays shown
            verify(!find(ex, "pair_BNB/ETH").visible)
            compare(find(ex, "soonToggleText").text, "+15 more pairs")
            press("soonToggle"); verify(find(ex, "pair_BNB/ETH").visible)
            press("soonToggle"); verify(!find(ex, "pair_BNB/ETH").visible)
            press("pair_ETH/XMR")
            verify(ex.soonMode); verify(find(ex, "pair_ETH/XMR").visible)
            verify(find(ex, "soonCard").visible)
            verify(!find(ex, "exchangeForm").visible)
            verify(find(ex, "soonText").text.indexOf("Monero") >= 0)
            press("pair_XMR/XMZ")
            verify(find(ex, "soonText").text.indexOf("two atomic swaps") >= 0)
            compare(engine.calls.length, 0)   // nothing is sent for a pair that is not trading
            ex.pick("SOL")
            verify(!find(ex, "soonCard").visible)
            verify(find(ex, "exchangeForm").visible)
        }
        function test_monero_pairs_trade_in_xmr_with_their_own_book_and_form() {
            // 0.5 ETH for 1.5 XMR offered (SAMPLE values); the market runs on the Monero wallet
            engine.xmrMarketStatus = { ETH: { configured: true, busy: false, coin: "ETH", chainId: 11155111, privateCoin: "XMR", relays: [], mine: [],
                listings: [ { id: "0xc1", pair: "ETH/XMR", side: "sell-xmz", chainId: 11155111, maker: "0x00000000000000000000000000000000000000b0",
                              valueWei: "500000000000000000", xmzAtoms: "1500000000000", expiresAt: root.now + 7200 },
                            { id: "0xc2", pair: "ETH/XMZ", side: "sell-xmz", chainId: 11155111, maker: "0x00000000000000000000000000000000000000b0",
                              valueWei: "1", xmzAtoms: "1", expiresAt: root.now + 7200 } ] } }
            verify(ex.xmrLive.indexOf("ETH/XMR") >= 0)
            verify(ex.soonPairs.indexOf("ETH/XMR") < 0)
            compare(ex.soonPairs.length, 13)   // ETH/XMR, and XMR/XMZ through ETH
            engine.calls = []
            press("pair_ETH/XMR")
            verify(ex.xmrPair); compare(ex.coin, "ETH"); compare(ex.base, "XMR")
            verify(find(ex, "pair_ETH/XMR").active); verify(!find(ex, "pair_ETH").active)
            compare(JSON.stringify(engine.calls[0]), JSON.stringify(["refreshXmr", "ETH"]))
            // Only the Monero offer, priced per XMR (12 places)
            compare(ex.book.asks.length, 1)
            compare(ex.book.asks[0].xmz, 1.5)
            compare(ex.book.asks[0].price.toFixed(6), "0.333333")
            var f = find(ex, "exchangeForm")
            compare(f.base, "XMR"); compare(f.places, 12)
            compare(f.b("Amount, XMZ"), "Amount, XMR")
            verify(find(ex, "exchangeForm").visible)
            // A limit order to buy 0.25 XMR goes to the Monero market, in XMR
            engine.calls = []
            f.type = "limit"; f.side = "buy"
            find(ex, "limitPrice").text = "0.3"
            find(ex, "orderAmount").text = "0.25"
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["postXmr", 11155111, "buy-xmz", "0.075", "0.25", 6, "0.025"]]))
            // Small Monero offers split too: the smallest part is 0.001 XMR (0.06 XMR: parts from 0.006)
            engine.calls = []
            find(ex, "orderAmount").text = "0.06"
            verify(f.canSplit); verify(f.partialOn); verify(f.ready, f.problem)
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["postXmr", 11155111, "buy-xmz", "0.018", "0.06", 6, "0.006"]]))
            find(ex, "minPart").text = "0.0009"; verify(!f.ready)      // under 0.001 XMR
            find(ex, "minPart").text = "0.001"; verify(f.ready, f.problem)
            find(ex, "minPart").text = ""
            // Under two smallest parts (0.002 XMR) an offer cannot split: it is posted whole
            engine.calls = []
            find(ex, "orderAmount").text = "0.0015"
            verify(find(ex, "partialToggle").on); verify(!f.canSplit); verify(!f.partialOn)
            verify(!find(ex, "minPart").visible)
            verify(find(ex, "partialLabel").text.indexOf("0.002 XMR") >= 0, find(ex, "partialLabel").text)
            verify(f.ready, f.problem)
            press("orderButton"); press("orderButton")
            compare(JSON.stringify(engine.calls), JSON.stringify([["postXmr", 11155111, "buy-xmz", "0.00045", "0.0015", 6, ""]]))
            find(ex, "orderAmount").text = "0.25"; verify(f.partialOn)
            // With no swaps, the chart still has Monero's market price (XMR's USD over ETH's), and the
            // form shows it beside a limit price, warning when that is far from it
            var oldUsd = ex.usd, oldHist = ex.usdHistory
            ex.usdHistory = { XMR: [[ex.now - 7200, 480], [ex.now - 3600, 490]], ETH: [[ex.now - 7200, 2000], [ex.now - 3600, 2000]] }
            ex.usd = { ETH: 2000, XMR: 500 }
            var chart = find(ex, "exchangeChart")
            chart.unit = "coin"
            verify(chart.noirLine.length > 0)
            compare(chart.noirLine[chart.noirLine.length - 1].p, 0.25)
            compare(find(ex, "refLegend").text, "Market")
            compare(f.marketPrice, 0.25)
            find(ex, "limitPrice").text = "0.3"
            compare(find(ex, "marketHint").text, "Market: 0.25 ETH per XMR · yours is 20% above")
            verify(find(ex, "marketHint").far)
            find(ex, "limitPrice").text = "0.26"; verify(!find(ex, "marketHint").far)
            chart.unit = "USD"; compare(chart.noirLine[chart.noirLine.length - 1].p, 500)
            ex.usd = oldUsd; ex.usdHistory = oldHist
            // Back to the XMZ pair
            ex.pick("SOL")
            verify(!ex.xmrPair); compare(ex.base, "XMZ"); compare(find(ex, "exchangeForm").base, "XMZ")
            engine.xmrMarketStatus = {}
        }
        function test_xmr_for_xmz_runs_as_two_swaps_through_a_coin() {
            // Someone sells up to 0.5 XMR for 0.1 SOL, any part from 0.1 XMR (SAMPLE values)
            engine.xmrMarketStatus = { SOL: { configured: true, busy: false, coin: "SOL", chainId: 501000101, privateCoin: "XMR", relays: [], mine: [],
                listings: [ { id: "0xd1", pair: "SOL/XMR", side: "sell-xmz", chainId: 501000101, maker: "0x00000000000000000000000000000000000000b0",
                              valueWei: "100000000", xmzAtoms: "500000000000", minXmzAtoms: "100000000000", expiresAt: root.now + 7200 } ] } }
            verify(ex.routeOpen); verify(ex.xmrLive.indexOf("XMR/XMZ") >= 0)
            engine.calls = []
            press("pair_XMR/XMZ")
            verify(ex.routeMode); verify(find(ex, "routePanel").visible); verify(!find(ex, "exchangeForm").visible)
            var rp = find(ex, "routePanel")
            compare(rp.viaNow, "SOL")
            find(ex, "routeAmount").text = "500"
            // 1: 500 XMZ into the best bid (0xb1, partial): 0.06 SOL; 2: 98% of it buys 0.294 XMR
            compare(find(ex, "routeStepOne").text, "1. Sell 500 XMZ for 0.06 SOL")
            verify(find(ex, "routeStepTwo").text.indexOf("2. Buy 0.294 XMR with 0.0588 SOL") === 0, find(ex, "routeStepTwo").text)
            press("routeStartOne")
            compare(JSON.stringify(engine.calls.filter(function (c) { return c[0] === "take" })), JSON.stringify([["take", "0xb1", "500"]]))
            compare(rp.step, 1)
            // An earlier finished trade is not step 1
            engine.evmTradeStatus = { configured: true, tradeDir: "/t/evm-trades/501000101/take-a2", snapshot: { done: true, stage: "Done(Swapped)" } }
            verify(!rp.stepOneDone)
            engine.evmTradeStatus = { configured: true, tradeDir: "/t/evm-trades/501000101/take-b1", snapshot: { done: false, stage: "Locked" } }
            verify(!rp.stepOneDone)
            engine.evmTradeStatus = { configured: true, tradeDir: "/t/evm-trades/501000101/take-b1", snapshot: { done: true, stage: "Done(Swapped)" } }
            verify(rp.stepOneDone)
            press("routeStartTwo")
            compare(JSON.stringify(engine.calls.filter(function (c) { return c[0] === "take" })), JSON.stringify([["take", "0xb1", "500"], ["take", "0xd1", "0.294"]]))
            compare(rp.step, 2)
            rp.reset()
            engine.evmTradeStatus = { configured: false }
            engine.xmrMarketStatus = {}
            verify(!ex.routeOpen)
            ex.pick("SOL")
        }
            function test_my_orders_show_finished_trades_and_each_pairs_coin() {
            function texts(item, out) { if (item.text !== undefined && item.visible) out.push(String(item.text)); for (var i = 0; i < item.children.length; ++i) texts(item.children[i], out); return out }
            var before = engine.evmMarketStatus
            var sol = JSON.parse(JSON.stringify(before.SOL))
            sol.mine = [ { kind: "offer", state: "finished", side: "buy-xmz", xmz: "2000", coin: "0.0242", price: "0.0000121", expiresAt: root.now - 60, folder: "/t/1" },
                         { kind: "take", state: "agreed", side: "sell-xmz", xmz: "500", coin: "0.006", price: "0.000012", expiresAt: root.now + 600, folder: "/t/2" } ]
            var next = JSON.parse(JSON.stringify(before)); next.SOL = sol
            engine.evmMarketStatus = next
            ex.bottomTab = "orders"
            var shown = texts(ex, []).join("|")
            verify(shown.indexOf("finished") >= 0, shown)
            verify(shown.indexOf("matched: see Trades above") >= 0, shown)
            verify(shown.indexOf("2000 XMZ") >= 0, shown)
            verify(shown.indexOf("|Amount|") >= 0, shown)
            compare(ex.openOrders, 0)
            engine.evmMarketStatus = before
        }
            function test_trade_history_exports_as_csv() {
            ex.bottomTab = "orders"
            verify(find(ex, "exportHistory").visible)
            engine.calls = []
            engine.exportTradeHistory("file:///home/me/Documents")
            compare(JSON.stringify(engine.calls), JSON.stringify([["exportHistory", "file:///home/me/Documents"]]))
            compare(find(ex, "exportHistoryResult").text, "3 trade(s) saved to file:///home/me/Documents/monzero-dex-trades-2026-11-24.csv")
        }
            function test_the_monero_wallet_shows_and_sends_from_the_funds_tab() {
            ex.bottomTab = "funds"
            // On an XMZ pair the XMZ wallet sits beside the coin's account; the Monero wallet is not shown
            ex.pick("SOL")
            verify(find(ex, "fundsXmzColumn").visible)
            verify(!find(ex, "fundsXmrColumn").visible)
            // On a Monero pair it is the Monero wallet beside the coin's account
            ex.pickXmr("SOL/XMR")
            compare(ex.base, "XMR")
            verify(!find(ex, "fundsXmzColumn").visible)
            verify(find(ex, "fundsXmrColumn").visible)
            compare(find(ex, "fundsXmr").text, "1.25 XMR available · 1.5 XMR in total")
            compare(find(ex, "xmrAddress").text, "5" + "A".repeat(94))
            find(ex, "xmrSendTo").text = "5" + "B".repeat(94)
            find(ex, "xmrSendAmount").text = "0.5"
            press("xmrSend")
            compare(JSON.stringify(engine.calls), JSON.stringify([["sendXmr", "5" + "B".repeat(94), "0.5"]]))
            engine.xmrWalletStatus = { state: "unconfigured" }
            verify(!find(ex, "fundsXmrColumn").visible)
            ex.pick("SOL")
        }
}
}
