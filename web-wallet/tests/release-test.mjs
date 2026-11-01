// Release tests for the Monzero web wallet build: the WebAssembly wallet (the same module the
// browser loads) against Monzero's own wallet (monzero-wallet-rpc, run offline). Nothing here uses
// a network or real funds.
//   node web-wallet/tests/release-test.mjs <monero-ts dist dir> <monzero-wallet-rpc>
import { createRequire } from "node:module";
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const [distDir, walletRpcBin] = process.argv.slice(2);
if (!distDir || !walletRpcBin) { console.error("usage: release-test.mjs <dist> <monzero-wallet-rpc>"); process.exit(2); }
const moneroTs = createRequire(import.meta.url)(join(distDir, "index.js"));
const { MoneroNetworkType, MoneroUtils } = moneroTs;

let failures = 0;
function check(ok, what) { console.log((ok ? "ok   " : "FAIL ") + what); if (!ok) failures++; }

// Monzero's own wallet, offline, for one network
async function walletRpc(net, port) {
  const dir = mkdtempSync(join(tmpdir(), "mz-webwallet-"));
  const args = ["--offline", "--wallet-dir", dir, "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", String(port),
                "--disable-rpc-login", "--non-interactive", "--log-file", join(dir, "rpc.log")];
  if (net === "testnet") args.unshift("--testnet");
  const proc = spawn(walletRpcBin, args, { stdio: "ignore" });
  const call = async (method, params = {}) => {
    const r = await fetch(`http://127.0.0.1:${port}/json_rpc`, { method: "POST", body: JSON.stringify({ jsonrpc: "2.0", id: 0, method, params }) });
    const j = await r.json();
    if (j.error) throw new Error(`${method}: ${j.error.message}`);
    return j.result;
  };
  for (let i = 0; i < 100; i++) {
    try { await call("get_version"); break; } catch { await new Promise((r) => setTimeout(r, 200)); }
  }
  return { call, stop: () => { proc.kill(); rmSync(dir, { recursive: true, force: true }); } };
}

const nets = [
  { name: "mainnet", type: MoneroNetworkType.MAINNET, port: 18311, prefix: "F" },
  { name: "testnet", type: MoneroNetworkType.TESTNET, port: 18312, prefix: "K" },
];

// Amounts: 1 XMZ is 10^11 atomic units
check(MoneroUtils.xmrToAtomicUnits(1) === 100000000000n, "1 XMZ = 100,000,000,000 atomic units");
check(MoneroUtils.atomicUnitsToXmr(123456789012n) === 1.23456789012, "1.23456789012 XMZ round trip (11 decimals)");
check(MoneroUtils.xmrToAtomicUnits("0.00000000001") === 1n, "smallest unit 0.00000000001 XMZ");

// A real Monero mainnet address (from Monero's own docs): the web wallet must refuse it
const moneroAddress = "44AFFq5kSiGBoZ4NMDwYtN18obc8AemS33DBLWs3H7otXft3XjrpDtQGv7SqSsaBYBb98uNbr2VBBEt7f2wfn3RVGQBEP3A";

for (const net of nets) {
  const rpc = await walletRpc(net.name, net.port);
  try {
    // 1. Created in the web wallet, restored in Monzero's wallet: same addresses
    const web = await moneroTs.createWalletKeys({ networkType: net.type });
    const words = await web.getSeed();
    const webPrimary = await web.getPrimaryAddress();
    const webSub = await web.getAddress(0, 1);
    check(webPrimary.startsWith(net.prefix), `${net.name}: web wallet address has the Monzero prefix (${webPrimary.slice(0, 4)}…)`);
    await rpc.call("restore_deterministic_wallet", { filename: "fromweb", seed: words, password: "", restore_height: 0, language: "English" });
    const rpcPrimary = (await rpc.call("get_address", { account_index: 0 })).address;
    await rpc.call("create_address", { account_index: 0 });
    const rpcSub = (await rpc.call("get_address", { account_index: 0, address_index: [1] })).addresses[0].address;
    check(rpcPrimary === webPrimary, `${net.name}: Monzero wallet restores the web wallet's words to the same primary address`);
    check(rpcSub === webSub, `${net.name}: and the same subaddress 0/1`);
    check((await rpc.call("query_key", { key_type: "view_key" })).key === await web.getPrivateViewKey(), `${net.name}: same private view key`);
    await rpc.call("close_wallet");

    // 2. Created in Monzero's wallet, restored in the web wallet: same addresses
    await rpc.call("create_wallet", { filename: "native", password: "", language: "English" });
    const nativeWords = (await rpc.call("query_key", { key_type: "mnemonic" })).key;
    const nativePrimary = (await rpc.call("get_address", { account_index: 0 })).address;
    const restored = await moneroTs.createWalletKeys({ networkType: net.type, seed: nativeWords });
    check(await restored.getPrimaryAddress() === nativePrimary, `${net.name}: web wallet restores Monzero wallet words to the same address`);
    await rpc.call("close_wallet");

    // 3. Address checks
    check(await MoneroUtils.isValidAddress(webPrimary, net.type), `${net.name}: own address valid`);
    check(!(await MoneroUtils.isValidAddress(moneroAddress, net.type)), `${net.name}: a Monero address is refused`);
    const other = nets.find((n) => n !== net);
    check(!(await MoneroUtils.isValidAddress(webPrimary, other.type)), `${net.name}: its address is refused on ${other.name}`);
  } finally {
    rpc.stop();
  }
}

console.log(failures ? `FAILED (${failures})` : "ALL PASSED");
process.exit(failures ? 1 : 0);
