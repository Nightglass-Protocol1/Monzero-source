// Monzero web wallet page. All wallet work is Monzero's own wallet code (WebAssembly) running in a
// web worker in this browser; this file only drives the screens. Never put untrusted text into
// innerHTML: everything shown goes through textContent.
import moneroTs from "monero-ts";
import qrcode from "qrcode-generator";
import { loadWallet, saveWallet, removeWallet } from "./store.js";

const NETWORK = __NETWORK__;                       // fixed per build: "testnet" or "mainnet"
const RELEASE = __RELEASE__;
const NET_TYPE = NETWORK === "mainnet" ? moneroTs.MoneroNetworkType.MAINNET : moneroTs.MoneroNetworkType.TESTNET;
const NODE = location.origin;                     // same-origin node gateway (allowlisted paths at the site root)
const COIN = 100000000000n;                        // 1 XMZ = 10^11 atomic units
const LOCK_AFTER_MS = 10 * 60 * 1000;

moneroTs.LibraryUtils.setWorkerDistPath(new URL("monero.worker.js", document.baseURI).href);

const $ = (id) => document.getElementById(id);
let wallet = null;            // the open wallet (in the worker)
let pending = null;           // a built, not yet sent transaction
let newWords = null;          // recovery words of a wallet being created, until checked
let lockTimer = null;
let saving = Promise.resolve();

// ---------- screens ----------
const screens = ["start", "password", "words", "verify", "unlock", "busy", "wallet"];
function show(name, busyText) {
  for (const s of screens) $("s-" + s).hidden = s !== name;
  if (busyText) $("busyText").textContent = busyText;
  $("lockBtn").hidden = name !== "wallet";
}
function notice(text) { $("notice").textContent = text || ""; $("notice").hidden = !text; }

// ---------- amounts and addresses ----------
// Exact decimal parsing: digits, optional point, at most 11 decimals; no floating point anywhere.
function parseXmz(text) {
  const t = String(text).trim();
  if (!/^\d{1,10}(\.\d{1,11})?$/.test(t)) return null;
  const [whole, frac = ""] = t.split(".");
  return BigInt(whole) * COIN + BigInt((frac + "00000000000").slice(0, 11));
}
function formatXmz(atomic) {
  const a = BigInt(atomic), whole = a / COIN, frac = (a % COIN).toString().padStart(11, "0").replace(/0+$/, "");
  return frac ? `${whole}.${frac}` : `${whole}`;
}
// Address with its first and last six characters highlighted (checked against what the sender was given)
function showAddress(el, address) {
  el.textContent = "";
  const b1 = document.createElement("b"); b1.textContent = address.slice(0, 6);
  const b2 = document.createElement("b"); b2.textContent = address.slice(-6);
  el.append(b1, document.createTextNode(address.slice(6, -6)), b2);
}

// ---------- start ----------
async function start() {
  $("net").textContent = NETWORK === "mainnet" ? "" : NETWORK.toUpperCase();
  $("net").hidden = NETWORK === "mainnet";
  $("about").textContent = `Release ${RELEASE} · ${NETWORK} · node: ${NODE}. Your keys never leave this browser.`;
  const saved = await loadWallet(NETWORK).catch(() => null);
  $("goOpen").hidden = !saved;
  $("goCreate").textContent = saved ? "Create another wallet (replaces the one stored here)" : "Create a new wallet";
  show("start");
}

$("goOpen").onclick = () => { $("unlockPw").value = ""; $("unlockError").textContent = ""; show("unlock"); $("unlockPw").focus(); };
$("goCreate").onclick = () => passwordScreen("create");
$("goRestore").onclick = () => passwordScreen("restore");
$("pwBack").onclick = start;
$("unlockBack").onclick = start;

let mode = "create";
function passwordScreen(m) {
  mode = m;
  $("pwTitle").textContent = m === "create" ? "Choose a password" : "Restore your wallet";
  $("restoreFields").hidden = m !== "restore";
  for (const id of ["pw1", "pw2", "restoreWords", "restoreHeight"]) $(id).value = "";
  $("pwError").textContent = "";
  show("password"); $("pw1").focus();
}

