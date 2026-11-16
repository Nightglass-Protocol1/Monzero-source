/* Monzero site accent switcher. Lets a visitor pick their own accent colour; the choice is
 * saved in this browser (localStorage) and applies on every page. It overrides the accent
 * custom properties of BOTH site design systems — the brand pages use --accent, the luxe
 * homepage uses --cyan — plus derived shades, so the whole site recolours together.
 * Self-contained: no dependencies, injects its own styles, and defaults to the site's teal. */
(function () {
  "use strict";
  var KEY = "monzero-accent", DEFAULT = "#3fdfe8";
  // name, hex ("" = the site default teal)
  var PRESETS = [
    ["Teal (default)", ""], ["Violet", "#a78bfa"], ["Indigo", "#7c8cff"], ["Sky", "#56b6ff"],
    ["Emerald", "#34e39b"], ["Lime", "#b6e24a"], ["Gold", "#f4c454"], ["Coral", "#ff8a6b"], ["Rose", "#fb7a99"]
  ];
  function rgbOf(h){ h = h.replace("#",""); if (h.length === 3) h = h.replace(/./g,"$&$&");
    return [parseInt(h.slice(0,2),16), parseInt(h.slice(2,4),16), parseInt(h.slice(4,6),16)]; }
  function mix(rgb, t, amt){ return rgb.map(function(c){ return Math.round(c + (t - c) * amt); }); }
  function hex(rgb){ return "#" + rgb.map(function(c){ return ("0" + Math.max(0,Math.min(255,c)).toString(16)).slice(-2); }).join(""); }
  var ACCENT_VARS = ["--accent","--accent-2","--accent-strong","--accent-muted","--accent-soft","--accent-rgb",
                     "--cyan","--cyan-hot","--cyan-deep","--cyan-rgb","--glow","--lime","--green"];
  function apply(h){
    var s = document.documentElement.style;
    if (!h){ ACCENT_VARS.forEach(function(v){ s.removeProperty(v); }); return; }  // back to the CSS default
    var rgb = rgbOf(h), lighter = hex(mix(rgb,255,.40)), light = hex(mix(rgb,255,.18)),
        darker = hex(mix(rgb,0,.40)), deep = hex(mix(rgb,0,.58)), csv = rgb.join(", ");
    s.setProperty("--accent", h);        s.setProperty("--accent-2", lighter);   s.setProperty("--accent-strong", light);
    s.setProperty("--accent-muted", darker); s.setProperty("--accent-soft", deep); s.setProperty("--accent-rgb", csv);
    s.setProperty("--cyan", h);          s.setProperty("--cyan-hot", lighter);    s.setProperty("--cyan-deep", darker);
    s.setProperty("--cyan-rgb", csv);    s.setProperty("--glow", "rgba(" + csv + ", .35)");
    s.setProperty("--lime", h);          s.setProperty("--green", h);
  }
  function saved(){ try { return localStorage.getItem(KEY) || ""; } catch (e) { return ""; } }
  function store(h){ try { h ? localStorage.setItem(KEY, h) : localStorage.removeItem(KEY); } catch (e) {} }
  apply(saved());  // run as early as possible (documentElement exists even in <head>)

  function styles(){
    if (document.getElementById("mz-theme-css")) return;
    var css = document.createElement("style"); css.id = "mz-theme-css";
    css.textContent =
      ".mz-theme{position:fixed;right:14px;bottom:14px;z-index:9999;font-family:inherit}" +
      ".mz-theme-btn{display:grid;place-items:center;width:42px;height:42px;border-radius:50%;cursor:pointer;" +
        "background:rgba(10,22,30,.9);border:1px solid rgba(255,255,255,.14);backdrop-filter:blur(6px);box-shadow:0 6px 20px rgba(0,0,0,.35)}" +
      ".mz-theme-btn:hover{border-color:var(--accent,#3fdfe8)}" +
      ".mz-theme-dot{width:17px;height:17px;border-radius:50%;background:var(--accent,#3fdfe8);box-shadow:0 0 0 3px rgba(255,255,255,.08)}" +
      ".mz-theme-pop{position:absolute;right:0;bottom:52px;width:208px;padding:13px;border-radius:14px;" +
        "background:rgba(9,18,24,.97);border:1px solid rgba(255,255,255,.14);box-shadow:0 14px 34px rgba(0,0,0,.5);color:#eaf4f6}" +
      ".mz-theme-pop[hidden]{display:none}" +
      ".mz-theme-h{margin:0 0 9px;font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:#8aa3b2}" +
      ".mz-theme-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:8px}" +
      ".mz-sw{width:100%;aspect-ratio:1;border-radius:50%;border:2px solid transparent;cursor:pointer;padding:0;outline:none}" +
      ".mz-sw:hover{transform:scale(1.08)}" +
      ".mz-sw[aria-current=true]{border-color:#eaf4f6;box-shadow:0 0 0 2px rgba(0,0,0,.6) inset}" +
      ".mz-theme-custom{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-top:12px;" +
        "font-size:12.5px;color:#8aa3b2;cursor:pointer}" +
      ".mz-theme-custom input{width:34px;height:26px;padding:0;border:1px solid rgba(255,255,255,.2);border-radius:7px;background:transparent;cursor:pointer}" +
      "@media print{.mz-theme{display:none}}";
    document.head.appendChild(css);
  }
  function build(){
    if (document.getElementById("mz-theme") || !document.body) return;
    styles();
    var wrap = document.createElement("div"); wrap.id = "mz-theme"; wrap.className = "mz-theme";
    var btn = document.createElement("button"); btn.type = "button"; btn.className = "mz-theme-btn";
    btn.setAttribute("aria-label", "Change accent colour"); btn.setAttribute("aria-expanded", "false");
    btn.innerHTML = '<span class="mz-theme-dot"></span>';
    var pop = document.createElement("div"); pop.className = "mz-theme-pop"; pop.hidden = true;
    pop.innerHTML = '<p class="mz-theme-h">Accent colour</p><div class="mz-theme-grid"></div>' +
      '<label class="mz-theme-custom">Custom<input type="color" aria-label="Custom accent colour"></label>';
    var grid = pop.querySelector(".mz-theme-grid"), picker = pop.querySelector("input");
    function mark(){ var cur = saved();
      grid.querySelectorAll(".mz-sw").forEach(function(sw, i){
        if ((PRESETS[i][1] || "") === cur) sw.setAttribute("aria-current", "true"); else sw.removeAttribute("aria-current"); });
      picker.value = cur || DEFAULT; }
    PRESETS.forEach(function(p){
      var sw = document.createElement("button"); sw.type = "button"; sw.className = "mz-sw";
      sw.title = p[0]; sw.style.background = p[1] || DEFAULT;
      sw.addEventListener("click", function(){ store(p[1]); apply(p[1]); mark(); });
      grid.appendChild(sw);
    });
    picker.addEventListener("input", function(){ store(picker.value); apply(picker.value); mark(); });
    btn.addEventListener("click", function(e){ e.stopPropagation(); var open = pop.hidden; pop.hidden = !open; btn.setAttribute("aria-expanded", String(open)); });
    document.addEventListener("click", function(e){ if (!wrap.contains(e.target)){ pop.hidden = true; btn.setAttribute("aria-expanded", "false"); } });
    wrap.appendChild(btn); wrap.appendChild(pop); document.body.appendChild(wrap); mark();
  }
  if (document.readyState !== "loading") build(); else document.addEventListener("DOMContentLoaded", build);
})();
