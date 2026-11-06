// Monzero community airdrop — dashboard logic. No seed/keys ever; only the public address and
// a secret dashboard token kept in this browser's localStorage.
const API = "/airdrop/api.php";
const TOKEN_KEY = "monzero_airdrop_token";
const REF_KEY = "monzero_airdrop_ref";
const $ = (id) => document.getElementById(id);
let CFG = null, BOT = "Monzero_bot";

const ls = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* private mode */ } },
  del(k) { try { localStorage.removeItem(k); } catch { /* ignore */ } },
};

async function api(action, { method = "GET", body = null, query = {} } = {}) {
  const q = new URLSearchParams({ action, ...query }).toString();
  const res = await fetch(`${API}?${q}`, {
    method, cache: "no-store", credentials: "omit",
    headers: body ? { "Content-Type": "application/json" } : {},
    body: body ? JSON.stringify(body) : null,
  });
  let data = {};
  try { data = await res.json(); } catch { /* empty */ }
  if (!res.ok) throw new Error(data.error || "Something went wrong. Try again.");
  return data;
}

function fmtPool(n) { return n > 0 ? (Number.isInteger(n) ? n.toLocaleString() : n.toLocaleString(undefined, { maximumFractionDigits: 4 })) + " XMZ" : "to be announced"; }

function shareText(refLink) {
  return `I'm earning @${CFG?.x || "MonzeroXMZ"} (XMZ) in the Monzero community airdrop — private, CPU-mined digital cash. No premine, no ICO. Join from zero: ${refLink} #Monzero #XMZ #privacy #RandomX`;
}

const TASKS = [
  { key: "wallet", title: "Create a wallet", desc: "You're in — your XMZ address is registered as your reward address." },
  { key: "telegram", title: "Join the Telegram", desc: "Join the channel, then verify in one tap." },
  { key: "mining", title: "Mine XMZ", desc: "Mine on the pool for 7 days over the month (they don't have to be in a row). Highest reward — real work for the network." },
  { key: "forum_post", title: "Join the forum & post", desc: "Sign up to the Monzero forum and make your first genuine post. A mod checks it.", review: true, openHref: "/community/board/", openLabel: "Open the forum ↗", placeholder: "Your forum username or post link" },
  { key: "forum_active", title: "Stay active on the forum", desc: "Over a week, make 5+ real posts (no spam). A mod reviews them for quality.", review: true, openHref: "/community/board/", openLabel: "Open the forum ↗", placeholder: "Your forum username" },
  { key: "run_node", title: "Run a node", desc: "Run your own Monzero node to strengthen the network, then submit its address. A mod verifies it's live.", review: true, openHref: "/mining/", openLabel: "Node setup \u2197", placeholder: "Your node IP or host, e.g. 203.0.113.9:6174" },
  { key: "share_x", title: "Share on X", desc: "Post about Monzero on X and paste the link. A moderator approves it.", review: true, openLabel: "Open X ↗", placeholder: "Paste your X post link" },
  { key: "share_instagram", title: "Share on Instagram", desc: "Post about Monzero on Instagram and paste the link. A moderator approves it.", review: true, openLabel: "Open Instagram ↗", placeholder: "Paste your Instagram post link" },
];

function badge(status) {
  if (status === "done" || status === "approved") return '<span class="ad-badge done">Done</span>';
  if (status === "pending") return '<span class="ad-badge pending">In review</span>';
  return '<span class="ad-badge">To do</span>';
}

