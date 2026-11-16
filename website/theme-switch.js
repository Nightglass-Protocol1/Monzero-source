/* Monzero site accent switcher. Lets a visitor pick their own accent colour; the choice is saved
 * in this browser (localStorage) and applies on every page. The site is painted in many bespoke
 * teal/mint gradients that aren't tied to one variable, so recolouring is done with a global
 * hue-rotate: it shifts every accent, gradient and glow toward the chosen hue at once. The logo
 * and photos are counter-rotated so they stay true. The default (teal) applies NO filter at all,
 * so sticky/fixed headers are untouched unless the visitor opts into a colour. Self-contained. */
(function () {
  "use strict";
  var KEY = "monzero-accent", BASE_HUE = 183;   // teal #3fdfe8
  // name, hex ("" = the site's default teal)
  var PRESETS = [
    ["Teal (default)", ""], ["Violet", "#a78bfa"], ["Indigo", "#7c8cff"], ["Sky", "#56b6ff"],
    ["Emerald", "#34e39b"], ["Lime", "#b6e24a"], ["Gold", "#f4c454"], ["Coral", "#ff8a6b"], ["Rose", "#fb7a99"]
  ];
  function rgb(h){ h = h.replace("#",""); if (h.length === 3) h = h.replace(/./g,"$&$&");
    return [parseInt(h.slice(0,2),16), parseInt(h.slice(2,4),16), parseInt(h.slice(4,6),16)]; }
  function hue(h){ var c = rgb(h).map(function(x){ return x/255; }), r = c[0], g = c[1], b = c[2],
      mx = Math.max(r,g,b), mn = Math.min(r,g,b), d = mx - mn, H = 0;
    if (d === 0) H = 0; else if (mx === r) H = ((g-b)/d) % 6; else if (mx === g) H = (b-r)/d + 2; else H = (r-g)/d + 4;
    H *= 60; if (H < 0) H += 360; return H; }
  function delta(h){ var d = (hue(h) - BASE_HUE) % 360; if (d > 180) d -= 360; if (d < -180) d += 360; return d; }

  function injectStyle(){
    if (document.getElementById("mz-tint-css") || !document.head) return;
    var s = document.createElement("style"); s.id = "mz-tint-css";
    // Each top-level element is filtered on itself (so its own sticky/fixed keeps working), and
    // images are counter-rotated back to their true colour. Only active when body.mz-tinted is set.
    s.textContent =
      "body.mz-tinted>*:not(#mz-theme){filter:hue-rotate(var(--mz-hue,0deg))}" +
      "body.mz-tinted img{filter:hue-rotate(calc(-1*var(--mz-hue,0deg)))}";
    document.head.appendChild(s);
  }
  function apply(h){
    injectStyle();
    var root = document.documentElement, body = document.body;
    if (!h){ root.style.removeProperty("--mz-hue"); if (body) body.classList.remove("mz-tinted"); return; }
    root.style.setProperty("--mz-hue", delta(h).toFixed(1) + "deg");
    if (body) body.classList.add("mz-tinted");
  }
  function saved(){ try { return localStorage.getItem(KEY) || ""; } catch (e) { return ""; } }
  function store(h){ try { h ? localStorage.setItem(KEY, h) : localStorage.removeItem(KEY); } catch (e) {} }
  apply(saved());

  function styles(){
    if (document.getElementById("mz-theme-css")) return;
    var css = document.createElement("style"); css.id = "mz-theme-css";
    css.textContent =
      ".mz-theme{position:fixed;left:14px;bottom:14px;z-index:9999;font-family:inherit;filter:none!important}" +
      ".mz-theme-btn{display:grid;place-items:center;width:42px;height:42px;border-radius:50%;cursor:pointer;" +
        "background:rgba(10,22,30,.92);border:1px solid rgba(255,255,255,.14);box-shadow:0 6px 20px rgba(0,0,0,.4)}" +
      ".mz-theme-btn:hover{border-color:rgba(255,255,255,.4)}" +
      ".mz-theme-dot{width:17px;height:17px;border-radius:50%;background:#3fdfe8;box-shadow:0 0 0 3px rgba(255,255,255,.08)}" +
      ".mz-theme-pop{position:absolute;left:0;bottom:52px;width:210px;padding:13px;border-radius:14px;" +
        "background:rgba(9,18,24,.98);border:1px solid rgba(255,255,255,.14);box-shadow:0 14px 34px rgba(0,0,0,.55);color:#eaf4f6}" +
      ".mz-theme-pop[hidden]{display:none}" +
      ".mz-theme-h{margin:0 0 9px;font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:#8aa3b2}" +
      ".mz-theme-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:8px}" +
      ".mz-sw{width:100%;aspect-ratio:1;border-radius:50%;border:2px solid transparent;cursor:pointer;padding:0}" +
      ".mz-sw:hover{transform:scale(1.08)}" +
      ".mz-sw[aria-current=true]{border-color:#eaf4f6;box-shadow:0 0 0 2px rgba(0,0,0,.6) inset}" +
      ".mz-theme-custom{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-top:12px;font-size:12.5px;color:#8aa3b2;cursor:pointer}" +
      ".mz-theme-custom input{width:34px;height:26px;padding:0;border:1px solid rgba(255,255,255,.2);border-radius:7px;background:transparent;cursor:pointer}" +
      ".mz-theme-reset{display:block;width:100%;margin-top:11px;padding:7px;border:1px solid rgba(255,255,255,.14);background:transparent;color:#8aa3b2;border-radius:8px;font:500 12px inherit;cursor:pointer}" +
      ".mz-theme-reset:hover{color:#eaf4f6}" +
      "@media print{.mz-theme{display:none}}";
    document.head.appendChild(css);
  }
  function build(){
    if (document.getElementById("mz-theme") || !document.body) return;
    styles();
    var wrap = document.createElement("div"); wrap.id = "mz-theme"; wrap.className = "mz-theme";
    var btn = document.createElement("button"); btn.type = "button"; btn.className = "mz-theme-btn";
    btn.setAttribute("aria-label", "Change accent colour"); btn.setAttribute("aria-expanded", "false");
    var dot = document.createElement("span"); dot.className = "mz-theme-dot"; btn.appendChild(dot);
    var pop = document.createElement("div"); pop.className = "mz-theme-pop"; pop.hidden = true;
    pop.innerHTML = '<p class="mz-theme-h">Accent colour</p><div class="mz-theme-grid"></div>' +
      '<label class="mz-theme-custom">Custom<input type="color" aria-label="Custom accent colour"></label>' +
      '<button type="button" class="mz-theme-reset">Reset to default</button>';
    var grid = pop.querySelector(".mz-theme-grid"), picker = pop.querySelector("input"), reset = pop.querySelector(".mz-theme-reset");
    function refresh(){ var cur = saved();
      dot.style.background = cur || "#3fdfe8";
      picker.value = cur || "#3fdfe8";
      grid.querySelectorAll(".mz-sw").forEach(function(sw, i){
        if ((PRESETS[i][1] || "") === cur) sw.setAttribute("aria-current","true"); else sw.removeAttribute("aria-current"); });
    }
    function choose(h){ store(h); apply(h); refresh(); }
    PRESETS.forEach(function(p){
      var sw = document.createElement("button"); sw.type = "button"; sw.className = "mz-sw";
      sw.title = p[0]; sw.style.background = p[1] || "#3fdfe8";
      sw.addEventListener("click", function(){ choose(p[1]); });
      grid.appendChild(sw);
    });
    picker.addEventListener("input", function(){ choose(picker.value); });
    reset.addEventListener("click", function(){ choose(""); });
    btn.addEventListener("click", function(e){ e.stopPropagation(); var open = pop.hidden; pop.hidden = !open; btn.setAttribute("aria-expanded", String(open)); });
    document.addEventListener("click", function(e){ if (!wrap.contains(e.target)){ pop.hidden = true; btn.setAttribute("aria-expanded","false"); } });
    wrap.appendChild(btn); wrap.appendChild(pop); document.body.appendChild(wrap);
    apply(saved());   // ensure body class is set now that <body> exists
    refresh();
  }
  if (document.readyState !== "loading") build(); else document.addEventListener("DOMContentLoaded", build);
})();
