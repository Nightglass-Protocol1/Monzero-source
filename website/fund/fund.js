// Live donation figures for /fund/, from /api/donations/ (public explorer data,
// cached on our server). Amounts arrive as whole-unit strings and are shown
// with BigInt arithmetic, so large wei totals stay exact.
(function () {
  "use strict";
  const REFRESH_MS = 5 * 60 * 1000;
  const usd = new Intl.NumberFormat("en-US", { style: "currency", currency: "USD", maximumFractionDigits: 0 });

  function units(atomic, decimals, shown) {
    const value = BigInt(atomic);
    const scale = 10n ** BigInt(decimals);
    const whole = value / scale;
    let fraction = (value % scale).toString().padStart(decimals, "0").slice(0, shown).replace(/0+$/, "");
    return whole.toLocaleString("en-US") + (fraction ? "." + fraction : "");
  }
  function dollars(atomic, decimals, price) {
    return Number(BigInt(atomic)) / 10 ** decimals * (price || 0);
  }
  function ago(seconds) {
    const minutes = Math.max(0, Math.round((Date.now() / 1000 - seconds) / 60));
    if (minutes < 1) return "just now";
    if (minutes < 60) return minutes + (minutes === 1 ? " minute ago" : " minutes ago");
    const hours = Math.round(minutes / 60);
    return hours + (hours === 1 ? " hour ago" : " hours ago");
  }
  function cell(row, text, className) {
    const td = document.createElement("td");
    td.textContent = text;
    if (className) td.className = className;
    row.appendChild(td);
    return td;
  }
  function link(href, text) {
    const a = document.createElement("a");
    a.href = href; a.rel = "noopener"; a.textContent = text;
    return a;
  }

  let lastData = null;
  // campaigns.mjs calls this after replacing the goals, so the bars are filled again.
  window.monzeroFundRender = () => { if (lastData) render(lastData); };
  function render(data) {
    lastData = data;
    const status = document.getElementById("live-status");
    // The server's list must match the addresses printed on this page.
    const printed = new Set([...document.querySelectorAll("[data-address]")].map((node) => node.dataset.address));
    if (data.coins.some((coin) => !printed.has(coin.address))) {
      status.textContent = "Warning: the live figures use an address that is not printed on this page. Do not send anything; report this to security@monzero.org.";
      status.classList.add("pending");
      return;
    }
    let received = 0, held = 0, count = 0, stale = [];
    const body = document.querySelector("#coin-table tbody");
    body.replaceChildren();
    for (const coin of data.coins) {
      const row = document.createElement("tr");
      cell(row, coin.name);
      if (coin.received === null) {
        cell(row, "unavailable", "num"); cell(row, "unavailable", "num"); cell(row, "–", "num");
      } else {
        const price = data.pricesUsd[coin.id];
        received += dollars(coin.received, coin.decimals, price);
        held += dollars(coin.balance, coin.decimals, price);
        count += coin.transactions;
        cell(row, units(coin.received, coin.decimals, 6) + " " + coin.symbol, "num");
        cell(row, units(coin.balance, coin.decimals, 6) + " " + coin.symbol, "num");
        cell(row, String(coin.transactions), "num");
      }
      if (!coin.current) stale.push(coin.name);
      cell(row, "", "num").appendChild(link(coin.addressUrl, "Explorer"));
      body.appendChild(row);
    }
    // XMZ is private: its total needs the wallet's view key, so it is listed but not counted.
    const xmz = document.createElement("tr");
    cell(xmz, "Monzero (XMZ)"); cell(xmz, "private", "num"); cell(xmz, "private", "num"); cell(xmz, "–", "num"); cell(xmz, "–", "num");
    body.appendChild(xmz);
    document.getElementById("total-received").textContent = usd.format(received);
    document.getElementById("total-held").textContent = usd.format(held);
    document.getElementById("total-count").textContent = count.toLocaleString("en-US");

    const list = document.getElementById("recent");
    const coins = Object.fromEntries(data.coins.map((coin) => [coin.id, coin]));
    if (data.recent.length) {
      list.replaceChildren();
      for (const item of data.recent) {
        const coin = coins[item.coin];
        const li = document.createElement("li");
        const amount = document.createElement("strong");
        amount.textContent = units(item.amount, coin.decimals, 8) + " " + coin.symbol;
        li.appendChild(amount);
        li.append(" " + (item.time ? ago(item.time) : "just sent") + (item.confirmed ? "" : " · unconfirmed") + " · ");
        li.appendChild(link(coin.txUrl + item.txid, "transaction"));
        list.appendChild(li);
      }
    }

    // Goals fill in order: the first takes everything up to its target.
    const percent = (part, whole) => {
      const value = whole > 0 ? Math.min(100, part / whole * 100) : 0;
      return (value > 0 && value < 1 ? value.toFixed(1) : Math.floor(value).toString()) + "%";
    };
    let left = received, allTargets = 0;
    for (const bar of document.querySelectorAll(".goal-bar")) {
      const target = Number(bar.dataset.target);
      allTargets += target;
      const raised = Math.min(left, target);
      left -= raised;
      bar.style.width = Math.min(100, raised / target * 100).toFixed(1) + "%";
      const label = document.querySelector('.goal-raised[data-goal="' + bar.dataset.goal + '"]');
      if (label) label.textContent = usd.format(raised);
      const share = document.querySelector('.goal-percent[data-goal="' + bar.dataset.goal + '"]');
      if (share) share.textContent = percent(raised, target) + (raised >= target ? " · goal reached" : "");
    }
    document.getElementById("overall-percent").textContent = percent(received, allTargets);
    document.getElementById("overall-target").textContent = usd.format(allTargets);
    document.getElementById("overall-bar").style.width = Math.min(100, received / allTargets * 100).toFixed(1) + "%";

    status.classList.remove("pending");
    status.textContent = "Updated " + ago(data.updatedAt) + " from public block explorers. US dollar values use today's prices." +
      (stale.length ? " Showing earlier figures for " + stale.join(", ") + " because its explorer did not answer." : "");
  }

  function load() {
    fetch("../api/donations/", { cache: "no-cache" })
      .then((response) => (response.ok ? response.json() : Promise.reject(new Error(String(response.status)))))
      .then(render)
      .catch(() => {
        document.getElementById("live-status").textContent =
          "Live figures are not available right now. Every address can still be checked on its explorer link.";
      });
  }

  document.addEventListener("click", (event) => {
    const button = event.target.closest("button.copy");
    if (!button || !navigator.clipboard) return;
    navigator.clipboard.writeText(button.dataset.copy).then(() => {
      button.textContent = "Copied";
      setTimeout(() => { button.textContent = "Copy address"; }, 2000);
    });
  });
  load();
  setInterval(load, REFRESH_MS);
})();
