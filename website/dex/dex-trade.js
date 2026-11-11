/* Trading ETH and BNB for XMZ from this page.

   The page is only the screen. The Monzero wallet on this computer is the engine: it holds the
   swap keys, checks every offer itself, and asks its user to approve each trade from the terms
   it holds. This page talks to it over 127.0.0.1 and can read balances and ask to post or take
   an offer; it cannot approve a trade or choose where proceeds go. MetaMask pays the coin into
   the wallet's trade account, and the wallet sends what is left back to the same MetaMask
   account when the trade is over. */
(function () {
  "use strict";
  var box = document.getElementById("trade-panel");
  if (!box) return;

  var LOCAL = /^(localhost|127\.0\.0\.1)$/.test(location.hostname);
  var TEST = LOCAL && /[?&]testnet=1/.test(location.search);   // test networks only from a local copy of the page
  var CHAINS = TEST
    ? { ETH: { id: 11155111, name: "Sepolia test network", control: null, tx: "https://sepolia.etherscan.io/tx/" },
        BNB: { id: 97, name: "BNB Smart Chain test network", control: null, tx: "https://testnet.bscscan.com/tx/" } }
    : { ETH: { id: 1, name: "Ethereum", control: "eth-mainnet", tx: "https://etherscan.io/tx/" },
        BNB: { id: 56, name: "BNB Smart Chain", control: "bnb-mainnet", tx: "https://bscscan.com/tx/",
               add: { chainId: "0x38", chainName: "BNB Smart Chain", nativeCurrency: { name: "BNB", symbol: "BNB", decimals: 18 },
                      rpcUrls: ["https://bsc-dataseed.binance.org"], blockExplorerUrls: ["https://bscscan.com"] } } };
  // SOL: Solana mainnet opens with its signed control document, like ETH and BNB. On a local copy
  // of the page: devnet, or with ?sol=local a local test validator.
  if (!TEST) CHAINS.SOL = { id: 501000101, name: "Solana", control: "sol-mainnet", sol: true, rpc: "https://solana-rpc.publicnode.com", tx: "https://solscan.io/tx/" };
  else CHAINS.SOL = /[?&]sol=local/.test(location.search)
    ? { id: 501000103, name: "Solana (local test)", control: null, sol: true, rpc: "http://127.0.0.1:18899", tx: "https://solscan.io/tx/" }
    : { id: 501000102, name: "Solana Devnet (test network)", control: null, sol: true, rpc: "https://api.devnet.solana.com", tx: "https://solscan.io/tx/", txEnd: "?cluster=devnet" };
  var PORTS = [38471, 38472, 38473, 38474, 38475];
  // Two wallets on one computer: ?wallet=38472 picks one (the wallet's port is one of the five)
  // Each picked wallet keeps its own saved connection, so two tabs can be linked to two wallets
  var STORE = "monzero-dex-link";
  (function () { var m = /[?&]wallet=(3847[1-5])(&|$)/.exec(location.search); if (m) { PORTS = [Number(m[1])]; STORE += "-" + m[1]; } })();
  var OWNER_GAS = 1500000n, CLAIMER_GAS = 500000n;   // what the wallet's engine wants in reserve, in gas units

  var st = {
    coin: "ETH", listings: [], side: "buy", link: null, account: "", status: null, wallets: null,
    pairing: null, note: "", error: "", busy: "", paying: "", told: "", stageKey: "", stageSince: 0, limits: {}, timer: null, form: { xmz: "", price: "" }, dismissed: ""
  };
  try { st.link = JSON.parse(localStorage.getItem(STORE) || "null"); } catch (e) { st.link = null; }
  if (st.link && PORTS.indexOf(st.link.port) < 0) st.link = null;   // saved for another wallet

  // ---- small helpers
  function esc(s) { return String(s == null ? "" : s).replace(/[&<>"']/g, function (c) { return "&#" + c.charCodeAt(0) + ";"; }); }
  function toUnits(text, places) {
    var m = /^([0-9]{1,30})(?:\.([0-9]{1,30}))?$/.exec(String(text).trim());
    if (!m || (m[2] || "").length > places) return null;
    return BigInt(m[1] + (m[2] || "").padEnd(places, "0"));
  }
  function fromUnits(v, places, show) {
    var s = v.toString().padStart(places + 1, "0");
    var whole = s.slice(0, s.length - places), part = s.slice(s.length - places);
    if (show != null) part = part.slice(0, show);
    part = part.replace(/0+$/, "");
    return part ? whole + "." + part : whole;
  }
  function short(a) { return a ? a.slice(0, 6) + "…" + a.slice(-4) : ""; }
  function chain() { return CHAINS[st.coin]; }
  // What differs on Solana: the coin has 9 decimals (lamports), the user's own wallet is a Solana
  // wallet such as Phantom, and the coin is paid in to the wallet's Solana trade account
  function sol() { var c = chain(); return !!(c && c.sol); }
  function dec() { return sol() ? 9 : 18; }
  function own() { return sol() ? "Phantom" : "MetaMask"; }
  function ownAccount() { return st.link ? (sol() ? st.link.solAccount : st.link.account) || "" : ""; }
  function payIn() { var w = st.status.wallet; return sol() ? w.solTradeAccount : w.tradeAccount; }
  function txLink(id) { var c = chain(); return c.tx + esc(id) + (c.txEnd || ""); }
  function hex(v) { return "0x" + v.toString(16); }
  function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }
  function saveLink() { try { if (st.link) localStorage.setItem(STORE, JSON.stringify(st.link)); else localStorage.removeItem(STORE); } catch (e) {} }

  // ---- the wallet on this computer
  function wallet(port, path, method, body, token) {
    var ctl = new AbortController(), t = setTimeout(function () { ctl.abort(); }, 8000);
    var headers = {};
    if (body) headers["content-type"] = "application/json";
    if (token) headers.authorization = "Bearer " + token;
    return fetch("http://127.0.0.1:" + port + path, { method: method || "GET", headers: headers, body: body ? JSON.stringify(body) : undefined, cache: "no-store", signal: ctl.signal })
      .then(function (r) { return r.json(); })
      .finally(function () { clearTimeout(t); });
  }
  function api(path, body) { return wallet(st.link.port, path, body ? "POST" : "GET", body, st.link.token); }
  function findWallets() {
    return Promise.all(PORTS.map(function (p) {
      return wallet(p, "/v1/hello", "GET", null, st.link && st.link.port === p ? st.link.token : null)
        .then(function (r) { return r && r.app === "monzero-wallet" ? { port: p, connected: !!r.connected } : null; })
        .catch(function () { return null; });
    })).then(function (all) { return all.filter(Boolean); });
  }

  // ---- a Solana wallet in the browser (Phantom) and the cluster
  var B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
  function b58encode(bytes) {
    var n = 0n, out = "", i;
    for (i = 0; i < bytes.length; i++) n = n * 256n + BigInt(bytes[i]);
    while (n > 0n) { out = B58[Number(n % 58n)] + out; n /= 58n; }
    for (i = 0; i < bytes.length && bytes[i] === 0; i++) out = "1" + out;
    return out;
  }
  function b58decode(text) {
    var n = 0n, i, out = [];
    for (i = 0; i < text.length; i++) { var d = B58.indexOf(text[i]); if (d < 0) throw new Error("Not a Solana address."); n = n * 58n + BigInt(d); }
    while (n > 0n) { out.unshift(Number(n % 256n)); n /= 256n; }
    for (i = 0; i < text.length && text[i] === "1"; i++) out.unshift(0);
    return out;
  }
  function solProvider() { return (window.phantom && window.phantom.solana) || (window.solana && window.solana.isPhantom ? window.solana : null) || window.solana || null; }
  function solRpc(method, params) {
    return fetch(chain().rpc, { method: "POST", headers: { "content-type": "application/json" }, cache: "no-store",
      body: JSON.stringify({ jsonrpc: "2.0", id: 1, method: method, params: params || [] }) })
      .then(function (r) { return r.json(); })
      .then(function (r) { if (r.error) throw new Error("Solana: " + (r.error.message || "request failed")); return r.result; });
  }
  function solBalance(account) { return solRpc("getBalance", [account, { commitment: "confirmed" }]).then(function (r) { return BigInt(r.value); }); }
  // The Solana library the site already hosts (same origin), loaded only when SOL is traded
  function loadWeb3() {
    if (window.solanaWeb3) return Promise.resolve();
    return new Promise(function (ok, no) {
      var el = document.createElement("script");
      el.src = "/app/solana-web3.iife.min.js";
      el.onload = function () { window.solanaWeb3 ? ok() : no(new Error("The Solana library did not load.")); };
      el.onerror = function () { no(new Error("The Solana library did not load.")); };
      document.head.appendChild(el);
    });
  }
  // A plain transfer from the user's Solana wallet, which signs and sends it. Built with the
  // standard library: a message laid out by hand was refused by Phantom ("Reached end of buffer").
  function solSend(to, lamports) {
    var from = st.link.solAccount, p = solProvider();
    return loadWeb3().then(function () { return p.connect(); }).then(function (r) {
      var now = r && r.publicKey ? r.publicKey.toString() : "";
      if (now && now !== from) throw new Error("Phantom is on another account than the one you connected (" + short(from) + "). Switch back, or disconnect and connect again.");
      return solRpc("getLatestBlockhash", [{ commitment: "confirmed" }]);
    }).then(function (b) {
      var web3 = window.solanaWeb3, payer = new web3.PublicKey(from);
      var tx = new web3.Transaction().add(web3.SystemProgram.transfer({ fromPubkey: payer, toPubkey: new web3.PublicKey(to), lamports: lamports }));
      tx.feePayer = payer; tx.recentBlockhash = b.value.blockhash;
      if (typeof p.signAndSendTransaction === "function") return p.signAndSendTransaction(tx);
      return p.request({ method: "signAndSendTransaction", params: { message: b58encode(tx.serializeMessage()) } });
    }).then(function (r) {
      var id = r && (r.signature || r);
      if (typeof id !== "string" || !id) throw new Error("Phantom did not return the payment's id.");
      return id;
    });
  }
  function solConfirmed(signature) {
    var tries = 0;
    function once() {
      return solRpc("getSignatureStatuses", [[signature], { searchTransactionHistory: true }]).then(function (r) {
        var s = r && r.value && r.value[0];
        if (s && s.err) throw new Error("The payment failed on chain.");
        if (s && (s.confirmationStatus === "confirmed" || s.confirmationStatus === "finalized")) return s;
        // Phantom sends on the network it is set to, which the page cannot see. A payment that never
        // shows up here was most likely sent on another one, where it is not valid and moves nothing.
        if (++tries > 20) throw new Error("This payment has not appeared on " + chain().name + ". Check that Phantom is set to the same network" +
          (chain().id === 501000101 ? "" : " (Settings, Developer Settings, Testnet Mode)") + ", then try again. A payment sent on another network is not valid there and moves nothing.");
        return sleep(2000).then(once);
      });
    }
    return once();
  }
  // One payment into the wallet's trade account, from the user's own wallet of this chain
  function pay(to, amount) {
    if (sol()) return solSend(to, amount).then(function (sig) { st.paying = sig; return sig; }).then(function (sig) {
      say("Payment sent from Phantom. Waiting for the network to confirm it (a few seconds)…");
      return solConfirmed(sig);
    }).then(function (r) { st.paying = ""; return r; }, function (e) { st.paying = ""; throw e; });
    return onChain().then(function () { return eth("eth_sendTransaction", [{ from: st.link.account, to: to, value: hex(amount) }]); }).then(function (tx) {
      st.paying = tx;
      say("Payment sent from MetaMask. Waiting for the network to confirm it (usually under a minute)…");
      return receipt(tx).then(function (r) { st.paying = ""; return r; }, function (e) { st.paying = ""; throw e; });
    });
  }

  // ---- MetaMask
  function eth(method, params) { return window.ethereum.request({ method: method, params: params || [] }); }
  function onChain() {
    var want = hex(BigInt(chain().id));
    return eth("eth_chainId").then(function (now) {
      if (BigInt(now) === BigInt(want)) return true;
      return eth("wallet_switchEthereumChain", [{ chainId: want }]).catch(function (e) {
        // MetaMask does not know the network yet: offer to add it (BNB Smart Chain is not built in)
        if (!e || e.code !== 4902 || !chain().add) throw e;
        return eth("wallet_addEthereumChain", [chain().add]);
      }).then(function () { return true; });
    });
  }
  function receipt(tx) {
    var tries = 0;
    function once() {
      return eth("eth_getTransactionReceipt", [tx]).then(function (r) {
        if (r && r.blockNumber) { if (BigInt(r.status || "0x1") !== 1n) throw new Error("The payment failed on chain."); return r; }
        if (++tries > 120) throw new Error("The payment is taking long to confirm. Wait for it in MetaMask, then try again: nothing is sent twice.");
        return sleep(3000).then(once);
      });
    }
    return once();
  }
  // Pay into the wallet's trade account whatever it still lacks for this trade
  function fund(need) {
    var acct = payIn();
    if (!acct) return Promise.reject(new Error("The wallet has not shown its " + st.coin + " trade account yet. Wait a moment and try again."));
    var held = sol() ? solBalance(acct) : onChain().then(function () { return eth("eth_getBalance", [acct, "latest"]); }).then(function (h) { return BigInt(h); });
    return held.then(function (have) {
      var missing = need - have;
      if (missing <= 0n) return null;
      say(own() + " will ask you to send " + fromUnits(missing, dec(), 8) + " " + st.coin + " to your trade account in the Monzero wallet.");
      return pay(acct, missing);
    });
  }
  function reserve(owner) {
    // Solana: fixed fees. The buyer's reserve covers the swap account's rent (returned when the
    // swap closes), fees and the minimum an account must keep; the seller's keeps its account
    // open while it pays for its claim. The same figures as the wallet's engine.
    if (sol()) return Promise.resolve(owner ? 4500000n : 1500000n);
    // On the trade's own network: the fee there is what counts
    // Price plus tip, doubled: the wallet pays a tip on top of the base fee, and a reserve that is too
    // small stops the trade until it is topped up. What is not spent goes back after the trade.
    return onChain().then(function () { return eth("eth_gasPrice"); }).then(function (gp) {
      return eth("eth_maxPriorityFeePerGas").catch(function () { return "0x0"; }).then(function (tip) {
        return (owner ? OWNER_GAS : CLAIMER_GAS) * (BigInt(gp) + BigInt(tip || "0x0")) * 2n;
      });
    });
  }

  // ---- limits the owner signed for this network (the wallet enforces them; shown here so nothing is paid in for a trade it would refuse)
  function loadLimits() {
    var c = chain();
    if (!c || !c.control || st.limits[st.coin]) return;
    st.limits[st.coin] = { loading: true };
    fetch("/dex/control/" + c.control + ".json", { cache: "no-store" }).then(function (r) { return r.ok ? r.json() : null; }).then(function (d) {
      var p = d && d.payload ? JSON.parse(d.payload) : null;
      st.limits[st.coin] = p && p.newTradesEnabled && p.expiresAt * 1000 > Date.now()
        ? { open: true, coin: BigInt(p.maxLtcAtomsPerTrade), xmz: BigInt(p.maxXmzAtomsPerTrade) } : { open: false };
      render();
    }).catch(function () { st.limits[st.coin] = { open: false }; render(); });
  }

  // ---- messages
  function say(text) { st.note = text; st.error = ""; render(); }
  function fail(e) {
    st.busy = "";
    var m = e && e.code === 4001 ? "You cancelled the request in MetaMask. Nothing was sent." : (e && e.message) || String(e);
    st.error = m; st.note = ""; render();
  }

  // ---- connecting
  function connect() {
    try { if (window.Notification && Notification.permission === "default") Notification.requestPermission(); } catch (e) {}
    if (st.busy) return;
    st.busy = "connect"; st.error = "";
    var port;
    // The user's own wallet for this coin; the other one, if connected before, stays connected
    var mine = { account: (st.link && st.link.account) || "", solAccount: (st.link && st.link.solAccount) || "" };
    var asked = sol()
      ? solProvider().connect().then(function (r) { mine.solAccount = r.publicKey.toString(); })
      : eth("eth_requestAccounts").then(function (a) { st.account = a[0]; mine.account = a[0]; });
    asked.then(function () {
      return findWallets();
    }).then(function (found) {
      if (!found.length) throw new Error("No Monzero wallet answered on this computer. Open your wallet (the version with the DEX) and try again. " +
        "If your browser asks whether this site may reach apps on your device, allow it. Chrome, Brave, Edge and Firefox work; Safari does not allow this.");
      port = found[0].port;
      return wallet(port, "/v1/pair", "POST", mine);
    }).then(function (r) {
      if (!r.ok) throw new Error(r.error === "account" ? own() + " gave no account." : "The wallet is busy with another connection request. Answer it there, then try again.");
      st.pairing = { id: r.id, code: r.code }; render();
      var tries = 0;
      function poll() {
        return wallet(port, "/v1/pair?id=" + r.id, "GET").then(function (p) {
          if (p.state === "accepted" && p.token) return p.token;
          if (p.state === "refused") throw new Error("The connection was refused in the wallet.");
          if (p.state !== "pending" || ++tries > 130) throw new Error("The connection request ran out of time. Try again.");
          return sleep(1500).then(poll);
        });
      }
      return poll();
    }).then(function (token) {
      st.link = { port: port, token: token, account: mine.account, solAccount: mine.solAccount }; saveLink();
      st.pairing = null; st.busy = ""; st.note = ""; refresh();
    }).catch(function (e) { st.pairing = null; fail(e); });
  }
  function disconnect() {
    if (st.link) api("/v1/disconnect", {}).catch(function () {});
    st.link = null; st.status = null; saveLink(); render();
  }

  // ---- reading the wallet
  function refresh() {
    if (!st.link) { render(); return Promise.resolve(); }
    return api("/v1/status").then(function (s) {
      if (!s.ok) { st.link = null; st.status = null; saveLink(); }
      else st.status = s;
      render();
    }).catch(function () { st.status = null; render(); });
  }
  function untilIdle() {
    var tries = 0;
    function once() {
      return api("/v1/status").then(function (s) {
        st.status = s.ok ? s : st.status;
        var m = s.ok && s.markets && s.markets[st.coin];
        if (!m || !m.busy) return s;
        if (++tries > 60) throw new Error("The wallet is still busy. Try again in a moment.");
        return sleep(1500).then(once);
      });
    }
    return sleep(800).then(once);
  }

  // ---- trading
  // What a seller's trading wallet must hold ready: the amount plus 1% (at least 1 XMZ) for the fee
  function xmzNeeded(atoms) { var extra = atoms / 100n; return atoms + (extra > 100000000000n ? extra : 100000000000n); }
  function checks(owner, valueWei, xmzAtoms) {
    var s = st.status, lim = st.limits[st.coin];
    if (!s || !s.wallet.keysReady) return "Your trading keys are locked. Press Unlock here; your Monzero wallet asks for your trading password.";
    if (s.trade && s.trade.configured && !(s.trade.snapshot && s.trade.snapshot.done)) return "A trade is already running. It has to finish first.";
    if (chain().control && (!lim || !lim.open)) return "Trading " + st.coin + " is not open right now.";
    if (lim && lim.open && (valueWei > lim.coin || xmzAtoms > lim.xmz))
      return "Above the limit while the DEX is new: at most " + fromUnits(lim.coin, dec(), 6) + " " + st.coin + " and " + fromUnits(lim.xmz, 11, 2) + " XMZ per trade.";
    if (!owner) {
      var unlocked = toUnits(String(s.wallet.unlocked || "0"), 11) || 0n, wanted = xmzNeeded(xmzAtoms);
      if (unlocked < wanted) return "Your trading wallet needs " + fromUnits(wanted, 11, 2) + " XMZ unlocked (the amount plus a little for the network fee); it has " + fromUnits(unlocked, 11, 2) +
        ". Press Add XMZ above, confirm the transfer in your Monzero wallet, and wait about 20 minutes for it to be ready.";
    }
    return "";
  }
  function post() {
    if (st.busy) return;
    var xmz = toUnits(st.form.xmz, 11), price = toUnits(st.form.price, dec());
    if (!xmz || !price || xmz === 0n || price === 0n) return fail(new Error("Enter the XMZ amount and the price."));
    var value = xmz * price / 100000000000n, owner = st.side === "buy";
    if (value === 0n) return fail(new Error("The total is too small."));
    var why = checks(owner, value, xmz);
    if (why) return fail(new Error(why));
    st.busy = "post"; say("Preparing…");
    reserve(owner).then(function (gas) { return fund(owner ? value + gas : gas); }).then(function () {
      say("Posting your offer…");
      return api("/v1/offer", { chainId: chain().id, side: owner ? "buy-xmz" : "sell-xmz", coin: fromUnits(value, dec()), xmz: fromUnits(xmz, 11), hours: 6 });
    }).then(function (r) {
      if (!r.ok) throw new Error(r.message || "The wallet could not post the offer. What you paid in stays in your trade account; you can send it back to " + own() + " below.");
      return untilIdle();
    }).then(function (s) {
      var m = s.markets[st.coin];
      st.busy = "";
      if (m && m.state !== "ok" && m.error) return fail(new Error(m.error));
      say("Your offer is posted. Keep the wallet open: when someone takes it, the wallet asks you to approve the trade.");
    }).catch(fail);
  }
  function take(id) {
    if (st.busy) return;
    var o = null;
    st.listings.forEach(function (x) { if (x.listing && x.listing.id === id) o = x.listing; });
    if (!o) return fail(new Error("That offer is gone."));
    var value = BigInt(o.value), xmz = BigInt(o.xmz), owner = o.side === "sell-xmz";   // we pay coin when the maker sells XMZ
    var why = checks(owner, value, xmz);
    if (why) return fail(new Error(why));
    st.busy = "take"; say("Preparing…");
    reserve(owner).then(function (gas) { return fund(owner ? value + gas : gas); }).then(function () {
      say("Asking the wallet to take the offer…");
      return api("/v1/refresh", { pair: st.coin + "/XMZ" });
    }).then(untilIdle).then(function () { return api("/v1/take", { id: id }); }).then(function (r) {
      if (!r.ok) throw new Error(r.message || "The wallet could not take this offer. What you paid in stays in your trade account; you can send it back to " + own() + " below.");
      st.busy = "";
      say("Offer taken. Waiting for the other side to answer; then the wallet asks you to approve the trade.");
    }).catch(fail);
  }
  // Things only the wallet may do (password, moving XMZ): the page asks it to show its own screen
  function ask(path, body, told) {
    api(path, body).then(function (r) {
      if (r && r.ok) say(told); else fail(new Error((r && r.message) || (r && r.error === "wait a moment" ? "One moment, then try again." : "The wallet could not do that now.")));
    }).catch(fail);
  }
  // What "Add XMZ" suggests: enough for the amount typed in the sell form (plus the fee margin),
  // less what the trading wallet already holds
  function wantedXmz() {
    var xmz = st.side === "sell" ? toUnits(st.form.xmz, 11) : null, w = st.status && st.status.wallet;
    if (!xmz || !w) return {};
    var have = toUnits(String(w.balance || "0"), 11) || 0n, need = xmzNeeded(xmz) - have + 100000000000n;   // one XMZ over, for the transfer fee and rounding
    return need > 0n ? { xmz: fromUnits(need, 11, 2) } : {};
  }
  function cancel(folder) {
    if (st.busy) return;
    st.busy = "cancel";
    api("/v1/cancel", { folder: folder }).then(untilIdle).then(function () { st.busy = ""; say("Offer cancelled."); }).catch(fail);
  }
  function giveBack() {
    if (st.busy) return;
    st.busy = "return"; say("Sending the unused " + st.coin + " back to " + own() + "…");
    api("/v1/return", { chainId: chain().id }).then(function (r) {
      if (!r.ok) throw new Error(r.message || "The wallet could not send the funds back now.");
      return untilIdle();
    }).then(function (s) {
      var m = s.markets[st.coin] || {}, w = m.withdrawal || {};
      st.busy = "";
      if (m.state !== "ok" && m.error) return fail(new Error(m.error));
      say(w.tx ? "Sent back " + fromUnits(BigInt(w.amountWei), dec(), 8) + " " + st.coin + " to " + short(w.to) + "." : "Nothing to send back.");
    }).catch(fail);
  }
  function topUp() {
    var need = st.status.trade.awaitingFunding || {}, to = sol() ? st.status.trade.solAccount || payIn() : st.status.wallet.tradeAccount;
    if (st.busy || !need.missingWei || !to) return;
    st.busy = "fund";
    say(own() + " will ask you to send " + fromUnits(BigInt(need.missingWei), dec(), 8) + " " + st.coin + ".");
    pay(to, BigInt(need.missingWei)).then(function () { st.busy = ""; say("Paid in. The trade continues by itself."); }).catch(fail);
  }

  // ---- drawing
  var STAGE = {
    "Start": "Ready to lock your coin once you approve.", "LockSent": "Locking your coin in the swap contract…", "Locked": "Your coin is locked. Waiting for the seller to lock the XMZ…",
    "ReadySent": "The XMZ is locked. Releasing the swap…", "Ready": "Waiting for the seller to claim…", "Sweeping": "The seller claimed. Moving the XMZ into your trading wallet…",
    "Done(Swapped)": "Swap complete.", "RefundSent": "Refunding your coin…", "Done(Refunded)": "Refunded: nothing was traded.",
    "WaitingForEvmLock": "Waiting for the buyer to lock the coin…", "XmzLockSent": "Locking your XMZ…", "XmzLocked": "Your XMZ is locked. Waiting for the buyer to release the swap…",
    "ClaimSent": "Claiming the coin…", "WaitingForRefund": "Waiting for the buyer's refund.", "SweepingBack": "The buyer refunded. Taking your XMZ back…"
  };
  // The trade as a list of steps, so it is always clear what has happened, what is happening now
  // and what is waited for. wait: what this step usually takes (the XMZ steps need 10 blocks).
  var XMZ_WAIT = "usually 20 to 40 minutes: it needs 10 XMZ blocks";
  var STEPS = {
    owner: [
      { at: ["Start"], text: "Approve the trade in your Monzero wallet" },
      { at: ["LockSent"], text: "Your %COIN% goes into the swap contract", wait: "under a minute" },
      { at: ["Locked"], text: "The seller locks the XMZ", wait: XMZ_WAIT },
      { at: ["ReadySent", "Ready"], text: "The swap is released and the seller is paid", wait: "a minute or two" },
      { at: ["Sweeping"], text: "Your XMZ arrives in your trading wallet", wait: XMZ_WAIT }
    ],
    claimer: [
      { at: ["WaitingForEvmLock"], text: "The buyer's %COIN% goes into the swap contract", wait: "a few minutes; the wallet then asks you to approve" },
      { at: ["XmzLockSent"], text: "Your XMZ is locked for the buyer", wait: XMZ_WAIT },
      { at: ["XmzLocked"], text: "The buyer releases the swap", wait: "a minute or two" },
      { at: ["ClaimSent"], text: "Your %COIN% is claimed and sent to your own wallet", wait: "a minute or two" }
    ]
  };
  // "8 of 10 confirmations", when the wallet reports it for the step that is waiting on XMZ blocks
  function counted(s) {
    var p = s.progress || {}, c = s.stage === "Sweeping" ? p.xmzSweep : (s.stage === "Locked" || s.stage === "XmzLockSent") ? p.xmzLock : null;
    if (!c || !c.needed) return "";
    var left = Math.max(0, c.needed - c.confirmations);
    return c.confirmations + " of " + c.needed + " confirmations" + (left ? " · about " + left * 2 + " to " + left * 4 + " minutes left" : "");
  }
  function stepsHTML(t) {
    var s = t.snapshot || {}, list = STEPS[s.role], stage = s.stage || "";
    if (!list || /Refund|SweepingBack/.test(stage)) return "";
    var now = -1;
    list.forEach(function (x, i) { if (x.at.indexOf(stage) >= 0) now = i; });
    if (s.done) now = list.length;
    if (now < 0) return "";
    // How long the current step has been running (this browser's clock, since the page saw it start)
    var key = (t.tradeId || "") + ":" + stage;
    if (st.stageKey !== key) { st.stageKey = key; st.stageSince = Date.now(); }
    var mins = Math.floor((Date.now() - st.stageSince) / 60000);
    return '<ol class="tp-track">' + list.map(function (x, i) {
      var cls = i < now ? "done" : i === now ? "now" : "next", text = esc(x.text.replace("%COIN%", st.coin)), more = "";
      if (i === now && !t.awaitingApproval && x.wait) more = '<span class="tp-wait">' + esc(counted(s) || x.wait) + (mins >= 1 ? " · " + mins + " min so far" : "") + "</span>";
      if (i === now && t.awaitingApproval) more = '<span class="tp-wait">waiting for you: look at your Monzero wallet</span>';
      return '<li class="' + cls + '"><span class="tp-dot" aria-hidden="true">' + (i < now ? "✓" : i === now ? "●" : "○") + "</span><span>" + text + more + "</span></li>";
    }).join("") + "</ol>";
  }
  function tradeHTML(t) {
    var s = t.snapshot || {}, o = s.offer || {}, c = chain(), buying = s.role === "owner", h = "";
    if (st.note.indexOf("Offer taken") === 0) st.note = "";   // the trade itself is on screen now
    var coinAmt = o.value ? fromUnits(BigInt(o.value), dec(), 8) + " " + st.coin : "", xmzAmt = o.xmzAmount != null ? fromUnits(BigInt(o.xmzAmount), 11, 4) + " XMZ" : "";
    h += '<p class="tp-terms">' + (buying ? "You pay <strong>" + esc(coinAmt) + "</strong> · you receive <strong>" + esc(xmzAmt) + "</strong>"
      : "You pay <strong>" + esc(xmzAmt) + "</strong> · you receive <strong>" + esc(coinAmt) + "</strong>") + "</p>";
    if (t.awaitingApproval) h += '<p class="tp-act">Approve this trade in your Monzero wallet. The wallet shows the amounts it holds; check them there.</p>';
    else h += '<p class="tp-stage">' + esc(STAGE[s.stage] || s.stage || "Starting…") + "</p>";
    h += stepsHTML(t);
    if (t.startRefusal) h += '<p class="tp-err">' + esc(t.startRefusal) + "</p>";
    if (t.error) h += '<p class="tp-err">' + esc(t.error) + "</p>";
    var need = t.awaitingFunding || {};
    if (need.missingWei) h += '<p class="tp-act">The trade account needs ' + esc(fromUnits(BigInt(need.missingWei), dec(), 8)) + " " + st.coin +
      ' more for network fees.</p><button class="button primary" type="button" data-act="topup">Send it from ' + own() + "</button>";
    var txs = s.evmTxs || {}, links = [];
    Object.keys(txs).forEach(function (k) { (txs[k] || []).forEach(function (x) { links.push('<a href="' + txLink(x) + '" rel="noopener" target="_blank">' + esc(k.replace("_", " ")) + "</a>"); }); });
    if (t.payout && t.payout.tx) links.push('<a href="' + txLink(t.payout.tx) + '" rel="noopener" target="_blank">sent back to ' + own() + "</a>");
    if (links.length) h += '<p class="tp-links">On chain: ' + links.join(" · ") + "</p>";
    if (s.done) {
      if (t.payout && t.payout.tx) h += '<p class="tp-note">' + esc(fromUnits(BigInt(t.payout.amountWei), dec(), 8)) + " " + st.coin + " went back to " + esc(short(t.payout.to)) + ".</p>";
      else if (t.payout && t.payout.sent === false) h += '<p class="tp-note">Nothing was left in the trade account to send back to ' + own() + ".</p>";
      if (buying && s.stage === "Done(Swapped)") h += '<p class="tp-note">Your XMZ is in your trading wallet, ready for the next trade. ' +
        '<button class="tp-link" type="button" data-act="xmz-return">Move it to my main balance</button></p>';
      h += '<button class="button secondary" type="button" data-act="dismiss">New trade</button>';
    } else if (!s.approved && (s.stage === "Start" || s.stage === "WaitingForEvmLock")) {
      // Nothing of yours is locked before you approve: the trade can simply be dropped
      h += '<p class="tp-note">Nothing of yours is locked yet. Changed your mind? <button class="tp-link" type="button" data-act="abandon">Cancel this trade</button></p>';
    } else h += '<p class="tp-note">Keep the wallet open until this finishes. If the other side stops, your funds are returned after the time limit.</p>';
    return '<div class="tp-trade">' + h + "</div>";
  }
  // XMZ that reached the trading wallet but is not usable yet (10 blocks after it arrives)
  function arrivingHTML(w) {
    var total = toUnits(String(w.balance || "0"), 11) || 0n, ready = toUnits(String(w.unlocked || "0"), 11) || 0n;
    if (total <= ready) return "";
    return ' <span class="tp-arriving">+ ' + esc(fromUnits(total - ready, 11, 4)) + " XMZ arriving (ready in up to 40 minutes)</span>";
  }
  // Things that need the user, or that just finished: say so in the tab title and, when the
  // browser allows it, in a notification, because the wallet and the page are different windows
  var TITLE = document.title;
  function notify(key, title, text) {
    if (st.told === key) return;
    st.told = key;
    try { if (window.Notification && Notification.permission === "granted" && document.hidden) new Notification(title, { body: text, tag: "monzero-dex" }); } catch (e) {}
  }
  function attention() {
    var s = st.status, t = s && s.trade, w = s && s.wallet, mark = "";
    if (s && w && !w.keysReady) mark = "Password needed";
    else if (t && t.configured && st.dismissed !== t.tradeId) {
      var need = t.awaitingFunding || {}, snap = t.snapshot || {};
      if (t.awaitingApproval) { mark = "Approve in wallet"; notify(t.tradeId + ":approve", "Approve your trade", "Your Monzero wallet is asking you to approve the trade."); }
      else if (need.missingWei) { mark = "Top-up needed"; notify(t.tradeId + ":topup", "Your trade needs a small top-up", "Open the DEX page to send it."); }
      else if (snap.done) { mark = snap.stage === "Done(Swapped)" ? "Swap complete" : "Trade ended"; notify(t.tradeId + ":done", mark, "Open the DEX page to see the result."); }
      else mark = "Trade running";
    }
    document.title = mark ? "(" + mark + ") " + TITLE : TITLE;
  }
  function offersHTML() {
    var me = (st.status.wallet.tradeAccount || "").toLowerCase(), want = st.side === "buy" ? "sell-xmz" : "buy-xmz", rows = [];
    st.listings.forEach(function (x) {
      var l = x.listing;
      if (!l || l.pair !== st.coin + "/XMZ" || Number(l.chainId) !== chain().id || l.side !== want || String(l.maker).toLowerCase() === me) return;
      var v = BigInt(l.value), z = BigInt(l.xmz);
      if (z === 0n) return;
      rows.push({ id: l.id, value: v, xmz: z, price: v * 100000000000n / z });
    });
    rows.sort(function (a, b) { return st.side === "buy" ? (a.price < b.price ? -1 : 1) : (a.price > b.price ? -1 : 1); });
    if (!rows.length) return '<p class="tp-note">No offers to take right now. Post your own below.</p>';
    return '<ul class="tp-offers">' + rows.slice(0, 6).map(function (r) {
      return "<li><span><strong>" + esc(fromUnits(r.xmz, 11, 2)) + " XMZ</strong> for " + esc(fromUnits(r.value, dec(), 8)) + " " + st.coin +
        '</span><button class="button primary" type="button" data-act="take" data-id="' + esc(r.id) + '">' + (st.side === "buy" ? "Buy" : "Sell") + "</button></li>";
    }).join("") + "</ul>";
  }
  function render() {
    var h = "", c = chain();
    if (!c) {
      h = '<p class="pro-order-lede">Pick ETH or BNB to trade from this page with MetaMask. The other coins are not open here yet.</p>' +
        '<div class="swap-reveal-cta"><a class="button primary" href="/download/">Get the app</a></div>';
    } else if (sol() ? !solProvider() : !window.ethereum) {
      h = '<p class="pro-order-lede">To trade ' + st.coin + ' from this page you need <strong>' + own() + '</strong> in this browser and the <strong>Monzero wallet</strong> open on this computer.</p>' +
        '<div class="swap-reveal-cta"><a class="button primary" href="/download/">Get the wallet</a><a class="button secondary" href="' +
        (sol() ? "https://phantom.com/download" : "https://metamask.io/download/") + '" rel="noopener">Get ' + own() + "</a></div>";
    } else if (st.pairing) {
      h = '<p class="pro-order-lede">Look at your Monzero wallet. Accept the connection there if it shows this code:</p><p class="tp-code">' + esc(st.pairing.code) + "</p>";
    } else if (st.link && st.status && !ownAccount()) {
      // Connected with the other kind of wallet only: this coin needs its own
      h = '<p class="pro-order-lede">Your Monzero wallet is connected. To trade ' + st.coin + ', connect <strong>' + own() + '</strong> too: it pays the ' + st.coin +
        ' in and receives what you are owed. The wallet will show a new code to accept.</p>' +
        '<div class="swap-reveal-cta"><button class="button primary" type="button" data-act="connect"' + (st.busy ? " disabled" : "") + ">Connect " + own() + "</button></div>";
    } else if (!st.link || !st.status) {
      h = '<p class="pro-order-lede">Trade ' + st.coin + ' for XMZ with <strong>' + own() + '</strong>. Your <strong>Monzero wallet</strong> on this computer runs the swap and asks you to approve each trade; nobody holds your coins.</p>' +
        '<ol class="tp-steps"><li>Open your Monzero wallet on this computer.</li><li>Connect here; the wallet shows a code to accept and asks for a trading password.</li><li>Pick an offer. ' + own() + ' pays in, the wallet asks you to approve.</li></ol>' +
        '<div class="swap-reveal-cta"><button class="button primary" type="button" data-act="connect"' + (st.busy ? " disabled" : "") + ">" + (st.link ? "Reconnect" : "Connect " + own() + " and wallet") + '</button><a class="button secondary" href="/download/">Get the wallet</a></div>';
    } else {
      var s = st.status, w = s.wallet, t = s.trade || {}, m = (s.markets || {})[st.coin] || {};
      h += '<ul class="relay-facts"><li><span>Wallet</span><strong>connected · ' + esc(String(w.unlocked || "0")) + ' XMZ ready</strong>' + arrivingHTML(w) + '  <button class="tp-link" type="button" data-act="xmz-add">Add XMZ</button>' +
        (Number(w.unlocked) > 0 ? ' · <button class="tp-link" type="button" data-act="xmz-return">Move back</button>' : "") +
        (payIn() ? ' <span class="tp-acct">· trade account ' + esc(short(payIn())) + "</span>" : "") + "</li><li><span>" + own() + "</span><strong>" + esc(short(sol() ? s.solAccount : s.account)) +
        "</strong></li><li><span>Network</span><strong>" + esc(c.name) + "</strong></li></ul>";
      if (!sol() && st.account && s.account && st.account.toLowerCase() !== s.account.toLowerCase())
        h += '<p class="tp-err">MetaMask is now on another account. Proceeds still go to ' + esc(short(s.account)) + ", the account you accepted in the wallet. Disconnect and connect again to change it.</p>";
      var live = t.configured && t.tradeId && st.dismissed !== t.tradeId;
      if (!w.keysReady) h += '<p class="tp-act">' + (w.keysBusy ? "Your wallet is unlocking your trading keys…" : w.keysExist ? "Your trading keys are locked. Your Monzero wallet asks for your trading password."
        : "One more step: your Monzero wallet asks you to choose a trading password.") + '</p>' +
        (w.keysBusy ? "" : '<button class="button primary" type="button" data-act="unlock">' + (w.keysExist ? "Unlock in the wallet" : "Set it up in the wallet") + "</button>");
      else if (live) h += tradeHTML(t);
      else {
        var lim = st.limits[st.coin];
        h += '<div class="tp-side" role="group"><button type="button" data-act="side" data-side="buy" class="' + (st.side === "buy" ? "on" : "") + '">Buy XMZ</button>' +
          '<button type="button" data-act="side" data-side="sell" class="' + (st.side === "sell" ? "on" : "") + '">Sell XMZ</button></div>';
        h += "<h3>Take an offer</h3>" + offersHTML();
        h += "<h3>Or post your own</h3>" + '<div class="tp-form"><label>XMZ<input id="tp-xmz" inputmode="decimal" autocomplete="off" value="' + esc(st.form.xmz) + '"></label>' +
          "<label>Price (" + st.coin + ' per XMZ)<input id="tp-price" inputmode="decimal" autocomplete="off" value="' + esc(st.form.price) + '"></label></div>' +
          '<p class="tp-note" id="tp-total"></p><button class="button secondary" type="button" data-act="post"' + (st.busy ? " disabled" : "") + ">" + (st.side === "buy" ? "Post offer to buy XMZ" : "Post offer to sell XMZ") + "</button>";
        if (lim && lim.open) h += '<p class="tp-note">While the DEX is new: at most ' + esc(fromUnits(lim.coin, dec(), 6)) + " " + st.coin + " and " + esc(fromUnits(lim.xmz, 11, 0)) + " XMZ per trade.</p>";
        var open = (m.mine || []).filter(function (x) { return x.state === "open"; });
        if (open.length) h += "<h3>Your open offers</h3>" + '<ul class="tp-offers">' + open.map(function (x) {
          return "<li><span>" + (x.side === "buy-xmz" ? "Buy " : "Sell ") + "<strong>" + esc(x.xmz) + " XMZ</strong> for " + esc(x.coin) + " " + st.coin +
            '</span><button class="button secondary" type="button" data-act="cancel" data-folder="' + esc(x.folder) + '">Cancel</button></li>';
        }).join("") + "</ul>";
        else h += '<p class="tp-note">Paid in ' + st.coin + ' you no longer need? <button class="tp-link" type="button" data-act="return">Send it back to ' + own() + '</button></p>';
      }
      h += '<p class="tp-note"><button class="tp-link" type="button" data-act="disconnect">Disconnect this page</button></p>';
    }
    if (st.note) h += '<p class="tp-say' + (st.busy ? " tp-busy" : "") + '" role="status">' + esc(st.note) +
      (st.paying && chain() ? ' <a href="' + txLink(st.paying) + '" rel="noopener" target="_blank">See the payment</a>' : "") + "</p>";
    attention();
    if (st.error) h += '<p class="tp-err" role="alert">' + esc(st.error) + "</p>";
    // Keep what is being typed
    var active = document.activeElement && document.activeElement.id, pos = active && document.activeElement.selectionStart;
    box.innerHTML = h;
    if (active === "tp-xmz" || active === "tp-price") { var el = document.getElementById(active); if (el) { el.focus(); try { el.setSelectionRange(pos, pos); } catch (e) {} } }
    total();
  }
  function total() {
    var el = document.getElementById("tp-total");
    if (!el) return;
    var xmz = toUnits(st.form.xmz, 11), price = toUnits(st.form.price, dec());
    el.textContent = xmz && price ? (st.side === "buy" ? "You pay " : "You receive ") + fromUnits(xmz * price / 100000000000n, dec(), 8) + " " + st.coin : "";
  }

  box.addEventListener("click", function (e) {
    var b = e.target.closest("[data-act]");
    if (!b) return;
    var act = b.getAttribute("data-act");
    if (act === "connect") connect();
    else if (act === "disconnect") disconnect();
    else if (act === "side") { st.side = b.getAttribute("data-side"); st.error = ""; render(); }
    else if (act === "post") post();
    else if (act === "take") take(b.getAttribute("data-id"));
    else if (act === "cancel") cancel(b.getAttribute("data-folder"));
    else if (act === "return") giveBack();
    else if (act === "topup") topUp();
    else if (act === "abandon") api("/v1/abandon", {}).then(function (r) {
      if (r && r.ok) say("Trade cancelled. What you paid in is still in your trade account; you can send it back to " + own() + " below."); else fail(new Error("This trade can no longer be cancelled here: it has started."));
      return refresh();
    }).catch(fail);
    else if (act === "unlock") ask("/v1/unlock", {}, "Look at your Monzero wallet: it is asking for your trading password.");
    else if (act === "xmz-add") ask("/v1/xmz-add", wantedXmz(), "Look at your Monzero wallet: its Send page is open with your trading wallet filled in. Enter the amount and confirm there. New XMZ is ready to trade after about 20 minutes.");
    else if (act === "xmz-return") ask("/v1/xmz-return", {}, "Look at your Monzero wallet: confirm there to move your trading XMZ back to your main balance.");
    else if (act === "dismiss") { st.dismissed = st.status.trade.tradeId; st.note = ""; render(); }
  });
  box.addEventListener("input", function (e) {
    if (e.target.id === "tp-xmz") st.form.xmz = e.target.value;
    if (e.target.id === "tp-price") st.form.price = e.target.value;
    total();
  });
  document.addEventListener("monzero-dex-book", function (e) {
    var d = e.detail || {};
    if (d.coin && d.coin !== st.coin) { st.coin = d.coin; st.error = ""; st.note = ""; }
    st.listings = d.listings || [];
    loadLimits(); render();
  });
  if (window.ethereum && window.ethereum.on) window.ethereum.on("accountsChanged", function (a) { st.account = (a && a[0]) || ""; render(); });

  function tick() { if (!document.hidden && st.link && !st.pairing) refresh(); }
  st.timer = setInterval(tick, 4000);
  if (window.ethereum) eth("eth_accounts").then(function (a) { st.account = (a && a[0]) || ""; }).catch(function () {});
  loadLimits(); refresh();
})();
