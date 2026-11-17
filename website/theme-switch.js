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

  // Styling lives in theme-switch.css (the CSP blocks <style> injected by JS). This only toggles a
  // class and sets a custom property via the CSSOM, both of which the CSP allows.
  // The filter recolours the body's children but not the body's own background (which also paints
  // the page margins), so that background is turned by the same hue-rotate matrix the browser uses.
  function rotateRgb(r, g, b, deg){
    var a = Math.cos(deg * Math.PI / 180), s = Math.sin(deg * Math.PI / 180);
    var m = [.213 + a*.787 - s*.213, .715 - a*.715 - s*.715, .072 - a*.072 + s*.928,
             .213 - a*.213 + s*.143, .715 + a*.285 + s*.140, .072 - a*.072 - s*.283,
             .213 - a*.213 - s*.787, .715 - a*.715 + s*.715, .072 + a*.928 + s*.072];
    function c(i){ return Math.max(0, Math.min(255, Math.round(m[i]*r + m[i+1]*g + m[i+2]*b))); }
    return [c(0), c(3), c(6)];
  }
  function rotateColours(css, deg){
    return css.replace(/rgba?\(\s*(\d+(?:\.\d+)?)[ ,]+(\d+(?:\.\d+)?)[ ,]+(\d+(?:\.\d+)?)\s*(?:[,/]\s*([\d.]+%?))?\s*\)/g,
      function(_, r, g, b, al){ var o = rotateRgb(+r, +g, +b, deg);
        return al !== undefined ? "rgba(" + o.join(", ") + ", " + al + ")" : "rgb(" + o.join(", ") + ")"; });
  }
  var pageBg = null;   // the body's own background before any tint
  function tintBackground(deg){
    var body = document.body; if (!body) return;
    if (pageBg === null){
      body.style.removeProperty("background-color"); body.style.removeProperty("background-image");
      var cs = getComputedStyle(body); pageBg = {color: cs.backgroundColor, image: cs.backgroundImage};
    }
    if (deg === null){ body.style.removeProperty("background-color"); body.style.removeProperty("background-image"); return; }
    body.style.setProperty("background-color", rotateColours(pageBg.color, deg));
    if (pageBg.image && pageBg.image !== "none") body.style.setProperty("background-image", rotateColours(pageBg.image, deg));
  }
  function apply(h){
    var root = document.documentElement, body = document.body;
    if (!h){ root.style.removeProperty("--mz-hue"); if (body) body.classList.remove("mz-tinted"); tintBackground(null); return; }
    var d = delta(h);
    root.style.setProperty("--mz-hue", d.toFixed(1) + "deg");
    if (body) body.classList.add("mz-tinted");
    tintBackground(d);
  }
  // The choice is shared by every *.monzero.org site (main site, web wallet, pool) through a cookie
  // on the parent domain; localStorage is kept as a fallback (and for older saved choices).
  var HEX = /^#[0-9a-f]{6}$/i;
  function cookieDomain(){ var h = location.hostname;
    return (h === "monzero.org" || /\.monzero\.org$/.test(h)) ? "; domain=.monzero.org" : ""; }
  function readCookie(){ var m = document.cookie.match(/(?:^|;\s*)monzero-accent=([^;]*)/);
    if (!m) return null; try { var v = decodeURIComponent(m[1]); return HEX.test(v) ? v : ""; } catch (e) { return ""; } }
  function writeCookie(h){ try {
    // An empty value is kept (not deleted) so "default" also wins over an old per-site localStorage choice.
    document.cookie = KEY + "=" + (h ? encodeURIComponent(h) : "") + "; max-age=31536000; path=/; samesite=lax" + (location.protocol === "https:" ? "; secure" : "") + cookieDomain();
  } catch (e) {} }
  function saved(){
    var c = readCookie(); if (c !== null) return c;
    var l = ""; try { l = localStorage.getItem(KEY) || ""; } catch (e) {}
    if (HEX.test(l)) { writeCookie(l); return l; }   // carry an older per-site choice over to all sites
    return "";
  }
  function store(h){ writeCookie(h); try { h ? localStorage.setItem(KEY, h) : localStorage.removeItem(KEY); } catch (e) {} }
  apply(saved());

  function build(){
    if (document.getElementById("mz-theme") || !document.body) return;
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
