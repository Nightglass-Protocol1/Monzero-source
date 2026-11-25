.pragma library
// The in-app exchange's arithmetic (Linux): order book rows from the helper's checked listings,
// which offer a market order takes, limit-order totals and 24 h figures from the relays' candles.
// Display and planning only: the amounts a trade uses are computed again, exactly, by the engine
// and the swap helper from the signed offer.

var XMZ_PLACES = 11
var ATOMS = 1e11
// The private coin of a pair: XMZ (11 decimal places) or Monero (XMR, 12). Every function that
// turns atoms into coins takes `places`; absent, XMZ's.
function placesOf(base) { return base === "XMR" ? 12 : XMZ_PLACES }
function scale(places) { return Math.pow(10, places === undefined ? XMZ_PLACES : places) }
// The longest an offer may stay open: 31 days (version-2 offers; a day for the first version)
var MAX_HOURS = 31 * 24

// "123450000000", 11 -> "1.2345"
function units(text, places) {
    var digits = String(text === undefined || text === null ? "" : text)
    if (!/^[0-9]+$/.test(digits)) return ""
    while (digits.length <= places) digits = "0" + digits
    var whole = digits.slice(0, digits.length - places).replace(/^0+(?=[0-9])/, "")
    var part = digits.slice(digits.length - places).replace(/0+$/, "")
    return part === "" ? whole : whole + "." + part
}

// A decimal number as plain text with at most `places` decimals, trailing zeros trimmed (never "1e-7")
function plain(value, places) {
    if (!isFinite(value)) return "0"
    var text = Number(value).toFixed(Math.max(0, Math.min(20, places)))
    if (text.indexOf(".") >= 0) text = text.replace(/0+$/, "").replace(/\.$/, "")
    return text === "-0" ? "0" : text
}

// Prices: six significant digits, plain decimals
function price(value) {
    if (!(value > 0) || !isFinite(value)) return "—"
    var places = Math.max(0, Math.min(18, 5 - Math.floor(Math.log(value) / Math.LN10)))
    return plain(value, places)
}

// Amounts: thousands separated whole part, up to `places` decimals
function amount(value, places) {
    if (!isFinite(value)) return "—"
    var text = plain(value, places === undefined ? 4 : places)
    var parts = text.split(".")
    var sign = parts[0].charAt(0) === "-" ? "-" : "", whole = sign ? parts[0].slice(1) : parts[0], grouped = ""
    while (whole.length > 3) { grouped = "," + whole.slice(-3) + grouped; whole = whole.slice(0, -3) }
    parts[0] = sign + whole + grouped
    return parts.join(".")
}

function usd(value) {
    if (!(value >= 0) || !isFinite(value)) return ""
    if (value === 0) return "$0"
    if (value >= 100) return "$" + amount(value, 0)
    if (value >= 1) return "$" + amount(value, 2)
    return "$" + price(value)
}

// XMZ as text -> atoms (number), or -1 when it is not a positive XMZ amount with at most 11 decimals
function xmzAtoms(text, places) {
    var p = places === undefined ? XMZ_PLACES : places
    var m = /^\s*([0-9]{1,9})(?:\.([0-9]{1,12}))?\s*$/.exec(String(text))
    if (!m || (m[2] || "").length > p) return -1
    var frac = (m[2] || "")
    while (frac.length < p) frac += "0"
    var atoms = Number(m[1]) * scale(p) + Number(frac)
    return atoms > 0 ? atoms : -1
}
function atomsText(atoms, places) { return units(String(Math.round(atoms)), places === undefined ? XMZ_PLACES : places) }

