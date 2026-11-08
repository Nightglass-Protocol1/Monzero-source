/* Monzero DEX — Atomic-style swap UI (demo). Every pair is XMZ <-> a counter coin.
   Rates are INDICATIVE only: there is no live price feed or trading on this page;
   real swaps run in the Monzero app. No external requests. */
(function () {
  "use strict";
  // Indicative USD anchors purely to compute a plausible receive amount. XMZ uses the DEX's
  // opening rate (not a market price); the other coins are replaced by live prices when the
  // markets feed answers.
  var COINS = {
    XMZ: { name: "Monzero", usd: 0.001, dp: 4 },
    BTC: { name: "Bitcoin", usd: 68000, dp: 8 },
    LTC: { name: "Litecoin", usd: 75, dp: 6 },
    BNB: { name: "BNB", usd: 600, dp: 6 },
    ETH: { name: "Ethereum", usd: 3500, dp: 6 },
    SOL: { name: "Solana", usd: 190, dp: 6 }
  };
  var COUNTERS = ["BTC", "LTC", "BNB", "ETH", "SOL"];
  var STATUS = { BTC: "beta", LTC: "soon", BNB: "soon", ETH: "soon", SOL: "soon" };
  // Project fee per pair: BTC dynamic 0.1%–0.5% (floor used for indicative math),
  // SOL likewise 0.1%–0.5%; LTC free; EVM (BNB/ETH) fee to be decided. See dex-mainnet/go-live for the live policy.
  var FEES = { BTC: 0.001, LTC: 0, BNB: 0, ETH: 0, SOL: 0.001 };
  var FEE_LABEL = { BTC: "0.1%–0.5%", LTC: "Free", BNB: "TBD", ETH: "TBD", SOL: "0.1%–0.5%" };

  var state = { dir: "xmz-to-coin", coin: "BTC" }; // dir: which way XMZ flows
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
    if (state.coin === "BTC") {
      // size-aware spread: clamp( ~2x on-chain cost / trade , 0.1% , 0.5% ) — indicative
      var btc = sT === "BTC" ? amt : (rT === "BTC" ? gross : 0);
      var sats = btc * 1e8;
      FEE = sats > 0 ? Math.min(0.005, Math.max(0.001, 8000 / sats)) : 0.005;
      feeText = (FEE * 100).toFixed(2) + "%";
    }
    var out = gross * (1 - FEE);
    $("recv-amount").value = fmt(out, COINS[rT].dp);
    $("d-rate").textContent = "1 " + sT + " ≈ " + fmt(rate, COINS[rT].dp) + " " + rT;
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
      var live = STATUS[c] === "beta";
      li.innerHTML =
        '<span class="pair-coins"><span class="cdot" data-c="XMZ">X</span><span class="cdot" data-c="' + c + '">' + c.slice(0, 1) + '</span></span>' +
        '<span class="pair-name">XMZ / ' + c + '</span>' +
        '<span class="pair-status ' + (live ? "live" : "") + '">' + (live ? "beta" : "soon") + '</span>';
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
        if (STATUS[c] && (p.status === "beta" || p.status === "soon")) STATUS[c] = p.status;
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
  var PAIRS = ["BTC", "ETH", "BNB", "LTC"];
  var NAME = { BTC: "Bitcoin", ETH: "Ethereum", BNB: "BNB", LTC: "Litecoin" };
  var EXPLORER = { BTC: "https://mempool.space/tx/", ETH: "https://etherscan.io/tx/", BNB: "https://bscscan.com/tx/", LTC: "https://litecoinspace.org/tx/" };
  // Bitcoin and Litecoin offers give the coin in satoshis/litoshis (`coin`); EVM offers in wei (`value`, hex)
  var UTXO = { BTC: true, LTC: true };
  var MAINNET = { BTC: "btc-mainnet", LTC: "ltc-mainnet" };
  var TF = { "1h": 3600, "4h": 14400, "1D": 86400 };
  var OPENING_USD = 0.001; // the DEX's opening quote, not a market price
  var pro = { coin: "ETH", tf: "1h", rendered: false, timer: null, chart: null, candles: null, vol: null, ref: null, usd: {}, gen: 0 };

  function fmt(n, d) { if (!isFinite(n)) return "0"; var s = n.toFixed(d); return s.indexOf(".") >= 0 ? s.replace(/0+$/, "").replace(/\.$/, "") : s; }
  function places(p) { return p > 0 ? Math.min(14, Math.max(2, 3 - Math.floor(Math.log(p) / Math.LN10))) : 2; }
  function price(p) { return fmt(p, places(p)); }
  function wei(hex) { return parseInt(hex, 16) / 1e18; }
  function coinOf(l) { return UTXO[pro.coin] ? Number(l.coin) / 1e8 : wei(l.value); }
  function coins(atoms) { return Number(atoms) / 1e11; }
  function pDot(t) { return '<span class="cdot" data-c="' + t + '">' + t.slice(0, 1) + '</span>'; }
  function esc(s) { return String(s).replace(/[&<>"']/g, function (c) { return "&#" + c.charCodeAt(0) + ";"; }); }
  function get(what, extra) {
    return fetch("/dex/relay.php?what=" + what + "&pair=" + encodeURIComponent(pro.coin + "/XMZ") + (extra || ""), { cache: "no-store" })
      .then(function (r) { return r.json(); });
  }
  function cssVar(name, fallback) { var v = getComputedStyle(document.documentElement).getPropertyValue(name).trim(); return v || fallback; }

  function ensureChart() {
    if (pro.chart || !window.LightweightCharts) return !!pro.chart;
    var up = cssVar("--success", "#93d9b6"), down = cssVar("--danger", "#ff8a9f"), text = cssVar("--muted", "#9eb6bd");
    var fmtOpt = { type: "custom", minMove: 1e-14, formatter: price };
    pro.chart = window.LightweightCharts.createChart($("price-chart"), {
      autoSize: true,
      layout: { background: { type: "solid", color: "transparent" }, textColor: text, attributionLogo: false },
      grid: { vertLines: { color: "rgba(128,128,128,.12)" }, horzLines: { color: "rgba(128,128,128,.12)" } },
      rightPriceScale: { borderVisible: false },
      timeScale: { borderVisible: false, timeVisible: true, secondsVisible: false },
      localization: { priceFormatter: price }
    });
    pro.candles = pro.chart.addCandlestickSeries({ upColor: up, downColor: down, borderVisible: false, wickUpColor: up, wickDownColor: down, priceFormat: fmtOpt });
    pro.vol = pro.chart.addHistogramSeries({ priceScaleId: "vol", priceFormat: { type: "volume" }, lastValueVisible: false, priceLineVisible: false });
    pro.chart.priceScale("vol").applyOptions({ scaleMargins: { top: 0.82, bottom: 0 } });
    pro.ref = pro.chart.addLineSeries({ color: text, lineWidth: 1, lineStyle: 2, priceLineVisible: false, crosshairMarkerVisible: false, title: "Opening rate", priceFormat: fmtOpt,
      // A flat line alone would give an axis of one repeated price: show ±20% around it
      autoscaleInfoProvider: function (base) {
        var r = base();
        if (r && r.priceRange && r.priceRange.maxValue === r.priceRange.minValue) {
          var m = Math.abs(r.priceRange.maxValue) * 0.2 || 1e-12;
          return { priceRange: { minValue: r.priceRange.minValue - m, maxValue: r.priceRange.maxValue + m } };
        }
        return r;
      } });
    return true;
  }

  function renderChart(list) {
    var empty = $("chart-empty"), interval = TF[pro.tf];
    if (!ensureChart()) { empty.hidden = false; empty.textContent = "The chart could not load."; return; }
    var up = cssVar("--success", "#93d9b6"), down = cssVar("--danger", "#ff8a9f");
    pro.candles.setData(list.map(function (c) { return { time: c.t, open: c.o, high: c.h, low: c.l, close: c.c }; }));
    pro.vol.setData(list.map(function (c) { return { time: c.t, value: c.v, color: (c.c >= c.o ? up : down) + "66" }; }));
    var usd = pro.usd[pro.coin], now = Math.floor(Date.now() / 1000), refData = [];
    if (usd > 0) {
      var ref = OPENING_USD / usd, from = list.length ? list[0].t : now - 30 * interval;
      from -= from % interval; var to = now - now % interval;
      refData = [{ time: from, value: ref }];
      if (to > from) refData.push({ time: to, value: ref });
    }
    pro.ref.setData(refData);
    if (list.length) { empty.hidden = true; }
    else {
      empty.hidden = false;
      empty.textContent = (UTXO[pro.coin] ? NAME[pro.coin] + " swaps are not reported to relays yet." : "No completed swaps yet.") +
        (usd > 0 ? " Dashed line: the $0.001 opening rate." : "");
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
      return '<a class="trade-row" href="' + EXPLORER[pro.coin] + esc(p.tx) + '" rel="noopener" title="The claim on chain"><span class="price ' + (up ? "up" : "down") + '">' + price(p.px) +
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
    renderChart([]);
  }

  function load() {
    var gen = ++pro.gen, since = Math.floor(Date.now() / 1000) - 400 * TF[pro.tf];
    Promise.all([get("listings"), get("prints"), get("candles", "&interval=" + TF[pro.tf] + "&since=" + since)]).then(function (r) {
      if (gen !== pro.gen) return;
      if (!r[0] || !r[0].relay) { noRelay("No relay is connected yet"); return; }
      $("pro-note").textContent = "Signed offers from the open relays";
      $("r-relay").textContent = r[0].relay;
      renderBook(r[0].listings || []);
      renderTrades(r[1].prints || []);
      renderChart(r[2].candles || []);
    }).catch(function () { if (gen === pro.gen) noRelay("The relays could not be reached"); });
  }

  function loadPrices() {
    return fetch("/markets/api.php?only=coins", { cache: "no-store" }).then(function (r) { return r.json(); }).then(function (d) {
      (d.coins || []).forEach(function (c) { if (c && c.symbol && c.usd > 0) pro.usd[c.symbol] = c.usd; });
    }).catch(function () {});
  }

  function syncPair() {
    $("pro-pair").innerHTML = pDot("XMZ") + '<span>XMZ / ' + pro.coin + '</span><span class="ccaret">▾</span>';
    $("chart-sub").textContent = "XMZ / " + pro.coin + " · completed swaps";
  }
  function show() { syncPair(); load(); }

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
  // #pro opens the order book; #pro-BTC (or -ETH, -BNB, -LTC) also picks the pair
  function fromHash() {
    var m = /^#pro(?:-([A-Z]{3}))?$/.exec(location.hash);
    if (m && m[1] && PAIRS.indexOf(m[1]) >= 0 && m[1] !== pro.coin) { pro.coin = m[1]; if (pro.rendered) show(); else syncPair(); }
    return m ? "pro" : "simple";
  }
  window.addEventListener("hashchange", function () { setMode(fromHash()); });
  if (/^#pro/.test(location.hash)) setMode(fromHash());
  $("pro-pair").addEventListener("click", function (e) { e.stopPropagation(); openPairMenu(); });
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
      if (notice) notice.innerHTML = '<strong>Live.</strong> Trade XMZ for ETH and BNB from your own wallet in the Monzero app, with small limits while the DEX is new. <a href="/dex/recovery/">How to recover your funds →</a>';
    })
    .catch(function () { /* stays "Coming soon" */ });
})();