function render(status, token) {
  $("ad-register").hidden = true;
  $("ad-dash").hidden = false;
  $("ad-total").textContent = status.earned;
  { const l = $("ad-score-label"); if (l) l.textContent = `XMZ earned · tasks ${status.task_earned}/${status.cap} + referrals ${status.referral_earned}/${status.referral_cap}`; }
  $("ad-myaddr").textContent = status.address;
  const refLink = `${location.origin}/airdrop/?ref=${status.ref_code}`;
  $("ad-reflink").value = refLink;
  $("ad-refcount").textContent = `${status.referrals} (${status.referral_earned}/${status.referral_cap} XMZ)`;
  $("ad-refpts").textContent = (CFG?.rewards?.referral ?? 1);
  // one-tap sharing of the referral link
  { const msg = "Join the Monzero airdrop and earn XMZ — private, CPU-mined crypto, no premine. Use my link:";
    const enc = encodeURIComponent, L = refLink;
    const sx = $("ad-share-x"), stg = $("ad-share-tg"), swa = $("ad-share-wa"), sn = $("ad-share-native");
    if (sx) sx.href = `https://twitter.com/intent/tweet?text=${enc(msg + " " + L + " #Monzero #XMZ #airdrop")}`;
    if (stg) stg.href = `https://t.me/share/url?url=${enc(L)}&text=${enc(msg)}`;
    if (swa) swa.href = `https://wa.me/?text=${enc(msg + " " + L)}`;
    if (sn && navigator.share) { sn.hidden = false; sn.onclick = () => { navigator.share({ title: "Monzero airdrop", text: msg, url: L }).catch(() => {}); }; }
  }

  const t = status.tasks, rw = status.rewards || (CFG?.rewards || {});
  const cards = TASKS.map((task) => {
    const st = t[task.key];
    const reward = rw[task.key] ?? 0;
    let act = "";
    if (task.key === "telegram" && st !== "done") {
      act = `<a class="btn primary" href="https://t.me/${BOT}?start=${token}" target="_blank" rel="noopener">Verify on Telegram ↗</a>
             <button class="btn" data-refresh>I've joined — refresh</button>`;
    } else if (task.key === "mining" && st !== "done") {
      act = `<p class="msg">Active days: <b>${status.mining_days || 0} / ${status.mining_days_required || 7}</b></p>
             <a class="btn primary" href="https://pool.monzero.org/" target="_blank" rel="noopener">Start mining ↗</a>
             <button class="btn" data-refresh>Check mining</button>`;
    } else if (task.review && st === "todo") {
      const href = task.key === "share_x" ? `https://twitter.com/intent/tweet?text=${encodeURIComponent(shareText(refLink))}`
        : task.key === "share_instagram" ? `https://instagram.com/${CFG?.instagram || "monzeroxmz"}`
        : (task.openHref || "#");
      const field = task.long
        ? `<textarea data-url="${task.key}" rows="3" placeholder="${task.placeholder}"></textarea>`
        : `<input data-url="${task.key}" placeholder="${task.placeholder}">`;
      act = `<a class="btn" href="${href}" target="_blank" rel="noopener">${task.openLabel}</a>
             <div class="share-in">${field}<button class="btn primary" data-share="${task.key}">Submit</button></div>
             <p class="msg" data-msg="${task.key}"></p>`;
    }
    return `<article class="ad-task ${st === "done" || st === "approved" ? "done" : ""}">
      <div class="ad-task-top"><h3>${task.title}</h3>${badge(st)}</div>
      <p>${task.desc}</p>
      <div class="ad-task-top"><span class="pts">${(st === "done" || st === "approved") ? reward + " XMZ" : "+" + reward + " XMZ"}</span></div>
      ${act ? `<div class="act">${act}</div>` : ""}
    </article>`;
  }).join("");
  $("ad-tasks").innerHTML = cards;

  // wire the dynamic buttons (no inline handlers — CSP safe)
  $("ad-tasks").querySelectorAll("[data-refresh]").forEach((b) => b.addEventListener("click", () => refresh(token)));
  $("ad-tasks").querySelectorAll("[data-share]").forEach((b) => b.addEventListener("click", () => submitShare(token, b.getAttribute("data-share"))));
}

