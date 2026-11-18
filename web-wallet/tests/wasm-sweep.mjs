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
  try {
    const txs = await a.sweepUnlocked({ address: bAddr, relay: true });
    console.log(`sweepUnlocked OK: ${txs.length} transaction(s), inputs ${txs.map(t => t.getInputs()?.length ?? "?").join(" + ")}`);
    await rpc("generateblocks", { amount_of_blocks: 1, wallet_address: bAddr });
    await a.sync();
    console.log(`web wallet A afterwards: ${Number(await a.getBalance()) / 1e11} XMZ`);
  } catch (e) {
    console.log(`sweepUnlocked ERROR: ${e.message}`);
  }
  await a.close(); await b.close();
} finally {
  node.kill();
}
process.exit(0);
