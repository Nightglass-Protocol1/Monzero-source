/* Homepage price band: the XMZ/USDT figures from NoirTrade (/markets/xmz.php), scrolling.
   Until the figures arrive, or if they cannot be read, the band keeps its plain wording. */
(function () {
  "use strict";
  var band = document.querySelector(".ticker-band"), track = band && band.querySelector(".ticker-track");
  if (!track) return;
  function price(v) { return Number(v).toFixed(8).replace(/0+$/, "").replace(/\.$/, ""); }
  function item(label, value, cls) {
    var s = document.createElement("span"); s.className = "tick";
    var l = document.createElement("i"); l.textContent = label;
    var v = document.createElement("b"); v.textContent = value; if (cls) v.className = cls;
    s.appendChild(l); s.appendChild(v); return s;
  }
  function render(d) {
    if (!d || !(d.last > 0) || d.stale) return;
    var items = [["XMZ/USDT", price(d.last), "tick-main"]];
    var c = d.change24hPercent;
    if (c != null) items.push(["24H", (c >= 0 ? "+" : "") + Number(c).toFixed(2) + "%", c > 0 ? "tick-up" : c < 0 ? "tick-down" : ""]);
    if (d.bid != null) items.push(["BID", price(d.bid)]);
    if (d.ask != null) items.push(["ASK", price(d.ask)]);
    if (d.volume24hXmz != null) items.push(["24H VOLUME", Math.round(d.volume24hXmz).toLocaleString("en-US") + " XMZ"]);
    items.push(["SOURCE", "NOIRTRADE"]);
    var frag = document.createDocumentFragment();
    // An even number of identical sets: the track slides by half its width, so it loops without a jump.
    for (var n = 0; n < 6; n++) items.forEach(function (it) { frag.appendChild(item(it[0], it[1], it[2])); });
    track.textContent = ""; track.appendChild(frag);
    band.classList.add("ticker-live");
    band.removeAttribute("aria-hidden");
    band.setAttribute("role", "link"); band.setAttribute("tabindex", "0");
    band.setAttribute("aria-label", "XMZ price on NoirTrade: " + price(d.last) + " USDT. Open the Markets page.");
  }
  function go() { location.href = "/markets/"; }
  band.addEventListener("click", function () { if (band.classList.contains("ticker-live")) go(); });
  band.addEventListener("keydown", function (e) { if (e.key === "Enter" && band.classList.contains("ticker-live")) go(); });
  function load() {
    fetch("/markets/xmz.php", { cache: "no-store" })
      .then(function (r) { return r.ok ? r.json() : null; }).then(render).catch(function () {});
  }
  load();
  setInterval(function () { if (!document.hidden) load(); }, 60000);
})();
