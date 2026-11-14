// Airdrop moderator panel: tabs, referral link + QR, banners (with a personal QR stamped on
// the social sizes), embed codes and ready-to-post copy. No network calls; everything is built
// in the browser from the page's data attributes.
import { encodeQr } from "/payments/request/qr.mjs";

const root = document.getElementById("adm");
document.documentElement.classList.add("js");
const $ = (id) => document.getElementById(id);

// ---------- Tabs ----------
const TABS = ["overview", "review", "promote", "leaderboard", "rewards"];
function showTab(name) {
  if (!TABS.includes(name)) name = "overview";
  for (const t of TABS) $(t)?.classList.toggle("on", t === name);
  for (const a of document.querySelectorAll(".adm-tabs a")) a.classList.toggle("on", a.dataset.tab === name);
}
document.addEventListener("click", (ev) => {
  const a = ev.target.closest("a[data-tab]");
  if (!a) return;
  ev.preventDefault();
  history.replaceState(null, "", "#" + a.dataset.tab);
  showTab(a.dataset.tab);
  window.scrollTo({ top: 0 });
});
window.addEventListener("hashchange", () => showTab(location.hash.slice(1)));
showTab(location.hash.slice(1));
requestAnimationFrame(() => window.scrollTo({ top: 0 }));  // a #tab link must not jump past the header
if (!root) throw new Error("not signed in");

// ---------- Clipboard ----------
async function copyText(text, button) {
  try { await navigator.clipboard.writeText(text); }
  catch {
    const t = document.createElement("textarea");
    t.value = text; document.body.append(t); t.select();
    try { document.execCommand("copy"); } catch { /* ignore */ }
    t.remove();
  }
  if (button) {
    const old = button.textContent;
    button.textContent = "Copied ✓"; button.classList.add("done");
    setTimeout(() => { button.textContent = old; button.classList.remove("done"); }, 1600);
  }
}
document.addEventListener("click", (ev) => {
  const b = ev.target.closest("[data-copy-input]");
  if (b) copyText($(b.dataset.copyInput).value, b);
});

// ---------- Referral link ----------
const D = root.dataset;
const CAP = D.cap || "25", ENDS = D.ends || "", REFREWARD = D.refreward || "0.5";
const TG = D.telegram || "MonzeroOfficial", XH = D.x || "MonzeroXMZ";
const STORE = "monzero_mod_ref";
const validCode = (c) => /^[A-Za-z0-9]{8}$/.test(c || "");
let code = D.ref || "";
try { const saved = localStorage.getItem(STORE); if (!code && validCode(saved)) code = saved; } catch { /* private mode */ }
const link = () => `${location.origin}/airdrop/${validCode(code) ? "?ref=" + code : ""}`;
const enc = encodeURIComponent;

// ---------- QR ----------
function drawQr(ctx, text, x, y, size) {
  const qr = encodeQr(text);
  const total = qr.size + 8, cell = size / total;
  ctx.fillStyle = "#fff"; ctx.fillRect(x, y, size, size);
  ctx.fillStyle = "#06121a";
  qr.modules.forEach((row, r) => row.forEach((dark, c) => {
    if (dark) ctx.fillRect(Math.floor(x + (c + 4) * cell), Math.floor(y + (r + 4) * cell), Math.ceil(cell), Math.ceil(cell));
  }));
}
function renderQr() {
  const cv = $("ref-qr"), ctx = cv.getContext("2d");
  cv.width = cv.height = 400;
  drawQr(ctx, link(), 0, 0, 400);
}
$("qr-download").addEventListener("click", () => {
  const cv = document.createElement("canvas"); cv.width = cv.height = 1024;
  drawQr(cv.getContext("2d"), link(), 0, 0, 1024);
  save(cv, `monzero-airdrop-qr${validCode(code) ? "-" + code : ""}.png`, "image/png");
});

function save(canvas, name, type = "image/jpeg") {
  canvas.toBlob((blob) => {
    const url = URL.createObjectURL(blob);
    const a = Object.assign(document.createElement("a"), { href: url, download: name });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 4000);
  }, type, 0.92);
}