// Order book of one pair. Listings as EvmMarketJob shows them: {id, pair, side, valueWei,
// xmzAtoms, minXmzAtoms?, maker, expiresAt}. A "sell-xmz" offer sells XMZ (an ask: you buy XMZ
// from it), a "buy-xmz" offer buys XMZ (a bid: you sell XMZ to it). Prices are coin per XMZ.
function book(listings, coin, decimals, myAccount, now, base) {
    var asks = [], bids = []
    var me = String(myAccount || "").toLowerCase()
    var pair = coin + "/" + (base || "XMZ")
    var ATOMS = scale(placesOf(base || "XMZ"))
    for (var i = 0; i < (listings || []).length; ++i) {
        var l = listings[i]
        if (String(l.pair).replace(/\s+/g, "") !== pair) continue
        if (now && Number(l.expiresAt) <= now) continue
        var atoms = Number(l.xmzAtoms), wei = Number(l.valueWei)
        if (!(atoms > 0) || !(wei > 0)) continue
        var xmz = atoms / ATOMS, value = wei / Math.pow(10, decimals)
        var min = l.minXmzAtoms ? Number(l.minXmzAtoms) : 0
        // A minimum of all of it is a whole offer (a long-lived whole offer is written that way)
        if (min >= atoms) min = 0
        var row = { id: l.id, price: value / xmz, xmz: xmz, atoms: atoms, coin: value, valueWei: String(l.valueWei),
                    partial: min > 0, minAtoms: min, min: min / ATOMS, mine: me !== "" && String(l.maker).toLowerCase() === me,
                    expiresAt: Number(l.expiresAt), side: l.side, total: 0 }
        if (l.side === "sell-xmz") asks.push(row)
        else if (l.side === "buy-xmz") bids.push(row)
    }
    // Best first; the same price: the larger offer first
    asks.sort(function (a, b) { return a.price - b.price || b.xmz - a.xmz })
    bids.sort(function (a, b) { return b.price - a.price || b.xmz - a.xmz })
    var cum = 0
    for (var j = 0; j < asks.length; ++j) { cum += asks[j].xmz; asks[j].total = cum }
    cum = 0
    for (var k = 0; k < bids.length; ++k) { cum += bids[k].xmz; bids[k].total = cum }
    var bestAsk = asks.length ? asks[0].price : 0, bestBid = bids.length ? bids[0].price : 0
    return { asks: asks, bids: bids, bestAsk: bestAsk, bestBid: bestBid,
             spread: bestAsk > 0 && bestBid > 0 ? bestAsk - bestBid : 0,
             mid: bestAsk > 0 && bestBid > 0 ? (bestAsk + bestBid) / 2 : (bestAsk || bestBid) }
}

// The engine's rule for a take: a whole offer only all of it; a partial one from its minimum,
// leaving nothing or at least the minimum
function fillOk(row, atoms) {
    if (!(atoms > 0) || atoms > row.atoms) return false
    if (atoms === row.atoms) return true
    if (!row.partial) return false
    return atoms >= row.minAtoms && row.atoms - atoms >= row.minAtoms
}

// The amount of `row` nearest to `atoms` that may be taken
function nearestFill(row, atoms) {
    if (!row.partial) return row.atoms
    var a = Math.max(row.minAtoms, Math.min(row.atoms, Math.round(atoms)))
    if (a !== row.atoms && row.atoms - a < row.minAtoms) a = row.atoms - row.minAtoms >= row.minAtoms ? row.atoms - row.minAtoms : row.atoms
    return a
}

// The coin for `atoms` of a row, at its price (display; the engine rounds in the maker's favour)
function fillCoin(row, atoms) { return atoms === row.atoms ? row.coin : row.coin * atoms / row.atoms }

// A market order: one trade with the best offer that can fill the amount (a trade is one offer).
// side "buy": buy XMZ from the asks; "sell": sell XMZ to the bids. Our own offers are skipped.
// {ok, row, atoms, coin, price} when an offer fills it; else {ok: false, reason, suggestion}
function planMarket(b, side, atoms, onlyId) {
    var rows = (side === "buy" ? b.asks : b.bids).filter(function (r) { return !r.mine && (!onlyId || r.id === onlyId) })
    if (!rows.length) return { ok: false, reason: onlyId ? "gone" : "empty" }
    if (!(atoms > 0)) return { ok: false, reason: "amount", suggestion: { row: rows[0], atoms: nearestFill(rows[0], rows[0].atoms) } }
    for (var i = 0; i < rows.length; ++i) {
        if (fillOk(rows[i], atoms)) return { ok: true, row: rows[i], atoms: atoms, coin: fillCoin(rows[i], atoms), price: rows[i].price }
    }
    // Nothing fills exactly this amount: the best-priced offer, at the nearest amount it allows
    var best = rows[0], bestDistance = Infinity
    for (var j = 0; j < rows.length; ++j) {
        var near = nearestFill(rows[j], atoms), d = Math.abs(near - atoms)
        // Prefer the better price unless another offer comes much closer to the amount asked
        if (d < bestDistance * 0.5 || (j === 0)) { best = rows[j]; bestDistance = d }
    }
    var a = nearestFill(best, atoms)
    return { ok: false, reason: atoms > best.atoms && !best.partial ? "whole" : atoms > best.atoms ? "larger" : "fit",
             suggestion: { row: best, atoms: a, coin: fillCoin(best, a) } }
}

