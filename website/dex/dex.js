/* Monzero DEX — Atomic-style swap UI (demo). Every pair is XMZ <-> a counter coin.
   Rates are INDICATIVE only: there is no live price feed or trading on this page;
   real swaps run in the Monzero app. No external requests. */
(function () {
  "use strict";
  // Indicative USD anchors purely to compute a plausible receive amount. Not a price feed.
  var COINS = {
    XMZ: { name: "Monzero", usd: 0.015, dp: 4 },
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

/* ===== Pro order-book mode. SIMULATED indicative market (random walk) — not real market
   data and no trading; real offers are taken in the app. Pauses when hidden / reduced-motion. ===== */
(function () {
  "use strict";
  var $ = function (id) { return document.getElementById(id); };
  if (!$("mode-pro")) return;
  var COINS = { XMZ: { usd: 0.015, dp: 4 }, BTC: { usd: 68000, dp: 8 }, LTC: { usd: 75, dp: 6 }, BNB: { usd: 600, dp: 6 }, ETH: { usd: 3500, dp: 6 }, SOL: { usd: 190, dp: 6 } };
  var NAME = { BTC: "Bitcoin", LTC: "Litecoin", BNB: "BNB", ETH: "Ethereum", SOL: "Solana" };
  var COUNTERS = ["BTC", "LTC", "BNB", "ETH", "SOL"];
  var TF = { "1m": { vol: 0.45, tpc: 3 }, "5m": { vol: 1, tpc: 5 }, "15m": { vol: 1.9, tpc: 8 }, "1h": { vol: 3.2, tpc: 12 }, "4h": { vol: 5.5, tpc: 18 }, "1D": { vol: 9, tpc: 26 } };
  var pro = { coin: "BTC", side: "buy", tf: "5m", rendered: false, mid: null, chg24: 0, vol: 0, trades: [], timer: null, candles: [], formTicks: 0, orders: [], orderId: 0, lastBook: null };
  var lastShown = 0;

  function baseMid() { return COINS.XMZ.usd / COINS[pro.coin].usd; }
  function mid() { return pro.mid != null ? pro.mid : baseMid(); }   // counter coin per 1 XMZ
  function dpp() { return COINS[pro.coin].dp; }
  function fmt(n, d) { if (!isFinite(n)) return "0"; var s = n.toFixed(d); return s.indexOf(".") >= 0 ? s.replace(/0+$/, "").replace(/\.$/, "") : s; }
  function pDot(t) { return '<span class="cdot" data-c="' + t + '">' + t.slice(0, 1) + '</span>'; }

  function genBook() {
    var m = mid(), asks = [], bids = [], cum, i, price, amt;
    cum = 0;
    for (i = 0; i < 13; i++) { price = m * (1 + (i + 1) * 0.0013 + Math.random() * 0.0009); amt = 40 + Math.random() * 900; cum += amt; asks.push({ price: price, amt: amt, cum: cum }); }
    cum = 0;
    for (i = 0; i < 13; i++) { price = m * (1 - (i + 1) * 0.0013 - Math.random() * 0.0009); amt = 40 + Math.random() * 900; cum += amt; bids.push({ price: price, amt: amt, cum: cum }); }
    var maxc = Math.max(asks[asks.length - 1].cum, bids[bids.length - 1].cum);
    return { asks: asks, bids: bids, m: m, maxc: maxc };
  }

  function rowHTML(r, maxc, cls) {
    return '<div class="book-row ' + cls + '" data-price="' + r.price + '"><span class="bar" data-d="' + (r.cum / maxc * 100).toFixed(1) + '"></span>' +
      '<span class="price">' + fmt(r.price, dpp() + 2) + '</span><span class="num">' + fmt(r.amt, 0) + '</span><span class="num">' + fmt(r.price * r.amt, dpp()) + '</span></div>';
  }

  function renderBook() {
    seedCandles();
    pro.chg24 = Math.random() * 7 - 2.5;
    pro.vol = 120000 + Math.random() * 400000;
    lastShown = pro.mid;
    pro.trades = [];
    var now = Date.now();
    for (var i = 0; i < 16; i++) pro.trades.push(makeTrade(now - (i + 1) * (6 + Math.random() * 40) * 1000));
    renderBookRows();
    renderTicker(false);
    renderTradeList(false);
    renderCandles();
    renderOrders();
    syncOrderUnits();
  }

  function renderBookRows() {
    var b = genBook();
    $("book-asks").innerHTML = b.asks.slice().reverse().map(function (r) { return rowHTML(r, b.maxc, "ask"); }).join("");
    $("book-bids").innerHTML = b.bids.map(function (r) { return rowHTML(r, b.maxc, "bid"); }).join("");
    Array.prototype.forEach.call(document.querySelectorAll("#view-pro .book-row .bar"), function (bar) { bar.style.width = bar.getAttribute("data-d") + "%"; });
    $("book-mid").textContent = fmt(b.m, dpp() + 2);
    $("book-spread-v").textContent = ((b.asks[0].price - b.bids[0].price) / b.m * 100).toFixed(2) + "%";
    pro.lastBook = b;
    renderDepth();
  }

  // Cumulative bid/ask depth areas, drawn as SVG paths (attributes only; CSP-safe).
  function renderDepth() {
    var b = pro.lastBook; if (!b || !$("depth-svg")) return;
    var bids = b.bids, asks = b.asks, maxc = b.maxc;
    var pmin = bids[bids.length - 1].price, pmax = asks[asks.length - 1].price, span = pmax - pmin || 1;
    var W = 1000, H = 220, top = 14;
    function X(pr) { return ((pr - pmin) / span * W).toFixed(1); }
    function Y(c) { return (H - c / maxc * (H - top)).toFixed(1); }
    var bpts = [], apts = [], i;
    for (i = bids.length - 1; i >= 0; i--) bpts.push([X(bids[i].price), Y(bids[i].cum)]);   // low price -> mid
    for (i = 0; i < asks.length; i++) apts.push([X(asks[i].price), Y(asks[i].cum)]);          // mid -> high price
    function line(pts) { return "M" + pts.map(function (q) { return q[0] + "," + q[1]; }).join(" L"); }
    function area(pts) { return line(pts) + " L" + pts[pts.length - 1][0] + "," + H + " L" + pts[0][0] + "," + H + " Z"; }
    $("d-bid-line").setAttribute("d", line(bpts));
    $("d-bid-area").setAttribute("d", area(bpts));
    $("d-ask-line").setAttribute("d", line(apts));
    $("d-ask-area").setAttribute("d", area(apts));
    var xm = X(b.m); $("d-mid").setAttribute("x1", xm); $("d-mid").setAttribute("x2", xm);
    $("d-xmin").textContent = fmt(pmin, dpp() + 2);
    $("d-xmid").textContent = fmt(b.m, dpp() + 2);
    $("d-xmax").textContent = fmt(pmax, dpp() + 2);
  }

  function renderTicker(flash) {
    var m = mid(), el = $("pro-last");
    el.textContent = fmt(m, dpp() + 2) + " " + pro.coin;
    if (flash) { el.classList.remove("tflash-up", "tflash-down"); void el.offsetWidth; el.classList.add(m >= lastShown ? "tflash-up" : "tflash-down"); }
    lastShown = m;
    var c = $("pro-chg"); c.textContent = (pro.chg24 >= 0 ? "+" : "") + pro.chg24.toFixed(2) + "%"; c.className = pro.chg24 >= 0 ? "up" : "down";
    $("pro-vol").textContent = fmt(pro.vol, 0) + " XMZ";
  }

  function makeTrade(t) {
    var m = mid();
    return { up: Math.random() > 0.48, price: m * (1 + (Math.random() - 0.5) * 0.006), amt: 20 + Math.random() * 700, t: t || Date.now() };
  }
  function tradeRowHTML(tr, isNew) {
    var d = new Date(tr.t), pp = function (x) { return ("0" + x).slice(-2); };
    return '<div class="trade-row' + (isNew ? " flash" : "") + '"><span class="price ' + (tr.up ? "up" : "down") + '">' + fmt(tr.price, dpp() + 2) + '</span><span class="num">' + fmt(tr.amt, 0) + '</span><span class="num">' + pp(d.getHours()) + ":" + pp(d.getMinutes()) + ":" + pp(d.getSeconds()) + '</span></div>';
  }
  function renderTradeList(flashFirst) {
    $("trades").innerHTML = pro.trades.map(function (tr, i) { return tradeRowHTML(tr, flashFirst && i === 0); }).join("");
  }

  function seedCandles() {
    var v = TF[pro.tf].vol, price = baseMid() * (1 + (Math.random() - 0.5) * 0.05 * v), i;
    pro.candles = []; pro.formTicks = 0;
    for (i = 0; i < 38; i++) {
      var o = price, c = o * (1 + (Math.random() - 0.5) * 0.012 * v);
      var h = Math.max(o, c) * (1 + Math.random() * 0.006 * v), l = Math.min(o, c) * (1 - Math.random() * 0.006 * v);
      pro.candles.push({ o: o, h: h, l: l, c: c }); price = c;
    }
    pro.mid = price; lastShown = price;
  }
  var SVGNS = "http://www.w3.org/2000/svg";
  function renderCandles() {
    var cs = pro.candles; if (!cs.length || !$("price-svg")) return;
    var lo = Infinity, hi = -Infinity;
    cs.forEach(function (c) { if (c.l < lo) lo = c.l; if (c.h > hi) hi = c.h; });
    if (hi <= lo) hi = lo * 1.001;
    var rng = hi - lo; hi += rng * 0.08; lo -= rng * 0.08;        // headroom so candles don't touch edges
    var W = 1000, H = 300, n = cs.length, slot = W / n, bodyW = Math.max(4, slot * 0.68);
    function Y(pr) { return (hi - pr) / (hi - lo) * H; }

    // gridlines + y-axis labels (4 levels)
    var grid = $("grid"); grid.textContent = "";
    var yax = $("chart-yaxis"); yax.textContent = "";
    for (var k = 0; k <= 4; k++) {
      var gp = lo + (hi - lo) * k / 4, gy = Y(gp);
      var ln = document.createElementNS(SVGNS, "line"); ln.setAttribute("class", "candle-grid");
      ln.setAttribute("x1", "0"); ln.setAttribute("x2", String(W)); ln.setAttribute("y1", gy.toFixed(1)); ln.setAttribute("y2", gy.toFixed(1));
      grid.appendChild(ln);
      var lab = document.createElement("span"); lab.textContent = fmt(gp, dpp() + 2);
      lab.style.top = (gy / H * 100) + "%"; yax.appendChild(lab);
    }

    // candles
    var g = $("candles"); g.textContent = "";
    cs.forEach(function (c, i) {
      var cx = i * slot + slot / 2, up = c.c >= c.o;
      var grp = document.createElementNS(SVGNS, "g"); grp.setAttribute("class", up ? "candle candle-up" : "candle candle-down");
      var wick = document.createElementNS(SVGNS, "line"); wick.setAttribute("class", "candle-wick");
      wick.setAttribute("x1", cx.toFixed(1)); wick.setAttribute("x2", cx.toFixed(1)); wick.setAttribute("y1", Y(c.h).toFixed(1)); wick.setAttribute("y2", Y(c.l).toFixed(1));
      var yo = Y(c.o), yc = Y(c.c), top = Math.min(yo, yc), hgt = Math.max(1.5, Math.abs(yo - yc));
      var body = document.createElementNS(SVGNS, "rect"); body.setAttribute("class", "candle-body");
      body.setAttribute("x", (cx - bodyW / 2).toFixed(1)); body.setAttribute("width", bodyW.toFixed(1)); body.setAttribute("y", top.toFixed(1)); body.setAttribute("height", hgt.toFixed(1));
      grp.appendChild(wick); grp.appendChild(body); g.appendChild(grp);
    });

    // last price line + tag
    var last = cs[cs.length - 1].c, ly = Y(last);
    $("price-last-line").setAttribute("y1", ly.toFixed(1)); $("price-last-line").setAttribute("y2", ly.toFixed(1));
    var tag = $("price-tag"); tag.textContent = fmt(last, dpp() + 2); tag.style.top = (ly / H * 100) + "%";
  }

  function placeOrder() {
    var price = parseFloat($("o-price").value) || 0, amt = parseFloat($("o-amount").value) || 0;
    if (price <= 0 || amt <= 0) return;
    pro.orders.unshift({ id: ++pro.orderId, side: pro.side, coin: pro.coin, price: price, amt: amt, total: price * amt });
    if (pro.orders.length === 1) $("pro-reveal").hidden = false;
    renderOrders();
    $("orders-card").scrollIntoView({ behavior: "smooth", block: "nearest" });
  }
  function renderOrders() {
    if (!$("orders-body")) return;
    var n = pro.orders.length;
    $("orders-count").textContent = n ? "(" + n + ")" : "";
    $("orders-empty").hidden = n > 0;
    $("orders-body").innerHTML = pro.orders.map(function (o) {
      return '<div class="orders-row"><span class="side-' + o.side + '">' + (o.side === "buy" ? "Buy" : "Sell") + '</span><span>XMZ / ' + o.coin + '</span>' +
        '<span class="num">' + fmt(o.price, COINS[o.coin].dp + 2) + '</span><span class="num">' + fmt(o.amt, 0) + '</span>' +
        '<span class="num">' + fmt(o.total, COINS[o.coin].dp) + ' ' + o.coin + '</span>' +
        '<button class="order-cancel" type="button" data-id="' + o.id + '">Cancel</button></div>';
    }).join("");
  }

  function reduceMotion() { return window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches; }
  function simTick() {
    pro.mid = mid() * (1 + (Math.random() - 0.5) * 0.006);
    var cur = pro.candles[pro.candles.length - 1];
    if (cur) { cur.c = pro.mid; if (pro.mid > cur.h) cur.h = pro.mid; if (pro.mid < cur.l) cur.l = pro.mid; }
    if (++pro.formTicks >= TF[pro.tf].tpc) { pro.formTicks = 0; pro.candles.push({ o: pro.mid, h: pro.mid, l: pro.mid, c: pro.mid }); if (pro.candles.length > 38) pro.candles.shift(); }
    pro.chg24 += (Math.random() - 0.5) * 0.15;
    pro.vol += Math.random() * 500;
    renderBookRows();
    renderTicker(true);
    pro.trades.unshift(makeTrade(Date.now()));
    if (pro.trades.length > 18) pro.trades.pop();
    renderTradeList(true);
    renderCandles();
  }
  function startSim() { stopSim(); if (!reduceMotion() && !document.hidden && !$("view-pro").hidden) pro.timer = setInterval(simTick, 1900); }
  function stopSim() { if (pro.timer) { clearInterval(pro.timer); pro.timer = null; } }

  function syncOrderUnits() {
    $("o-price-unit").textContent = pro.coin;
    $("o-total-unit").textContent = pro.coin;
    if (!$("o-price").value) $("o-price").value = fmt(mid(), dpp());
    $("pro-pair").innerHTML = pDot("XMZ") + '<span>XMZ / ' + pro.coin + '</span><span class="ccaret">▾</span>';
    $("o-place").textContent = (pro.side === "buy" ? "Place buy order" : "Place sell order");
    var cs = $("chart-sub"); if (cs) cs.textContent = "XMZ / " + pro.coin + " \u00b7 indicative";
    orderCalc();
  }
  function orderCalc() {
    var p = parseFloat($("o-price").value) || 0, a = parseFloat($("o-amount").value) || 0;
    $("o-total").value = fmt(p * a, dpp());
  }

  // Pair dropdown for pro mode
  var menu = null;
  function closeMenu() { if (menu) { menu.remove(); menu = null; } $("pro-pair").setAttribute("aria-expanded", "false"); }
  function openPairMenu() {
    if (menu) { closeMenu(); return; }
    menu = document.createElement("div"); menu.className = "coin-menu"; menu.setAttribute("role", "listbox");
    COUNTERS.forEach(function (c) {
      var b = document.createElement("button"); b.type = "button"; b.setAttribute("role", "option");
      if (c === pro.coin) b.setAttribute("aria-selected", "true");
      b.innerHTML = pDot(c) + "<span>XMZ / " + c + "</span><small>" + NAME[c] + "</small>";
      b.addEventListener("click", function () { pro.coin = c; $("o-price").value = ""; renderBook(); closeMenu(); });
      menu.appendChild(b);
    });
    var host = $("view-pro"); host.style.position = "relative";
    var pr = $("pro-pair").getBoundingClientRect(), hr = host.getBoundingClientRect();
    menu.style.top = (pr.bottom - hr.top + 6) + "px"; menu.style.left = (pr.left - hr.left) + "px";
    host.appendChild(menu); $("pro-pair").setAttribute("aria-expanded", "true");
  }

  function setMode(prov) {
    var proOn = prov === "pro";
    $("view-simple").hidden = proOn;
    $("view-pro").hidden = !proOn;
    $("mode-simple").classList.toggle("on", !proOn);
    $("mode-pro").classList.toggle("on", proOn);
    $("mode-simple").setAttribute("aria-selected", String(!proOn));
    $("mode-pro").setAttribute("aria-selected", String(proOn));
    if (proOn && !pro.rendered) { pro.rendered = true; renderBook(); }
    if (proOn) startSim(); else stopSim();
  }

  $("mode-simple").addEventListener("click", function () { setMode("simple"); if (history.replaceState) history.replaceState(null, "", location.pathname); });
  $("mode-pro").addEventListener("click", function () { setMode("pro"); if (history.replaceState) history.replaceState(null, "", "#pro"); });
  // Shareable deep link: /dex/#pro opens the pro view.
  window.addEventListener("hashchange", function () { setMode(location.hash === "#pro" ? "pro" : "simple"); });
  if (location.hash === "#pro") setMode("pro");
  $("pro-pair").addEventListener("click", function (e) { e.stopPropagation(); openPairMenu(); });
  $("o-price").addEventListener("input", orderCalc);
  $("o-amount").addEventListener("input", orderCalc);
  $("ot-buy").addEventListener("click", function () { pro.side = "buy"; this.classList.add("on"); $("ot-sell").classList.remove("on"); syncOrderUnits(); });
  $("ot-sell").addEventListener("click", function () { pro.side = "sell"; this.classList.add("on"); $("ot-buy").classList.remove("on"); syncOrderUnits(); });
  $("tf-group").addEventListener("click", function (e) {
    var b = e.target.closest && e.target.closest("button[data-tf]"); if (!b) return;
    pro.tf = b.getAttribute("data-tf");
    Array.prototype.forEach.call(this.children, function (x) { x.classList.toggle("on", x === b); });
    seedCandles(); renderCandles();
  });
  $("o-place").addEventListener("click", placeOrder);
  $("orders-body").addEventListener("click", function (e) { var b = e.target.closest && e.target.closest(".order-cancel"); if (!b) return; var id = +b.getAttribute("data-id"); pro.orders = pro.orders.filter(function (o) { return o.id !== id; }); renderOrders(); });
  $("view-pro").addEventListener("click", function (e) { var row = e.target.closest && e.target.closest(".book-row"); if (row) { $("o-price").value = fmt(parseFloat(row.getAttribute("data-price")), dpp()); orderCalc(); } });
  document.addEventListener("visibilitychange", function () { if (!$("view-pro").hidden) { if (document.hidden) stopSim(); else startSim(); } });
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
      if (notice) notice.innerHTML = '<strong>Live.</strong> Trade XMZ for BTC or SOL (0.1%–0.5%) or Litecoin (free) from your own wallet in the Monzero app. <a href="/dex/recovery/">How to recover your funds →</a>';
    })
    .catch(function () { /* stays "Coming soon" */ });
})();