async function submitShare(token, task) {
  const input = $("ad-tasks").querySelector(`[data-url="${task}"]`);
  const msg = $("ad-tasks").querySelector(`[data-msg="${task}"]`);
  if (!input) return;
  msg.textContent = "Submitting…"; msg.classList.remove("err");
  try {
    const r = await api("submit", { method: "POST", body: { token, task, detail: input.value.trim() } });
    render(r.status, token);
  } catch (e) { msg.textContent = e.message; msg.classList.add("err"); }
}

async function refresh(token) {
  try { const r = await api("status", { query: { token } }); render(r.status, token); }
  catch { ls.del(TOKEN_KEY); showRegister(); }
}

function showRegister() {
  $("ad-dash").hidden = true;
  $("ad-register").hidden = !(CFG && CFG.live);
  if (CFG && !CFG.live) $("ad-register-error").textContent = "The airdrop isn't open right now — check back soon.";
}

async function registerFlow() {
  const addr = $("ad-address").value.trim();
  const err = $("ad-register-error");
  err.textContent = "";
  if (!addr) { err.textContent = "Enter your XMZ address."; return; }
  $("ad-join-btn").disabled = true;
  try {
    const ref = ls.get(REF_KEY) || "";
    const r = await api("register", { method: "POST", body: { address: addr, ref } });
    if (r.token) ls.set(TOKEN_KEY, r.token);
    const token = r.token || ls.get(TOKEN_KEY);
    ls.del(REF_KEY);
    if (!r.token && !token) { err.textContent = "This address is already registered on another device — open this page there, or it will still count."; return; }
    render(r.status, token);
  } catch (e) { err.textContent = e.message; }
  finally { $("ad-join-btn").disabled = false; }
}

async function loadLeaderboard() {
  try {
    const { leaderboard } = await api("leaderboard");
    $("ad-board").innerHTML = leaderboard.length
      ? leaderboard.map((e) => `<li><span class="addr">${e.address}</span><span class="pts">${e.xmz} XMZ</span></li>`).join("")
      : '<li class="ad-muted">Be the first — complete a task to appear here.</li>';
  } catch { $("ad-board").innerHTML = '<li class="ad-muted">Leaderboard unavailable.</li>'; }
}

async function init() {
  // capture a referral code from the URL before anything else
  const url = new URL(location.href);
  const ref = url.searchParams.get("ref");
  if (ref && /^[A-Za-z0-9]{8}$/.test(ref)) ls.set(REF_KEY, ref);

  try { CFG = await api("config"); } catch { CFG = { live: false, points: {} }; }
  $("ad-pool").textContent = fmtPool(CFG.pool_xmz || 0);
  $("ad-state").textContent = CFG.live ? "Open" : "Not open yet";
  $("ad-summary").hidden = false;
  try { const t = await fetch("/api/telegram/", { cache: "no-store" }).then((r) => r.json()); if (t && t.username) BOT = t.username; } catch { /* keep default */ }

  $("ad-register-form").addEventListener("submit", (e) => { e.preventDefault(); registerFlow(); });
  $("ad-reset").addEventListener("click", () => { ls.del(TOKEN_KEY); showRegister(); });
  $("ad-refcopy").addEventListener("click", async () => {
    try { await navigator.clipboard.writeText($("ad-reflink").value); $("ad-refcopy").textContent = "Copied"; setTimeout(() => ($("ad-refcopy").textContent = "Copy"), 1500); } catch { $("ad-reflink").select(); }
  });

  const hashTok = location.hash.replace("#", "").trim();
  const token = (/^[a-f0-9]{32}$/.test(hashTok) ? hashTok : null) || ls.get(TOKEN_KEY);
  if (token) { ls.set(TOKEN_KEY, token); await refresh(token); } else showRegister();
  loadLeaderboard();
  // count towards participants shown in the summary
  try { const { leaderboard } = await api("leaderboard"); $("ad-count").textContent = leaderboard.length ? `${leaderboard.length}+` : "0"; } catch { /* ignore */ }
}

init();