// Spend at most `budget` coin on the best ask that allows it (one offer: a trade is one offer):
// all of a whole offer that costs no more, or the largest part of a partial one that fits.
// {ok, row, atoms, coin, price} or {ok: false, reason: "empty" | "short"}
function planSpend(b, budget) {
    var rows = b.asks.filter(function (r) { return !r.mine })
    if (!rows.length) return { ok: false, reason: "empty" }
    if (!(budget > 0)) return { ok: false, reason: "short" }
    for (var i = 0; i < rows.length; ++i) {
        var r = rows[i]
        if (r.coin <= budget) return { ok: true, row: r, atoms: r.atoms, coin: r.coin, price: r.price }
        if (!r.partial) continue
        var atoms = Math.floor(budget / r.coin * r.atoms)
        if (atoms < r.atoms && r.atoms - atoms < r.minAtoms) atoms = r.atoms - r.minAtoms
        if (fillOk(r, atoms)) return { ok: true, row: r, atoms: atoms, coin: fillCoin(r, atoms), price: r.price }
    }
    return { ok: false, reason: "short" }
}

// Coin text for a limit order of `atoms` XMZ at `unitPrice` coin per XMZ, at most `decimals` places
// (9 shown at most: smaller steps are not meaningful at these prices)
function limitCoin(unitPrice, atoms, decimals, places) {
    var v = unitPrice * atoms / scale(places)
    if (!(v > 0) || !isFinite(v)) return ""
    return plain(v, Math.min(decimals, 9))
}

// The least smallest part a partial offer may name: 1 XMZ, or 0.001 XMR (12 places)
function leastPart(places) {
    return places === 12 ? 0.001 : 1e11 / scale(places)
}

// A sensible smallest part for a new partial offer: a tenth, at least the least part, in whole
// least parts (whole XMZ; thousandths of XMR)
function defaultMin(atoms, places) {
    var least = leastPart(places)
    var tenth = Math.floor(atoms / scale(places) / 10 / least + 1e-9) * least
    return Number(plain(Math.max(least, tenth), 3))
}

// 24 h figures from hourly candles {t, o, h, l, c, v, vc, n} (relay format), `now` in seconds
function dayStats(candles, now) {
    var out = { last: 0, open: 0, change: NaN, high: 0, low: 0, volume: 0, volumeCoin: 0, trades: 0 }
    if (!candles || !candles.length) return out
    var sorted = candles.slice().sort(function (a, b) { return a.t - b.t })
    out.last = sorted[sorted.length - 1].c
    var from = now - 86400
    var first = null
    for (var i = 0; i < sorted.length; ++i) {
        var c = sorted[i]
        if (c.t + 3600 <= from) continue
        if (!first) first = c
        out.high = Math.max(out.high, c.h)
        out.low = out.low > 0 ? Math.min(out.low, c.l) : c.l
        out.volume += c.v
        out.volumeCoin += c.vc
        out.trades += c.n
    }
    // The change against the last price before the window (or the window's first open)
    var before = null
    for (var j = sorted.length - 1; j >= 0; --j) if (sorted[j].t + 3600 <= from) { before = sorted[j]; break }
    // No trade in the last 24 h: no change to show
    out.open = before ? before.c : first ? first.o : 0
    if (out.open > 0 && first) out.change = (out.last - out.open) / out.open * 100
    return out
}

