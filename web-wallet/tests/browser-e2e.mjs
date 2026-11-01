// End-to-end test of the built web wallet page in Chromium (testnet): create a wallet and confirm its
// recovery words, receive tXMZ, send some back through the confirmation screen, lock and unlock from
// the browser's encrypted storage. Fails on any Content-Security-Policy violation or page error.
//   node web-wallet/tests/browser-e2e.mjs <page url> <funding wallet rpc url>
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const [pageUrl, fundingUrl] = process.argv.slice(2);
const PORT = 9333, PASSWORD = "correct horse battery 42";
const log = (m) => console.log(new Date().toISOString().slice(11, 19), m);
let failures = 0;
const check = (ok, what) => { console.log((ok ? "ok   " : "FAIL ") + what); if (!ok) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const funding = async (method, params = {}) => {
  const j = await (await fetch(fundingUrl + "/json_rpc", { method: "POST", body: JSON.stringify({ jsonrpc: "2.0", id: 0, method, params }) })).json();
  if (j.error) throw new Error(`${method}: ${j.error.message}`); return j.result;
};

const profile = mkdtempSync(join(tmpdir(), "mz-ww-chrome-"));
const chrome = spawn("chromium", ["--headless=new", `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
  "--no-first-run", "--no-default-browser-check", "about:blank"], { stdio: "ignore" });
let target;
for (let i = 0; i < 50 && !target; i++) {
  try { target = (await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json()).find((t) => t.type === "page"); } catch { await sleep(200); }
}
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let nextId = 1; const waiting = new Map(); const problems = [];
ws.onmessage = (m) => {
  const msg = JSON.parse(m.data);
  if (msg.id && waiting.has(msg.id)) { waiting.get(msg.id)(msg); waiting.delete(msg.id); return; }
  if (msg.method === "Log.entryAdded" && /Content Security Policy|Refused to/i.test(msg.params.entry.text)) problems.push("CSP: " + msg.params.entry.text);
  if (msg.method === "Runtime.exceptionThrown") problems.push("exception: " + (msg.params.exceptionDetails.exception?.description || msg.params.exceptionDetails.text));
  if (msg.method === "Runtime.consoleAPICalled" && msg.params.type === "error") problems.push("console.error: " + msg.params.args.map((a) => a.value ?? a.description).join(" "));
};
const cdp = (method, params = {}) => new Promise((resolve) => { const id = nextId++; waiting.set(id, resolve); ws.send(JSON.stringify({ id, method, params })); });
const js = async (expr) => (await cdp("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true })).result?.result?.value;
const waitFor = async (expr, what, seconds = 120) => {
  for (let i = 0; i < seconds * 2; i++) { if (await js(expr)) return true; await sleep(500); }
  check(false, "timed out waiting for: " + what); throw new Error(what);
};
const visible = (id) => `!document.getElementById(${JSON.stringify(id)}).hidden`;
const set = (id, v) => js(`(e=>{e.value=${JSON.stringify(v)};e.dispatchEvent(new Event('input'))})(document.getElementById(${JSON.stringify(id)}))`);
const click = (id) => js(`document.getElementById(${JSON.stringify(id)}).click()`);
const text = (id) => js(`document.getElementById(${JSON.stringify(id)}).textContent`);

try {
  await cdp("Runtime.enable"); await cdp("Log.enable"); await cdp("Page.enable");
  await cdp("Page.navigate", { url: pageUrl });
  await waitFor(visible("s-start"), "start screen");
  check(await text("net") === "TESTNET", "testnet badge shown");

  // Create: password, recovery words, check three of them
  await click("goCreate");
  await set("pw1", "short"); await set("pw2", "short"); await click("pwNext");
  check((await text("pwError")).includes("10 characters"), "a short password is refused");
  await set("pw1", PASSWORD); await set("pw2", PASSWORD); await click("pwNext");
  await waitFor(visible("s-words"), "recovery words", 180);
  const words = await js(`[...document.querySelectorAll('#wordList li')].map(l=>l.textContent)`);
  check(words.length === 25, "25 recovery words shown");
  check(await js(`document.getElementById('wordsNext').disabled`), "Continue is disabled until 'I wrote them down' is ticked");
  await js(`(c=>{c.checked=true;c.dispatchEvent(new Event('change'))})(document.getElementById('wroteDown'))`);
  await click("wordsNext");
  await waitFor(visible("s-verify"), "word check");
  check(await js(`document.getElementById('wordList').children.length === 0`), "the words are removed from the page while checking");
  await js(`[...document.querySelectorAll('#verifyFields input')].forEach(i=>i.value='wrong')`); await click("verifyNext");
  check((await text("verifyError")).includes("do not match"), "wrong words are refused");
  await js(`(w=>[...document.querySelectorAll('#verifyFields input')].forEach(i=>i.value=w[Number(i.dataset.index)]))(${JSON.stringify(words)})`);
  await click("verifyNext");
  await waitFor(visible("s-wallet"), "wallet screen", 180);
  const address = await text("address");
  check(address.startsWith("K") && address.length > 90, `testnet address shown (${address.slice(0, 8)}…)`);
  check(await js(`document.getElementById('qr').src.startsWith('data:image/gif')`), "receive QR drawn");
  check((await text("qrText")) === "monzero:" + address, "QR holds monzero:<address>");
  await waitFor(`document.getElementById('syncState').textContent==='Up to date'`, "sync", 300);

  // Receive 3 tXMZ
  const fund = await funding("transfer", { destinations: [{ amount: 3 * 100000000000, address }], priority: 0 });
  log("funded: " + fund.tx_hash);
  await waitFor(`document.getElementById('unlocked').textContent==='3'`, "3 tXMZ unlocked", 2400);
  check(true, "received 3 tXMZ and it unlocked");
  await click(`t-history`); await js(`document.querySelector('[data-tab=history]').click()`);
  check((await js(`document.getElementById('history').textContent`)).includes(fund.tx_hash), "the incoming transaction is in History");

  // Send 1 tXMZ back through the confirmation screen
  await js(`document.querySelector('[data-tab=send]').click()`);
  const back = (await funding("get_address")).address;
  await set("sendTo", "44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A");
  await set("sendAmount", "1"); await click("sendReview");
  await waitFor(`document.getElementById('sendToError').textContent.length>0`, "Monero address refused", 30);
  check(true, "a Monero address is refused");
  await set("sendTo", back); await set("sendAmount", "1.123456789012"); await click("sendReview");
  await waitFor(`document.getElementById('sendError').textContent.length>0`, "12 decimals refused", 30);
  check(true, "an amount with 12 decimals is refused");
  await set("sendAmount", "1"); await click("sendReview");
  await waitFor(visible("sendConfirm"), "confirmation screen", 120);
  const fee = await text("cFee");
  check((await text("cAmount")) === "1" && Number(fee) > 0 && Number(fee) < 0.1, `confirmation shows 1 XMZ and a fee of ${fee}`);
  check((await text("cTo")) === back, "confirmation shows the recipient address");
  await click("sendGo");
  await waitFor(`document.getElementById('sendResult').textContent.startsWith('Sent')`, "sent", 120);
  const sentHash = (await text("sendResult")).replace("Sent. Transaction ", "");
  log("sent: " + sentHash);
  let seen = false;
  for (let i = 0; i < 30 && !seen; i++) {
    await funding("refresh");
    const t = await funding("get_transfers", { in: true, pool: true });
    seen = [...(t.in || []), ...(t.pool || [])].some((x) => x.txid === sentHash && x.amount === 100000000000);
    if (!seen) await sleep(5000);
  }
  check(seen, "Monzero's own wallet receives exactly 1 tXMZ from the page");

  // Lock, then unlock from the encrypted browser storage
  await click("lockBtn");
  await waitFor(visible("s-start"), "locked");
  check(await js(`!document.getElementById('goOpen').hidden`), "the saved wallet is offered after locking");
  check((await text("address")) === "" && (await text("balance")) === "", "the address and balance are cleared when locked");
  await click("goOpen"); await set("unlockPw", "wrong password!"); await click("unlockBtn");
  await waitFor(`document.getElementById('unlockError').textContent.length>0`, "wrong password refused", 120);
  check(true, "a wrong password does not open the wallet");
  await set("unlockPw", PASSWORD); await click("unlockBtn");
  await waitFor(visible("s-wallet"), "unlocked", 180);
  check((await text("address")) === address, "unlocked wallet has the same address");
  await waitFor(`document.getElementById('history').textContent.includes(${JSON.stringify(sentHash)})`, "history after unlock", 300);
  check(true, "history survives lock and unlock (stored encrypted in the browser)");
  const stored = await js(`new Promise(r=>{const q=indexedDB.open('monzero-web-wallet');q.onsuccess=()=>{const t=q.result.transaction('wallets').objectStore('wallets').get('testnet');t.onsuccess=()=>{const v=t.result;const keys=new TextDecoder('latin1').decode(v.keys);r({hasKeys:v.keys.length>0,plainWords:${JSON.stringify(words.slice(0, 3).join(" "))}&&keys.includes(${JSON.stringify(words[0] + " " + words[1])}),plainPassword:keys.includes(${JSON.stringify(PASSWORD)})})}}})`);
  check(stored.hasKeys && !stored.plainWords && !stored.plainPassword, "stored keys are encrypted (no recovery words or password in clear)");
} catch (e) {
  check(false, "stopped: " + e.message);
} finally {
  check(problems.length === 0, problems.length ? "page problems:\n  " + problems.slice(0, 10).join("\n  ") : "no CSP violations or page errors");
  ws.close(); chrome.kill(); await sleep(1000); rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 500 });
  console.log(failures ? `FAILED (${failures})` : "ALL PASSED");
  process.exit(failures ? 1 : 0);
}
