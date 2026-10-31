import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
Item {
    id: root
    width: 980; height: 2000
    property var appWindow: app
    property var translationManager: translations
    QtObject { id: app; property bool themeTransition: false }
    QtObject { id: translations; property string emptyString: "" }
    function releaseFocus() {}

    function offer(side, xmzPerCoin, coinAmount) {
        return { pair: "BTC/XMZ", side: side, priceAtomic: String(Math.round(xmzPerCoin * 1e11)),
                 amountAtomic: String(Math.round(coinAmount * 1e8)) }
    }
    // SAMPLE order book for screenshots and tests: not market data.
    property var sampleOffers: [
        offer("SELL", 96400, 0.004), offer("SELL", 96850, 0.010), offer("SELL", 97300, 0.006),
        offer("SELL", 98100, 0.015), offer("SELL", 99000, 0.020), offer("SELL", 100500, 0.012),
        offer("BUY", 95700, 0.005), offer("BUY", 95200, 0.012), offer("BUY", 94600, 0.008),
        offer("BUY", 93800, 0.018), offer("BUY", 92500, 0.025), offer("BUY", 91000, 0.010),
        { pair: "LTC/XMZ", side: "SELL", priceAtomic: "90000000000000", amountAtomic: "100000000" }
    ]
    // SAMPLE completed trades (a deterministic wander), one every 2 hours
    property var sampleTrades: {
        var out = [], p = 93000, t = 1900000000
        for (var i = 0; i < 48; ++i) {
            p += Math.sin(i * 0.7) * 600 + Math.cos(i * 0.31) * 450 + 70
            out.push({ time: t + i * 7200, priceAtomic: String(Math.round(p * 1e11)) })
        }
        return out
    }

    // The trade area as the page lays it out, on the app's background
    Rectangle {
        id: shot
        width: 700; height: area.implicitHeight + 40
        color: "#0B1D21"
        Column {
            id: area
            x: 20; y: 20; width: 660; spacing: 12
            M.DexTradeProgress { id: progress; width: parent.width; visible: false; coin: "BNB" }
            M.DexMarketPanel { id: market; width: parent.width; coin: card.coin; offers: root.sampleOffers }
            M.DexTradeCard { id: card; width: parent.width; offers: root.sampleOffers; xmzBalance: 2500 }
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
            market.chartMode = "depth"; market.trades = []; card.direction = "sell"; card.coin = "BTC"
            card.amountText = ""; card.reviewing = false; progress.visible = false
        }

        function test_bookSortsAndTotals() {
            var b = market.book
            compare(b.asks.length, 6); compare(b.bids.length, 6)              // LTC offer left out
            compare(b.asks[0].price, 96400); compare(b.bids[0].price, 95700)
            fuzzyCompare(b.asks[5].total, 0.067, 1e-9)
            fuzzyCompare(b.bids[0].xmz, 95700 * 0.005, 1e-6)
            compare(child(market, "bestAsk").text, "96400.00".replace(/0+$/, "").replace(/\.$/, ""))
            verify(child(market, "spread").text.indexOf("%") > 0)
            verify(!child(market, "chartEmpty").visible)
        }
        function test_emptyStatesAreHonest() {
            market.chartMode = "price"
            verify(child(market, "chartEmpty").visible)                     // no trades: no line
            card.coin = "BCH"; market.chartMode = "depth"
            verify(child(market, "chartEmpty").visible)
            compare(child(market, "bestAsk").text, "—")
        }
        function test_pickingAnOfferFillsTheCard() {
            var picked = null
            market.offerPicked.connect(function (o) { picked = o })
            var asks = child(market, "asks")
            wait(50)
            mouseClick(asks.children[asks.children.length - 2])            // nearest the middle: best ask
            verify(picked !== null); compare(picked.side, "SELL")
        }

        // Screenshots of the trade area with the SAMPLE book (file names say so)
        function test_zz_screenshots() {
            card.amountText = "500"; wait(200)
            save(shot, "sample-1-depth-chart-order-book-and-sell-quote.png")
            market.chartMode = "price"; market.trades = root.sampleTrades; wait(200)
            save(shot, "sample-2-price-chart.png")
            market.chartMode = "depth"; card.direction = "buy"; card.amountText = "450"; wait(100)
            child(card, "reviewButton").clicked(); card.startBlockedReason = "Testnet preview: taking an offer from the app comes next."; wait(200)
            save(shot, "sample-3-buy-review.png")
            card.reviewing = false; card.amountText = ""
            progress.trade = { configured: true, state: "ok", canApprove: true, commitment: "0xabc",
                snapshot: { role: "owner", stage: "Start", offer: { value: "20000000000000000", xmzAmount: "2000000000000" },
                            deadlines: { timeout1: "1900000000", timeout2: "1900003600" } } }
            progress.visible = true; wait(200)
            save(shot, "sample-4-active-trade-approval.png")
            progress.trade = { configured: true, state: "ok", snapshot: { role: "claimer", stage: "XmzLocked",
                offer: { value: "20000000000000000", xmzAmount: "2000000000000" }, deadlines: { timeout1: "1900000000", timeout2: "1900003600" } } }
            wait(200)
            save(shot, "sample-5-active-trade-in-progress.png")
        }
    }
}