// Candles for the chart: as given, ascending, with gaps filled by flat candles at the last close
// (so time reads evenly), at most `limit` of them
function chartCandles(candles, interval, now, limit) {
    if (!candles || !candles.length) return []
    var sorted = candles.slice().sort(function (a, b) { return a.t - b.t })
    var out = [], byT = {}
    for (var i = 0; i < sorted.length; ++i) byT[sorted[i].t] = sorted[i]
    var start = sorted[0].t, end = Math.floor(now / interval) * interval
    if (end < sorted[sorted.length - 1].t) end = sorted[sorted.length - 1].t
    var steps = Math.floor((end - start) / interval)
    if (steps > limit) start = end - (limit - 1) * interval
    var last = null
    for (var k = sorted.length - 1; k >= 0; --k) if (sorted[k].t < start) { last = sorted[k].c; break }
    for (var t = start; t <= end; t += interval) {
        var c = byT[t]
        if (c) { out.push({ t: t, o: c.o, h: c.h, l: c.l, c: c.c, v: c.v, vc: c.vc, n: c.n }); last = c.c }
        else if (last !== null) out.push({ t: t, o: last, h: last, l: last, c: last, v: 0, vc: 0, n: 0, flat: true })
    }
    return out
}

// NoirTrade's XMZ/USDT market ({trades: [[ms, price], …]}) -> [{t (seconds), p (USDT per XMZ)}], oldest first
function noirPoints(noir) {
    var out = []
    var list = noir && noir.trades ? noir.trades : []
    for (var i = 0; i < list.length; ++i) {
        var ms = Number(list[i][0]), p = Number(list[i][1])
        if (ms > 0 && p > 0 && isFinite(p)) out.push({ t: Math.floor(ms / 1000), p: p })
    }
    out.sort(function (a, b) { return a.t - b.t })
    return out
}

// ---- The price chart (one dot per swap, or candles once there is enough trading) ----

function isUsdt(coin) { return coin === "USDT" || coin === "USDT-BSC" }

// The coin's USD price at `at` (unix seconds): the last hourly price at or before it from
// `history` ({coin: [[seconds, usd], …]} oldest first; the first one for older times), the price
// now for the newest hour; 1 for USDT; null when unknown
function usdAt(history, coin, at, nowUsd) {
    if (isUsdt(coin)) return 1
    var h = history && history[coin]
    if (!h || !h.length) return nowUsd > 0 ? nowUsd : null
    if (at >= h[h.length - 1][0] + 3600) return nowUsd > 0 ? nowUsd : h[h.length - 1][1]
    if (at <= h[0][0]) return h[0][1]
    var lo = 0, hi = h.length - 1
    while (lo < hi) { var mid = (lo + hi + 1) >> 1; if (h[mid][0] <= at) lo = mid; else hi = mid - 1 }
    return h[lo][1]
}

// The swaps to plot, oldest first: {t, p (per XMZ, in `unit`), xmz, coin, tx}. `byCoin` is coin ->
// trades as from trades(); `coins` the pairs to include; `unit` "USD" (each swap at its coin's
// USD price then) or the single coin itself
function chartPoints(byCoin, coins, unit, history, usdNow) {
    var out = []
    for (var i = 0; i < coins.length; ++i) {
        var c = coins[i], list = (byCoin && byCoin[c]) || []
        for (var j = 0; j < list.length; ++j) {
            var tr = list[j], p = tr.price
            if (unit === "USD") { var u = usdAt(history, c, tr.at, (usdNow || {})[c]); if (!(u > 0)) continue; p = p * u }
            if (p > 0 && tr.xmz > 0) out.push({ t: tr.at, p: p, xmz: tr.xmz, coin: c, tx: tr.tx || "" })
        }
    }
    out.sort(function (a, b) { return a.t - b.t })
    return out
}

