// The web wallet's WebAssembly wallet sweeps 400 small outputs on a regtest chain.
//   node wasm-sweep.mjs <monero-ts dist> <monzerod> <work dir>
import { createRequire } from "node:module";
import { spawn } from "node:child_process";
import { join } from "node:path";
import { mkdirSync } from "node:fs";
const [dist, monzerod, work] = process.argv.slice(2);
const moneroTs = createRequire(import.meta.url)(join(dist, "index.js"));
const PORT = 38191;
mkdirSync(work, { recursive: true });
const node = spawn(monzerod, ["--regtest", "--offline", "--fixed-difficulty", "1", "--data-dir", join(work, "node"),
  "--rpc-bind-port", String(PORT), "--p2p-bind-port", "38190", "--no-zmq", "--non-interactive", "--log-level", "0"], { stdio: "ignore" });
const rpc = async (method, params = {}) => {
  const r = await fetch(`http://127.0.0.1:${PORT}/json_rpc`, { method: "POST", body: JSON.stringify({ jsonrpc: "2.0", id: 0, method, params }) });
  const j = await r.json(); if (j.error) throw new Error(`${method}: ${j.error.message}`); return j.result;
};
try {
  for (let i = 0; ; i++) { try { await rpc("get_info"); break; } catch { if (i > 120) throw new Error("node down"); await new Promise(r => setTimeout(r, 1000)); } }
  const server = { uri: `http://127.0.0.1:${PORT}` };
  const a = await moneroTs.createWalletFull({ networkType: moneroTs.MoneroNetworkType.MAINNET, password: "x", server, regtest: true, proxyToWorker: false });
  const b = await moneroTs.createWalletFull({ networkType: moneroTs.MoneroNetworkType.MAINNET, password: "x", server, regtest: true, proxyToWorker: false });
  const aAddr = await a.getPrimaryAddress(), bAddr = await b.getPrimaryAddress();
  const t0 = Date.now();
  for (let i = 0; i < 400; i++) {
    await rpc("generateblocks", { amount_of_blocks: 29, wallet_address: bAddr });
    await rpc("generateblocks", { amount_of_blocks: 1, wallet_address: aAddr });
  }
  await rpc("generateblocks", { amount_of_blocks: 100, wallet_address: bAddr });
  console.log(`height ${(await rpc("get_info")).height} in ${Math.round((Date.now() - t0) / 1000)}s`);
  await a.sync();
  const outs = await a.getOutputs({ isSpent: false });
  console.log(`web wallet A: ${outs.length} outputs, unlocked ${Number(await a.getUnlockedBalance()) / 1e11} XMZ`);
  // As the page: a listener on the wallet
  await a.addListener(new class extends moneroTs.MoneroWalletListener { async onBalancesChanged() {} async onOutputSpent() {} async onOutputReceived() {} async onSyncProgress() {} });
  const amount = 6000000000000000n; // 60,000 XMZ
  try { await a.createTx({ accountIndex: 0, address: bAddr, amount, relay: false }); console.log("createTx (old page): OK"); }
  catch (e) { console.log(`createTx (old page) 60,000 XMZ: ERROR ${e.message}`); }
  try {
    const txs = await a.createTxs({ accountIndex: 0, address: bAddr, amount, relay: false });
    const sent = txs.reduce((s, t) => s + t.getOutgoingAmount(), 0n), fee = txs.reduce((s, t) => s + t.getFee(), 0n);
    console.log(`createTxs (new page) 60,000 XMZ: ${txs.length} transaction(s), inputs ${txs.map(t => t.getInputs()?.length ?? "?").join(" + ")}, sending ${Number(sent) / 1e11} XMZ, fee ${Number(fee) / 1e11}`);
    const sentHashes = [];
    try { for (const t of txs) sentHashes.push(await a.relayTx(t.getMetadata())); console.log("relayTx OK"); }
    catch (e) { console.log(`relayTx ERROR after ${sentHashes.length} returned hashes: ${e.message}`); }
    const pool = await rpc("get_transaction_pool_stats").catch(() => null);
    const inPool = await fetch(`http://127.0.0.1:${PORT}/get_transaction_pool_hashes`, { method: "POST" }).then(r => r.json());
    console.log(`node's pool holds ${(inPool.tx_hashes || []).length} transaction(s); built ${txs.map(t => t.getHash().slice(0, 10)).join(", ")}; in pool: ${txs.filter(t => (inPool.tx_hashes || []).includes(t.getHash())).length}/${txs.length}`);
    await rpc("generateblocks", { amount_of_blocks: 1, wallet_address: bAddr });
    await b.sync(); await a.sync();
    console.log(`relayed and mined; B received ${Number(await b.getBalance(0)) / 1e11} XMZ in total (incl. its mining); A left ${Number(await a.getBalance()) / 1e11} XMZ`);
    const inB = (await b.getTransfers({ isIncoming: true })).filter(t => !t.getTx().getIsMinerTx()).reduce((s, t) => s + t.getAmount(), 0n);
    console.log(`B received from A: ${Number(inB) / 1e11} XMZ`);
  } catch (e) {
    console.log(`createTxs ERROR: ${e.message}`);
  }
  await a.close(); await b.close();
} finally {
  node.kill();
}
process.exit(0);
