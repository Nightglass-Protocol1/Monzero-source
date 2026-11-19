/* Monzero DEX — Atomic-style swap UI (demo). Every pair is XMZ <-> a counter coin.
   Rates are INDICATIVE only: there is no live price feed or trading on this page;
   real swaps run in the Monzero app. No external requests. */
(function () {
  "use strict";
  // Indicative USD anchors purely to compute a plausible receive amount. XMZ uses the DEX's
  // opening rate (not a market price); the other coins are replaced by live prices when the
  // markets feed answers.
  var COINS = {
    XMZ: { name: "Monzero", usd: 0.01, dp: 4 },
    BNB: { name: "BNB", usd: 600, dp: 6 },
    ETH: { name: "Ethereum", usd: 3500, dp: 6 },
    SOL: { name: "Solana", usd: 190, dp: 6 },
    USDT: { name: "Tether (Solana)", usd: 1, dp: 2 },
    "USDT-BSC": { name: "Tether (BNB Chain)", usd: 1, dp: 2 }
  };
  // The pairs open to the public. Bitcoin and Litecoin are not listed while they are being
  // finished (12 Nov 2026); put them back here, in PAIRS below and in /dex/api.php when they open.
  var COUNTERS = ["ETH", "BNB", "SOL", "USDT", "USDT-BSC"];
  // USDT (on Solana) shows as "soon" until /dex/api.php marks it live (when its limits are signed)
  var STATUS = { ETH: "live", BNB: "live", SOL: "live", USDT: "live", "USDT-BSC": "live" };
  // Project fee per pair: SOL dynamic 0.1%–0.5% (floor used for indicative math);
  // EVM (BNB/ETH) fee to be decided. See dex-mainnet/go-live for the live policy.
  var FEES = { BNB: 0, ETH: 0, SOL: 0.001, USDT: 0, "USDT-BSC": 0 };
  var FEE_LABEL = { BNB: "TBD", ETH: "TBD", SOL: "0.1%–0.5%", USDT: "0", "USDT-BSC": "0" };

  var state = { dir: "xmz-to-coin", coin: "ETH" }; // dir: which way XMZ flows
  var $ = function (id) { return document.getElementById(id); };
  function sym(side) { // the ticker on a given side
    if (side === "send") return state.dir === "xmz-to-coin" ? "XMZ" : state.coin;
    return state.dir === "xmz-to-coin" ? state.coin : "XMZ";
  }
  function fmt(n, dp) {
    if (!isFinite(n)) return "0";
    var s = n.toFixed(dp);
    return s.indexOf(".") >= 0 ? s.replace(/0+$/, "").replace(/\.$/, "") : s;
  }

  function coinButtonHTML(ticker, fixed) {
    return '<span class="cdot" data-c="' + ticker + '">' + ticker.slice(0, 1) + '</span>' +
      '<span>' + ticker + '</span><span class="ccaret">' + (fixed ? "" : "▾") + '</span>';
  }

  function renderCoinButtons() {
    var sendT = sym("send"), recvT = sym("recv");
    var sendFixed = sendT === "XMZ", recvFixed = recvT === "XMZ";
    var sc = $("send-coin"), rc = $("recv-coin");
    sc.innerHTML = coinButtonHTML(sendT, sendFixed);
    rc.innerHTML = coinButtonHTML(recvT, recvFixed);
    sc.className = "coin-pick" + (sendFixed ? " fixed" : "");
    rc.className = "coin-pick" + (recvFixed ? " fixed" : "");
    sc.disabled = sendFixed;
    rc.disabled = recvFixed;
  }

  function recalc() {
    var sT = sym("send"), rT = sym("recv");
    var amt = parseFloat($("send-amount").value) || 0;
    var rate = COINS[sT].usd / COINS[rT].usd;      // rT units per 1 sT
    var gross = amt * rate;
    var FEE = FEES[state.coin] || 0;
    var feeText = FEE_LABEL[state.coin] || "\u2014";
    var out = gross * (1 - FEE);
    $("recv-amount").value = fmt(out, COINS[rT].dp);
    // A small rate (1 XMZ in ETH) needs more places than the amount fields show
    var rateDp = rate > 0 && rate < 0.001 ? Math.min(14, 2 - Math.floor(Math.log(rate) / Math.LN10)) : COINS[rT].dp;
    $("d-rate").textContent = "1 " + sT + " ≈ " + fmt(rate, rateDp) + " " + rT;
    $("d-time").textContent = (rT === "XMZ" || sT === "XMZ") ? "~20–40 min" : "~20–40 min";
    var pairCoin = state.coin;
    var pf = $("d-projfee"); if (pf) pf.textContent = feeText;
    var rp = $("reveal-pair"); if (rp) rp.textContent = "XMZ / " + pairCoin;
  }

  function renderPairs() {
    var ul = $("pairs"); if (!ul) return;
    ul.innerHTML = "";
    COUNTERS.forEach(function (c) {
      var li = document.createElement("li");
      li.setAttribute("role", "button");
      li.setAttribute("tabindex", "0");
      var live = STATUS[c] === "live", label = live ? "live" : (STATUS[c] === "beta" ? "beta" : "soon");
      li.innerHTML =
        '<span class="pair-coins"><span class="cdot" data-c="XMZ">X</span><span class="cdot" data-c="' + c + '">' + c.slice(0, 1) + '</span></span>' +
        '<span class="pair-name">XMZ / ' + c + '</span>' +
        '<span class="pair-status ' + (live ? "live" : "") + '">' + label + '</span>';
      function pick() { state.coin = c; renderCoinButtons(); recalc(); closeMenus(); }
      li.addEventListener("click", pick);
      li.addEventListener("keydown", function (e) { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); pick(); } });
      ul.appendChild(li);
    });
  }

  // Coin dropdown for the counter-coin side
  var openMenu = null;
  function closeMenus() {
    if (openMenu) { openMenu.remove(); openMenu = null; }
    $("send-coin").setAttribute("aria-expanded", "false");
    $("recv-coin").setAttribute("aria-expanded", "false");
  }
  function openCoinMenu(btn) {
    if (openMenu) { closeMenus(); return; }
    var tpl = $("coin-menu-tpl");
    var menu = tpl.content.firstElementChild.cloneNode(true);
    COUNTERS.forEach(function (c) {
      var b = document.createElement("button");
      b.type = "button";
      b.setAttribute("role", "option");
      if (c === state.coin) b.setAttribute("aria-selected", "true");
      b.innerHTML = '<span class="cdot" data-c="' + c + '">' + c.slice(0, 1) + '</span><span>' + c + '</span><small>' + COINS[c].name + '</small>';
      b.addEventListener("click", function () { state.coin = c; renderCoinButtons(); recalc(); closeMenus(); });
      menu.appendChild(b);
    });
    var card = $("swapcard");
    card.style.position = "relative";
    var r = btn.getBoundingClientRect(), cr = card.getBoundingClientRect();
    menu.style.top = (r.bottom - cr.top + 6) + "px";
    menu.style.right = (cr.right - r.right) + "px";
    card.appendChild(menu);
    openMenu = menu;
    btn.setAttribute("aria-expanded", "true");
  }

  function init() {
    renderCoinButtons();
    renderPairs();
    recalc();
    // Live coin prices for the calculator, and each pair's status as the site's DEX API reports it
    fetch("/markets/api.php?only=coins", { cache: "no-store" }).then(function (r) { return r.json(); }).then(function (d) {
      (d.coins || []).forEach(function (c) { if (c && COINS[c.symbol] && c.symbol !== "XMZ" && c.usd > 0) COINS[c.symbol].usd = c.usd; });
      recalc();
    }).catch(function () {});
    fetch("/dex/api.php", { cache: "no-store" }).then(function (r) { return r.json(); }).then(function (d) {
      (d.pairs || []).forEach(function (p) {
        var c = p && typeof p.pair === "string" ? p.pair.replace("XMZ/", "") : "";
        if (STATUS[c] && (p.status === "live" || p.status === "beta" || p.status === "soon")) STATUS[c] = p.status;
      });
      renderPairs();
    }).catch(function () {});
    $("send-amount").addEventListener("input", recalc);
    $("swap-flip").addEventListener("click", function () {
      state.dir = state.dir === "xmz-to-coin" ? "coin-to-xmz" : "xmz-to-coin";
      // keep the typed number as the new "send" amount
      renderCoinButtons(); recalc(); closeMenus();
    });
    $("send-coin").addEventListener("click", function (e) { if (!this.disabled) { e.stopPropagation(); openCoinMenu(this); } });
    $("recv-coin").addEventListener("click", function (e) { if (!this.disabled) { e.stopPropagation(); openCoinMenu(this); } });
    $("swap-go").addEventListener("click", function () {
      var rev = $("swap-reveal");
      rev.hidden = !rev.hidden;
      if (!rev.hidden) rev.scrollIntoView({ behavior: "smooth", block: "nearest" });
    });
    document.addEventListener("click", function (e) {
      if (openMenu && !openMenu.contains(e.target)) closeMenus();
    });
    document.addEventListener("keydown", function (e) { if (e.key === "Escape") closeMenus(); });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();

/* ===== Market view: real offers and completed swaps from the open DEX relays, read through
   /dex/relay.php. Offers are signed by their makers; trades are swaps a relay read on chain.
   Posting or taking an offer happens in the Monzero app. ===== */
(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  if (!$("mode-pro")) return;
  var PAIRS = ["ETH", "BNB", "SOL", "USDT", "USDT-BSC"];   // Bitcoin and Litecoin are not listed for now (see COUNTERS above)
  var NAME = { BTC: "Bitcoin", ETH: "Ethereum", BNB: "BNB", LTC: "Litecoin", SOL: "Solana", USDT: "USDT (Solana)", "USDT-BSC": "USDT (BNB Chain)" };
  var EXPLORER = { BTC: "https://mempool.space/tx/", ETH: "https://etherscan.io/tx/", BNB: "https://bscscan.com/tx/", LTC: "https://litecoinspace.org/tx/", SOL: "https://explorer.solana.com/tx/", USDT: "https://explorer.solana.com/tx/", "USDT-BSC": "https://bscscan.com/tx/" };
  // A recorded trade's claim on its chain's explorer (Solana's test cluster is named in the link)
  function claimLink(p) { return EXPLORER[pro.coin] + encodeURIComponent(p.tx) + (p.chainId === 501000102 ? "?cluster=devnet" : ""); }
  // Bitcoin and Litecoin offers give the coin in satoshis/litoshis (`coin`); EVM offers in wei (`value`, hex)
  var UTXO = { BTC: true, LTC: true };
  var MAINNET = { BTC: "btc-mainnet", LTC: "ltc-mainnet" };
  var TF = { "1h": 3600, "4h": 14400, "1D": 86400 };
  var DEC = { ETH: 18, BNB: 18, SOL: 9, USDT: 6, "USDT-BSC": 18 };
  var SPAN = 120;   // chart intervals across the chart, at most
  var pro = { coin: "ETH", tf: "1h", rendered: false, timer: null, chart: null, candles: null, dots: null, vol: null, ext: null, noir: null, range: null,
    unit: "USD", kind: "auto", prints: {}, history: {}, shown: [], cds: [], noirLine: [], drawn: "trades", sub: "", loads: 0, usd: {}, gen: 0 };
  // A swap under this share of the volume on the chart does not stretch the price axis (a 1 XMZ
  // test trade at a far-off price would otherwise press every real trade flat)
  var TINY_SHARE = 0.001;
  var NOIR_COLOR = "#e8b04b";

  function fmt(n, d) { if (!isFinite(n)) return "0"; var s = n.toFixed(d); return s.indexOf(".") >= 0 ? s.replace(/0+$/, "").replace(/\.$/, "") : s; }
  function places(p) { return p > 0 ? Math.min(14, Math.max(2, 3 - Math.floor(Math.log(p) / Math.LN10))) : 2; }
  function price(p) { return fmt(p, places(p)); }
  // The coin's smallest unit: wei (18 decimals), or lamports on Solana (9)
  function wei(hex) { return parseInt(hex, 16) / (pro.coin === "USDT" ? 1e6 : pro.coin === "SOL" ? 1e9 : 1e18); }
  function coinOf(l) { return UTXO[pro.coin] ? Number(l.coin) / 1e8 : wei(l.value); }
  function coins(atoms) { return Number(atoms) / 1e11; }
  function pDot(t) { return '<span class="cdot" data-c="' + t + '">' + t.slice(0, 1) + '</span>'; }
  function esc(s) { return String(s).replace(/[&<>"']/g, function (c) { return "&#" + c.charCodeAt(0) + ";"; }); }
  function get(what, extra) {
    return fetch("/dex/relay.php?what=" + what + "&pair=" + encodeURIComponent(pro.coin + "/XMZ") + (extra || ""), { cache: "no-store" })
      .then(function (r) { return r.json(); });
  }
  function cssVar(name, fallback) { var v = getComputedStyle(document.documentElement).getPropertyValue(name).trim(); return v || fallback; }

  // ---- The price chart. The same rules as the wallet's chart (components/dexExchange.js there):
  // one dot per swap sized by its XMZ while intervals hold one swap or none, candles once they hold
  // several; in USD across every pair (each swap at its coin's USD price then) or in the pair's coin.
  function isUsdt(c) { return c === "USDT" || c === "USDT-BSC"; }
  function coinName(c) { return c === "USDT-BSC" ? "USDT" : c; }
  // The coin's USD price at `at`: the last hourly price at or before it (the first one for older
  // times, the price now for the newest hour); 1 for USDT; null when unknown
  function usdAt(c, at) {
    if (isUsdt(c)) return 1;
    var h = pro.history[c], now = pro.usd[c];
    if (!h || !h.length) return now > 0 ? now : null;
    if (at >= h[h.length - 1][0] + 3600) return now > 0 ? now : h[h.length - 1][1];
    if (at <= h[0][0]) return h[0][1];
    var lo = 0, hi = h.length - 1;
    while (lo < hi) { var mid = (lo + hi + 1) >> 1; if (h[mid][0] <= at) lo = mid; else hi = mid - 1; }
    return h[lo][1];
  }
  // The swaps to plot, oldest first: {t, p (per XMZ in USD or in the coin), xmz, coin}
  function chartPoints(coins, inUsd) {
    var out = [];
    coins.forEach(function (c) {
      (pro.prints[c] || []).forEach(function (pr) {
        var value = parseInt(String(pr.value), 16) / Math.pow(10, DEC[c] || 18), xmz = Number(pr.xmz) / 1e11;
        var p = value / xmz;
        if (inUsd) { var u = usdAt(c, Number(pr.at)); if (!(u > 0)) return; p *= u; }
        if (p > 0 && xmz > 0) out.push({ t: Number(pr.at), p: p, xmz: xmz, coin: c });
      });
    });
    return out.sort(function (a, b) { return a.t - b.t; });
  }
  // Candles from swaps: only intervals that had one
  function candlesOf(points, interval) {
    var out = [], cur = null;
    points.forEach(function (pt) {
      var t = pt.t - pt.t % interval;
      if (!cur || cur.t !== t) { cur = { t: t, o: pt.p, h: pt.p, l: pt.p, c: pt.p, v: 0, n: 0 }; out.push(cur); }
      cur.h = Math.max(cur.h, pt.p); cur.l = Math.min(cur.l, pt.p); cur.c = pt.p; cur.v += pt.xmz; cur.n++;
    });
    return out;
  }
  function autoKind(points, cds) { return points.length >= 20 && points.length >= 2 * cds.length ? "candles" : "trades"; }
  // The price axis from items ({lo, hi, v}) with real volume and the extra prices; `off`: tiny ones beyond it
  function axisRange(items, extras) {
    var total = 0, lo = Infinity, hi = 0, off = [];
    items.forEach(function (i) { total += i.v > 0 ? i.v : 0; });
    items.forEach(function (i) { if (!(total > 0) || i.v >= total * TINY_SHARE) { lo = Math.min(lo, i.lo); hi = Math.max(hi, i.hi); } });
    extras.forEach(function (x) { if (x > 0) { lo = Math.min(lo, x); hi = Math.max(hi, x); } });
    if (!(hi > 0)) return null;
    items.forEach(function (i) { if (i.hi > hi || i.lo < lo) off.push(i); });
    return { lo: lo, hi: hi, off: off };
  }
  // NoirTrade as steps: each interval's last trade from `from` on, carried to now; in a coin, each
  // USDT price over the coin's USD price then
  function noirSteps(inUsd, interval, from, now) {
    var n = pro.noir, byT = {}, times = [], out = [];
    var before = null;   // the price standing when the chart starts
    ((n && n.trades) || []).forEach(function (tr) {
      var at = Math.floor(Number(tr[0]) / 1000), p = Number(tr[1]);
      if (!(p > 0)) return;
      if (!(at >= from)) { if (!before || at >= before.t) before = { t: at, p: p }; return; }
      var b = at - at % interval;
      if (!(b in byT)) times.push(b);
      byT[b] = { t: at, p: p };
    });
    var f = from - from % interval;
    if (before && !(f in byT)) { times.push(f); byT[f] = before; }
    times.sort(function (a, b) { return a - b; }).forEach(function (b) {
      var u = inUsd ? 1 : usdAt(pro.coin, byT[b].t);
      if (u > 0) out.push({ t: b, p: byT[b].p / u });
    });
    var end = now - now % interval;
    if (out.length && out[out.length - 1].t < end) out.push({ t: end, p: out[out.length - 1].p });
    return out;
  }

  function ensureChart() {
    if (pro.chart || !window.LightweightCharts) return !!pro.chart;
    var up = cssVar("--success", "#93d9b6"), down = cssVar("--danger", "#ff8a9f"), text = cssVar("--muted", "#9eb6bd");
    var fmtOpt = { type: "custom", minMove: 1e-14, formatter: price };
    var scaled = function (base) { return pro.range ? { priceRange: pro.range } : base(); };
    pro.chart = window.LightweightCharts.createChart($("price-chart"), {
      autoSize: true,
      layout: { background: { type: "solid", color: "transparent" }, textColor: text, attributionLogo: false },
      grid: { vertLines: { color: "rgba(128,128,128,.12)" }, horzLines: { color: "rgba(128,128,128,.12)" } },
      rightPriceScale: { borderVisible: false },
      timeScale: { borderVisible: false, timeVisible: true, secondsVisible: false },
      localization: { priceFormatter: price }
    });
    pro.candles = pro.chart.addCandlestickSeries({ upColor: up, downColor: down, borderVisible: false, wickUpColor: up, wickDownColor: down, priceFormat: fmtOpt,
      autoscaleInfoProvider: scaled });
    // The swaps as dots: an invisible line whose points carry a circle each, sized by its XMZ
    pro.dots = pro.chart.addLineSeries({ lineVisible: false, color: cssVar("--accent", "#6fd3c1"), priceLineVisible: false, crosshairMarkerVisible: false,
      lastValueVisible: true, priceFormat: fmtOpt, autoscaleInfoProvider: scaled });
    pro.vol = pro.chart.addHistogramSeries({ priceScaleId: "vol", priceFormat: { type: "volume" }, lastValueVisible: false, priceLineVisible: false });
    pro.chart.priceScale("vol").applyOptions({ scaleMargins: { top: 0.82, bottom: 0 } });
    pro.ext = pro.chart.addLineSeries({ color: NOIR_COLOR, lineWidth: 2, lineType: 1, priceLineVisible: false, crosshairMarkerVisible: false,
      title: "NoirTrade", priceFormat: fmtOpt });
    pro.chart.subscribeCrosshairMove(function (param) {
      var sub = $("chart-sub");
      if (!param || param.time === undefined) { sub.textContent = pro.sub; return; }
      sub.textContent = readout(Number(param.time)) || pro.sub;
    });
    return true;
  }

  function money(p) { return pro.unit === "USD" ? "$" + price(p) : price(p) + " " + coinName(pro.coin); }
  function when(t) { var d = new Date(t * 1000), pp = function (x) { return ("0" + x).slice(-2); }; return d.getDate() + " " + d.toLocaleString("en", { month: "short" }) + " " + pp(d.getHours()) + ":" + pp(d.getMinutes()); }
  // The hover read-out: the swap (or candle) at that time, and NoirTrade then
  function readout(t) {
    var iv = TF[pro.tf], parts = [], n = null;
    pro.noirLine.forEach(function (s) { if (s.t <= t) n = s.p; });
    if (pro.drawn === "candles") {
      pro.cds.forEach(function (c) { if (c.t === t) parts.push(when(t) + " · O " + price(c.o) + " H " + price(c.h) + " L " + price(c.l) + " C " + price(c.c) + " · " + c.n + (c.n === 1 ? " swap, " : " swaps, ") + fmt(c.v, 0) + " XMZ"); });
    } else {
      pro.shown.forEach(function (s) { if (s.key === t) parts.push(when(s.t) + " · " + fmt(s.xmz, 2) + " XMZ at " + money(s.p) + (pro.unit === "USD" ? " (" + (s.coin === "USDT-BSC" ? "USDT on BNB Chain" : s.coin) + ")" : "")); });
    }
    if (!parts.length) parts.push(when(t - t % iv));
    return parts.join("; ") + (n !== null ? " · NoirTrade " + money(n) : "");
  }

  function renderChart() {
    var empty = $("chart-empty"), interval = TF[pro.tf], inUsd = pro.unit === "USD";
    if (!ensureChart()) { empty.hidden = false; empty.textContent = "The chart could not load."; return; }
    var up = cssVar("--success", "#93d9b6"), down = cssVar("--danger", "#ff8a9f"), accent = cssVar("--accent", "#6fd3c1");
    var now = Math.floor(Date.now() / 1000), tEnd = now - now % interval, limit = tEnd - (SPAN - 1) * interval;
    var points = chartPoints(inUsd ? PAIRS : [pro.coin], inUsd), noirFirst = Infinity;
    ((pro.noir && pro.noir.trades) || []).forEach(function (tr) { var at = Math.floor(Number(tr[0]) / 1000); if (at >= limit && at < noirFirst) noirFirst = at; });
    var first = Math.min(noirFirst, (points.filter(function (p) { return p.t >= limit; })[0] || { t: Infinity }).t);
    var tStart = isFinite(first) ? Math.max(limit, first - first % interval - interval) : limit;
    var shown = points.filter(function (p) { return p.t >= tStart; }), cds = candlesOf(shown, interval);
    var drawn = pro.kind === "auto" ? autoKind(shown, cds) : pro.kind;
    var noirLine = (inUsd || isUsdt(pro.coin) || pro.usd[pro.coin] > 0) ? noirSteps(inUsd, interval, tStart, now) : [];
    var items = drawn === "candles" ? cds.map(function (c) { return { lo: c.l, hi: c.h, v: c.v, t: c.t }; })
      : shown.map(function (p) { return { lo: p.p, hi: p.p, v: p.xmz, t: p.t - p.t % interval }; });
    var range = axisRange(items, noirLine.map(function (s) { return s.p; }));
    pro.range = range ? { minValue: range.lo, maxValue: range.hi } : null;
    pro.shown = shown; pro.cds = cds; pro.noirLine = noirLine; pro.drawn = drawn;

    // Every interval on the time axis (so time is to scale), with its volume where it had swaps
    var vol = {}; cds.forEach(function (c) { vol[c.t] = c; });
    var bars = [];
    for (var t = tStart; t <= tEnd; t += interval) bars.push(vol[t] ? { time: t, value: vol[t].v, color: (drawn === "candles" ? (vol[t].c >= vol[t].o ? up : down) : accent) + "55" } : { time: t });
    pro.vol.setData(bars);
    pro.vol.setMarkers(range ? range.off.map(function (i) {
      var above = i.hi > range.hi;
      return { time: i.t, position: "aboveBar", shape: above ? "arrowUp" : "arrowDown", color: cssVar("--muted", "#9eb6bd"), text: price(above ? i.hi : i.lo) + " · " + fmt(i.v, 2) + " XMZ" };
    }) : []);
    if (drawn === "candles") {
      pro.candles.setData(cds.map(function (c) { return { time: c.t, open: c.o, high: c.h, low: c.l, close: c.c }; }));
      pro.dots.setData([]); pro.dots.setMarkers([]);
    } else {
      // One point per swap at its interval (a second swap in the same interval moves a few seconds on)
      var big = 0, used = {}, data = [];
      shown.forEach(function (p) { big = Math.max(big, p.xmz); });
      shown.forEach(function (p) {
        var key = p.t - p.t % interval; while (used[key]) key += 1;
        used[key] = true; p.key = key; data.push({ time: key, value: p.p });
      });
      data.sort(function (a, b) { return a.time - b.time; });
      pro.candles.setData([]);
      pro.dots.setData(data);
      pro.dots.setMarkers(shown.slice().sort(function (a, b) { return a.key - b.key; }).map(function (p) {
        return { time: p.key, position: "inBar", shape: "circle", color: accent, size: 0.6 + 2.4 * Math.sqrt(p.xmz / big) };
      }));
    }
    pro.ext.setData(noirLine.map(function (s) { return { time: s.t, value: s.p }; }));
    pro.ext.applyOptions({ title: pro.noir && pro.noir.stale ? "NoirTrade (old)" : "NoirTrade" });
    Array.prototype.forEach.call($("kind-group").children, function (x) { x.classList.toggle("on", x.getAttribute("data-kind") === drawn); });
    Array.prototype.forEach.call($("unit-group").children, function (x) { x.classList.toggle("on", (x.getAttribute("data-unit") === "USD") === inUsd); });
    $("unit-coin").textContent = coinName(pro.coin);
    pro.sub = (inUsd ? "XMZ in USD · all pairs" : "XMZ / " + coinName(pro.coin)) + " · " + (drawn === "candles" ? "candles" : "one dot per swap") +
      (noirLine.length ? " · amber: NoirTrade" : "") + (range && range.off.length ? " · " + range.off.length + (range.off.length === 1 ? " tiny swap" : " tiny swaps") + " off the scale" : "");
    $("chart-sub").textContent = pro.sub;
    if (shown.length) empty.hidden = true;
    else {
      empty.hidden = false;
      empty.textContent = (inUsd ? "No completed swaps in this time range yet." : "No completed XMZ/" + coinName(pro.coin) + " swaps in this time range yet.") +
        (noirLine.length ? " The amber line is NoirTrade." : "");
    }
    pro.chart.timeScale().fitContent();
  }

  function book(listings) {
    var asks = [], bids = [];
    listings.forEach(function (o) {
      var l = o.listing;
      if (!l || l.pair !== pro.coin + "/XMZ" || (UTXO[pro.coin] && l.network !== MAINNET[pro.coin])) return;
      var c = coinOf(l), row = { price: c / coins(l.xmz), amt: coins(l.xmz), total: c };
      if (!(row.price > 0)) return;
      (l.side === "sell-xmz" ? asks : bids).push(row);
    });
    asks.sort(function (a, b) { return a.price - b.price; });
    bids.sort(function (a, b) { return b.price - a.price; });
    var cum = 0; asks.forEach(function (r) { cum += r.amt; r.cum = cum; });
    cum = 0; bids.forEach(function (r) { cum += r.amt; r.cum = cum; });
    var maxc = Math.max(asks.length ? asks[asks.length - 1].cum : 0, bids.length ? bids[bids.length - 1].cum : 0) || 1;
    return { asks: asks, bids: bids, maxc: maxc };
  }
  function rowHTML(r, maxc, cls) {
    return '<div class="book-row ' + cls + '"><span class="bar" data-d="' + (r.cum / maxc * 100).toFixed(1) + '"></span>' +
      '<span class="price">' + price(r.price) + '</span><span class="num">' + fmt(r.amt, 2) + '</span><span class="num">' + fmt(r.total, 6) + '</span></div>';
  }
  function renderBook(listings) {
    var b = book(listings);
    $("book-asks").innerHTML = b.asks.length ? b.asks.slice().reverse().map(function (r) { return rowHTML(r, b.maxc, "ask"); }).join("") : '<p class="book-empty">No offers to sell XMZ</p>';
    $("book-bids").innerHTML = b.bids.length ? b.bids.map(function (r) { return rowHTML(r, b.maxc, "bid"); }).join("") : '<p class="book-empty">No offers to buy XMZ</p>';
    Array.prototype.forEach.call(document.querySelectorAll("#view-pro .book-row .bar"), function (bar) { bar.style.width = bar.getAttribute("data-d") + "%"; });
    var mid = b.asks.length && b.bids.length ? (b.asks[0].price + b.bids[0].price) / 2 : null;
    $("book-mid").textContent = mid ? price(mid) : "—";
    $("book-spread-v").textContent = mid ? ((b.asks[0].price - b.bids[0].price) / mid * 100).toFixed(2) + "%" : "—";
    renderDepth(b, mid);
    $("r-offers").textContent = String(b.asks.length + b.bids.length);
  }

  function renderDepth(b, mid) {
    var ids = ["d-bid-line", "d-bid-area", "d-ask-line", "d-ask-area"];
    if (!b.asks.length && !b.bids.length) {
      ids.forEach(function (id) { $(id).setAttribute("d", ""); });
      $("d-xmin").textContent = $("d-xmid").textContent = $("d-xmax").textContent = "—";
      return;
    }
    var prices = b.asks.concat(b.bids).map(function (r) { return r.price; });
    var pmin = Math.min.apply(null, prices), pmax = Math.max.apply(null, prices), pad = (pmax - pmin) * 0.08 || pmin * 0.05;
    pmin -= pad; pmax += pad;
    var W = 1000, H = 220, top = 14, span = pmax - pmin;
    function X(pr) { return ((pr - pmin) / span * W).toFixed(1); }
    function Y(c) { return (H - c / b.maxc * (H - top)).toFixed(1); }
    function steps(rows, toward) { // a stepped cumulative line from the best price outward
      var pts = [], prev = 0;
      rows.forEach(function (r) { pts.push([X(r.price), Y(prev)]); pts.push([X(r.price), Y(r.cum)]); prev = r.cum; });
      if (rows.length) pts.push([X(toward), Y(prev)]);
      return pts;
    }
    function line(pts) { return pts.length ? "M" + pts.map(function (q) { return q[0] + "," + q[1]; }).join(" L") : ""; }
    function area(pts) { return pts.length ? line(pts) + " L" + pts[pts.length - 1][0] + "," + H + " L" + pts[0][0] + "," + H + " Z" : ""; }
    var bp = steps(b.bids, pmin), ap = steps(b.asks, pmax);
    $("d-bid-line").setAttribute("d", line(bp)); $("d-bid-area").setAttribute("d", area(bp));
    $("d-ask-line").setAttribute("d", line(ap)); $("d-ask-area").setAttribute("d", area(ap));
    var m = mid || (pmin + pmax) / 2, xm = X(m);
    $("d-mid").setAttribute("x1", xm); $("d-mid").setAttribute("x2", xm);
    $("d-xmin").textContent = price(pmin); $("d-xmid").textContent = price(m); $("d-xmax").textContent = price(pmax);
  }

  function renderTrades(prints) {
    var now = Date.now() / 1000;
    prints.forEach(function (p, i) { p.px = coinOf(p) / coins(p.xmz); });
    $("trades").innerHTML = prints.length ? prints.slice(0, 20).map(function (p, i) {
      var older = prints[i + 1], up = !older || p.px >= older.px, d = new Date(p.at * 1000), pp = function (x) { return ("0" + x).slice(-2); };
      var when = (now - p.at < 86400 ? "" : pp(d.getMonth() + 1) + "-" + pp(d.getDate()) + " ") + pp(d.getHours()) + ":" + pp(d.getMinutes());
      return '<a class="trade-row" href="' + esc(claimLink(p)) + '" rel="noopener" title="The claim on chain"><span class="price ' + (up ? "up" : "down") + '">' + price(p.px) +
        '</span><span class="num">' + fmt(coins(p.xmz), 2) + '</span><span class="num">' + when + '</span></a>';
    }).join("") : '<p class="book-empty">No completed swaps yet</p>';
    var last = prints[0], before = null, vol = 0;
    prints.forEach(function (p) { if (now - p.at <= 86400) vol += coins(p.xmz); else if (!before) before = p; });
    $("pro-last").textContent = last ? price(last.px) + " " + pro.coin : "—";
    var c = $("pro-chg");
    if (last && before) { var ch = (last.px / before.px - 1) * 100; c.textContent = (ch >= 0 ? "+" : "") + ch.toFixed(2) + "%"; c.className = ch >= 0 ? "up" : "down"; }
    else { c.textContent = "—"; c.className = ""; }
    $("pro-vol").textContent = fmt(vol, 0) + " XMZ";
  }

  function noRelay(why) {
    $("pro-note").textContent = why;
    $("r-relay").textContent = "none reachable";
    renderBook([]); renderTrades([]);
    renderChart();
  }

  function load() {
    var gen = ++pro.gen;
    if (++pro.loads % 10 === 0) loadPrices();
    var noir = fetch("/markets/xmz.php", { cache: "no-store" }).then(function (r) { return r.json(); }).catch(function () { return null; });
    // Every pair's swaps: the chart shows them all in USD
    var prints = Promise.all(PAIRS.map(function (c) {
      return fetch("/dex/relay.php?what=prints&pair=" + encodeURIComponent(c + "/XMZ"), { cache: "no-store" }).then(function (r) { return r.json(); }).catch(function () { return null; });
    }));
    Promise.all([get("listings"), prints, noir]).then(function (r) {
      if (gen !== pro.gen) return;
      if (r[2] && r[2].trades) pro.noir = r[2];
      PAIRS.forEach(function (c, i) { if (r[1][i] && r[1][i].prints) pro.prints[c] = r[1][i].prints; });
      if (!r[0] || !r[0].relay) { noRelay("No relay is connected yet"); return; }
      $("pro-note").textContent = "Signed offers from the open relays";
      $("r-relay").textContent = r[0].relay;
      renderBook(r[0].listings || []);
      announce(r[0].listings || []);
      renderTrades(pro.prints[pro.coin] || []);
      renderChart();
    }).catch(function () { if (gen === pro.gen) noRelay("The relays could not be reached"); });
  }

  function loadPrices() {
    // Prices now and hourly for the last days, to value each swap at its time
    return fetch("/markets/api.php", { cache: "no-store" }).then(function (r) { return r.json(); }).then(function (d) {
      (d.coins || []).forEach(function (c) { if (c && c.symbol && c.usd > 0) pro.usd[c.symbol] = c.usd; });
      if (d.history && typeof d.history === "object") pro.history = d.history;
    }).catch(function () {});
  }

  function syncPair() {
    $("pro-pair").innerHTML = pDot("XMZ") + '<span>XMZ / ' + pro.coin + '</span><span class="ccaret">▾</span>';
    $("chart-sub").textContent = "XMZ / " + pro.coin + " · completed swaps";
    $("unit-coin").textContent = coinName(pro.coin);
  }
  // The trade panel (dex-trade.js) follows the selected pair and the relays' offers
  function announce(listings) { document.dispatchEvent(new CustomEvent("monzero-dex-book", { detail: { coin: pro.coin, listings: listings || [] } })); }
  function show() { syncPair(); announce([]); load(); }

  function start() { stop(); if (!document.hidden && !$("view-pro").hidden) pro.timer = setInterval(load, 30000); }
  function stop() { if (pro.timer) { clearInterval(pro.timer); pro.timer = null; } }

  var menu = null;
  function closeMenu() { if (menu) { menu.remove(); menu = null; } $("pro-pair").setAttribute("aria-expanded", "false"); }
  function openPairMenu() {
    if (menu) { closeMenu(); return; }
    menu = document.createElement("div"); menu.className = "coin-menu"; menu.setAttribute("role", "listbox");
    PAIRS.forEach(function (c) {
      var b = document.createElement("button"); b.type = "button"; b.setAttribute("role", "option");
      if (c === pro.coin) b.setAttribute("aria-selected", "true");
      b.innerHTML = pDot(c) + "<span>XMZ / " + c + "</span><small>" + NAME[c] + "</small>";
      b.addEventListener("click", function () { pro.coin = c; show(); closeMenu(); });
      menu.appendChild(b);
    });
    var host = $("view-pro"); host.style.position = "relative";
    var pr = $("pro-pair").getBoundingClientRect(), hr = host.getBoundingClientRect();
    menu.style.top = (pr.bottom - hr.top + 6) + "px"; menu.style.left = (pr.left - hr.left) + "px";
    host.appendChild(menu); $("pro-pair").setAttribute("aria-expanded", "true");
  }

  function setMode(m) {
    var proOn = m === "pro";
    $("view-simple").hidden = proOn;
    $("view-pro").hidden = !proOn;
    $("mode-simple").classList.toggle("on", !proOn);
    $("mode-pro").classList.toggle("on", proOn);
    $("mode-simple").setAttribute("aria-selected", String(!proOn));
    $("mode-pro").setAttribute("aria-selected", String(proOn));
    if (proOn && !pro.rendered) { pro.rendered = true; loadPrices().then(show); }
    if (proOn) start(); else stop();
  }

  $("mode-simple").addEventListener("click", function () { setMode("simple"); if (history.replaceState) history.replaceState(null, "", location.pathname); });
  $("mode-pro").addEventListener("click", function () { setMode("pro"); if (history.replaceState) history.replaceState(null, "", "#pro"); });
  // Shareable deep link: /dex/#pro opens the market view.
  // #pro opens the order book; #pro-ETH (or -BNB, -SOL, -USDT, -USDT-BSC) also picks the pair
  function fromHash() {
    var m = /^#pro(?:-([A-Z]{3,4}(?:-BSC)?))?$/.exec(location.hash);
    if (m && m[1] && PAIRS.indexOf(m[1]) >= 0 && m[1] !== pro.coin) { pro.coin = m[1]; if (pro.rendered) show(); else syncPair(); }
    return m ? "pro" : "simple";
  }
  window.addEventListener("hashchange", function () { setMode(fromHash()); });
  if (/^#pro/.test(location.hash)) setMode(fromHash());
  $("pro-pair").addEventListener("click", function (e) { e.stopPropagation(); openPairMenu(); });
  $("unit-group").addEventListener("click", function (e) {
    var b = e.target.closest && e.target.closest("button[data-unit]"); if (!b) return;
    pro.unit = b.getAttribute("data-unit"); renderChart();
  });
  $("kind-group").addEventListener("click", function (e) {
    var b = e.target.closest && e.target.closest("button[data-kind]"); if (!b) return;
    pro.kind = b.getAttribute("data-kind"); renderChart();
  });
  $("tf-group").addEventListener("click", function (e) {
    var b = e.target.closest && e.target.closest("button[data-tf]"); if (!b) return;
    pro.tf = b.getAttribute("data-tf");
    Array.prototype.forEach.call(this.children, function (x) { x.classList.toggle("on", x === b); });
    load();
  });
  document.addEventListener("visibilitychange", function () { if (!$("view-pro").hidden) { if (document.hidden) stop(); else { load(); start(); } } });
  document.addEventListener("click", function (e) { if (menu && !menu.contains(e.target)) closeMenu(); });
  document.addEventListener("keydown", function (e) { if (e.key === "Escape") closeMenu(); });
})();

/* Launch gate. The DEX page shows "Coming soon" until the owner flips the
   server-side ready flag (monzero-private/dex-status.php -> live=true). No launch
   date is published. This controls PRESENTATION only: real trading is gated by the
   app's signed control document / kill-switch. */
(function () {
  var el = document.getElementById("dex-launch");
  if (!el) return;
  fetch("/dex/api.php", { cache: "no-store" })
    .then(function (r) { return r.json(); })
    .then(function (s) {
      if (!s || !s.live) return;
      var notice = document.querySelector(".dex-notice");
      el.classList.add("live");
      el.innerHTML = '<span class="dc-live">🟢 Monzero DEX is live</span>';
      if (notice) notice.innerHTML = '<strong>Live.</strong> Trade XMZ for ETH, BNB and SOL from your own wallet: on this page with MetaMask or Phantom, with the Monzero wallet open to run the swap. Small limits per trade while the DEX is new. <a href="/dex/recovery/">How to recover your funds →</a>';
    })
    .catch(function () { /* stays "Coming soon" */ });
})();