// ---------- Banners ----------
// qr: where the personal QR card goes on the full-size image (kept clear in the artwork).
const BANNERS = [
  { file: "airdrop-post-1600x900.jpg", title: "Landscape post", dim: "1600 × 900", kind: "social", use: "X, Telegram, Discord, Facebook, LinkedIn", qr: { x: 1220, y: 490, w: 300 } },
  { file: "airdrop-square-1080x1080.jpg", title: "Square post", dim: "1080 × 1080", kind: "social", use: "Instagram feed, Facebook, Threads", qr: { x: 760, y: 720, w: 250 } },
  { file: "airdrop-story-1080x1920.jpg", title: "Story", dim: "1080 × 1920", kind: "story", use: "Instagram / Facebook stories, TikTok, WhatsApp status", qr: { x: 380, y: 1245, w: 320 } },
  { file: "airdrop-x-header-1500x500.jpg", title: "Profile header", dim: "1500 × 500", kind: "header", use: "X / Twitter header while the round runs" },
  { file: "airdrop-web-728x90.jpg", title: "Leaderboard ad", dim: "728 × 90", kind: "web", use: "Top of a website or forum", w: 728, h: 90 },
  { file: "airdrop-web-970x250.jpg", title: "Billboard ad", dim: "970 × 250", kind: "web", use: "Wide website header slot", w: 970, h: 250 },
  { file: "airdrop-web-300x250.jpg", title: "Medium rectangle", dim: "300 × 250", kind: "web", use: "Sidebars, blog posts, forum signatures", w: 300, h: 250 },
  { file: "airdrop-web-160x600.jpg", title: "Skyscraper", dim: "160 × 600", kind: "web", use: "Website sidebars", w: 160, h: 600 },
];
const PROMO = "/airdrop/promo/";
const abs = (f) => `${location.origin}${PROMO}${f}`;
const html = (s) => s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

function renderBanners() {
  $("banners").innerHTML = BANNERS.map((b, i) => `
    <article class="bcard" data-kind="${b.kind}">
      <div class="bthumb"><img src="${PROMO}thumbs/${b.file}" alt="${html(b.title)} banner" loading="lazy"></div>
      <div class="binfo">
        <h4>${html(b.title)} <span class="dim">${b.dim}</span></h4>
        <p>${html(b.use)}</p>
        <div class="bbtns">
          ${b.qr ? `<button class="btn primary small" data-qrimg="${i}">Download with my QR</button>` : ""}
          <a class="btn small${b.qr ? " ghost" : ""}" href="${PROMO}${b.file}" download>Download</a>
          ${b.kind === "web" ? `<button class="btn ghost small" data-embed="html" data-i="${i}">Copy HTML</button><button class="btn ghost small" data-embed="bb" data-i="${i}">Copy forum code</button>` : ""}
        </div>
      </div>
    </article>`).join("");
}
function embed(kind, b) {
  const alt = `Monzero community airdrop: earn up to ${CAP} XMZ`;
  return kind === "html"
    ? `<a href="${link()}" target="_blank" rel="noopener"><img src="${abs(b.file)}" width="${b.w}" height="${b.h}" alt="${alt}"></a>`
    : `[url=${link()}][img]${abs(b.file)}[/img][/url]`;
}
function roundRect(ctx, x, y, w, h, r) {
  ctx.beginPath(); ctx.moveTo(x + r, y); ctx.arcTo(x + w, y, x + w, y + h, r); ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath();
}
async function bannerWithQr(b, button) {
  button.disabled = true; const label = button.textContent; button.textContent = "Preparing…";
  try {
    const img = new Image(); img.src = PROMO + b.file; await img.decode();
    const cv = document.createElement("canvas"); cv.width = img.naturalWidth; cv.height = img.naturalHeight;
    const ctx = cv.getContext("2d"); ctx.drawImage(img, 0, 0);
    const { x, y, w } = b.qr, pad = Math.round(w * 0.06), cap = Math.round(w * 0.24);
    ctx.save(); ctx.shadowColor = "rgba(0,0,0,.45)"; ctx.shadowBlur = 30; ctx.fillStyle = "#ffffff";
    roundRect(ctx, x, y, w, w + cap, Math.round(w * 0.07)); ctx.fill(); ctx.restore();
    drawQr(ctx, link(), x + pad, y + pad, w - pad * 2);
    ctx.fillStyle = "#06121a"; ctx.textAlign = "center"; ctx.textBaseline = "middle";
    ctx.font = `700 ${Math.round(w * 0.075)}px system-ui, "Segoe UI", sans-serif`;
    ctx.fillText(validCode(code) ? "Scan to join · my link" : "Scan to join", x + w / 2, y + w + cap * 0.25);
    ctx.font = `500 ${Math.round(w * 0.06)}px ui-monospace, monospace`; ctx.fillStyle = "#38535e";
    ctx.fillText(validCode(code) ? `ref ${code}` : "monzero.org/airdrop", x + w / 2, y + w + cap * 0.62);
    save(cv, b.file.replace(".jpg", validCode(code) ? `-${code}.jpg` : "-qr.jpg"));
  } catch { alert("Couldn't build the image in this browser. Use the plain download instead."); }
  finally { button.disabled = false; button.textContent = label; }
}
$("banners").addEventListener("click", (ev) => {
  const q = ev.target.closest("[data-qrimg]");
  if (q) return bannerWithQr(BANNERS[+q.dataset.qrimg], q);
  const e = ev.target.closest("[data-embed]");
  if (e) copyText(embed(e.dataset.embed, BANNERS[+e.dataset.i]), e);
});
document.querySelector(".banner-filter").addEventListener("click", (ev) => {
  const c = ev.target.closest("[data-filter]"); if (!c) return;
  for (const x of document.querySelectorAll(".banner-filter .chip")) x.classList.toggle("on", x === c);
  for (const card of document.querySelectorAll(".bcard")) card.hidden = c.dataset.filter !== "all" && card.dataset.kind !== c.dataset.filter;
});

