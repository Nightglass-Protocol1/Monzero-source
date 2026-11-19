.pragma library
// The in-app exchange's arithmetic (Linux): order book rows from the helper's checked listings,
// which offer a market order takes, limit-order totals and 24 h figures from the relays' candles.
// Display and planning only: the amounts a trade uses are computed again, exactly, by the engine
// and the swap helper from the signed offer.

var XMZ_PLACES = 11
var ATOMS = 1e11
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
function xmzAtoms(text) {
    var m = /^\s*([0-9]{1,9})(?:\.([0-9]{1,11}))?\s*$/.exec(String(text))
    if (!m) return -1
    var frac = (m[2] || "")
    while (frac.length < XMZ_PLACES) frac += "0"
    var atoms = Number(m[1]) * ATOMS + Number(frac)
    return atoms > 0 ? atoms : -1
}
function atomsText(atoms) { return units(String(Math.round(atoms)), XMZ_PLACES) }

// Order book of one pair. Listings as EvmMarketJob shows them: {id, pair, side, valueWei,
// xmzAtoms, minXmzAtoms?, maker, expiresAt}. A "sell-xmz" offer sells XMZ (an ask: you buy XMZ
// from it), a "buy-xmz" offer buys XMZ (a bid: you sell XMZ to it). Prices are coin per XMZ.
function book(listings, coin, decimals, myAccount, now) {
    var asks = [], bids = []
    var me = String(myAccount || "").toLowerCase()
    var pair = coin + "/XMZ"
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

// Coin text for a limit order of `atoms` XMZ at `unitPrice` coin per XMZ, at most `decimals` places
// (9 shown at most: smaller steps are not meaningful at these prices)
function limitCoin(unitPrice, atoms, decimals) {
    var v = unitPrice * atoms / ATOMS
    if (!(v > 0) || !isFinite(v)) return ""
    return plain(v, Math.min(decimals, 9))
}

// A sensible smallest part for a new partial offer: a tenth, at least 1 XMZ, whole XMZ
function defaultMin(atoms) {
    var tenth = Math.floor(atoms / ATOMS / 10)
    return Math.max(1, tenth)
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

// Chart slots for NoirTrade alone (a pair with no swaps yet): from its first trade (at most `limit`
// slots back) to now, with no candle in them
function noirGrid(points, interval, now, limit) {
    if (!points || !points.length) return []
    var end = Math.floor(now / interval) * interval, start = Math.floor(points[0].t / interval) * interval
    if (end - start > (limit - 1) * interval) start = end - (limit - 1) * interval
    var out = []
    for (var t = start; t <= end; t += interval) out.push({ t: t, empty: true })
    return out
}

// For each chart slot, NoirTrade's last trade by the end of it, in the pair's coin (USDT price over
// the coin's USD price now); null before its first trade or with no coin price
function noirSeries(points, slots, interval, coinUsd) {
    var out = [], j = 0, last = null
    for (var i = 0; i < (slots || []).length; ++i) {
        while (j < points.length && points[j].t < slots[i].t + interval) { last = points[j].p; ++j }
        out.push(last !== null && coinUsd > 0 ? last / coinUsd : null)
    }
    return out
}

// Prints (relay format: {at, value (hex wei/lamports), xmz (atoms), tx, chainId}) -> newest first
function trades(prints, decimals) {
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
