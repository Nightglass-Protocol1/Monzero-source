/* Monzero /markets/ — the XMZ price on NoirTrade (/markets/xmz.php), and live prices
   of the assets XMZ swaps against with an indexed 7-day chart (/markets/api.php,
   CoinGecko upstream, cached). */
(function () {
  "use strict";
  // Categorical palette (validated, dark-surface variants), fixed per coin — never by rank.
  var COLOR = { BTC: "#3987e5", LTC: "#d95926", BNB: "#199e70", ETH: "#c98500", SOL: "#d55181" };
  var ORDER = ["BTC", "LTC", "BNB", "ETH", "SOL"];

  function usd(v) {
    if (v == null) return "—";
    var d = v < 1 ? 5 : v < 100 ? 3 : 2;
    return "$" + Number(v).toLocaleString("en-US", { minimumFractionDigits: 2, maximumFractionDigits: d });
  }
  function pct(v) { return v == null ? "—" : (v >= 0 ? "+" : "") + Number(v).toFixed(2) + "%"; }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }

  function renderTable(d) {
    var tb = document.getElementById("mkt-rows");
    if (!tb) return;
    tb.innerHTML = "";
    var bySym = {}; (d.coins || []).forEach(function (c) { bySym[c.symbol] = c; });
    ORDER.forEach(function (sym) {
      var c = bySym[sym]; if (!c) return;
      var tr = el("tr");
      var dot = el("span", "mkt-dot"); dot.style.background = COLOR[sym];
      var name = el("td", "mkt-name"); name.appendChild(dot);
      name.appendChild(el("strong", null, sym)); name.appendChild(el("span", "mkt-full", " " + c.name));
      var price = el("td", "mkt-price num", usd(c.usd));
      var chg = el("td", "num " + (c.change24h == null ? "" : c.change24h >= 0 ? "up" : "down"), pct(c.change24h));
      tr.appendChild(name); tr.appendChild(price); tr.appendChild(chg);
      tb.appendChild(tr);
    });
  }

  function renderChart(d) {
    var canvas = document.getElementById("mkt-chart");
    if (!canvas || typeof Chart === "undefined") return;
    // Labels from the first coin that has history; index each series to 100 at its first point.
    var labels = null;
    var datasets = ORDER.map(function (sym) {
      var h = (d.history && d.history[sym]) || [];
      if (h.length && !labels) labels = h.map(function (p) {
        return new Date(p[0] * 1000).toLocaleDateString("en-US", { month: "short", day: "numeric" });
      });
      var base = h.length ? h[0][1] : 1;
      return {
        label: sym,
        data: h.map(function (p) { return +(p[1] / base * 100).toFixed(2); }),
        borderColor: COLOR[sym], backgroundColor: COLOR[sym],
        borderWidth: 2, pointRadius: 0, pointHoverRadius: 4, tension: 0.25, fill: false,
      };
    }).filter(function (ds) { return ds.data.length; });
    if (!datasets.length) return;
    var ink = "#edf3f7", muted = "#9eb6bd", line = "#28444d";
    new Chart(canvas.getContext("2d"), {
      type: "line",
      data: { labels: labels || [], datasets: datasets },
      options: {
        responsive: true, maintainAspectRatio: false,
        interaction: { mode: "index", intersect: false },
        plugins: {
          legend: { labels: { color: ink, usePointStyle: true, boxWidth: 8, font: { size: 13 } } },
          tooltip: {
            backgroundColor: "#0d1e27", borderColor: line, borderWidth: 1, titleColor: ink, bodyColor: ink,
            callbacks: { label: function (c) { return c.dataset.label + ": " + c.parsed.y.toFixed(1); } },
          },
        },
        scales: {
          x: { grid: { color: line, drawTicks: false }, ticks: { color: muted, maxTicksLimit: 8 }, border: { color: line } },
          y: { grid: { color: line, drawTicks: false }, ticks: { color: muted }, border: { color: line },
               title: { display: true, text: "Indexed to 100 (7 days ago)", color: muted } },
        },
      },
    });
  }

  function renderSwaps(d) {
    var tb = document.getElementById("mkt-swaps");
    if (!tb) return;
    tb.innerHTML = "";
    var badge = { settled: ["Settled on mainnet", "ok"], validating: ["Validating", "warn"], next: ["Next", "muted"] };
    (d.dexSwaps || []).forEach(function (s) {
      var tr = el("tr");
      tr.appendChild(el("td", "mkt-pair", s.pair));
      var b = badge[s.status] || [s.status, "muted"];
      var td = el("td"); var sp = el("span", "mkt-badge " + b[1], b[0] + (s.date ? " · " + s.date : "")); td.appendChild(sp);
      tr.appendChild(td);
      tr.appendChild(el("td", "mkt-settled", s.settled || "—"));
      tb.appendChild(tr);
    });
  }

  function load() {
    fetch("/markets/api.php", { cache: "no-store" })
      .then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); })
      .then(function (d) {
        renderTable(d); renderChart(d); renderSwaps(d);
        var u = document.getElementById("mkt-updated");
        if (u && d.updated) u.textContent = "Live · updated " + new Date(d.updated).toLocaleString() + " · source " + (d.source || "");
      })
      .catch(function () {
        var s = document.getElementById("mkt-updated");
        if (s) s.textContent = "Live price data is unavailable right now — try again shortly.";
      });
  }
  /* ---- XMZ price on NoirTrade (/markets/xmz.php) ---- */
  var ACCENT = "#3fdfe8", xmzChart = null;
  function id(x) { return document.getElementById(x); }
  function set(x, text, cls) { var e = id(x); if (!e) return; e.textContent = text; if (cls != null) e.className = "mkt-stat-value " + cls; }
  function price(v) { return v == null ? "—" : Number(v).toFixed(8).replace(/0+$/, "").replace(/\.$/, ""); }
  function amount(v, max) { return v == null ? "—" : Number(v).toLocaleString("en-US", { maximumFractionDigits: max }); }
  function when(ms) { return new Date(ms).toLocaleString(undefined, { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" }); }

  function renderBook(tbodyId, rows, cls) {
    var tb = id(tbodyId); if (!tb) return;
    tb.innerHTML = "";
    if (!rows.length) { var tr0 = el("tr"); var td0 = el("td", "mkt-full", "No orders"); td0.colSpan = 2; tr0.appendChild(td0); tb.appendChild(tr0); return; }
    rows.forEach(function (r) {
      var tr = el("tr");
      tr.appendChild(el("td", cls, price(r[0])));
      tr.appendChild(el("td", "num", amount(r[1], 2)));
      tb.appendChild(tr);
    });
  }

  function renderTrades(trades) {
    var tb = id("xmz-trades"); if (!tb) return;
    tb.innerHTML = "";
    if (!trades.length) { var tr0 = el("tr"); var td0 = el("td", "mkt-full", "No trades yet"); td0.colSpan = 4; tr0.appendChild(td0); tb.appendChild(tr0); return; }
    trades.slice(-50).reverse().forEach(function (t) {
      var tr = el("tr");
      tr.appendChild(el("td", null, when(t[0])));
      tr.appendChild(el("td", "num", price(t[1])));
      tr.appendChild(el("td", "num", amount(t[2], 2)));
      tr.appendChild(el("td", "num", amount(t[3], 4)));
      tb.appendChild(tr);
    });
  }

  function renderXmzChart(trades) {
    var canvas = id("xmz-chart");
    if (!canvas || typeof Chart === "undefined") return;
    var pts = trades.map(function (t) { return { x: t[0], y: t[1] }; });
    if (xmzChart) { xmzChart.data.datasets[0].data = pts; xmzChart.update("none"); return; }
    if (pts.length < 2) { var n = id("xmz-chart-note"); if (n) n.textContent = "There are not enough trades yet to draw a chart."; return; }
    var ink = "#edf3f7", muted = "#9eb6bd", line = "#28444d";
    xmzChart = new Chart(canvas.getContext("2d"), {
      type: "line",
      data: { datasets: [{ label: "XMZ/USDT", data: pts, borderColor: ACCENT, backgroundColor: ACCENT,
        borderWidth: 2, pointRadius: 2.5, pointHoverRadius: 5, tension: 0, fill: false }] },
      options: {
        responsive: true, maintainAspectRatio: false, parsing: false,
        interaction: { mode: "nearest", intersect: false },
        plugins: {
          legend: { display: false },
          tooltip: {
            backgroundColor: "#0d1e27", borderColor: line, borderWidth: 1, titleColor: ink, bodyColor: ink,
            callbacks: {
              title: function (items) { return items.length ? when(items[0].parsed.x) : ""; },
              label: function (c) { return price(c.parsed.y) + " USDT per XMZ"; },
            },
          },
        },
        scales: {
          x: { type: "linear", grid: { color: line, drawTicks: false }, border: { color: line },
               ticks: { color: muted, maxTicksLimit: 6, maxRotation: 0,
                 callback: function (v) { return when(v); } } },
          y: { beginAtZero: true, grid: { color: line, drawTicks: false }, border: { color: line },
               ticks: { color: muted, callback: function (v) { return price(v); } },
               title: { display: true, text: "USDT per XMZ", color: muted } },
        },
      },
    });
  }

  function renderXmz(d) {
    set("xmz-last", price(d.last));
    var per = id("xmz-per-usd");
    if (per) per.textContent = d.last > 0 ? "1 USDT buys about " + amount(1 / d.last, 0) + " XMZ at that price." : "";
    var c = d.change24hPercent;
    set("xmz-change", c == null ? "—" : pct(c), c == null || c === 0 ? "" : c > 0 ? "mkt-up" : "mkt-down");
    set("xmz-bid", d.bid == null ? "No bids" : price(d.bid));
    set("xmz-ask", d.ask == null ? "No asks" : price(d.ask));
    set("xmz-spread", d.spreadPercent == null ? "—" : Number(d.spreadPercent).toFixed(1) + "%", d.spreadPercent > 10 ? "mkt-warn" : "");
    set("xmz-volume", d.volume24hXmz == null ? "—" : amount(d.volume24hXmz, 0) + " XMZ" + (d.volume24hUsdt ? " (" + amount(d.volume24hUsdt, 2) + " USDT)" : ""));
    set("xmz-range", d.high24h == null || d.low24h == null ? "No trades" : price(d.high24h) + " / " + price(d.low24h));
    set("xmz-last-trade", d.lastTradeAt ? when(Date.parse(d.lastTradeAt)) : "—");
    var notes = [];
    if (d.volume24hXmz === 0) notes.push("No XMZ has traded in the last 24 hours, so the price shown is from the last trade before that.");
    if (d.spreadPercent > 10) notes.push("The best bid and best ask are far apart (" + Number(d.spreadPercent).toFixed(0) + "% of the price between them), so an order placed now can fill a long way from the last price.");
    var caution = id("xmz-caution");
    if (caution) { caution.hidden = !notes.length; caution.textContent = notes.join(" "); }
    var book = d.book || {};
    renderBook("xmz-bids", book.bids || [], "mkt-up");
    renderBook("xmz-asks", book.asks || [], "mkt-down");
    renderTrades(d.trades || []);
    renderXmzChart(d.trades || []);
    var u = id("xmz-updated");
    if (u) {
      var read = new Date(d.updated).toLocaleString();
      u.textContent = d.stale ? "Out of date: NoirTrade could not be reached. These figures were read " + read + "."
                              : "Live · read from NoirTrade " + read;
      u.className = "mkt-status" + (d.stale ? " mkt-warn" : "");
    }
  }

  function loadXmz() {
    fetch("/markets/xmz.php", { cache: "no-store" })
      .then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); })
      .then(renderXmz)
      .catch(function () {
        var s = id("xmz-updated");
        if (s && !xmzChart && id("xmz-last").textContent === "—") s.textContent = "The XMZ price is unavailable right now. Try again shortly, or look on NoirTrade directly.";
        else if (s) { s.textContent = "Could not refresh just now; the figures below are from the last successful read."; }
      });
  }

  function start() { load(); loadXmz(); setInterval(function () { if (!document.hidden) loadXmz(); }, 60000); }
  if (document.readyState !== "loading") start();
  else document.addEventListener("DOMContentLoaded", start);
})();