// ---------- Ready-to-post copy ----------
const SNIPPETS = [
  { key: "x", title: "X post", note: "Fits in one post. Links count as 23 characters.", x: true,
    text: (L) => `The @${XH} community airdrop is live 🪂\n\nEarn up to ${CAP} $XMZ: mine with your CPU, join the Telegram, post on the forum, share.\n\nPrivate, CPU-mined, no premine, no ICO. Ends ${ENDS}.\n\n${L}\n\n#Monzero #XMZ #airdrop #privacy` },
  { key: "thread", title: "X thread", note: "Three posts. Paste them as a thread.",
    text: (L) => `1/ Monzero (XMZ) is private digital cash anyone can mine with a normal CPU. No premine, no ICO, no token sale.\n\nThe community airdrop is open until ${ENDS} 🧵\n\n2/ Each task pays a set amount of XMZ, up to ${CAP} XMZ per person:\n⛏️ Mine on the pool for 7 days\n💬 Join the Telegram\n📝 Post on the forum\n🖥️ Run a node\n📣 Share Monzero\n\n3/ Rewards come from the mining pool's fee, not a token sale. No seed, keys or payment are ever asked for.\n\nJoin from zero 👇\n${L}\n\n#Monzero #XMZ` },
  { key: "tg", title: "Telegram / WhatsApp", note: "For your own groups and channels.",
    text: (L) => `🪂 Monzero community airdrop\n\nEarn up to ${CAP} XMZ by doing real things for the network:\n• Mine XMZ with any CPU (7 active days)\n• Join the Telegram: t.me/${TG}\n• Post on the Monzero forum\n• Run a node or share Monzero\n\nNo seed, no private keys, no payment — only your public XMZ address. Ends ${ENDS}.\n\n👉 ${L}` },
  { key: "friend", title: "Message a friend", note: "Short and personal. Only send to people you know.",
    text: (L) => `Hey! I've been mining Monzero (XMZ) — private crypto you can mine on a normal computer. Their community airdrop pays up to ${CAP} XMZ for mining, joining the Telegram and the forum. Takes a few minutes to start: ${L}` },
  { key: "reddit", title: "Reddit post", note: "Check each subreddit's rules on promotion first. Title on the first line.",
    text: (L) => `Monzero (XMZ) community airdrop: earn up to ${CAP} XMZ for mining, forum posts and running a node (ends ${ENDS})\n\nMonzero is a CPU-mineable (RandomX) privacy coin with no premine and no ICO. The project is running a community airdrop funded by the mining pool's fee.\n\nHow it works:\n- Register your public XMZ address (never your seed)\n- Each task pays a fixed amount: mining for 7 days, joining the Telegram, posting on the forum, running a node, sharing\n- Up to ${CAP} XMZ per person, paid in one batch at the end\n\nDetails and sign-up: ${L}\n\nDisclosure: I'm a moderator for the airdrop. Happy to answer questions.` },
  { key: "discord", title: "Discord", note: "Markdown formatting works in Discord.",
    text: (L) => `**🪂 Monzero community airdrop is open**\nEarn up to **${CAP} XMZ** — mine, join the Telegram, post on the forum, run a node.\n> No seed, no keys, no payment. Only your public XMZ address.\nEnds **${ENDS}** → ${L}` },
  { key: "forum", title: "Forum (BBCode)", note: "For bitcointalk-style forums that use BBCode.",
    text: (L) => `[b]Monzero (XMZ) community airdrop — earn up to ${CAP} XMZ[/b]\n\n[url=${L}][img]${abs("airdrop-post-1600x900.jpg")}[/img][/url]\n\nMonzero is private, CPU-mined (RandomX) digital cash. No premine, no ICO.\n\n[list]\n[*]Mine on the pool for 7 days\n[*]Join the Telegram\n[*]Post on the Monzero forum\n[*]Run a node, share Monzero\n[/list]\n\nRewards come from the mining pool's fee. Ends ${ENDS}.\n[url=${L}]Join the airdrop[/url] — only your public address is needed, never your seed.` },
  { key: "ig", title: "Instagram / TikTok caption", note: "Links aren't clickable in captions: put your link in your bio and use the QR banner.",
    text: () => `Private money you can mine on your own computer 💻⛏️\n\nThe Monzero community airdrop pays up to ${CAP} XMZ for mining, joining the Telegram and posting on the forum. Ends ${ENDS}.\n\nScan the QR or tap the link in bio 🔗\n\n#Monzero #XMZ #crypto #privacy #mining #airdrop #cpumining` },
  { key: "email", title: "Email", note: "Subject on the first line.",
    text: (L) => `Earn some Monzero (XMZ) in their community airdrop\n\nHi,\n\nI thought you might like this. Monzero is a privacy-focused cryptocurrency that anyone can mine with a normal CPU — no premine and no ICO. They're running a community airdrop until ${ENDS}: you can earn up to ${CAP} XMZ for mining for a week, joining the Telegram and taking part on the forum.\n\nYou only ever give your public XMZ address — never a seed, password or payment.\n\nSign up here: ${L}\n\nCheers` },
];
let current = SNIPPETS[0];
function renderSnip() {
  $("snip-title").textContent = current.title;
  $("snip-note").textContent = current.note;
  $("snip-text").value = current.text(link());
  countSnip();
  for (const b of document.querySelectorAll("#snip-tabs .chip")) b.classList.toggle("on", b.dataset.k === current.key);
}
function countSnip() {
  const t = $("snip-text").value;
  const xlen = t.replace(/https?:\/\/\S+/g, "x".repeat(23)).length;
  $("snip-count").textContent = current.x ? `${xlen} / 280 characters on X${xlen > 280 ? " — too long" : ""}` : `${t.length} characters`;
}
$("snip-tabs").innerHTML = SNIPPETS.map((s) => `<button class="chip" data-k="${s.key}">${html(s.title)}</button>`).join("");
$("snip-tabs").addEventListener("click", (ev) => {
  const b = ev.target.closest("[data-k]"); if (!b) return;
  current = SNIPPETS.find((s) => s.key === b.dataset.k) || SNIPPETS[0]; renderSnip();
});
$("snip-text").addEventListener("input", countSnip);