$("pwNext").onclick = async () => {
  const pw = $("pw1").value;
  if (pw.length < 10) return ($("pwError").textContent = "Use at least 10 characters.");
  if (pw !== $("pw2").value) return ($("pwError").textContent = "The passwords are different.");
  if (mode === "restore") {
    const words = $("restoreWords").value.trim().toLowerCase().split(/\s+/).filter(Boolean);
    if (words.length !== 25) return ($("pwError").textContent = `Enter all 25 words (you entered ${words.length}).`);
    const h = $("restoreHeight").value.trim();
    if (h && !/^\d{1,9}$/.test(h)) return ($("pwError").textContent = "The block height must be a whole number.");
    show("busy", "Restoring your wallet…");
    try {
      wallet = await moneroTs.createWalletFull({ networkType: NET_TYPE, password: pw, seed: words.join(" "),
        restoreHeight: h ? Number(h) : 0, server: { uri: NODE } });
    } catch (e) {
      show("password"); return ($("pwError").textContent = "These recovery words are not valid: " + (e.message || e));
    }
    $("restoreWords").value = "";
    await persist();
    return openWallet();
  }
  show("busy", "Creating your wallet…");
  wallet = await moneroTs.createWalletFull({ networkType: NET_TYPE, password: pw, server: { uri: NODE } });
  newWords = (await wallet.getSeed()).split(" ");
  showWords(newWords, $("wordList"));
  $("wroteDown").checked = false; $("wordsNext").disabled = true;
  show("words");
};

function showWords(words, list) {
  list.textContent = "";
  for (const w of words) { const li = document.createElement("li"); li.textContent = w; list.append(li); }
}
$("wroteDown").onchange = () => { $("wordsNext").disabled = !$("wroteDown").checked; };

// Check three random words before the wallet is saved, so it is never kept without a backup
let asked = [];
$("wordsNext").onclick = () => {
  $("wordList").textContent = "";
  const picks = new Set(); const r = new Uint32Array(8);
  while (picks.size < 3) { crypto.getRandomValues(r); for (const x of r) if (picks.size < 3) picks.add(x % 25); }
  asked = [...picks].sort((a, b) => a - b);
  const box = $("verifyFields"); box.textContent = "";
  for (const i of asked) {
    const l = document.createElement("label"); l.textContent = `Word ${i + 1}`;
    const inp = document.createElement("input"); inp.autocomplete = "off"; inp.spellcheck = false; inp.dataset.index = i;
    l.append(inp); box.append(l);
  }
  $("verifyError").textContent = "";
  show("verify");
};
$("verifyBack").onclick = () => { showWords(newWords, $("wordList")); show("words"); };
$("verifyNext").onclick = async () => {
  const ok = [...$("verifyFields").querySelectorAll("input")].every((inp) => inp.value.trim().toLowerCase() === newWords[Number(inp.dataset.index)]);
  if (!ok) return ($("verifyError").textContent = "One or more words do not match. Check your list.");
  newWords = null; $("verifyFields").textContent = "";
  show("busy", "Saving your wallet…");
  await persist();
  openWallet();
};

// ---------- unlock ----------
$("unlockBtn").onclick = async () => {
  const saved = await loadWallet(NETWORK);
  if (!saved) return start();
  show("busy", "Unlocking…");
  try {
    wallet = await moneroTs.openWalletFull({ networkType: NET_TYPE, password: $("unlockPw").value,
      keysData: saved.keys, cacheData: saved.cache, server: { uri: NODE } });
  } catch (e) {
    show("unlock"); $("unlockError").textContent = "Wrong password, or the stored wallet is damaged."; return;
  } finally { $("unlockPw").value = ""; }
  openWallet();
};
$("unlockPw").addEventListener("keydown", (e) => { if (e.key === "Enter") $("unlockBtn").click(); });

