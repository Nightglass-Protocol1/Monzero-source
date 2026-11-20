// Coin-for-coin trades on the DEX page: ETH, BNB and SOL swapped straight across, no XMZ leg.
//
// The page is only a screen for the Monzero wallet it is connected to (the connection the order
// book's trade panel made, kept in this browser). Everything shown comes from the wallet's own
// checked view (/v1/status "direct"); the page may ask the wallet to post, take or cancel an
// offer. It can never approve a trade: the wallet shows the terms it holds and the user approves
// there. When a trade finishes, the wallet pays what it brought in to the account accepted for
// this page. Hidden unless the connected wallet reports the "direct" feature.
(function () {
  "use strict";
  var box = document.getElementById("direct-panel"), section = document.getElementById("view-direct");
  if (!box || !section) return;

  var PHONE = /Android/i.test(navigator.userAgent || "");
  var LOOK = PHONE ? "Tap the Monzero notification on this phone" : "Look at your Monzero wallet";
  var STORE = "monzero-dex-link", PORTS = [38471, 38472, 38473, 38474, 38475];
  (function () { var m = /[?&]wallet=(3847[1-5])(&|$)/.exec(location.search); if (m) { PORTS = [Number(m[1])]; STORE += "-" + m[1]; } })();
  var HOSTS = PHONE ? ["127.0.0.1", "localhost"] : ["127.0.0.1"], hostAt = 0;
  var NETWORKS = {
    1: { name: "Ethereum", hex: "0x1", tx: "https://etherscan.io/tx/" },
    56: { name: "BNB Smart Chain", hex: "0x38", tx: "https://bscscan.com/tx/",
          add: { chainId: "0x38", chainName: "BNB Smart Chain", nativeCurrency: { name: "BNB", symbol: "BNB", decimals: 18 }, rpcUrls: ["https://bsc-dataseed.binance.org"], blockExplorerUrls: ["https://bscscan.com"] } },
    11155111: { name: "Sepolia (test network)", hex: "0xaa36a7", tx: "https://sepolia.etherscan.io/tx/" },
    97: { name: "BNB Smart Chain Testnet", hex: "0x61", tx: "https://testnet.bscscan.com/tx/" },
    501000101: { name: "Solana", tx: "https://solscan.io/tx/" },
    501000102: { name: "Solana Devnet (test network)", tx: "https://solscan.io/tx/", txEnd: "?cluster=devnet" },
    501000103: { name: "Solana (local test)", tx: "https://solscan.io/tx/" }
  };
  var st = { link: null, status: null, pair: "", give: "", note: "", error: "", busy: "", confirm: "", form: { give: "", get: "", hours: "24" } };

  function esc(s) { return String(s == null ? "" : s).replace(/[&<>"']/g, function (c) { return "&#" + c.charCodeAt(0) + ";"; }); }
  function short(a) { return a ? a.slice(0, 6) + "…" + a.slice(-4) : ""; }
  // Coins by market id: a chain's own coin is its chain id; its USDT is a market of its own
  // (Solana's: the chain id + 100; BNB Chain's: + 100,000,000), as in the XMZ pairs
  function chainOfMarket(id) { id = Number(id); return id >= 501000201 && id <= 501000203 ? id - 100 : id >= 100000000 && id < 200000000 ? id - 100000000 : id; }
  function solana(id) { id = chainOfMarket(id); return id >= 501000101 && id <= 501000103; }
  function net(id) { return (NETWORKS[chainOfMarket(id)] || {}).name || "chain " + id; }
  function label(coin) { return coin === "USDT" ? "USDT-SOL" : coin; }
  function pairLabel(p) { var x = String(p).split("/"); return label(x[0]) + " / " + label(x[1]); }
  function units(text, places) {
    var d = String(text || "0");
    if (!/^[0-9]+$/.test(d)) return "?";
    while (d.length <= places) d = "0" + d;
    var whole = d.slice(0, d.length - places), part = d.slice(d.length - places).replace(/0+$/, "");
    return part === "" ? whole : whole + "." + part;
  }
  function price(x) { if (!(x > 0)) return "—"; var p = Math.max(0, Math.min(18, 5 - Math.floor(Math.log10(x)))); return x.toFixed(p).replace(/\.?0+$/, ""); }
  function timeLeft(at) {
    var s = Math.max(0, Math.round(at - Date.now() / 1000));
    return s >= 86400 ? Math.floor(s / 86400) + "d left" : s >= 3600 ? Math.floor(s / 3600) + "h left" : Math.max(1, Math.floor(s / 60)) + "m left";
  }
  function loadLink() {
    try { st.link = JSON.parse(localStorage.getItem(STORE) || "null"); } catch (e) { st.link = null; }
    if (st.link && (PORTS.indexOf(st.link.port) < 0 || !st.link.token)) st.link = null;
  }
  function api(path, body) {
    var headers = { authorization: "Bearer " + st.link.token };
    if (body) headers["content-type"] = "application/json";
    function go(n) {
      var i = (hostAt + n) % HOSTS.length, ctl = new AbortController(), t = setTimeout(function () { ctl.abort(); }, 8000);
      return fetch("http://" + HOSTS[i] + ":" + st.link.port + path, { method: body ? "POST" : "GET", headers: headers, body: body ? JSON.stringify(body) : undefined, cache: "no-store", signal: ctl.signal })
        .then(function (r) { hostAt = i; return r.json(); }, function (e) { if (n + 1 < HOSTS.length && !ctl.signal.aborted) return go(n + 1); throw e; })
        .finally(function () { clearTimeout(t); });
    }
    return go(0);
  }
  function direct() { return (st.status && st.status.direct) || null; }
  function enabled() { var f = st.status && st.status.features; return !!(f && f.indexOf && f.indexOf("direct") >= 0 && direct()); }
  function chainOf(coin) { var c = (direct() || {}).chains || []; for (var i = 0; i < c.length; ++i) if (c[i].coin === coin) return Number(c[i].chainId); return 0; }
  function own(chainId) { var s = st.status || {}; return (solana(chainId) ? s.solAccount : s.account) || ""; }
  function tradeAccount(chainId) { var w = (st.status || {}).wallet || {}; return (solana(chainId) ? w.solTradeAccount : w.tradeAccount) || ""; }
  function say(text) { st.note = text; st.error = ""; render(); }
  function fail(e) { st.busy = ""; st.error = (e && e.message) || String(e); st.note = ""; render(); }

  // What a trade is doing now, in plain words (the wallet's stage names)
  function stageText(t) {
    var tr = t.trade || {}, s = tr.stage || "", leader = t.kind === "take", give = (t.gives || {}).coin || "", get = (t.wants || {}).coin || "";
    if (t.state === "open") return t.kind === "offer" ? "open · " + timeLeft(t.expiresAt) : "waiting for the maker's answer";
    if (t.state === "cancelled") return "cancelled";
    if (t.state === "expired") return "expired, not taken";
    if (t.state === "refused") return "not matched: " + (t.detail || "the maker refused");
    if (s === "Done(Swapped)") return "swapped: you received " + get;
    if (s === "Done(Refunded)") return "refunded: your " + give + " came back";
    if (s === "Done(Aborted)") return "stopped before anything was locked";
    if (s === "Done(Missed)") return "ended without a swap";
    if (leader && tr.approved !== true) return "matched: " + LOOK.toLowerCase() + " and approve the trade there";
    switch (s) {
      case "Start": case "LockSent": return leader ? "locking your " + give + "…" : "waiting for the taker to lock " + get;
      case "Locked": case "ReadySent": return leader ? "your " + give + " is locked" : "your " + give + " is locked; waiting for the taker to collect it";
      case "Waiting": return "your " + give + " is locked; waiting for the maker to lock " + get;
      case "WaitingForA": return "waiting for the taker to lock " + get;
      case "Ready": return "your " + give + " is locked; waiting for the taker to collect it";
      case "ClaimSent": case "Claiming": return "collecting your " + get + "…";
      case "GaveUp": return "the other side did not lock in time: your " + give + " comes back after its time limit";
      case "RefundSent": return "taking your " + give + " back…";
    }
    return s || "starting…";
  }

  // Paying a missing amount into the trade account from MetaMask (ETH, BNB); a Solana amount is
  // shown with the account to send it to
  function payEvm(chainId, to, wei, token) {
    var n = NETWORKS[chainId];
    if (!window.ethereum || !n || !n.hex) return Promise.reject(new Error("MetaMask is not in this browser."));
    var eth = function (m, p) { return window.ethereum.request({ method: m, params: p || [] }); };
    return eth("eth_requestAccounts").then(function (a) {
      if (!a || !a[0] || a[0].toLowerCase() !== String(st.status.account || "").toLowerCase()) throw new Error("Switch MetaMask to the account you connected (" + short(st.status.account) + ").");
      return eth("eth_chainId");
    }).then(function (now) {
      if (now === n.hex) return;
      return eth("wallet_switchEthereumChain", [{ chainId: n.hex }]).catch(function (e) {
        if (e && e.code === 4902 && n.add) return eth("wallet_addEthereumChain", [n.add]);
        throw e;
      });
    }).then(function () {
      if (!token || /^(0x)?0*$/.test(token)) return eth("eth_sendTransaction", [{ from: st.status.account, to: to, value: "0x" + BigInt(wei).toString(16) }]);
      // A token (USDT on BNB Chain): transfer(to, amount) on the token contract
      var pad = function (h) { return String(h).replace(/^0x/, "").toLowerCase().padStart(64, "0"); };
      return eth("eth_sendTransaction", [{ from: st.status.account, to: token, data: "0xa9059cbb" + pad(to) + pad(BigInt(wei).toString(16)) }]);
    });
  }

  function refresh() {
    loadLink();
    if (!st.link) { st.status = null; render(); return Promise.resolve(); }
    return api("/v1/status").then(function (s) { st.status = s && s.ok ? s : null; render(); }).catch(function () { st.status = null; render(); });
  }
  function act(path, body, done) {
    st.busy = path; st.error = ""; render();
    return api(path, body).then(function (r) {
      st.busy = "";
      if (!r || !r.ok) throw new Error((r && (r.message || r.error)) || "The wallet could not do that now.");
      say(done);
      return refresh();
    }).catch(fail);
  }

  function bookHTML(d, base, quote) {
    var mineAcct = String(((st.status || {}).wallet || {}).tradeAccount || "").toLowerCase();
    var now = Date.now() / 1000;
    var list = (d.listings || []).filter(function (l) { return l.pair === st.pair && l.expiresAt > now; });
    var asks = list.filter(function (l) { return l.side === "sell"; }).sort(function (a, b) { return Number(a.price) - Number(b.price); });
    var bids = list.filter(function (l) { return l.side === "buy"; }).sort(function (a, b) { return Number(b.price) - Number(a.price); });
    function rows(set, title, verb) {
      var h = "<h3>" + esc(title) + "</h3>";
      if (!set.length) return h + '<p class="tp-note">none</p>';
      return h + '<ul class="tp-offers">' + set.slice(0, 8).map(function (l) {
        var mine = String(l.maker || "").toLowerCase() === mineAcct, confirming = st.confirm === l.id;
        return "<li><span>" + esc(l.base) + " " + esc(label(base)) + " for " + esc(l.quote) + " " + esc(label(quote)) + '<span class="tp-min">' + esc(l.price) + " " + esc(label(quote)) + " per " + esc(label(base)) + " · " + timeLeft(l.expiresAt) + "</span>" +
          (confirming ? '<span class="tp-min">You lock ' + esc(l.wants.amount) + " " + esc(label(l.wants.coin)) + " and receive " + esc(l.gives.amount) + " " + esc(label(l.gives.coin)) +
            ". Your wallet asks you to approve before anything is locked; if the maker then does not lock theirs, your " + esc(label(l.wants.coin)) + " comes back after about 9 hours.</span>" : "") + "</span>" +
          (mine ? '<span class="tp-min">yours</span>' : '<span class="tp-take"><button class="button ' + (confirming ? "primary" : "secondary") + '" type="button" data-act="take" data-id="' + esc(l.id) + '"' + (st.busy || d.keys !== true ? " disabled" : "") + ">" +
            (confirming ? "Confirm" : verb) + "</button></span>") + "</li>";
      }).join("") + "</ul>";
    }
    return rows(asks, "Selling " + label(base), "Buy " + label(base)) + rows(bids, "Buying " + label(base), "Sell " + label(base));
  }

  function formHTML(base, quote) {
    var give = st.give === quote ? quote : base, get = give === base ? quote : base;
    var g = Number(st.form.give), w = Number(st.form.get);
    var line = g > 0 && w > 0 ? price(give === base ? w / g : g / w) + " " + label(quote) + " per " + label(base) : "";
    return "<h3>Post an offer</h3>" +
      '<div class="tp-side"><button type="button" data-act="give" data-coin="' + esc(base) + '"' + (give === base ? ' class="on"' : "") + ">I give " + esc(label(base)) + '</button><button type="button" data-act="give" data-coin="' + esc(quote) + '"' + (give === quote ? ' class="on"' : "") + ">I give " + esc(label(quote)) + "</button></div>" +
      '<div class="tp-form"><label>You give (' + esc(label(give)) + ')<input id="dd-give" inputmode="decimal" autocomplete="off" value="' + esc(st.form.give) + '"></label>' +
      "<label>You get (" + esc(label(get)) + ')<input id="dd-get" inputmode="decimal" autocomplete="off" value="' + esc(st.form.get) + '"></label>' +
      '<label>Open for<select id="dd-hours">' + [["6", "6 hours"], ["24", "1 day"], ["168", "1 week"]].map(function (o) { return '<option value="' + o[0] + '"' + (st.form.hours === o[0] ? " selected" : "") + ">" + o[1] + "</option>"; }).join("") + "</select></label></div>" +
      '<p class="tp-note" id="dd-price">' + (line ? "Price: " + esc(line) : "") + "</p>" +
      '<button class="button primary" type="button" data-act="post" data-give="' + esc(give) + '" data-get="' + esc(get) + '"' + (st.busy || !(g > 0 && w > 0) ? " disabled" : "") + ">Post: give " + esc(label(give)) + " for " + esc(label(get)) + "</button>" +
      '<p class="tp-note">Posting is your approval: when someone takes the offer, your wallet locks your ' + esc(label(give)) + " once they have locked their " + esc(label(get)) + ", and collects it. Keep the wallet running while the offer is open, with a little of each chain's own coin in its trade account for the network fees.</p>";
  }

  function tradesHTML(d) {
    var order = { agreed: 0, open: 1, done: 2 };
    var mine = (d.mine || []).slice().sort(function (a, b) { var x = order[a.state] !== undefined ? order[a.state] : 3, y = order[b.state] !== undefined ? order[b.state] : 3; return x - y || b.expiresAt - a.expiresAt; });
    if (!mine.length) return "";
    return "<h3>Your coin-for-coin trades</h3>" + '<ul class="tp-offers">' + mine.slice(0, 12).map(function (t) {
      var tr = t.trade || {}, need = tr.awaitingFunding, paid = tr.payout && (tr.payout.tx || tr.payout.note);
      var h = "<li><span>" + esc(pairLabel(t.pair)) + ": give " + esc(t.gives.amount) + " " + esc(label(t.gives.coin)) + " → get " + esc(t.wants.amount) + " " + esc(label(t.wants.coin)) +
        '<span class="tp-min">' + esc(stageText(t)) + "</span>";
      if (need) {
        h += '<span class="tp-min tp-warn">Waiting for funds: ' + esc(need.text) + "</span>";
      }
      if (paid) {
        var n = NETWORKS[chainOfMarket(((tr.stage === "Done(Swapped)" ? t.wants : t.gives) || {}).chainId)] || {};
        h += '<span class="tp-min">Sent to ' + esc(short(tr.payout.to)) + (tr.payout.tx && n.tx ? ' · <a href="' + esc(n.tx + tr.payout.tx + (n.txEnd || "")) + '" rel="noopener" target="_blank">transaction</a>' : "") + "</span>";
      }
      h += "</span>";
      if (t.state === "open") h += '<span class="tp-take"><button class="button secondary" type="button" data-act="cancel" data-folder="' + esc(t.folder) + '"' + (st.busy ? " disabled" : "") + ">" + (t.kind === "offer" ? "Cancel" : "Stop waiting") + "</button></span>";
      else if (need && !solana(need.chainId)) h += '<span class="tp-take"><button class="button primary" type="button" data-act="fund" data-chain="' + esc(need.chainId) + '" data-to="' + esc(need.account) + '" data-wei="' + esc(need.missing) + '" data-token="' + esc(need.asset || "") + '"' + (st.busy ? " disabled" : "") + ">Pay with MetaMask</button></span>";
      return h + "</li>";
    }).join("") + "</ul>";
  }

  function render() {
    var d = direct();
    section.hidden = !enabled();
    if (section.hidden) return;
    var pairs = d.pairs || [];
    if (pairs.indexOf(st.pair) < 0) st.pair = pairs[0] || "";
    var base = st.pair.split("/")[0] || "", quote = st.pair.split("/")[1] || "";
    var h = '<p class="pro-order-lede">Swap ETH, BNB, SOL and USDT straight across, with no XMZ in between. Each side locks its coin in the swap contract on its own chain and one secret releases both; if the other side does not follow through, your coin comes back after its time limit. Your Monzero wallet runs it and asks you to approve each trade you take.</p>';
    h += '<div class="dd-pairs">' + pairs.map(function (p) { return '<button type="button" data-act="pair" data-pair="' + esc(p) + '"' + (p === st.pair ? ' class="on"' : "") + ">" + esc(pairLabel(p)) + "</button>"; }).join("") + "</div>";
    if (d.keys !== true) h += '<p class="tp-act">Unlock your trading keys in the wallet to trade (the order book panel above asks for it).</p>';
    if ((d.state === "failed" || d.state === "refused") && d.error && (d.command === "directListings" || d.command === "directPost" || d.command === "directTake" || d.command === "directPayout"))
      h += '<p class="tp-err">' + (d.command === "directListings" ? "The offers could not be loaded: " : "Not done: ") + esc(d.error) + "</p>";
    if (d.message) h += '<p class="tp-err">' + esc(d.message) + "</p>";
    h += '<div class="dd-grid"><div>' + bookHTML(d, base, quote) + "</div><div>" + formHTML(base, quote) + "</div></div>";
    h += tradesHTML(d);
    var accts = (d.chains || []).map(function (c) { var a = tradeAccount(c.chainId); return a ? "<li><span>" + esc(label(c.coin)) + " trade account (" + esc(net(c.chainId)) + ")</span><strong>" + esc(a) + "</strong></li>" : ""; }).join("");
    if (accts) h += '<h3>Trade accounts</h3><ul class="relay-facts">' + accts + '</ul><p class="tp-note">Your coin is locked from, and paid into, the wallet\'s trade account on each chain. What a finished trade brings in is sent on to ' +
      (own(1) || own(501000101) ? "the MetaMask or Phantom account you connected" : "your connected wallet") + ".</p>";
    if (st.error) h += '<p class="tp-err">' + esc(st.error) + "</p>";
    else if (st.note) h += '<p class="tp-say">' + esc(st.note) + "</p>";
    box.innerHTML = h;
  }

  box.addEventListener("click", function (e) {
    var b = e.target.closest("[data-act]");
    if (!b || b.disabled) return;
    var a = b.getAttribute("data-act");
    if (a === "pair") { st.pair = b.getAttribute("data-pair"); st.confirm = ""; st.give = ""; render(); }
    else if (a === "give") { st.give = b.getAttribute("data-coin"); render(); }
    else if (a === "take") {
      var id = b.getAttribute("data-id");
      if (st.confirm !== id) { st.confirm = id; render(); return; }
      st.confirm = "";
      act("/v1/direct/take", { id: id }, "Taken. " + LOOK + ": once the maker answers, it shows the trade for you to approve.");
    }
    else if (a === "post") {
      var give = b.getAttribute("data-give"), get = b.getAttribute("data-get");
      act("/v1/direct/offer", { givesChainId: chainOf(give), gives: st.form.give.trim(), wantsChainId: chainOf(get), wants: st.form.get.trim(), hours: Number(st.form.hours) },
        "Offer posted. Keep your Monzero wallet running while it is open.").then(function () { if (!st.error) { st.form.give = ""; st.form.get = ""; render(); } });
    }
    else if (a === "cancel") act("/v1/direct/cancel", { folder: b.getAttribute("data-folder") }, "Done.");
    else if (a === "fund") {
      st.busy = "fund"; render();
      payEvm(Number(b.getAttribute("data-chain")), b.getAttribute("data-to"), b.getAttribute("data-wei"), b.getAttribute("data-token"))
        .then(function () { st.busy = ""; say("Paid in. The trade continues by itself."); }).catch(fail);
    }
  });
  box.addEventListener("input", function (e) {
    if (e.target.id === "dd-give") st.form.give = e.target.value;
    else if (e.target.id === "dd-get") st.form.get = e.target.value;
    else return;
    // Not a full redraw (it would take the focus from the field): the button and the price only
    var g = Number(st.form.give), w = Number(st.form.get), post = box.querySelector('[data-act="post"]'), p = box.querySelector("#dd-price");
    if (post) post.disabled = !!st.busy || !(g > 0 && w > 0);
    if (p && post) {
      var base = st.pair.split("/")[0], quote = st.pair.split("/")[1], give = post.getAttribute("data-give");
      p.textContent = g > 0 && w > 0 ? "Price: " + price(give === base ? w / g : g / w) + " " + quote + " per " + base : "";
    }
  });
  box.addEventListener("change", function (e) { if (e.target.id === "dd-hours") st.form.hours = e.target.value; });
  // Re-rendering replaces the inputs: keep typing undisturbed by refreshing only when no field has focus
  setInterval(function () {
    if (document.hidden) return;
    var f = document.activeElement;
    if (f && box.contains(f) && /^(INPUT|SELECT)$/.test(f.tagName)) return;
    refresh();
  }, 6000);
  setInterval(function () { if (!document.hidden && enabled()) api("/v1/direct/refresh", {}).catch(function () {}); }, 20000);
  refresh().then(function () { if (enabled()) api("/v1/direct/refresh", {}).catch(function () {}); });
})();
