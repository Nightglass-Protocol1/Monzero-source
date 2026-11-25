import QtQuick 2.9

import "dexExchange.js" as X

// Sell mined XMZ on the DEX, when the user turned it on. Every two minutes:
//  1. finds XMZ this wallet mined since it was turned on: block rewards (its own miner) and,
//     when chosen, payouts from the Monzero pool (matched by transaction id with the pool's list)
//  2. counts the chosen share of it once spendable (rewards 60 blocks, payouts 10)
//  3. when a batch is ready, moves it to the wallet's own DEX trading wallet (no dialog: only
//     that address, only that amount; see main.qml dexAutoSend)
//  4. sells it there to buyers paying at least the user's minimum price, never below it; what
//     no buyer takes at that price waits. Proceeds stay in the trade account (Funds tab).
// State is kept in the settings, so a restart neither forgets nor counts anything twice.
Item {
    id: auto
    property var dex: null
    property var settings: null
    property string status: ""
    readonly property bool on: !!settings && settings.dexAutoSell === true
    readonly property var feed: dex && dex.exchangeFeed ? dex.exchangeFeed : null

    function state() {
        var st = {}
        try { st = JSON.parse(settings.dexAutoSellState || "{}") || {} } catch (e) { st = {} }
        st.seen = st.seen || []; st.owed = Number(st.owed) || 0; st.moved = Number(st.moved) || 0; st.sold = Number(st.sold) || 0
        return st
    }
    function save(st) { settings.dexAutoSellState = JSON.stringify(st) }
    function xmz(atoms) { return X.amount(atoms / 1e11, 2) }

    // Turned on now: only XMZ mined from this moment counts
    function enable(on) {
        if (!settings) return
        if (on && !(Number(settings.dexAutoSellMinUsd) > 0)) { status = qsTr("Set a minimum price first."); return }
        settings.dexAutoSell = on
        if (on) { settings.dexAutoSellSince = Math.floor(Date.now() / 1000); status = qsTr("On: XMZ mined from now on will be sold.") }
        else status = qsTr("Off.")
        if (on) step()
    }

    Timer { interval: 120000; repeat: true; running: auto.on; triggeredOnStart: true; onTriggered: auto.step() }

    function step() {
        if (!on || !dex || typeof currentWallet === "undefined" || !currentWallet || currentWallet.viewOnly) return
        if (typeof appWindow !== "undefined" && appWindow.walletSynced !== true) { status = qsTr("Waiting for the wallet to sync."); return }
        var st = state()
        var usePool = settings.dexAutoSellPool !== false
        var pool = null
        if (feed) {
            if (usePool) feed.poolPayments(currentWallet.address(0, 0))
            feed.usdPrices()
            pool = feed.data ? feed.data.poolPayments : null
        }
        // 1-2: what was mined, once spendable
        var poolIds = {}
        if (pool) for (var p = 0; p < pool.length; ++p) poolIds[pool[p].txid] = true
        var share = Math.max(1, Math.min(100, Number(settings.dexAutoSellPercent) || 100))
        var got = currentWallet.history.received(Number(settings.dexAutoSellSince) || 0)
        var seen = {}
        for (var s = 0; s < st.seen.length; ++s) seen[st.seen[s]] = true
        for (var i = 0; i < got.length; ++i) {
            var r = got[i]
            if (seen[r.hash]) continue
            if (Number(r.confirmations) < (r.coinbase ? 60 : 10)) continue
            if (!r.coinbase && usePool && !pool) continue          // the pool's list not loaded yet: look again later
            st.seen.push(r.hash); seen[r.hash] = true
            if (r.coinbase || poolIds[r.hash]) st.owed += Math.floor(Number(r.atoms) * share / 100)
        }
        if (st.seen.length > 2000) st.seen = st.seen.slice(st.seen.length - 2000)
        save(st)
        // 3: a batch ready: into the trading wallet
        var trading = dex.dexWalletStatus || {}
        var batch = Math.max(1, Number(settings.dexAutoSellBatch) || 500) * 1e11
        if (st.owed >= batch && trading.address && trading.keysReady === true && typeof appWindow !== "undefined" && !appWindow.dexAutoTx) {
            var amount = Math.floor(st.owed)
            if (currentWallet.unlockedBalance() < amount + 1e9) status = qsTr("%1 XMZ mined to sell: waiting until it is spendable.").arg(xmz(amount))
            else if (appWindow.dexAutoSend(trading.address, amount, function (ok, txids, error) {
                var now = auto.state()
                if (ok) {
                    now.owed = Math.max(0, now.owed - amount); now.moved += amount
                    auto.status = qsTr("Moved %1 mined XMZ to the trading wallet. It sells once spendable (about 20 minutes).").arg(auto.xmz(amount))
                } else auto.status = qsTr("The mined XMZ could not be moved: %1").arg(error)
                auto.save(now)
            })) status = qsTr("Moving %1 mined XMZ to the trading wallet…").arg(xmz(amount))
        } else if (st.owed > 0 && st.owed < batch) status = qsTr("%1 XMZ mined to sell; it sells in batches of %2 XMZ.").arg(xmz(st.owed)).arg(xmz(batch))
        // 4: sell what was moved, at the minimum price or better
        if (st.moved > 0) sell(st, trading)
    }

    function sell(st, trading) {
        var coin = settings.dexAutoSellCoin || "SOL"
        var m = (dex.evmMarketStatus || {})[coin]
        if (!m || m.configured !== true || trading.keysReady !== true || trading.state !== "ready") return
        var usd = coin === "USDT" || coin === "USDT-BSC" ? 1 : Number(((feed && feed.data && feed.data.usd) || {})[coin]) || 0
        var floor = Number(settings.dexAutoSellMinUsd) || 0
        if (!(usd > 0) || !(floor > 0)) { status = qsTr("Waiting for the %1 price.").arg(coin); return }
        if (dex.pendingTakes > 0) return
        var decimals = coin === "USDT" ? 6 : coin === "SOL" ? 9 : 18
        var b = X.book(m.listings || [], coin, decimals, trading.tradeAccount || "", Date.now() / 1000, "XMZ")
        var bids = b.bids.filter(function (row) { return row.price * usd >= floor })
        // The lock fees come from the moved XMZ too: sell a little less than all of it
        var target = Math.floor(st.moved / 1.01) - 2e11
        if (target < 1e11) return
        var plan = X.planSwap({ asks: [], bids: bids }, "sell", target)
        if (!plan.legs.length) { status = qsTr("%1 XMZ waiting for a buyer at $%2 or more.").arg(xmz(st.moved)).arg(floor); return }
        if (Number(trading.unlocked) * 1e11 < X.sellNeed(plan.atoms, plan.legs.length)) { status = qsTr("Waiting for the moved XMZ to unlock in the trading wallet."); return }
        var takes = plan.legs.map(function (l) { return { id: l.row.id, xmz: l.atoms === l.row.atoms ? "" : X.atomsText(l.atoms) } })
        if (!dex.takeEvmOffers(takes)) return
        var now = state()
        now.moved = Math.max(0, now.moved - (plan.short ? plan.atoms : st.moved)); now.sold += plan.atoms
        save(now)
        status = qsTr("Selling %1 mined XMZ for about %2 %3 (%n trade(s)).", "", plan.legs.length).arg(xmz(plan.atoms)).arg(X.plain(plan.coin, 6)).arg(coin)
    }
}