// ---------- Hashtags ----------
const TAGS = ["#Monzero", "#XMZ", "$XMZ", "#airdrop", "#privacy", "#crypto", "#CPUmining", "#RandomX", "#nopremine", `@${XH}`];
$("tags").innerHTML = TAGS.map((t) => `<button class="chip" data-tag="${html(t)}">${html(t)}</button>`).join("") + `<button class="chip" data-tag="${html(TAGS.slice(0, 6).join(" "))}">Copy set</button>`;
$("tags").addEventListener("click", (ev) => { const b = ev.target.closest("[data-tag]"); if (b) copyText(b.dataset.tag, b); });

// ---------- Wire-up ----------
function refreshAll() {
  const L = link(), msg = `Join the Monzero community airdrop and earn up to ${CAP} XMZ — private, CPU-mined, no premine.`;
  $("ref-link").value = L;
  $("sh-x").href = `https://twitter.com/intent/tweet?text=${enc(msg + " " + L + " #Monzero #XMZ")}`;
  $("sh-tg").href = `https://t.me/share/url?url=${enc(L)}&text=${enc(msg)}`;
  $("sh-wa").href = `https://wa.me/?text=${enc(msg + " " + L)}`;
  $("sh-rd").href = `https://www.reddit.com/submit?url=${enc(L)}&title=${enc("Monzero (XMZ) community airdrop: earn up to " + CAP + " XMZ")}`;
  $("sh-fb").href = `https://www.facebook.com/sharer/sharer.php?u=${enc(L)}`;
  $("sh-em").href = `mailto:?subject=${enc("Monzero community airdrop")}&body=${enc(msg + "\n\n" + L)}`;
  renderQr(); renderSnip();
}
const codeInput = $("ref-code");
codeInput.value = code;
codeInput.addEventListener("input", () => {
  const v = codeInput.value.trim();
  codeInput.setCustomValidity(v === "" || validCode(v) ? "" : "8 letters or digits");
  if (v === "" || validCode(v)) {
    code = v;
    try { v ? localStorage.setItem(STORE, v) : localStorage.removeItem(STORE); } catch { /* ignore */ }
    refreshAll();
  }
});
renderBanners();
refreshAll();