// ---------- the open wallet ----------
async function persist() {
  if (!wallet) return;
  saving = saving.then(async () => {
    const [keys, cache] = await wallet.getData();
    await saveWallet(NETWORK, keys, cache);
  }).catch((e) => notice("Could not save the wallet in this browser: " + (e.message || e)));
  return saving;
}

async function openWallet() {
  show("wallet"); tab("receive");
  const address = await wallet.getPrimaryAddress();
  receive(address);
  await wallet.addListener(new (class extends moneroTs.MoneroWalletListener {
    async onSyncProgress(height, startHeight, endHeight, percent) {
      $("syncBar").style.width = Math.round(percent * 100) + "%";
      $("syncState").textContent = percent >= 1 ? "Up to date" : `Syncing ${height} / ${endHeight}`;
      if (percent >= 1) { refresh(); persist(); }
    }
    async onBalancesChanged() { refresh(); persist(); }
  })());
  refresh();
  try { await wallet.startSyncing(10000); } catch (e) { $("syncState").textContent = "Cannot reach the node: " + (e.message || e); }
  armLock();
}

async function refresh() {
  if (!wallet) return;
  $("balance").textContent = formatXmz(await wallet.getBalance());
  $("unlocked").textContent = formatXmz(await wallet.getUnlockedBalance());
  history();
}

// Tabs
for (const b of document.querySelectorAll(".tabs button")) b.onclick = () => tab(b.dataset.tab);
function tab(name) {
  for (const b of document.querySelectorAll(".tabs button")) b.classList.toggle("active", b.dataset.tab === name);
  for (const t of ["receive", "send", "history", "settings"]) $("t-" + t).hidden = t !== name;
  if (name !== "settings") { $("revealList").textContent = ""; $("revealList").hidden = true; }
}

// Receive
let currentAddress = "";
async function receive(address) {
  currentAddress = address; showAddress($("address"), address); drawQr();
}
async function drawQr() {
  const amount = $("reqAmount").value.trim();
  const atomic = amount ? parseXmz(amount) : null;
  const uri = await wallet.getPaymentUri(new moneroTs.MoneroTxConfig(atomic ? { address: currentAddress, amount: atomic } : { address: currentAddress }));
  const qr = qrcode(0, "M"); qr.addData(uri); qr.make();
  $("qr").src = qr.createDataURL(6, 2);
  $("qrText").textContent = amount && !atomic ? "Enter an amount with at most 11 decimals." : uri;
}
$("reqAmount").oninput = () => { if (wallet) drawQr(); };
$("copyAddress").onclick = () => navigator.clipboard.writeText(currentAddress).then(() => notice("Address copied."));
$("newSub").onclick = async () => { const s = await wallet.createSubaddress(0); receive(s.getAddress()); persist(); };

// Send
let sendAll = false;
$("sendMax").onclick = () => { sendAll = true; $("sendAmount").value = "all available"; };
$("sendAmount").oninput = () => { sendAll = false; };
$("sendReview").onclick = async () => {
  $("sendError").textContent = ""; $("sendToError").textContent = ""; $("sendResult").textContent = "";
  const to = $("sendTo").value.trim();
  if (!(await moneroTs.MoneroUtils.isValidAddress(to, NET_TYPE))) return ($("sendToError").textContent = `This is not a Monzero ${NETWORK} address.`);
  const amount = sendAll ? null : parseXmz($("sendAmount").value);
  if (!sendAll && (!amount || amount <= 0n)) return ($("sendError").textContent = "Enter an amount, with at most 11 decimals.");
  $("sendReview").disabled = true;
  try {
    // Built and fee-checked by Monzero's wallet code; nothing is broadcast until the user confirms
    const txs = sendAll ? await wallet.sweepUnlocked({ address: to, relay: false })
                        : [await wallet.createTx({ accountIndex: 0, address: to, amount, relay: false })];
    const fee = txs.reduce((s, t) => s + t.getFee(), 0n);
    const sent = txs.reduce((s, t) => s + t.getOutgoingAmount(), 0n);
    pending = txs;
    showAddress($("cTo"), to);
    $("cAmount").textContent = formatXmz(sent); $("cFee").textContent = formatXmz(fee); $("cTotal").textContent = formatXmz(sent + fee);
    $("sendConfirm").hidden = false;
  } catch (e) {
    $("sendError").textContent = e.message || String(e);
  } finally { $("sendReview").disabled = false; }
};
$("sendCancel").onclick = () => { pending = null; $("sendConfirm").hidden = true; };
$("sendGo").onclick = async () => {
  if (!pending) return;
  $("sendGo").disabled = true;
  try {
    const hashes = [];
    for (const t of pending) hashes.push(await wallet.relayTx(t.getMetadata()));
    $("sendResult").textContent = "Sent. Transaction " + hashes.join(", ");
    $("sendTo").value = ""; $("sendAmount").value = ""; sendAll = false;
    persist(); refresh();
  } catch (e) {
    $("sendError").textContent = "Not sent: " + (e.message || e);
  } finally { pending = null; $("sendConfirm").hidden = true; $("sendGo").disabled = false; }
};

