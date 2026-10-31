import QtQuick 2.9
import QtTest 1.2
import "../../components" as M
Item {
    width: 1000; height: 1400
    property var appWindow: app
    property var translationManager: translations
    QtObject { id: app; property bool themeTransition: false }
    QtObject { id: translations; property string emptyString: "" }
    function releaseFocus() {}

    // Offers: price is XMZ per coin (11 decimals); amount is coin atoms.
    function offer(pair, side, xmzPerCoin, coinAmount, decimals) {
        return { pair: pair, side: side, priceAtomic: String(Math.round(xmzPerCoin * 1e11)),
                 amountAtomic: String(Math.round(coinAmount * Math.pow(10, decimals))) }
    }
    property var book: [
        offer("BTC/XMZ", "SELL", 200000, 0.01, 8),   // sells 0.01 BTC at 200k XMZ/BTC
        offer("BTC/XMZ", "SELL", 190000, 0.001, 8),  // cheaper, but small
        offer("BTC/XMZ", "BUY", 180000, 0.02, 8),    // buys up to 0.02 BTC with XMZ
        offer("BTC / XMZ", "BUY", 185000, 0.005, 8),
        offer("LTC/XMZ", "SELL", 900, 1, 8)
    ]

    M.DexTradeCard { id: card; width: 900; offers: book; xmzBalance: 1000 }
    M.DexTradeProgress { id: progress; width: 900; y: 900; coin: "BNB" }

    TestCase {
        name: "DexTrade"; when: windowShown
        function child(parent, name) { return findChild(parent, name) }
        function init() {
            card.direction = "sell"; card.coin = "BTC"; card.amountText = ""; card.reviewing = false
            card.canStart = false; card.startBlockedReason = ""
            progress.trade = ({}); progress.confirmed = false
        }

        function test_sellPicksTheBestOfferThatFills() {
            // 100 XMZ needs 0.0005263 BTC at 190k (fits the small offer) and is the most BTC
            var q = card.quoteFor("sell", "BTC", 100, book, 8, 1)
            verify(q.ok); compare(q.price, 190000)
            fuzzyCompare(q.gross, 100 / 190000, 1e-12)
            fuzzyCompare(q.fee, q.gross * 0.01, 1e-12)
            fuzzyCompare(q.receive, q.gross * 0.99, 1e-12)
            compare(q.send, 100)
            // 1000 XMZ is too big for the 190k offer: the 200k one fills it
            compare(card.quoteFor("sell", "BTC", 1000, book, 8, 1).price, 200000)
        }
        function test_buyPicksTheCheapestOfferThatFills() {
            var q = card.quoteFor("buy", "BTC", 500, book, 8, 1)
            verify(q.ok); compare(q.price, 185000)     // most XMZ per BTC; pair spacing ignored
            fuzzyCompare(q.send, 500 / 185000 * 1.01, 1e-12)
            compare(q.receive, 500)
            // Too big for the 185k offer (0.005 BTC = 925 XMZ): the 180k one
            compare(card.quoteFor("buy", "BTC", 1000, book, 8, 1).price, 180000)
        }
        function test_problemsAreExplained() {
            verify(card.quoteFor("sell", "BTC", 0, book, 8, 1).empty)
            verify(card.quoteFor("sell", "BCH", 1, book, 8, 1).noOffers)
            var big = card.quoteFor("sell", "BTC", 5000, book, 8, 1)
            verify(!big.ok); verify(big.reason.indexOf("2000") >= 0)   // largest: 0.01 BTC x 200k
            verify(!card.quoteFor("sell", "BTC", 1, book, undefined, 1).ok)
        }
        function test_cardShowsQuoteAndBlocksUnavailableStart() {
            card.amountText = "100"
            verify(child(card, "quoteBox").visible)
            compare(child(card, "quote_1").text, card.show(card.quote.receive, "BTC"))
            var review = child(card, "reviewButton")
            verify(review.enabled); review.clicked()
            verify(child(card, "reviewPanel").visible)
            verify(!child(card, "startButton").enabled)
            card.startBlockedReason = "not yet"
            verify(child(card, "startBlocked").visible)
            card.amountText = "101"; verify(!card.reviewing)          // editing goes back to the form
        }
        function test_cannotSellMoreThanTheBalance() {
            card.amountText = "1500"
            verify(!child(card, "reviewButton").enabled)
        }
        function test_startSendsTheQuote() {
            var sent = null
            card.startRequested.connect(function(q) { sent = q })
            card.canStart = true; card.amountText = "100"
            child(card, "reviewButton").clicked(); child(card, "startButton").clicked()
            verify(sent !== null); compare(sent.xmz, 100); compare(sent.coin, "BTC")
        }

        function trade(role, stage, canApprove) {
            return { configured: true, state: "ok", canApprove: canApprove === true, commitment: "0xabc",
                     snapshot: { role: role, stage: stage, offer: { value: "20000000000000000", xmzAmount: "2000000000000" },
                                 deadlines: { timeout1: "1900000000", timeout2: "1900003600" } } }
        }
        function test_buyerStepsAndApproval() {
            progress.trade = trade("owner", "Start", true)
            compare(child(progress, "tradeTitle").text, "Buying 20 XMZ")
            compare(child(progress, "tradeSummary").text, "You pay 0.02 BNB and receive 20 XMZ.")
            verify(child(progress, "step_1").current)
            verify(child(progress, "approvalBox").visible)
            var approve = child(progress, "approveButton")
            verify(!approve.enabled)                                  // amounts not confirmed yet
            var approved = ""
            progress.approve.connect(function(c) { approved = c })
            child(progress, "approvalConfirm").clicked(); verify(approve.enabled)
            approve.clicked(); compare(approved, "0xabc")
            verify(child(progress, "tradeRefundTime").text.indexOf("refunded") >= 0)
        }
        function test_sellerAndFinishedStates() {
            progress.trade = trade("claimer", "XmzLocked")
            verify(child(progress, "step_0").done); verify(child(progress, "step_2").current)
            verify(!child(progress, "approvalBox").visible)
            progress.trade = trade("claimer", "Done(Swapped)")
            verify(child(progress, "step_3").done)
            compare(child(progress, "tradeTitle").text, "Sold 20 XMZ")
            compare(child(progress, "tradeNow").text, "Done. The BNB is in your account.")
            progress.trade = trade("owner", "Done(Refunded)")
            verify(progress.progress.refund); verify(!child(progress, "step_3").done)
            verify(!child(progress, "tradeRefundTime").visible)
        }
        function test_unknownStageIsShownNotHidden() {
            progress.trade = trade("owner", "SomethingNew")
            compare(child(progress, "tradeNow").text, "Working on it (SomethingNew).")
        }
        function test_approvalTickResetsWhenApprovalNoLongerNeeded() {
            progress.trade = trade("owner", "Start", true); progress.confirmed = true
            progress.trade = trade("owner", "LockSent", false)
            verify(!progress.confirmed)
        }
    }
}
