import QtQuick 2.9
import QtQuick.Layouts 1.1
import QtQuick.Dialogs 1.2

import "." as M
import "dexExchange.js" as X

// The in-app exchange (Linux): XMZ against ETH, BNB and SOL without the DEX website. Pair tabs,
// a stats bar, the price chart, the order book and a buy/sell form, with your orders, the running
// trade, recent market trades and your trading funds below.
//
// Offers come from the open relays through the pinned swap helper (maker signature and contract
// checked) via the engine; orders go to the same engine calls the website uses (take, post,
// cancel), and the swap itself still asks for approval before anything is locked. The chart,
// recent trades, coin balances and USD prices are read-only public data fetched here.
ColumnLayout {
    id: ex
    property var dex: ({})                 // DexEngine
    // Public data (chart, trades, balances, USD prices) is fetched only while this is set (tests clear it)
    property bool fetchEnabled: true
    // On screen: offers, chart and balances refresh only while the page is open
    property bool active: true
    spacing: 12

    // ---- state from the engine
    readonly property var wallet: (dex && dex.dexWalletStatus) || ({})
    // The Monero trading wallet, where this app runs one
    readonly property var xmrWallet: (dex && dex.xmrWalletStatus) || ({})
    readonly property bool xmrShown: xmrWallet.state !== undefined && xmrWallet.state !== "unconfigured"
    readonly property var markets: (dex && dex.evmMarketStatus) || ({})
    readonly property var trade: (dex && dex.evmTradeStatus) || ({})
    // Monero (XMR) against the same coins: their own markets, on the app's Monero wallet
    readonly property var xmrMarkets: (dex && dex.xmrMarketStatus) || ({})
    property string base: "XMZ"            // the coin bought and sold on the shown pair: XMZ, or XMR
    readonly property bool xmrPair: base === "XMR"
    readonly property string dataKey: xmrPair ? coin + "/XMR" : coin   // its chart and swaps
    readonly property var market: (xmrPair ? xmrMarkets : markets)[coin] || ({})
    // Coin-for-coin trades (ETH, BNB, SOL straight across): their own tab, when this wallet has them
    readonly property var direct: (dex && dex.directMarketStatus) || ({})
    readonly property bool directMode: coin === "DIRECT" && direct.configured === true
    property string directPair: ""          // the direct pair picked in the pair row ("BNB/ETH")
    function pickDirect(p) { directPair = p; pick("DIRECT") }
    // A direct pair as people read it: Solana's USDT and BNB Chain's named by their chains
    function directCoin(t) { return label(t) }   // one name for each coin on every tile: "USDT (SOL)", "USDT (BNB)"
    function directLabel(p) {
        var x = String(p).split("/")
        if (x[1] === "XMR") x = [x[1], x[0]]   // Monero pairs read XMR first
        return directCoin(x[0]) + " / " + directCoin(x[1])
    }
    // Every pair announced for the exchange. Those not trading in this wallet yet (the XMR pairs, and
    // USDT where no USDT is pinned on this network) still get a tile, marked "coming soon".
    readonly property var announcedPairs: ["BNB/ETH", "BNB/SOL", "ETH/SOL",
        "ETH/USDT", "ETH/USDT-BSC", "BNB/USDT", "BNB/USDT-BSC", "SOL/USDT", "SOL/USDT-BSC",
        "ETH/XMR", "BNB/XMR", "SOL/XMR", "USDT/XMR", "USDT-BSC/XMR", "XMR/XMZ"]
    readonly property var livePairs: direct.configured === true ? (direct.pairs || []) : []
    // "USDT/XMR" -> "USDT": the coin of a Monero pair ("" for XMR/XMZ, which is two swaps)
    function xmrCoinOf(p) { var x = String(p).split("/"); return x[1] === "XMR" ? x[0] : "" }
    // XMR/XMZ runs as two swaps through a coin with both an XMZ and an XMR market here
    readonly property bool routeOpen: allCoins.some(function (c) { return (markets[c] || {}).configured === true && (xmrMarkets[c] || {}).configured === true })
    readonly property var xmrLive: announcedPairs.filter(function (p) {
        if (p === "XMR/XMZ") return routeOpen
        var c = xmrCoinOf(p); return c !== "" && (xmrMarkets[c] || {}).configured === true
    })
    readonly property var soonPairs: announcedPairs.filter(function (p) { return livePairs.indexOf(p) < 0 && xmrLive.indexOf(p) < 0 })
    property string soonPair: ""            // the coming-soon pair picked in the pair row
    property bool soonOpen: false           // the coming-soon tiles are folded into one tile until opened
    readonly property bool soonMode: coin === "SOON"
    readonly property bool routeMode: coin === "ROUTE"
    // The XMZ pair screens (stats, chart, book, form, orders) give way to a direct or coming-soon pair
    readonly property bool pairMode: directMode || soonMode || routeMode
    function pickSoon(p) { soonPair = p; pick("SOON") }
    function pickXmr(p) { pick(xmrCoinOf(p), "XMR") }
    readonly property bool keysReady: wallet.keysReady === true
    // USDT (on Solana) appears once the wallet runs its market
    readonly property var allCoins: ["ETH", "BNB", "SOL"].concat(markets.USDT ? ["USDT"] : []).concat(markets["USDT-BSC"] ? ["USDT-BSC"] : [])
    // How a market is named on screen (two USDT markets: on Solana and on BNB Smart Chain)
    function label(c) { return c === "USDT" ? "USDT (SOL)" : c === "USDT-BSC" ? "USDT (BNB)" : c }
    readonly property string bscUsdt: "0x55d398326f99059fF775485246999027B3197955"
    property string coin: "ETH"
    // SOL and USDT live on Solana (same trade account, Solana addresses)
    function onSolana(c) { return c === "SOL" || c === "USDT" }
    readonly property string usdtMint: "Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB"
    readonly property int coinDecimals: decimalsOf(coin)
    readonly property int chainId: Number(market.chainId) || 0
    readonly property var relays: (market.relays && market.relays.length) ? market.relays : ["https://relay1.monzero.org", "https://relay2.monzero.org"]
    readonly property string myAccount: wallet.tradeAccount || ""
    readonly property string coinAccount: onSolana(coin) ? (wallet.solTradeAccount || "") : myAccount
    readonly property var tradeSnapshot: trade.snapshot || ({})
    readonly property bool tradeRunning: trade.configured === true && tradeSnapshot.done !== true
    readonly property string tradeCoin: {
        if (trade.coin) return trade.coin   // the wallet names it (USDT on Solana too)
        var o = tradeSnapshot.offer
        var id = o ? Number(o.chainId) : 0
        return id >= 501000101 && id <= 501000103 ? "SOL" : id === 56 || id === 97 ? "BNB" : "ETH"
    }

    // ---- public data fetched here
    property real now: Date.now() / 1000
    property int interval: 3600
    property var candles: ({})             // coin -> hourly candles (for the tabs and the stats)
    property var prints: ({})              // coin -> trades, newest first
    property var usd: ({})                 // coin -> USD price
    property string withdrawError: ""      // the Withdraw button's own refusal (before the helper runs)
    property var noir: null                // NoirTrade's XMZ/USDT market (from monzero.org), for the chart
    property var usdHistory: ({})          // coin -> [[seconds, usd], …] hourly, to value each swap at its time
    // Monero's market price as a reference line, in NoirTrade's form ({last, stale, trades: [[ms, USD]]}):
    // XMR's hourly USD price and the price now; the chart turns it into the pair's coin
    readonly property var xmrMarketRef: {
        var h = usdHistory.XMR || [], t = []
        for (var i = 0; i < h.length; ++i) t.push([Number(h[i][0]) * 1000, Number(h[i][1])])
        if (usd.XMR > 0) t.push([now * 1000, usd.XMR])
        return t.length ? { last: usd.XMR || 0, stale: false, trades: t } : null
    }
    property var balances: ({})            // coin -> trade account balance (coin)
    property string localError: ""
    property string pickedId: ""
    property string bottomTab: "orders"
    property var withdrawTo: {
        var m = {}
        try { m = JSON.parse(typeof persistentSettings !== "undefined" ? (persistentSettings.dexExternalWallets || "{}") : "{}") || {} } catch (e) { m = {} }
        return m
    }

    readonly property var book: X.book(market.listings || [], coin, coinDecimals, myAccount, now, base)
    readonly property var stats: X.dayStats(candles[dataKey] || [], now)
    readonly property real coinUsd: usd[coin] || (coin === "USDT" || coin === "USDT-BSC" ? 1 : 0)
    readonly property real xmzBalance: wallet.state === "ready" && wallet.unlocked !== undefined ? Number(wallet.unlocked) : -1
    readonly property real xmrBalance: xmrWallet.state === "ready" && xmrWallet.unlocked !== undefined ? Number(xmrWallet.unlocked) : -1
    readonly property real coinBalance: balances[coin] !== undefined ? balances[coin] : -1
    readonly property var myOrders: {
        var out = []
        var sets = [[markets, "XMZ"], [xmrMarkets, "XMR"]]
        for (var s = 0; s < sets.length; ++s) for (var i = 0; i < allCoins.length; ++i) {
            var mine = (sets[s][0][allCoins[i]] || {}).mine || []
            for (var j = 0; j < mine.length; ++j) {
                var o = mine[j]
                if (o.state === "renewed") continue
                if ((o.state === "cancelled" || o.state === "withdrawn") && Number(o.expiresAt) < now - 86400) continue
                out.push({ coin: allCoins[i], base: sets[s][1], kind: o.kind, state: o.state, side: o.side, xmz: o.xmz, amount: o.coin, price: o.price,
                           expiresAt: Number(o.expiresAt), folder: o.folder, detail: o.detail || "", renew: o.renew === true })
            }
        }
        out.sort(function (a, b) { return (a.state === "open" ? 0 : 1) - (b.state === "open" ? 0 : 1) || b.expiresAt - a.expiresAt })
        return out
    }
    readonly property int openOrders: myOrders.filter(function (o) { return o.state === "open" }).length
    property string historyNote: ""         // what the last trade-history export did
    FileDialog {
        id: historyFolder
        title: qsTr("Choose a folder for your trade history")
        selectFolder: true
        onAccepted: { ex.historyNote = qsTr("Exporting…"); ex.dex.exportTradeHistory(String(folder)) }
    }
    Connections {
        target: ex.dex && typeof ex.dex.exportTradeHistory === "function" ? ex.dex : null
        ignoreUnknownSignals: true
        function onTradeHistoryExported(path, rows, error) {
            ex.historyNote = error !== "" ? error : qsTr("%n trade(s) saved to %1", "", rows).arg(path)
        }
    }

    readonly property string blockedReason: {
        if (wallet.state === undefined || wallet.state === "unconfigured") return qsTr("Trading is starting…")
        if (!keysReady) return qsTr("Unlock trading first")
        if (wallet.state !== "ready") return qsTr("The trading wallet is starting…")
        if (market.configured !== true) return qsTr("This market is starting…")
        return ""
    }

    function marketChain(c) { return Number((markets[c] || {}).chainId) || 0 }
    function rpcFor(id) {
        return ({ 1: "https://ethereum-rpc.publicnode.com", 56: "https://bsc-rpc.publicnode.com", 100000056: "https://bsc-rpc.publicnode.com", 501000101: "https://solana-rpc.publicnode.com", 501000201: "https://solana-rpc.publicnode.com",
                  11155111: "https://ethereum-sepolia-rpc.publicnode.com", 97: "https://bsc-testnet-rpc.publicnode.com", 501000102: "https://api.devnet.solana.com" })[id] || ""
    }
    function networkName(id) {
        return id === 1 ? qsTr("Ethereum") : id === 56 ? qsTr("BNB Smart Chain") : id === 501000101 ? qsTr("Solana") : id === 501000201 ? qsTr("Solana (USDT)") : id === 100000056 ? qsTr("BNB Smart Chain (USDT)")
             : id === 11155111 ? qsTr("Sepolia (test network)") : id === 97 ? qsTr("BNB Smart Chain Testnet") : id === 501000102 ? qsTr("Solana Devnet") : ""
    }

    // ---- fetching (read-only public data). In the app it comes through the engine's feed (QML may
    // not use the network there); without one (tests, previews) the page fetches it itself.
    readonly property var feed: dex && dex.exchangeFeed ? dex.exchangeFeed : null
    Connections {
        target: ex.feed
        ignoreUnknownSignals: true
        function onChanged() { ex.syncFeed() }
    }
    // The order form checks the operator's caps again when the signed caps document changes
    property int riskTick: 0
    Connections {
        target: ex.dex && typeof ex.dex.dexRiskChanged === "function" ? ex.dex : null
        ignoreUnknownSignals: true
        function onDexRiskChanged() { ex.riskTick++ }
    }
    function decimalsOf(c) { return c === "USDT" ? 6 : c === "SOL" ? 9 : 18 }   // USDT on BNB Smart Chain has 18
    function syncFeed() {
        var d = feed.data
        var cs = JSON.parse(JSON.stringify(candles)), ps = JSON.parse(JSON.stringify(prints)), bs = JSON.parse(JSON.stringify(balances))
        for (var i = 0; i < allCoins.length; ++i) {
            var c = allCoins[i]
            if (d["candles:" + c + ":3600"]) cs[c] = d["candles:" + c + ":3600"]
            if (d["prints:" + c]) ps[c] = X.trades(d["prints:" + c], decimalsOf(c))
            if (d["balance:" + c] !== undefined) bs[c] = Number(d["balance:" + c]) / Math.pow(10, decimalsOf(c))
        }
        if (xmrPair) {
            if (d["candles:" + dataKey + ":3600"]) cs[dataKey] = d["candles:" + dataKey + ":3600"]
            if (d["prints:" + dataKey]) ps[dataKey] = X.trades(d["prints:" + dataKey], decimalsOf(coin), 12)
        }
        candles = cs; prints = ps; balances = bs
        if (d.usd) usd = d.usd
        if (d.usdHistory) usdHistory = d.usdHistory
        if (d.noir) noir = d.noir
    }
    function get(url, done) {
        if (!fetchEnabled) { done(null); return }
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function () {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            var data = null
            if (xhr.status === 200) { try { data = JSON.parse(xhr.responseText) } catch (e) { data = null } }
            done(data)
        }
        xhr.open("GET", url); xhr.send()
    }
    function post(url, body, done) {
        if (!fetchEnabled) { done(null); return }
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function () {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            var data = null
            if (xhr.status === 200) { try { data = JSON.parse(xhr.responseText) } catch (e) { data = null } }
            done(data)
        }
        xhr.open("POST", url); xhr.setRequestHeader("Content-Type", "application/json"); xhr.send(JSON.stringify(body))
    }
    // From the first relay that answers
    function relayGet(path, done, i) {
        i = i || 0
        if (i >= relays.length) { done(null); return }
        get(String(relays[i]).replace(/\/+$/, "") + path, function (d) { if (d) done(d); else relayGet(path, done, i + 1) })
    }
    function loadCandles(c) {
        if (feed) { feed.candles(c, 3600); return }
        relayGet("/v1/candles?interval=3600&pair=" + encodeURIComponent(c.indexOf("/") > 0 ? c : c + "/XMZ"), function (d) {
            if (!d || !d.candles) return
            var m = JSON.parse(JSON.stringify(ex.candles)); m[c] = d.candles; ex.candles = m
        })
    }
    function loadPrints(c) {
        if (feed) { feed.prints(c); return }
        relayGet("/v1/prints?pair=" + encodeURIComponent(c.indexOf("/") > 0 ? c : c + "/XMZ"), function (d) {
            if (!d || !d.prints) return
            var coin = c.split("/")[0], places = c.indexOf("/XMR") > 0 ? 12 : 11
            var m = JSON.parse(JSON.stringify(ex.prints)); m[c] = X.trades(d.prints, decimalsOf(coin), places); ex.prints = m
        })
    }
    function loadNoir() {
        if (feed) { if (typeof feed.noirTrade === "function") feed.noirTrade(); return }
        get("https://monzero.org/markets/xmz.php", function (d) { if (d && d.trades) ex.noir = { last: Number(d.last) || 0, stale: d.stale === true, trades: d.trades } })
    }
    function loadUsd() {
        if (feed) { feed.usdPrices(); return }
        get("https://monzero.org/markets/api.php", function (d) {
            if (!d || !d.coins) return
            var m = {}
            for (var i = 0; i < d.coins.length; ++i) if (Number(d.coins[i].usd) > 0) m[d.coins[i].symbol] = Number(d.coins[i].usd)
            ex.usd = m
            if (d.history) ex.usdHistory = d.history
        })
    }
    function loadBalance(c) {
        if (feed) { feed.balance(c); return }
        var id = marketChain(c), rpc = rpcFor(id)
        var account = onSolana(c) ? (wallet.solTradeAccount || "") : myAccount
        if (!rpc || !account) return
        var store = function (value) { var m = JSON.parse(JSON.stringify(ex.balances)); m[c] = value; ex.balances = m }
        if (c === "USDT") {
            post(rpc, { jsonrpc: "2.0", id: 1, method: "getTokenAccountsByOwner", params: [account, { mint: usdtMint }, { encoding: "jsonParsed", commitment: "confirmed" }] }, function (d) {
                var a = d && d.result && d.result.value
                if (!a) return
                var units = 0
                for (var i = 0; i < a.length; ++i) units += Number(a[i].account.data.parsed.info.tokenAmount.amount || 0)
                store(units / 1e6)
            })
        } else if (c === "USDT-BSC") {
            post(rpc, { jsonrpc: "2.0", id: 1, method: "eth_call", params: [{ to: bscUsdt, data: "0x70a08231" + "000000000000000000000000" + account.slice(2).toLowerCase() }, "latest"] }, function (d) {
                if (d && typeof d.result === "string") store(parseInt(d.result, 16) / 1e18)
            })
        } else if (c === "SOL") {
            post(rpc, { jsonrpc: "2.0", id: 1, method: "getBalance", params: [account, { commitment: "confirmed" }] }, function (d) {
                if (d && d.result && d.result.value !== undefined) store(Number(d.result.value) / 1e9)
            })
        } else {
            post(rpc, { jsonrpc: "2.0", id: 1, method: "eth_getBalance", params: [account, "latest"] }, function (d) {
                if (d && typeof d.result === "string") store(parseInt(d.result, 16) / 1e18)
            })
        }
    }
    function refreshOffers() {
        if (directMode) { dex.refreshDirectOffers(); return }
        if (xmrPair) { if (market.configured === true && market.busy !== true) dex.refreshXmrOffers(coin); return }
        if (market.configured === true && market.busy !== true) dex.refreshEvmOffers(coin + "/XMZ")
    }
    function refreshAll() {
        // Every pair's swaps: the chart shows them all in USD
        for (var i = 0; i < allCoins.length; ++i) { loadCandles(allCoins[i]); loadBalance(allCoins[i]); loadPrints(allCoins[i]) }
        if (xmrPair) { loadCandles(dataKey); loadPrints(dataKey) }
    }
    function pick(c, b) {
        b = b || "XMZ"
        if (c === coin && b === base) return
        coin = c; base = b; pickedId = ""; localError = ""
        if (c === "SOON" || c === "ROUTE") { return }
        if (c === "DIRECT") { var cs = direct.chains || []; for (var i = 0; i < cs.length; ++i) loadBalance(cs[i].coin) }
        else { loadPrints(dataKey); loadBalance(c); if (xmrPair) loadCandles(dataKey) }
        refreshOffers()
    }
    function setWithdrawTo(c, address) {
        var m = JSON.parse(JSON.stringify(withdrawTo))
        if (address) m[c] = address; else delete m[c]
        if (typeof persistentSettings !== "undefined") {
            persistentSettings.dexExternalWallets = JSON.stringify(m)
            if (c === "BNB") persistentSettings.dexExternalWallet = address || ""
        }
        withdrawTo = m
    }
    function mainAddress() {
        return typeof appWindow !== "undefined" && appWindow.currentWallet ? appWindow.currentWallet.address(0, 0) : ""
    }

    Timer {
        // Offers every 20 s, the rest every minute; the clock for expiries every 5 s
        interval: 5000; running: ex.visible && ex.active; repeat: true; triggeredOnStart: true
        property int tick: 0
        onRunningChanged: if (running) tick = 0     // fresh data as soon as the page opens
        onTriggered: {
            ex.now = Date.now() / 1000
            if (tick % 4 === 0) ex.refreshOffers()
            if (tick % 12 === 0) ex.refreshAll()
            if (tick % 120 === 0) ex.loadUsd()
            if (tick % 24 === 0) ex.loadNoir()
            tick++
        }
    }
    onKeysReadyChanged: if (keysReady) refreshAll()

    // ---- header: title and pairs
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        Text { text: qsTr("Exchange"); font.pixelSize: 26; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
        Text {
            text: qsTr("Atomic swaps · your coins stay yours until each swap completes or refunds")
            font.pixelSize: 12; color: M.Style.dimmedFontColor; Layout.fillWidth: true; elide: Text.ElideRight
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            model: ex.allCoins
            delegate: Rectangle {
                objectName: "pair_" + modelData
                readonly property bool active: ex.coin === modelData && !ex.xmrPair
                readonly property var st: X.dayStats(ex.candles[modelData] || [], ex.now)
                width: 136; height: 44; radius: 6
                color: active ? M.Style.surfaceRaised : "transparent"
                border.color: active ? M.Style.accent : M.Style.accentMuted
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text { text: "XMZ / " + ex.label(modelData); font.pixelSize: 13; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                    Row {
                        spacing: 8
                        Text { text: st.last > 0 ? X.price(st.last) + " " + modelData : qsTr("no trades yet"); font.pixelSize: 10; color: M.Style.dimmedFontColor }
                        Text {
                            visible: !isNaN(st.change)
                            text: isNaN(st.change) ? "" : (st.change >= 0 ? "+" : "") + st.change.toFixed(2) + "%"
                            font.pixelSize: 10; color: st.change >= 0 ? M.Style.green : "#E5677A"
                        }
                    }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ex.pick(modelData) }
            }
        }
        // Coin-for-coin pairs (ETH, BNB, SOL straight across), each its own tile
        Repeater {
            model: ex.livePairs
            delegate: Rectangle {
                objectName: "pair_" + modelData
                readonly property bool active: ex.directMode && ex.directPair === modelData
                readonly property int offers: (ex.direct.listings || []).filter(function (l) { return l.pair === modelData && l.expiresAt > ex.now }).length
                width: 136; height: 44; radius: 6
                color: active ? M.Style.surfaceRaised : "transparent"
                border.color: active ? M.Style.accent : M.Style.accentMuted
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text { text: ex.directLabel(modelData); font.pixelSize: 13; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                    Text { text: offers > 0 ? qsTr("%n offer(s)", "", offers) : qsTr("coin for coin"); font.pixelSize: 10; color: M.Style.dimmedFontColor }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ex.pickDirect(modelData) }
            }
        }
        // Monero pairs trading in this wallet
        Repeater {
            model: ex.xmrLive
            delegate: Rectangle {
                objectName: "pair_" + modelData
                readonly property string pairCoin: ex.xmrCoinOf(modelData)
                readonly property bool route: modelData === "XMR/XMZ"
                readonly property bool active: route ? ex.routeMode : ex.xmrPair && ex.coin === pairCoin
                readonly property int offers: ((ex.xmrMarkets[pairCoin] || {}).listings || []).filter(function (l) { return l.expiresAt > ex.now }).length
                width: 136; height: 44; radius: 6
                color: active ? M.Style.surfaceRaised : "transparent"
                border.color: active ? M.Style.accent : M.Style.accentMuted
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text { text: ex.directLabel(modelData); font.pixelSize: 13; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                    Text { text: route ? qsTr("two swaps") : offers > 0 ? qsTr("%n offer(s)", "", offers) : qsTr("no offers yet"); font.pixelSize: 10; color: M.Style.dimmedFontColor }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: route ? ex.pick("ROUTE") : ex.pickXmr(modelData) }
            }
        }
        // Announced pairs not trading here yet
        Repeater {
            model: ex.soonPairs
            delegate: Rectangle {
                objectName: "pair_" + modelData
                readonly property bool active: ex.soonMode && ex.soonPair === modelData
                visible: ex.soonOpen || active
                width: 136; height: 44; radius: 6
                color: active ? M.Style.surfaceRaised : "transparent"
                border.color: active ? M.Style.accent : M.Style.accentMuted
                opacity: active ? 1 : 0.6
                Column {
                    anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text { text: ex.directLabel(modelData); font.pixelSize: 13; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                    Text { text: qsTr("coming soon"); font.pixelSize: 10; color: M.Style.dimmedFontColor }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ex.pickSoon(modelData) }
            }
        }
        // One tile for every pair still to come: opens (and folds) their tiles
        Rectangle {
            objectName: "soonToggle"
            visible: ex.soonPairs.length > 0
            width: 136; height: 44; radius: 6
            color: "transparent"; border.color: M.Style.accentMuted; opacity: 0.8
            Column {
                anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text { objectName: "soonToggleText"; text: ex.soonOpen ? qsTr("Fewer pairs") : qsTr("+%n more pairs", "", ex.soonPairs.length); font.pixelSize: 13; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                Text { text: ex.soonOpen ? qsTr("hide coming soon") : qsTr("coming soon"); font.pixelSize: 10; color: M.Style.dimmedFontColor }
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ex.soonOpen = !ex.soonOpen }
        }
    }

    // ---- unlock trading (once per session)
    Rectangle {
        objectName: "unlockCard"
        Layout.fillWidth: true
        visible: !ex.keysReady && ex.wallet.state !== undefined && ex.wallet.state !== "unconfigured"
        radius: 8; color: "transparent"; border.color: M.Style.accent
        implicitHeight: unlockCol.implicitHeight + 28
        ColumnLayout {
            id: unlockCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
            spacing: 8
            // Keys made by another copy of the wallet (upgraded into a new folder, or moved): its
            // helper program is used again only after the user approves this copy's own
            ColumnLayout {
                objectName: "keysHelperReview"
                readonly property var review: ex.wallet.keysHelperReview || null
                visible: !!review
                Layout.fillWidth: true; spacing: 6
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 14; color: M.Style.defaultFontColor
                    text: qsTr("Your trading keys were set up by another copy of this wallet. To unlock them here, this copy's own key helper takes over.")
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 12; color: M.Style.dimmedFontColor
                    text: !parent.review ? "" : qsTr("Before: %1").arg(parent.review.previousHelper) + "\n" + qsTr("Now: %1").arg(parent.review.helper)
                        + "\n" + (parent.review.previousDigest === parent.review.helperDigest
                                  ? qsTr("Same program (SHA-256 %1…): only its folder changed.").arg(String(parent.review.helperDigest).slice(0, 16))
                                  : qsTr("A newer program (SHA-256 %1…, was %2…), checked against the value built into this wallet.").arg(String(parent.review.helperDigest).slice(0, 16)).arg(String(parent.review.previousDigest).slice(0, 16)))
                }
                M.StandardButton {
                    objectName: "approveKeysHelper"
                    text: qsTr("Use this copy's key helper")
                    onClicked: if (!ex.dex.approveTradingKeysHelper(parent.review.commitment)) ex.localError = qsTr("The key wallet could not be moved to this copy. Nothing was changed.")
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.errorColor
                    visible: ex.localError !== ""
                    text: ex.localError
                }
            }
            Text {
                visible: !ex.wallet.keysHelperReview
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 14; color: M.Style.defaultFontColor
                text: ex.wallet.keysBusy === true ? qsTr("Setting up your trading keys…")
                    : ex.wallet.keysExist === true ? qsTr("Enter your trading password to start trading. Browsing the market needs no password.")
                    : qsTr("Choose a trading password to start trading. Your trading keys are made and kept encrypted on this device; keep the password safe, nobody can restore it.")
            }
            RowLayout {
                Layout.fillWidth: true
                visible: ex.wallet.keysBusy !== true && !ex.wallet.keysHelperReview
                M.LineEdit { id: pw; objectName: "exchangePassword"; Layout.fillWidth: true; password: true; fontSize: 15; inputPaddingTop: 8; inputPaddingBottom: 8
                             placeholderText: ex.wallet.keysExist === true ? qsTr("Trading password") : qsTr("New trading password"); placeholderFontSize: 15
                             onAccepted: unlockButton.go() }
                M.LineEdit { id: pw2; Layout.fillWidth: true; password: true; visible: ex.wallet.keysExist !== true; fontSize: 15; inputPaddingTop: 8; inputPaddingBottom: 8
                             placeholderText: qsTr("Repeat the password"); placeholderFontSize: 15; onAccepted: unlockButton.go() }
                M.StandardButton {
                    id: unlockButton; objectName: "exchangeUnlock"
                    text: ex.wallet.keysExist === true ? qsTr("Unlock") : qsTr("Create trading keys")
                    enabled: pw.text !== "" && (ex.wallet.keysExist === true || pw.text === pw2.text)
                    function go() { if (!enabled) return; ex.dex.setUpTradingKeys(pw.text); pw.text = ""; pw2.text = "" }
                    onClicked: go()
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.errorColor
                visible: (ex.wallet.keysMessage || "") !== ""
                text: ex.wallet.keysMessage || ""
            }
        }
    }

    // ---- every open trade (several run side by side): pick one to show it on the trade card
    readonly property var allTrades: (dex && dex.evmTrades) || []
    readonly property int runningTrades: allTrades.filter(function (t) { return t.done !== true }).length
    readonly property int waitingApproval: allTrades.filter(function (t) { return t.awaitingApproval === true && t.done !== true }).length
    function tradeLabel(t) {
        var d = Number(t.decimals) || 18, b = t.privateCoin === "XMR" ? "XMR" : "XMZ"
        var amount = t.xmzAmount !== undefined ? X.amount(Number(t.xmzAmount) / Math.pow(10, b === "XMR" ? 12 : 11), b === "XMR" ? 4 : 0) + " " + b : ""
        var coinPart = t.value ? X.plain(Number(t.value) / Math.pow(10, d), 6) + " " + label(t.coin || "") : ""
        var verb = t.role === "owner" ? qsTr("Buy") : t.role === "claimer" ? qsTr("Sell") : ""
        return (verb + " " + amount + (coinPart ? " · " + coinPart : "")).trim() || qsTr("Trade starting…")
    }
    function tradeState(t) {
        if (t.done === true) return t.stage === "Done(Swapped)" ? qsTr("Done") : String(t.stage || "").indexOf("Refund") >= 0 ? qsTr("Refunded") : qsTr("Finished")
        if (t.awaitingApproval === true) return qsTr("Needs approval")
        if (t.awaitingFunding === true) return qsTr("Needs funds")
        if ((t.error || "") !== "") return qsTr("Retrying")
        return qsTr("Running")
    }
    // ---- one trade in full: only when it needs the user (approval, funds, a refusal) or they opened
    // it from My orders; the trades otherwise show as rows with ticks in My orders
    property bool tradeDetail: false
    readonly property bool tradeNeedsYou: ex.trade.configured === true && tradeSnapshot.done !== true
        && ((ex.trade.awaitingApproval === true && ex.trade.autoApprove !== true)
            || Object.keys(ex.trade.awaitingFunding || {}).length > 0 || (ex.trade.startRefusal || "") !== "")
    Text {
        visible: ex.tradeDetail && ex.trade.configured === true && !ex.tradeNeedsYou
        Layout.alignment: Qt.AlignRight
        text: qsTr("Hide trade details"); font.pixelSize: 12; font.underline: true; color: M.Style.accent
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ex.tradeDetail = false }
    }
    M.DexTradeProgress {
        objectName: "exchangeTrade"
        Layout.fillWidth: true
        visible: ex.trade.configured === true && (ex.tradeDetail || ex.tradeNeedsYou)
        trade: ex.trade
        coin: ex.tradeCoin
        coinDecimals: ex.decimalsOf(ex.tradeCoin)
        errorText: ex.localError
        externalWallet: ex.withdrawTo[ex.tradeCoin] || ""
        onApprove: ex.localError = ex.dex.approveEvmTrade(commitment) ? "" : qsTr("Approval could not start.")
        onRefresh: ex.localError = ex.dex.refreshEvmTrade() ? "" : qsTr("Refresh could not start.")
        onPayoutRequested: ex.localError = ex.dex.payoutEvmTrade(ex.withdrawTo[ex.tradeCoin] || "") ? "" : qsTr("The payout could not start.")
    }
    Text {
        objectName: "abandonTrade"
        visible: ex.trade.configured === true && ex.trade.awaitingApproval === true && (ex.tradeDetail || ex.tradeNeedsYou)
        text: qsTr("Changed your mind? Cancel this trade (nothing is locked yet)")
        font.pixelSize: 12; color: M.Style.dimmedFontColor; font.underline: true
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ex.localError = ex.dex.abandonEvmTrade() ? "" : qsTr("The trade could not be cancelled now.") }
    }

    // ---- stats bar
    Rectangle {
        Layout.fillWidth: true
        visible: !ex.pairMode
        implicitHeight: statsFlow.implicitHeight + 20
        radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
        Flow {
            id: statsFlow
            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 14
            spacing: 28
            Repeater {
                model: [
                    { k: qsTr("Last price"), v: ex.stats.last > 0 ? X.price(ex.stats.last) + " " + ex.coin : "—",
                      s: ex.stats.last > 0 && ex.coinUsd > 0 ? "≈ " + X.usd(ex.stats.last * ex.coinUsd) : "" },
                    { k: qsTr("24h change"), v: isNaN(ex.stats.change) ? "—" : (ex.stats.change >= 0 ? "+" : "") + ex.stats.change.toFixed(2) + "%", c: isNaN(ex.stats.change) ? "" : ex.stats.change >= 0 ? "up" : "down" },
                    { k: qsTr("24h high"), v: ex.stats.high > 0 ? X.price(ex.stats.high) : "—" },
                    { k: qsTr("24h low"), v: ex.stats.low > 0 ? X.price(ex.stats.low) : "—" },
                    { k: qsTr("24h volume"), v: ex.stats.volume > 0 ? X.amount(ex.stats.volume, ex.xmrPair ? 4 : 0) + " " + ex.base : "—", s: ex.stats.volumeCoin > 0 ? X.plain(ex.stats.volumeCoin, 4) + " " + ex.coin : "" },
                    { k: qsTr("Best bid"), v: ex.book.bestBid > 0 ? X.price(ex.book.bestBid) : "—", c: "up" },
                    { k: qsTr("Best ask"), v: ex.book.bestAsk > 0 ? X.price(ex.book.bestAsk) : "—", c: "down" }
                ]
                delegate: Column {
                    spacing: 2
                    Text { text: modelData.k; font.pixelSize: 11; color: M.Style.dimmedFontColor }
                    Text { text: modelData.v; font.pixelSize: 15; font.family: M.Style.fontMonoMedium.name
                           color: modelData.c === "up" ? M.Style.green : modelData.c === "down" ? "#E5677A" : M.Style.defaultFontColor }
                    Text { visible: !!modelData.s; text: modelData.s || ""; font.pixelSize: 11; color: M.Style.dimmedFontColor }
                }
            }
        }
    }

    // Offers that could not be loaded must not look like an empty market
    Text {
        objectName: "marketNotice"
        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.warningColor
        visible: text !== "" && !ex.pairMode
        text: ex.market.configured !== true ? ""
            : (ex.market.state === "failed" || ex.market.state === "refused") && ex.market.command === "listings"
              ? qsTr("The %1 offers could not be loaded from the relays: %2. Retrying…").arg(ex.coin).arg(ex.market.error || qsTr("no answer"))
            : ""
    }

    // ---- chart, book and form: three columns when wide, otherwise stacked
    GridLayout {
        id: grid
        Layout.fillWidth: true
        visible: !ex.pairMode
        columns: ex.width >= 1180 ? 3 : ex.width >= 820 ? 2 : 1
        columnSpacing: 12; rowSpacing: 12
        M.DexPriceChart {
            objectName: "exchangeChart"
            Layout.fillWidth: true
            Layout.columnSpan: grid.columns === 2 ? 2 : 1
            Layout.preferredHeight: grid.columns === 3 ? Math.max(460, book.implicitHeight) : 380
            Layout.alignment: Qt.AlignTop
            coin: ex.coin
            base: ex.base
            // A Monero pair: its own swaps only (the USD view of all pairs is XMZ's)
            coins: ex.xmrPair ? [ex.coin] : ex.allCoins
            trades: { if (!ex.xmrPair) return ex.prints; var t = {}; t[ex.coin] = ex.prints[ex.dataKey] || []; return t }
            usd: ex.usd
            usdHistory: ex.usdHistory
            interval: ex.interval
            bestBid: ex.book.bestBid
            bestAsk: ex.book.bestAsk
            noir: ex.xmrPair ? ex.xmrMarketRef : ex.noir
            refName: ex.xmrPair ? qsTr("Market") : "NoirTrade"
            refTip: ex.xmrPair ? (inUsd ? qsTr("Monero's market price in USD (hourly, from monzero.org/markets)")
                                        : qsTr("Monero's market price in %1: XMR's USD price over %1's, hourly (monzero.org/markets)").arg(ex.label(ex.coin))) : ""
            loading: Object.keys(ex.prints).length === 0
            now: ex.now
            onIntervalPicked: ex.interval = seconds
        }
        M.DexOrderBook {
            id: book
            objectName: "exchangeBook"
            Layout.fillWidth: grid.columns !== 3
            Layout.preferredWidth: grid.columns === 3 ? 330 : -1
            Layout.alignment: Qt.AlignTop
            coin: ex.coin
            base: ex.base
            book: ex.book
            lastPrice: ex.stats.last
            coinUsd: ex.coinUsd
            pickedId: ex.pickedId
            onPicked: {
                if (row.mine) { ex.bottomTab = "orders"; return }
                ex.pickedId = row.id
                form.fillFrom(row)
            }
        }
        M.DexOrderForm {
            id: form
            objectName: "exchangeForm"
            Layout.fillWidth: grid.columns !== 3
            Layout.preferredWidth: grid.columns === 3 ? 340 : -1
            Layout.alignment: Qt.AlignTop
            coin: ex.coin
            base: ex.base
            coinDecimals: ex.coinDecimals
            book: ex.book
            coinUsd: ex.coinUsd
            marketUsd: ex.xmrPair ? (ex.usd.XMR || 0) : (ex.noir && !ex.noir.stale ? Number(ex.noir.last) || 0 : 0)
            coinBalance: ex.coinBalance
            xmzBalance: ex.xmrPair ? ex.xmrBalance : ex.xmzBalance
            blockedReason: ex.blockedReason
            limitCheck: ex.chainId > 0 && typeof ex.dex.evmOrderRefusal === "function"
                        ? function (coinText, xmzText) { return ex.xmrPair ? ex.dex.xmrOrderRefusal(ex.chainId, coinText, xmzText) : ex.dex.evmOrderRefusal(ex.chainId, coinText, xmzText) } : null
            limitTick: ex.riskTick
            busy: ex.market.busy === true
            message: ex.localError !== "" ? ex.localError : (ex.market.message || "")
            pickedId: ex.pickedId
            onClearPick: ex.pickedId = ""
            onTakeRequested: {
                ex.localError = ""
                if (ex.dex.takeEvmOffer(listingId, xmz)) { ex.pickedId = ""; ex.bottomTab = "orders" }
                else if (!(ex.market.message || "")) ex.localError = qsTr("The offer could not be taken now.")
            }
            onPostRequested: {
                ex.localError = ""
                if (ex.xmrPair ? ex.dex.postXmrOffer(ex.chainId, side, coinAmount, xmz, hours, minXmz, renew)
                               : ex.dex.postEvmOffer(ex.chainId, side, coinAmount, xmz, hours, minXmz, renew)) ex.bottomTab = "orders"
                else if (!(ex.market.message || "")) ex.localError = qsTr("The offer could not be posted now.")
            }
        }
    }

    // ---- a pair that is announced but not trading in this wallet yet
    Rectangle {
        objectName: "soonCard"
        Layout.fillWidth: true
        visible: ex.soonMode
        implicitHeight: soonCol.implicitHeight + 28
        radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
        ColumnLayout {
            id: soonCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
            spacing: 6
            Text { text: ex.directLabel(ex.soonPair) + " · " + qsTr("coming soon"); font.pixelSize: 18; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
            Text {
                objectName: "soonText"
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                font.pixelSize: 13; color: M.Style.dimmedFontColor
                text: ex.soonPair === "XMR/XMZ"
                      ? qsTr("XMR for XMZ will run as two atomic swaps behind this one pair (XMZ to ETH, BNB or SOL, then that coin to XMR). It opens here once it has passed its test-network and real-funds tests.")
                      : ex.soonPair.indexOf("XMR") >= 0
                      ? qsTr("Monero swapped straight across, coin for coin, from this wallet's own Monero trading wallet. It opens here once it has passed its test-network and real-funds tests.")
                      : qsTr("Coin for coin, straight across. It opens here once it has passed its test-network and real-funds tests (USDT has no test-network token, so USDT pairs show only on the real networks).")
            }
        }
    }

    // ---- XMR for XMZ: two swaps through a coin both trade against
    M.DexRoutePanel {
        objectName: "routePanel"
        Layout.fillWidth: true
        visible: ex.routeMode
        dex: ex.dex
        xmzMarkets: ex.markets
        xmrMarkets: ex.xmrMarkets
        trade: ex.trade
        coins: ex.allCoins
        myAccount: ex.myAccount
        now: ex.now
        keysReady: ex.keysReady
    }

    // ---- coin for coin: its own book, form, trades and accounts
    M.DexDirectPanel {
        objectName: "directPanel"
        Layout.fillWidth: true
        visible: ex.directMode
        pair: ex.directPair
        showPairs: false
        dex: ex.dex
        wallet: ex.wallet
        balances: ex.balances
        withdrawTo: ex.withdrawTo
        usd: ex.usd
        now: ex.now
        riskTick: ex.riskTick
        networkName: ex.networkName
        onAddressUsed: ex.setWithdrawTo(coin, address)
    }

    // ---- below: orders, market trades, funds
    Rectangle {
        Layout.fillWidth: true
        visible: !ex.pairMode
        implicitHeight: bottomCol.implicitHeight + 24
        radius: 8; color: M.Style.surfaceRaised; border.color: M.Style.accentMuted
        ColumnLayout {
            id: bottomCol
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
            spacing: 10
            RowLayout {
                spacing: 22
                Repeater {
                    model: [ { k: "orders", t: qsTr("My orders") + (ex.openOrders + ex.runningTrades ? " (" + (ex.openOrders + ex.runningTrades) + ")" : "") },
                             { k: "trades", t: qsTr("Market trades") },
                             { k: "funds", t: qsTr("Funds") } ]
                    delegate: Column {
                        objectName: "tab_" + modelData.k
                        spacing: 4
                        Text { text: modelData.t; font.pixelSize: 14; color: ex.bottomTab === modelData.k ? M.Style.defaultFontColor : M.Style.dimmedFontColor
                               MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: ex.bottomTab = modelData.k } }
                        Rectangle { width: parent.width; height: 2; color: ex.bottomTab === modelData.k ? M.Style.accent : "transparent" }
                    }
                }
            }

            // My orders: offers I posted and offers I took
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                visible: ex.bottomTab === "orders"
                // Trades under way (several run at once): one row each, a tick for every step done
                RowLayout {
                    Layout.fillWidth: true
                    visible: ex.allTrades.length > 0
                    Text { text: qsTr("Trades"); font.pixelSize: 13; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor; Layout.fillWidth: true }
                    M.StandardButton {
                        objectName: "approveAll"
                        visible: ex.waitingApproval > 1 && ex.trade.autoApprove !== true
                        small: true
                        text: qsTr("Approve all (%1)").arg(ex.waitingApproval)
                        onClicked: ex.localError = ex.dex.approveAllEvmTrades() > 0 ? "" : qsTr("No trade could be approved now.")
                    }
                    M.CheckBox {
                        objectName: "autoApprove"
                        text: qsTr("Approve my trades automatically")
                        toggleOnClick: false
                        checked: typeof persistentSettings !== "undefined" ? persistentSettings.dexAutoApprove : ex.trade.autoApprove === true
                        onClicked: {
                            if (typeof persistentSettings !== "undefined") persistentSettings.dexAutoApprove = !persistentSettings.dexAutoApprove
                            else ex.dex.setAutoApproveEvmTrades(ex.trade.autoApprove !== true)
                        }
                    }
                }
                Repeater {
                    model: ex.allTrades
                    delegate: Rectangle {
                        id: tradeRow
                        objectName: "tradeRow"
                        Layout.fillWidth: true
                        readonly property var st: X.tradeStep(modelData.role, modelData.stage)
                        readonly property bool buyer: modelData.role === "owner"
                        readonly property string c: ex.label(modelData.coin || "")
                        readonly property string b: modelData.privateCoin === "XMR" ? "XMR" : "XMZ"
                        readonly property bool attention: modelData.done !== true && (modelData.awaitingFunding === true || (modelData.awaitingApproval === true && ex.trade.autoApprove !== true))
                        implicitHeight: rowCol.implicitHeight + 12
                        radius: 6; color: modelData.selected && ex.tradeDetail ? M.Style.accentMuted : "transparent"
                        border.color: attention ? M.Style.warningColor : st.refund ? M.Style.warningColor : M.Style.accentMuted
                        ColumnLayout {
                            id: rowCol
                            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 8
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: ex.tradeLabel(modelData); font.pixelSize: 13; color: tradeRow.buyer ? M.Style.green : "#E5677A"; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: ex.tradeState(modelData); font.pixelSize: 12; color: tradeRow.attention ? M.Style.warningColor : M.Style.dimmedFontColor }
                                Text {
                                    text: qsTr("Details"); font.pixelSize: 12; font.underline: true; color: M.Style.accent
                                    MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                                                onClicked: { ex.dex.selectEvmTrade(modelData.tradeDir); ex.tradeDetail = true } }
                                }
                            }
                            // The four steps: ✓ done, ● under way, ○ still to come (↩ a refund stopped it there)
                            Flow {
                                Layout.fillWidth: true; spacing: 14
                                Repeater {
                                    model: tradeRow.buyer
                                        ? [qsTr("Agreed"), qsTr("You lock %1").arg(tradeRow.c), qsTr("Seller locks %1").arg(tradeRow.b), qsTr("%1 arrives").arg(tradeRow.b)]
                                        : [qsTr("Agreed"), qsTr("Buyer locks %1").arg(tradeRow.c), qsTr("You lock %1").arg(tradeRow.b), qsTr("%1 arrives").arg(tradeRow.c)]
                                    delegate: Text {
                                        readonly property var st: tradeRow.st
                                        readonly property bool done: index < st.step
                                        readonly property bool current: index === st.step && !st.finished
                                        text: (done ? "✓ " : current ? (st.refund ? "↩ " : "● ") : "○ ") + modelData
                                        font.pixelSize: 12
                                        color: done ? M.Style.green : current ? (st.refund ? M.Style.warningColor : M.Style.defaultFontColor) : M.Style.dimmedFontColor
                                    }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: ex.myOrders.length > 0
                    Repeater {
                        model: [[qsTr("Pair"), 2], [qsTr("Order"), 3], [qsTr("Price"), 3], [qsTr("Amount"), 3], [qsTr("Total"), 3], [qsTr("Status"), 5], ["", 2]]
                        delegate: Text { text: modelData[0]; font.pixelSize: 11; color: M.Style.dimmedFontColor; Layout.fillWidth: true; Layout.preferredWidth: modelData[1] }
                    }
                }
                Repeater {
                    model: ex.myOrders
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        readonly property bool buys: (modelData.kind === "offer") === (modelData.side === "buy-xmz")
                        Text { text: (modelData.base || "XMZ") + "/" + ex.label(modelData.coin); font.pixelSize: 13; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 2 }
                        Text { text: (parent.buys ? qsTr("Buy") : qsTr("Sell")) + " · " + (modelData.kind === "offer" ? qsTr("limit") : qsTr("market"))
                               font.pixelSize: 13; color: parent.buys ? M.Style.green : "#E5677A"; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Text { text: modelData.price || "—"; font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Text { text: modelData.xmz + " " + (modelData.base || "XMZ"); font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Text { text: modelData.amount + " " + modelData.coin; font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 5; elide: Text.ElideRight
                            font.pixelSize: 12; color: M.Style.dimmedFontColor
                            text: (modelData.state === "open" ? (modelData.kind === "offer" ? (modelData.renew ? qsTr("open · renews itself") : qsTr("open · %1").arg(X.timeLeft(modelData.expiresAt, ex.now))) : qsTr("waiting for the maker"))
                                  : modelData.state === "agreed" ? qsTr("matched: see Trades above")
                                  : modelData.state === "finished" ? qsTr("finished")
                                  : modelData.state === "expired" ? qsTr("expired") : modelData.state === "cancelled" ? qsTr("cancelled")
                                  : modelData.state === "withdrawn" ? qsTr("withdrawn: funds no longer there")
                                  : modelData.state === "refused" ? qsTr("not matched: the maker refused") : modelData.state)
                                  + (modelData.detail ? " · " + modelData.detail : "")
                        }
                        Item {
                            Layout.fillWidth: true; Layout.preferredWidth: 2; implicitHeight: 22
                            Text {
                                id: keepOpen
                                visible: modelData.kind === "offer" && modelData.state === "open" && typeof ex.dex.setEvmOfferRenew === "function"
                                anchors.right: cancelText.left; anchors.rightMargin: 14
                                text: modelData.renew ? qsTr("Stop renewing") : qsTr("Keep open"); font.pixelSize: 13; color: M.Style.accent
                                MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                                            onClicked: ex.localError = ex.dex.setEvmOfferRenew(modelData.folder, !modelData.renew) ? "" : qsTr("That could not be changed now.") }
                            }
                            Text {
                                id: cancelText
                                visible: modelData.state === "open"
                                anchors.right: parent.right
                                text: modelData.kind === "offer" ? qsTr("Cancel") : qsTr("Stop waiting"); font.pixelSize: 13; color: "#E5677A"
                                MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                                            onClicked: ex.localError = ex.dex.cancelEvmOffer(modelData.folder) ? ""
                                                     : modelData.kind === "offer" ? qsTr("The offer could not be cancelled now.") : qsTr("The wallet is busy: try again in a few seconds.") }
                            }
                        }
                    }
                }
                Text {
                    visible: ex.myOrders.length === 0
                    text: qsTr("No orders yet. Buy or sell above; your open offers and trades appear here.")
                    font.pixelSize: 13; color: M.Style.dimmedFontColor
                }
                // Every trade of this wallet (XMZ and Monero pairs, all coins) as a CSV file, for records and taxes
                RowLayout {
                    Layout.topMargin: 6; spacing: 10
                    visible: !!ex.dex && typeof ex.dex.exportTradeHistory === "function"
                    Text {
                        objectName: "exportHistory"
                        text: qsTr("Export trade history (CSV)"); font.pixelSize: 12; font.underline: true; color: M.Style.accent
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: historyFolder.open() }
                    }
                    Text { objectName: "exportHistoryResult"; text: ex.historyNote; font.pixelSize: 12; color: M.Style.dimmedFontColor; Layout.fillWidth: true; elide: Text.ElideMiddle }
                }
            }

            // Market trades: completed swaps of this pair, newest first
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                visible: ex.bottomTab === "trades"
                readonly property var list: (ex.prints[ex.dataKey] || []).slice(0, 30)
                RowLayout {
                    Layout.fillWidth: true
                    visible: parent.list.length > 0
                    Repeater {
                        model: [[qsTr("Time"), 3], [qsTr("Price (%1)").arg(ex.coin), 3], [qsTr("XMZ"), 3], [qsTr("Total"), 3], [qsTr("Transaction"), 3]]
                        delegate: Text { text: modelData[0]; font.pixelSize: 11; color: M.Style.dimmedFontColor; Layout.fillWidth: true; Layout.preferredWidth: modelData[1] }
                    }
                }
                Repeater {
                    model: parent.list
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        Text { text: Qt.formatDateTime(new Date(modelData.at * 1000), "d MMM HH:mm"); font.pixelSize: 13; color: M.Style.dimmedFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Text { text: X.price(modelData.price); font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Text { text: X.amount(modelData.xmz, 2); font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Text { text: X.plain(modelData.coin, 6) + " " + ex.coin; font.pixelSize: 13; font.family: M.Style.fontMonoRegular.name; color: M.Style.defaultFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Text { text: modelData.tx.slice(0, 10) + "…"; font.pixelSize: 12; font.family: M.Style.fontMonoRegular.name; color: M.Style.dimmedFontColor; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                    }
                }
                Text {
                    visible: parent.list.length === 0
                    text: qsTr("No completed %2/%1 swaps yet.").arg(ex.coin).arg(ex.base)
                    font.pixelSize: 13; color: M.Style.dimmedFontColor
                }
            }

            // Funds: the wallet of the coin this pair trades (XMZ, or XMR on a Monero pair; both on the
            // XMR/XMZ route) beside this pair's trade account (coin), so the coin's account is never
            // pushed onto a second row by a wallet the pair does not use
            GridLayout {
                Layout.fillWidth: true
                visible: ex.bottomTab === "funds"
                columns: ex.width >= 900 ? 2 : 1
                columnSpacing: 24; rowSpacing: 16
                ColumnLayout {
                    objectName: "fundsXmzColumn"
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 8
                    visible: ex.base !== "XMR" || ex.routeMode
                    Text { text: qsTr("XMZ · trading wallet"); font.pixelSize: 14; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                    Text {
                        objectName: "fundsXmz"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.defaultFontColor
                        text: ex.wallet.state === "ready" ? qsTr("%1 XMZ available · %2 XMZ in total").arg(ex.wallet.unlocked).arg(ex.wallet.balance)
                            : ex.wallet.state === "failed" ? qsTr("The trading wallet is not running: %1").arg(ex.wallet.error || "")
                            : qsTr("Starting the trading wallet…")
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                        text: qsTr("XMZ you sell is locked from here; XMZ you buy arrives here. Move XMZ in from your main balance, and back whenever you like.")
                    }
                    M.LineEdit { Layout.fillWidth: true; readOnly: true; copyButton: true; fontSize: 12; text: ex.wallet.address || ""; visible: (ex.wallet.address || "") !== "" }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ex.wallet.state === "ready"
                        M.LineEdit { id: xmzMove; Layout.fillWidth: true; fontSize: 14; inputPaddingTop: 6; inputPaddingBottom: 6; placeholderText: qsTr("XMZ (empty: all)"); placeholderFontSize: 14 }
                        M.StandardButton {
                            small: true; text: qsTr("Add from my wallet")
                            onClicked: {
                                if (typeof middlePanel === "undefined") return
                                middlePanel.state = "Transfer"
                                middlePanel.transferView.sendTo(ex.wallet.address, "", qsTr("XMZ for trading on the DEX"), xmzMove.text)
                                if (typeof leftPanel !== "undefined") leftPanel.selectItem(middlePanel.state)
                            }
                        }
                        M.StandardButton {
                            small: true; primary: false; text: qsTr("Move to my wallet")
                            enabled: ex.wallet.busy !== true && ex.mainAddress() !== ""
                            onClicked: ex.localError = ex.dex.sendFromDexWallet(ex.mainAddress(), xmzMove.text) ? "" : qsTr("The XMZ could not be moved now.")
                        }
                    }
                }
                // XMR: the app's Monero trading wallet (on a Monero node), moved in and out like the XMZ one
                ColumnLayout {
                    objectName: "fundsXmrColumn"
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 8
                    visible: ex.xmrShown && (ex.base === "XMR" || ex.routeMode)
                    Text { text: qsTr("XMR · Monero trading wallet"); font.pixelSize: 14; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                    Text {
                        objectName: "fundsXmr"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.defaultFontColor
                        text: ex.xmrWallet.state === "ready" ? qsTr("%1 XMR available · %2 XMR in total").arg(ex.xmrWallet.unlocked).arg(ex.xmrWallet.balance)
                            : ex.xmrWallet.state === "failed" ? qsTr("The Monero wallet is not running: %1").arg(ex.xmrWallet.error || "")
                            : qsTr("Starting the Monero wallet…")
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                        text: qsTr("Send XMR here from any Monero wallet to trade it. XMR you buy arrives here; send it on to your own Monero wallet whenever you like. Node: %1").arg(ex.xmrWallet.daemon || "—")
                    }
                    M.LineEdit { objectName: "xmrAddress"; Layout.fillWidth: true; readOnly: true; copyButton: true; fontSize: 12; text: ex.xmrWallet.address || ""; visible: (ex.xmrWallet.address || "") !== "" }
                    // The address on its own line (a Monero address is 95 characters); the amount and button below
                    M.LineEdit { id: xmrTo; objectName: "xmrSendTo"; Layout.fillWidth: true; visible: ex.xmrWallet.state === "ready"; fontSize: 13; inputPaddingTop: 6; inputPaddingBottom: 6; placeholderText: qsTr("Your Monero address"); placeholderFontSize: 13 }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ex.xmrWallet.state === "ready"
                        M.LineEdit { id: xmrAmount; objectName: "xmrSendAmount"; Layout.fillWidth: true; fontSize: 13; inputPaddingTop: 6; inputPaddingBottom: 6; placeholderText: qsTr("XMR (empty: all)"); placeholderFontSize: 13 }
                        M.StandardButton {
                            objectName: "xmrSend"
                            small: true; text: qsTr("Send XMR")
                            enabled: xmrTo.text.trim() !== "" && ex.xmrWallet.busy !== true
                            onClicked: ex.localError = ex.dex.sendFromXmrWallet(xmrTo.text.trim(), xmrAmount.text.trim()) ? "" : qsTr("The XMR could not be sent: check the address and amount.")
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 8
                    Text { text: qsTr("%1 · trade account on %2").arg(ex.coin).arg(ex.networkName(ex.chainId) || ex.coin); font.pixelSize: 14; font.family: M.Style.fontMedium.name; color: M.Style.defaultFontColor }
                    Text {
                        objectName: "fundsCoin"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13; color: M.Style.defaultFontColor
                        text: ex.coinAccount === "" ? (ex.keysReady ? qsTr("Finding your account…") : qsTr("Unlock trading to see your account."))
                            : ex.coinBalance >= 0 ? qsTr("%1 %2").arg(X.plain(ex.coinBalance, 8)).arg(ex.coin) + (ex.coinUsd > 0 ? "  ≈ " + X.usd(ex.coinBalance * ex.coinUsd) : "")
                            : qsTr("Reading the balance…")
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                        text: ex.coin === "USDT"
                              ? qsTr("To buy XMZ, send USDT on Solana (SPL, not ERC-20 or TRC-20) here, plus about 0.015 SOL for network fees. When the project's relays pay USDT traders' fees, no SOL is needed: the fees are then paid in USDT. It is this app's own account: only your trading keys can move it.")
                              : ex.coin === "USDT-BSC"
                              ? qsTr("To buy XMZ, send USDT on BNB Smart Chain (BEP-20, not ERC-20 or TRC-20) here, plus a little BNB for network fees. When the project's relays pay USDT traders' fees, a seller needs no BNB: the fee is then paid from the USDT received. It is this app's own account: only your trading keys can move it.")
                              : qsTr("To buy XMZ, send %1 on %2 here (plus a little for network fees). It is this app's own account: only your trading keys can move it.").arg(ex.coin).arg(ex.networkName(ex.chainId) || ex.coin)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ex.coinAccount !== ""
                        spacing: 12
                        Image {
                            Layout.preferredWidth: 96; Layout.preferredHeight: 96; smooth: false; fillMode: Image.PreserveAspectFit
                            source: !parent.visible ? "" : ex.coin === "USDT" ? "image://qrcode/solana:" + ex.coinAccount + "?spl-token=" + ex.usdtMint
                                  : ex.coin === "USDT-BSC" ? "image://qrcode/ethereum:" + ex.bscUsdt + "@56/transfer?address=" + ex.coinAccount
                                  : ex.coin === "SOL" ? "image://qrcode/solana:" + ex.coinAccount
                                  : "image://qrcode/ethereum:" + ex.coinAccount + "@" + ex.chainId
                        }
                        M.LineEdit { Layout.fillWidth: true; readOnly: true; copyButton: true; fontSize: 12; text: ex.coinAccount }
                    }
                    Text { text: qsTr("Withdraw to your own %1 wallet").arg(ex.coin); font.pixelSize: 12; color: M.Style.dimmedFontColor; visible: ex.coinAccount !== "" }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: ex.coinAccount !== ""
                        M.LineEdit { id: withdrawAddress; Layout.fillWidth: true; fontSize: 13; inputPaddingTop: 6; inputPaddingBottom: 6
                                     text: ex.withdrawTo[ex.coin] || ""; placeholderText: ex.onSolana(ex.coin) ? qsTr("Solana address") : qsTr("0x… address"); placeholderFontSize: 13 }
                        M.StandardButton {
                            small: true; text: qsTr("Withdraw")
                            enabled: withdrawAddress.text.trim() !== "" && ex.market.busy !== true
                            onClicked: {
                                var a = withdrawAddress.text.trim()
                                ex.setWithdrawTo(ex.coin, a)
                                ex.withdrawError = ex.dex.returnEvmFunds(ex.chainId, a) ? "" : (ex.market.message || qsTr("The withdrawal could not start. If the wallet is busy, try again in a few seconds."))
                            }
                        }
                    }
                    // What the last withdrawal did: the helper's answer, or why it was refused
                    Text {
                        objectName: "withdrawResult"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                        readonly property var w: ex.market.withdrawal || ({})
                        readonly property bool mine: ex.market.command === "withdraw"
                        visible: ex.coinAccount !== "" && (mine || ex.withdrawError !== "")
                        color: ex.withdrawError !== "" || (mine && (ex.market.state === "refused" || ex.market.state === "failed" || (ex.market.state === "ok" && !w.tx)))
                               ? "#E5677A" : M.Style.defaultFontColor
                        text: ex.withdrawError !== "" ? ex.withdrawError
                              : ex.market.busy === true ? qsTr("Sending…")
                              : ex.market.state === "refused" || ex.market.state === "failed" ? qsTr("Not sent: %1").arg(ex.market.error || qsTr("no answer"))
                              : w.tx ? qsTr("Sent to %1. Transaction: %2").arg(w.to || "").arg(w.tx)
                              : qsTr("Not sent: %1").arg(w.reason || qsTr("nothing to send"))
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: M.Style.dimmedFontColor
                        visible: ex.coinAccount !== ""
                        text: qsTr("Sends what no open offer or running trade needs, less the network fee. This address is also where a finished trade pays out.")
                    }
                }
            }
        }
    }
}