// Candles from swaps: only intervals that had a swap (no made-up flat candles in between)
function candlesOf(points, interval) {
    var out = [], cur = null
    for (var i = 0; i < points.length; ++i) {
        var pt = points[i], t = Math.floor(pt.t / interval) * interval
        if (!cur || cur.t !== t) { cur = { t: t, o: pt.p, h: pt.p, l: pt.p, c: pt.p, v: 0, n: 0 }; out.push(cur) }
        cur.h = Math.max(cur.h, pt.p); cur.l = Math.min(cur.l, pt.p); cur.c = pt.p; cur.v += pt.xmz; cur.n++
    }
    return out
}

// Candles say something once intervals hold several swaps: at least 20 swaps, 2 or more per candle
function autoKind(points, candles) {
    return points.length >= 20 && points.length >= 2 * candles.length ? "candles" : "trades"
}

// The price axis: from items ({lo, hi, v}) with real volume and the extra prices (lines on the
// chart); an item under 0.1% of the volume shown does not stretch it and is listed in `off`
// when it lies beyond. Null when there is nothing to show.
var TINY_SHARE = 0.001
function axisRange(items, extras) {
    var total = 0, lo = Infinity, hi = 0, off = []
    for (var i = 0; i < items.length; ++i) total += items[i].v > 0 ? items[i].v : 0
    for (var j = 0; j < items.length; ++j)
        if (!(total > 0) || items[j].v >= total * TINY_SHARE) { lo = Math.min(lo, items[j].lo); hi = Math.max(hi, items[j].hi) }
    for (var k = 0; k < (extras || []).length; ++k) if (extras[k] > 0) { lo = Math.min(lo, extras[k]); hi = Math.max(hi, extras[k]) }
    if (!(hi > 0)) return null
    for (var m = 0; m < items.length; ++m) if (items[m].hi > hi || items[m].lo < lo) off.push(items[m])
    return { lo: lo, hi: hi, off: off }
}

// NoirTrade as steps in `unit`: the price standing at `from` (its last trade before), then the
// last trade of each interval ({t, p}), carried to now; in a coin, each USDT price over the coin's
// USD price at that time
function noirSteps(points, unit, coin, history, usdNow, interval, from, now) {
    var out = [], byT = {}, times = []
    for (var i = 0; i < (points || []).length; ++i) {
        var pt = points[i]
        if (pt.t < from) {
            if (i + 1 === points.length || points[i + 1].t >= from) { var f = Math.floor(from / interval) * interval; times.push(f); byT[f] = pt }
            continue
        }
        var b = Math.floor(pt.t / interval) * interval
        if (!(b in byT)) times.push(b)
        byT[b] = pt
    }
    for (var j = 0; j < times.length; ++j) {
        var q = byT[times[j]], u = unit === "USD" ? 1 : usdAt(history, coin, q.t, usdNow)
        if (u > 0) out.push({ t: times[j], p: q.p / u })
    }
    var end = Math.floor(now / interval) * interval
    if (out.length && out[out.length - 1].t < end) out.push({ t: end, p: out[out.length - 1].p })
    return out
}

// Prints (relay format: {at, value (hex wei/lamports), xmz (atoms), tx, chainId}) -> newest first
function trades(prints, decimals, places) {
    var ATOMS = scale(places)
    var out = []
    for (var i = 0; i < (prints || []).length; ++i) {
        var p = prints[i]
        var value = parseInt(String(p.value), 16)
        if (!/^0x[0-9a-fA-F]+$/.test(String(p.value))) value = Number(p.value)
        var xmz = Number(p.xmz) / ATOMS
        if (!(value > 0) || !(xmz > 0)) continue
        var coin = value / Math.pow(10, decimals)
        out.push({ at: Number(p.at), xmz: xmz, coin: coin, price: coin / xmz, tx: String(p.tx || ""), listing: String(p.listing || "") })
    }
    out.sort(function (a, b) { return b.at - a.at })
    return out
}

