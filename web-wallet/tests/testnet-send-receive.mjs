// Testnet send/receive with the web wallet's full wallet (WebAssembly, synced from a node): receive
// tXMZ from a funding wallet, wait until it unlocks, send part back, and check the funding wallet
// (Monzero's own wallet RPC) sees it. Testnet only.
//   node web-wallet/tests/testnet-send-receive.mjs <monero-ts dist dir> <node url> <funding wallet rpc url>
import { createRequire } from "node:module";
import { join } from "node:path";
import { randomBytes } from "node:crypto";

const [distDir, nodeUrl, fundingUrl] = process.argv.slice(2);
const moneroTs = createRequire(import.meta.url)(join(distDir, "index.js"));
const { MoneroNetworkType, MoneroUtils, MoneroTxConfig } = moneroTs;
const log = (m) => console.log(new Date().toISOString().slice(11, 19), m);
let failures = 0;
const check = (ok, what) => { console.log((ok ? "ok   " : "FAIL ") + what); if (!ok) failures++; };
const funding = async (method, params = {}) => {
  const r = await fetch(fundingUrl + "/json_rpc", { method: "POST", body: JSON.stringify({ jsonrpc: "2.0", id: 0, method, params }) });
  const j = await r.json(); if (j.error) throw new Error(`${method}: ${j.error.message}`); return j.result;
};
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));

const wallet = await moneroTs.createWalletFull({
  networkType: MoneroNetworkType.TESTNET, password: randomBytes(16).toString("hex"),
  server: { uri: nodeUrl },   // a new random wallet starts at the node's current height
});
const address = await wallet.getPrimaryAddress();
log(`web wallet ${address.slice(0, 12)}…`);

// The node must report Monzero testnet and the wallet must sync to its height
const info = await (await fetch(nodeUrl + "/get_info")).json();
check(info.nettype === "testnet", "node is Monzero testnet");

// Receive 5 tXMZ
const fundTx = await funding("transfer", { destinations: [{ amount: 5 * 100000000000, address }], priority: 0 });
log(`funding tx ${fundTx.tx_hash}`);
let unlocked = 0n;
for (let i = 0; i < 360 && unlocked < 5n * 100000000000n; i++) {
  await wallet.sync(); unlocked = await wallet.getUnlockedBalance();
  if (i % 10 === 0) log(`height ${await wallet.getHeight()} balance ${MoneroUtils.atomicUnitsToXmr(await wallet.getBalance())} unlocked ${MoneroUtils.atomicUnitsToXmr(unlocked)}`);
  if (unlocked < 5n * 100000000000n) await sleep(15);
}
check(unlocked === 5n * 100000000000n, "received exactly 5 tXMZ and it unlocked");
const incoming = await wallet.getTransfers({ isIncoming: true });
check(incoming.some((t) => t.getTx().getHash() === fundTx.tx_hash), "the funding transaction is in the web wallet's history");

// Send 1 tXMZ back
const fundingAddress = (await funding("get_address")).address;
const tx = await wallet.createTx(new MoneroTxConfig({ accountIndex: 0, address: fundingAddress, amount: 100000000000n, relay: true }));
const sentHash = tx.getHash();
log(`sent 1 tXMZ, tx ${sentHash}, fee ${MoneroUtils.atomicUnitsToXmr(tx.getFee())} tXMZ`);
check(tx.getFee() > 0n && tx.getFee() < 100000000000n, "fee is positive and well under 1 XMZ");
let seen = false;
for (let i = 0; i < 40 && !seen; i++) {
  await funding("refresh");
  const t = await funding("get_transfers", { in: true, pool: true });
  seen = [...(t.in || []), ...(t.pool || [])].some((x) => x.txid === sentHash && x.amount === 100000000000);
  if (!seen) await sleep(5);
}
check(seen, "Monzero's own wallet sees exactly 1 tXMZ from the web wallet");
// Payment link
const uri = await wallet.getPaymentUri(new MoneroTxConfig({ address, amount: 250000000000n }));
// Monzero formats amounts with all 11 decimals, as its own wallet does
check(uri === `monzero:${address}?tx_amount=2.50000000000`, `payment link monzero:…?tx_amount=2.50000000000`);
await wallet.close();
console.log(failures ? `FAILED (${failures})` : "ALL PASSED");
process.exit(failures ? 1 : 0);
