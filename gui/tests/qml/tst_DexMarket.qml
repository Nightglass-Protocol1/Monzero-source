import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
Item {
    id: root
    width: 1400; height: 1100
    property var appWindow: app
    property var translationManager: translations
    QtObject { id: app; property bool themeTransition: false }
    QtObject { id: translations; property string emptyString: "" }
    function releaseFocus() {}

    function offer(side, xmzPerCoin, coinAmount) {
        return { pair: "BTC/XMZ", side: side, priceAtomic: String(Math.round(xmzPerCoin * 1e11)),
                 amountAtomic: String(Math.round(coinAmount * 1e8)) }
    }
    // SAMPLE order book for screenshots and tests: not market data. Levels around $0.001 per XMZ
    // with BTC at about $77,000 (77,000,000 XMZ per BTC), as chosen on 2026-10-31.
    property var sampleOffers: [
        offer("SELL", 77400000, 0.0004), offer("SELL", 77800000, 0.0010), offer("SELL", 78300000, 0.0006),
        offer("SELL", 79000000, 0.0015), offer("SELL", 80000000, 0.0020), offer("SELL", 81500000, 0.0012),
        offer("BUY", 76600000, 0.0005), offer("BUY", 76100000, 0.0012), offer("BUY", 75500000, 0.0008),
        offer("BUY", 74700000, 0.0018), offer("BUY", 73500000, 0.0025), offer("BUY", 72000000, 0.0010),
        { pair: "LTC/XMZ", side: "SELL", priceAtomic: "90000000000000", amountAtomic: "100000000" }
    ]
    readonly property real sampleNow: 1900000000 + 5 * 86400
    // SAMPLE completed trades: a deterministic wander every 20 minutes over 5 days per pair, with
    // levels near $0.001 per XMZ (BTC ~$77k, LTC ~$90, BCH ~$480, BNB ~$700).
    // The wander ends at `level` (the pair's order-book mid) and `phase` makes each pair move differently.
    function sampleTradesFor(pair, level, coinDecimals, size, phase) {
        var prices = [], p = 1
        for (var i = 0; i < 360; ++i) {
            var k = i + phase
            p *= 1 + Math.sin(k * 0.37) * 0.004 + Math.cos(k * 0.113) * 0.003 + Math.sin(k * 1.9) * 0.002 + Math.sin(k * 0.021) * 0.0015
            prices.push(p)
        }
        var out = [], t0 = 1900000000, scale = level / prices[prices.length - 1]
        for (var j = 0; j < prices.length; ++j) {
            var amount = size * (0.4 + (Math.sin((j + phase) * 2.3) + 1) * 0.8) * (1 + Math.abs(Math.sin(j * 0.05)))
            out.push({ pair: pair, time: t0 + j * 1200, priceAtomic: String(Math.round(prices[j] * scale * 1e11)),
                       amountAtomic: String(Math.round(amount * Math.pow(10, coinDecimals))) })
        }
        return out
    }
    property var sampleTrades: sampleTradesFor("BTC/XMZ", 77000000, 8, 0.0006, 0)
        .concat(sampleTradesFor("LTC/XMZ", 90000, 8, 0.4, 40))
        .concat(sampleTradesFor("BCH/XMZ", 480000, 8, 0.08, 95))
        .concat(sampleTradesFor("BNB/XMZ", 700000, 18, 0.05, 160))

    // The trade area as the page lays it out on a wide window, on the app's background
    Rectangle {
        id: shot
        width: 1300; height: area.implicitHeight + 40
        color: "#0B1D21"
        Column {
            id: area
            x: 20; y: 20; width: 1260; spacing: 12
            M.DexTradeProgress { id: progress; width: parent.width; visible: false; coin: "BNB" }
            M.DexPairTabs { id: tabs; width: parent.width; coins: card.coins; coin: card.coin; market: market; coinDecimals: card.coinDecimals; onPicked: card.coin = coin }
            Row {
                spacing: 12
                M.DexMarketPanel { id: market; width: 868; height: implicitHeight; coin: card.coin; coinDecimals: card.coinDecimals[card.coin]; offers: root.sampleOffers; now: root.sampleNow }
                Column {
                    spacing: 12
                    M.DexTradeCard { id: card; width: 380; height: implicitHeight; offers: root.sampleOffers; xmzBalance: 250000; showCoinPicker: false }
                    M.DexExternalWallet { id: mm; width: 380; height: implicitHeight }
                    M.DexExternalWallet { id: mmBtc; width: 380; height: implicitHeight; coin: "BTC"; visible: mm.address !== "" }
                    M.DexExternalWallet { id: mmLtc; width: 380; height: implicitHeight; coin: "LTC"; visible: mm.address !== "" }
                    M.DexExternalWallet { id: mmBch; width: 380; height: implicitHeight; coin: "BCH"; visible: mm.address !== "" }
                }
            }
        }
    }

    TestCase {
        name: "DexMarket"; when: windowShown
        function child(parent, name) { return findChild(parent, name) }
        function save(item, name) {
            var done = false
            item.grabToImage(function (r) { verify(r.saveToFile("build/dex-trade-ui/" + name)); done = true })
            tryVerify(function () { return done }, 5000)
        }
        function init() {
            market.chartMode = "candles"; market.candleSeconds = 3600; market.trades = []; card.direction = "buy"; card.coin = "BTC"
            card.amountText = ""; card.reviewing = false; progress.visible = false
        }

        function test_bookSortsAndTotals() {
            var b = market.book
            compare(b.asks.length, 6); compare(b.bids.length, 6)              // LTC offer left out
            compare(b.asks[0].price, 77400000); compare(b.bids[0].price, 76600000)
            fuzzyCompare(b.asks[5].total, 0.0067, 1e-9)
            fuzzyCompare(b.bids[0].xmz, 76600000 * 0.0005, 1e-6)
            compare(child(market, "bestAsk").text, "77,400,000")
            verify(child(market, "midPrice").text.indexOf("%") > 0)
        }
        function test_candlesFromTrades() {
            var t0 = 1900000000 - 1900000000 % 3600
            var trades = [
                { pair: "BTC/XMZ", time: t0 + 10, priceAtomic: "10000000000000", amountAtomic: "100000" },
                { pair: "BTC/XMZ", time: t0 + 20, priceAtomic: "12000000000000", amountAtomic: "200000" },
                { pair: "BTC/XMZ", time: t0 + 30, priceAtomic: "9000000000000", amountAtomic: "100000" },
                { pair: "BTC/XMZ", time: t0 + 40, priceAtomic: "11000000000000", amountAtomic: "100000" },
                { pair: "BTC/XMZ", time: t0 + 7300, priceAtomic: "11500000000000" },           // gap hour left out
                { pair: "LTC/XMZ", time: t0 + 50, priceAtomic: "1" }
            ]
            var c = market.buildCandles(market.tradesFor(trades, "BTC"), 3600, 8)
            compare(c.length, 2)
            compare([c[0].o, c[0].h, c[0].l, c[0].c], [100, 120, 90, 110])
            fuzzyCompare(c[0].v, 0.005, 1e-12)
            compare(c[1].t, t0 + 7200); compare(c[1].v, 0)
            var s = market.dayStats(market.tradesFor(trades, "BTC"), t0 + 7300, 8)
            compare(s.last, 115); compare(s.high, 120); compare(s.low, 90)
            fuzzyCompare(s.change, 15, 1e-9)
        }
        function test_emptyStatesAreHonest() {
            verify(child(market, "chartEmpty").visible)                     // no trades: no candles
            compare(child(market, "lastPrice").text, "—"); compare(child(market, "change24h").text, "—")
            card.coin = "BCH"; market.chartMode = "depth"
            verify(child(market, "chartEmpty").visible)
            compare(child(market, "bestAsk").text, "—")
        }
        function test_pairTabsChooseTheMarket() {
            mouseClick(child(tabs, "pairTab_LTC"))
            compare(card.coin, "LTC"); compare(market.coin, "LTC")
            compare(market.book.asks.length, 1)
        }
        function test_buySellTabs() {
            mouseClick(child(card, "direction_sell")); compare(card.direction, "sell")
            mouseClick(child(card, "direction_buy")); compare(card.direction, "buy")
        }
        function test_pickingAnOfferFillsTheCard() {
            var picked = null
            market.offerPicked.connect(function (o) { picked = o })
            var asks = child(market, "asks")
            wait(50)
            mouseClick(asks.children[asks.children.length - 2])            // nearest the middle: best ask
            verify(picked !== null); compare(picked.side, "SELL")
        }

        // Screenshots of the trade area with SAMPLE offers and trades (file names say so)
        function test_zz_screenshots() {
            market.trades = root.sampleTrades; card.direction = "buy"; card.amountText = "40000"; wait(250)
            save(shot, "sample-1-exchange-candles-1h-buy.png")
            market.candleSeconds = 14400; card.direction = "sell"; card.amountText = "50000"; wait(250)
            save(shot, "sample-2-exchange-candles-4h-sell.png")
            market.chartMode = "depth"; card.direction = "buy"; card.amountText = "40000"; wait(100)
            mouseClick(child(card, "reviewButton")); card.startBlockedReason = "Testnet preview: taking an offer from the app comes next."; wait(250)
            save(shot, "sample-3-exchange-depth-buy-review.png")
            card.reviewing = false; card.amountText = ""; market.chartMode = "candles"; market.candleSeconds = 3600
            progress.trade = { configured: true, state: "ok", canApprove: true, commitment: "0xabc",
                snapshot: { role: "owner", stage: "Start", offer: { value: "10000000000000000", xmzAmount: "700000000000000" },
                            deadlines: { timeout1: "1900000000", timeout2: "1900003600" } } }
            progress.visible = true; wait(250)
            save(shot, "sample-4-exchange-active-trade-approval.png")
            // MetaMask connected; the trade's own account waits for its BNB (sample addresses)
            mm.address = "0x4b7c0a922d663ec68Bc6d5D5f9fa77E420C28FAF"
            mmBtc.address = "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"     // SAMPLE addresses
            progress.externalWallet = mm.address
            progress.trade = { configured: true, state: "ok", canApprove: false, commitment: "0xabc", account: "0x9C06a3d4f56fFF037C11f24b77B3C0F0eBCd8e0C",
                awaitingFunding: { neededWei: "10050000000000000", haveWei: "0", missingWei: "10050000000000000" },
                snapshot: { role: "owner", stage: "Start", offer: { chainId: 56, value: "10000000000000000", xmzAmount: "700000000000000" },
                            deadlines: { timeout1: "1900000000", timeout2: "1900003600" } } }
            wait(400)
            save(shot, "sample-5-exchange-metamask-funding.png")
            mm.address = ""; mmBtc.address = ""; progress.externalWallet = ""
        }
    }
}