// "6 h", "1 day", "2 weeks", "31 days": how long an offer stays open
function lifetimeText(hours) {
    if (hours < 24) return hours + " h"
    var days = hours / 24
    if (days === 7 || days === 14 || days === 21 || days === 28) return days / 7 === 1 ? "1 week" : (days / 7) + " weeks"
    if (Math.floor(days) === days) return days === 1 ? "1 day" : days + " days"
    return plain(days, 1) + " days"
}
// Hours from a custom amount and unit ("h" or "d"); 0 when not between an hour and 31 days
function customHours(text, unit) {
    var n = Number(String(text).trim())
    if (!/^[0-9]+(\.[0-9]+)?$/.test(String(text).trim()) || !(n > 0)) return 0
    var h = Math.round(unit === "d" ? n * 24 : n)
    return h >= 1 && h <= MAX_HOURS ? h : 0
}
// "5.8 h left", "6 days left" for an offer expiring at `expiresAt` (seconds)
function timeLeft(expiresAt, now) {
    var s = Math.max(0, expiresAt - now)
    if (s < 48 * 3600) return plain(Math.round(s / 360) / 10, 1) + " h left"
    return Math.floor(s / 86400) + " days left"
}

// Where a trade is, as four steps (agreed, first lock, second lock, coin arrives): {step 0..4, refund,
// finished}. The same table as the trade card (DexTradeProgress.stepFor), without its words.
function tradeStep(role, stage) {
    var owner = { "Start": [1], "LockSent": [1], "Locked": [2], "ReadySent": [3], "Ready": [3], "Sweeping": [3], "Done(Swapped)": [4],
                  "RefundSent": [2, true], "Done(Refunded)": [2, true, true], "Done(Aborted)": [1, true, true] }
    var claimer = { "WaitingForEvmLock": [1], "XmzLockSent": [2], "XmzLocked": [2], "ClaimSent": [3], "Done(Swapped)": [4],
                    "WaitingForRefund": [3, true], "SweepingBack": [3, true], "Done(Refunded)": [3, true, true], "Done(Aborted)": [1, true, true] }
    var row = (role === "owner" ? owner : claimer)[stage]
    if (!row) return { step: 0, refund: false, finished: false }
    return { step: row[0], refund: row[1] === true, finished: row[0] === 4 || row[2] === true }
}

// The simple Swap screen: buy or sell `atoms` across as many offers as it takes, best price first
// (whole offers only when all of them fits what is left; partial ones from their minimum, leaving
// none or at least the minimum). {legs: [{row, atoms, coin}], atoms, coin, price, short}
function planSwap(b, side, atoms) {
    var rows = (side === "buy" ? b.asks : b.bids).filter(function (r) { return !r.mine })
    var left = Math.round(atoms), legs = [], coin = 0
    for (var i = 0; i < rows.length && left > 0; ++i) {
        var r = rows[i], take = Math.min(left, r.atoms)
        if (!fillOk(r, take)) {
            // A partial offer that would be left with less than its minimum: take less of it
            if (r.partial && take < r.atoms && r.atoms - take < r.minAtoms && fillOk(r, r.atoms - r.minAtoms)) take = r.atoms - r.minAtoms
            else continue
        }
        var c = fillCoin(r, take)
        legs.push({ row: r, atoms: take, coin: c })
        coin += c; left -= take
    }
    var got = Math.round(atoms) - left
    return { legs: legs, atoms: got, coin: coin, price: got > 0 ? coin / (got / ATOMS) : 0, short: left > 0, left: left }
}

// What a buyer's trade account must hold for one trade besides the price: the network fees the
// wallet keeps in reserve (gas for lock, release and a refund, doubled; on Solana the swap
// account's rent, returned when the swap closes). Most of it stays in the account afterwards.
// {coin: the chain's own coin, amount}
function feeReserve(coin) {
    if (coin === "SOL" || coin === "USDT") return { coin: "SOL", amount: 0.0045 }
    if (coin === "BNB" || coin === "USDT-BSC") return { coin: "BNB", amount: 0.0015 }
    if (coin === "ETH") return { coin: "ETH", amount: 0.0045 }
    return { coin: coin, amount: 0 }
}

// XMZ the trading wallet must hold unlocked to sell `atoms` in `legs` trades: each lock also pays
// its transaction fee (the wallet keeps 1% and at least 1 XMZ per trade for it)
function sellNeed(atoms, legs) {
    return atoms + Math.max(atoms / 100, legs * ATOMS)
}
