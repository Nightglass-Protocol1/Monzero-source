/* Monzero /markets/ — live swap-pair prices + indexed 30-day chart.
   Data: /markets/api.php (CoinGecko upstream, cached). XMZ has no market price and
   is not shown here; these are the assets XMZ swaps against. */
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

  function fmtCoinAmt(v) {
    if (v == null) return "—";
    if (v >= 1) return (+v.toFixed(4)).toString();
    var p = Number(v).toPrecision(3);           // 3 sig figs
    if (p.indexOf("e") >= 0) {                    // expand sci-notation to plain decimals
      var dec = Math.min(18, -Math.floor(Math.log10(v)) + 2);
      p = Number(v).toFixed(dec);
    }
    return p.replace(/0+$/, "").replace(/\.$/, "");
  }
  function fmtXmz(v) { return v == null ? "—" : Math.round(v).toLocaleString("en-US"); }

  function renderOpening(d) {
    var tb = document.getElementById("mkt-opening-rows");
    if (!tb) return;
    var pairs = (d.openingRate && d.openingRate.pairs) || [];
    var bySym = {}; pairs.forEach(function (p) { bySym[p.symbol] = p; });
    tb.innerHTML = "";
    ORDER.forEach(function (sym) {
      var p = bySym[sym]; var tr = el("tr");
      var dot = el("span", "mkt-dot"); dot.style.background = COLOR[sym];
      var name = el("td", "mkt-name"); name.appendChild(dot);
      name.appendChild(el("strong", null, "XMZ/" + sym));
      tr.appendChild(name);
      tr.appendChild(el("td", "num", p ? fmtCoinAmt(p.coinPerXmz) + " " + sym : "—"));
      tr.appendChild(el("td", "num", p ? fmtXmz(p.xmzPerCoin) + " XMZ" : "—"));
      tb.appendChild(tr);
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
        renderTable(d); renderOpening(d); renderChart(d); renderSwaps(d);
        var u = document.getElementById("mkt-updated");
        if (u && d.updated) u.textContent = "Live · updated " + new Date(d.updated).toLocaleString() + " · source " + (d.source || "");
      })
      .catch(function () {
        var s = document.getElementById("mkt-updated");
        if (s) s.textContent = "Live price data is unavailable right now — try again shortly.";
      });
  }
  if (document.readyState !== "loading") load();
  else document.addEventListener("DOMContentLoaded", load);
})();
