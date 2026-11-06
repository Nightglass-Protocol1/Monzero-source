// Homepage panel: the same live donation figures as /fund/, in brief.
(function () {
  "use strict";
  const usd = new Intl.NumberFormat("en-US", { style: "currency", currency: "USD", maximumFractionDigits: 0 });
  const percent = (part, whole) => {
    const value = Math.min(100, part / whole * 100);
    return (value > 0 && value < 1 ? value.toFixed(1) : Math.floor(value).toString()) + "%";
  };
  const set = (id, text) => { const node = document.getElementById(id); if (node) node.textContent = text; };
  const width = (id, part, whole) => { const node = document.getElementById(id); if (node) node.style.width = Math.min(100, part / whole * 100).toFixed(1) + "%"; };
  const goals = fetch("fund/goals.json", { cache: "no-cache" }).then((r) => (r.ok ? r.json() : null)).catch(() => null);
  fetch("api/donations/", { cache: "no-cache" })
    .then((response) => (response.ok ? response.json() : Promise.reject(new Error(String(response.status)))))
    .then((data) => goals.then((g) => [data, g]))
    .then(([data, g]) => {
      const open = g && g.schema === "monzero-fund-goals-v1" && Array.isArray(g.records) ? g.records.filter((x) => x && x.status !== "closed" && /^[1-9]\d{0,8}$/.test(x.target_usd)) : [];
      const first = open.length ? Number(open[0].target_usd) : 15000;
      const all = open.length ? open.reduce((sum, x) => sum + Number(x.target_usd), 0) : 20000;
      if (open.length) {
        set("support-goal-name", typeof open[0].title === "string" ? open[0].title.slice(0, 80) : "First goal");
        set("support-goal-target", usd.format(first));
        set("support-all-target", usd.format(all));
      }
      let received = 0, count = 0;
      for (const coin of data.coins) {
        if (coin.received === null) continue;
        received += Number(BigInt(coin.received)) / 10 ** coin.decimals * (data.pricesUsd[coin.id] || 0);
        count += coin.transactions;
      }
      set("support-raised", usd.format(received));
      set("support-count", count.toLocaleString("en-US"));
      set("support-goal-percent", percent(Math.min(received, first), first));
      width("support-goal-bar", received, first);
      set("support-all-percent", percent(received, all));
      width("support-all-bar", received, all);
      set("support-status", "Live from public block explorers, valued in US dollars at today's prices.");
    })
    .catch(() => set("support-status", "Live figures are not available right now; see monzero.org/fund/."));
})();