// History
async function history() {
  const list = $("history");
  const transfers = (await wallet.getTransfers({ accountIndex: 0 })).slice(-50).reverse();
  list.textContent = "";
  $("historyEmpty").hidden = transfers.length > 0;
  for (const t of transfers) {
    const li = document.createElement("li");
    const head = document.createElement("div");
    head.className = t.getIsIncoming() ? "in" : "out";
    const tx = t.getTx();
    head.textContent = `${t.getIsIncoming() ? "Received" : "Sent"} ${formatXmz(t.getAmount())} XMZ · ${tx.getIsConfirmed() ? (tx.getNumConfirmations() ?? 0) + " confirmations" : "pending"}`;
    const hash = document.createElement("div"); hash.className = "hash"; hash.textContent = tx.getHash();
    li.append(head, hash); list.append(li);
  }
}

// Settings: recovery words need the password again
$("revealBtn").onclick = async () => {
  $("revealError").textContent = "";
  const saved = await loadWallet(NETWORK);
  try {
    // Checking the password with the stored keys, without touching the open wallet
    const check = await moneroTs.openWalletFull({ networkType: NET_TYPE, password: $("revealPw").value, keysData: saved.keys });
    await check.close(false);
  } catch { $("revealError").textContent = "Wrong password."; return; } finally { $("revealPw").value = ""; }
  showWords((await wallet.getSeed()).split(" "), $("revealList"));
  $("revealList").hidden = false;
  setTimeout(() => { $("revealList").textContent = ""; $("revealList").hidden = true; }, 60000);
};
$("removeBtn").onclick = async () => {
  if (!confirm("Remove the wallet from this browser? Make sure you have your 25 recovery words.")) return;
  await lock(false);
  await removeWallet(NETWORK);
  start();
};

// ---------- locking ----------
async function lock(save = true) {
  clearTimeout(lockTimer);
  if (!wallet) return;
  const w = wallet; wallet = null; pending = null;
  try { await w.stopSyncing(); if (save) { const [keys, cache] = await w.getData(); await saveWallet(NETWORK, keys, cache); } } catch {}
  try { await w.close(false); } catch {}
  for (const id of ["address", "cTo", "history", "revealList", "qrText", "balance", "unlocked"]) $(id).textContent = "";
  $("qr").removeAttribute("src");
}
function armLock() {
  clearTimeout(lockTimer);
  if (wallet) lockTimer = setTimeout(async () => { await lock(); notice("Locked after 10 minutes without use."); start(); }, LOCK_AFTER_MS);
}
for (const ev of ["pointerdown", "keydown"]) document.addEventListener(ev, armLock, { passive: true });
$("lockBtn").onclick = async () => { await lock(); notice(""); start(); };
window.addEventListener("pagehide", () => { if (wallet) persist(); });

start().catch((e) => notice("Could not start: " + (e.message || e)));
