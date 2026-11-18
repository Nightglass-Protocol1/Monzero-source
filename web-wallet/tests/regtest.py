#!/usr/bin/env python3
"""Reproduce 'Transaction sanity check failed' on a sweep with many inputs, and check the fix.
   regtest.py <old bin dir> <fixed bin dir> <work dir>"""
import json, os, subprocess, sys, time, urllib.request

OLD, NEW, WORK = sys.argv[1:4]
D_PORT, W_PORT = 38181, 38183
procs = []

def rpc(port, method, params=None, timeout=600):
    body = json.dumps({"jsonrpc": "2.0", "id": 0, "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{port}/json_rpc", body, {"content-type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=timeout))

def wait(port):
    for _ in range(240):
        try:
            rpc(port, "get_version", timeout=5); return
        except Exception:
            time.sleep(1)
    raise SystemExit(f"port {port} never answered")

def start(cmd, log):
    p = subprocess.Popen(cmd, stdout=open(log, "w"), stderr=subprocess.STDOUT); procs.append(p); return p

def wallet_rpc(bindir):
    p = start([f"{bindir}/monzero-wallet-rpc", "--shared-ringdb-dir", f"{WORK}/ringdb", "--allow-mismatched-daemon-version", "--daemon-address", f"127.0.0.1:{D_PORT}",
               "--rpc-bind-port", str(W_PORT), "--disable-rpc-login", "--wallet-dir", f"{WORK}/wallets",
               "--trusted-daemon", "--log-level", "1", "--log-file", f"{WORK}/wallet-{os.path.basename(os.path.dirname(bindir))}.log"],
              f"{WORK}/wallet-rpc.out")
    wait(W_PORT); return p

def stop(p):
    p.terminate(); p.wait(30)

try:
    os.makedirs(f"{WORK}/wallets", exist_ok=True)
    start([f"{OLD}/monzerod", "--regtest", "--offline", "--fixed-difficulty", "1", "--data-dir", f"{WORK}/node",
           "--rpc-bind-port", str(D_PORT), "--p2p-bind-port", "38180", "--no-zmq", "--non-interactive",
           "--log-level", "0"], f"{WORK}/node.out")
    wait(D_PORT)
    w = wallet_rpc(OLD)
    addr = {}
    for name in ("a", "b"):
        rpc(W_PORT, "create_wallet", {"filename": name, "language": "English"})
        addr[name] = rpc(W_PORT, "get_address")["result"]["address"]
    print("mining (each 30th block pays A)…", flush=True)
    t0 = time.time()
    for i in range(400):
        rpc(D_PORT, "generateblocks", {"amount_of_blocks": 29, "wallet_address": addr["b"]})
        rpc(D_PORT, "generateblocks", {"amount_of_blocks": 1, "wallet_address": addr["a"]})
    rpc(D_PORT, "generateblocks", {"amount_of_blocks": 100, "wallet_address": addr["b"]})   # unlock A's coinbases
    info = rpc(D_PORT, "get_info")["result"]
    print(f"height {info['height']} in {time.time()-t0:.0f}s", flush=True)

    def sweep(label):
        rpc(W_PORT, "open_wallet", {"filename": "a"})
        rpc(W_PORT, "refresh", {}, timeout=1800)
        bal = rpc(W_PORT, "get_balance")["result"]
        outs = rpc(W_PORT, "incoming_transfers", {"transfer_type": "available"})["result"].get("transfers", [])
        print(f"[{label}] A: {len(outs)} spendable outputs, unlocked {bal['unlocked_balance']/1e11:.2f} XMZ", flush=True)
        r = rpc(W_PORT, "sweep_all", {"address": addr["b"], "get_tx_hex": True}, timeout=1800)
        if "error" in r:
            print(f"[{label}] sweep_all ERROR: {r['error']['message']}", flush=True); return None
        txs = r["result"]["tx_hash_list"]
        print(f"[{label}] sweep_all OK: {len(txs)} transaction(s) sent", flush=True)
        return r["result"]

    old = sweep("published v1.3.3")
    stop(w)
    w = wallet_rpc(NEW)
    new = sweep("fixed")
    if new:
        pool = rpc(D_PORT, "get_transaction_pool_hashes.bin", timeout=60) if False else None
        rpc(D_PORT, "generateblocks", {"amount_of_blocks": 1, "wallet_address": addr["b"]})
        import struct
        # Each sent tx: inputs and unique ring members, straight from the chain
        req = urllib.request.Request(f"http://127.0.0.1:{D_PORT}/get_transactions",
            json.dumps({"txs_hashes": new["tx_hash_list"], "decode_as_json": True}).encode(), {"content-type": "application/json"})
        res = json.load(urllib.request.urlopen(req, timeout=120))
        for t in res["txs"]:
            tx = json.loads(t["as_json"]); idx = []; n = 0
            for vin in tx["vin"]:
                offs = vin["key"]["key_offsets"]; s = 0; ring = []
                for o in offs: s += o; ring.append(s)
                idx += ring; n += len(ring)
            print(f"  tx {t['tx_hash'][:12]}: block {t.get('block_height')} inputs {len(tx['vin'])}, ring members {n}, unique {len(set(idx))} ({len(set(idx))/n:.0%})", flush=True)
        rpc(W_PORT, "refresh", {}, timeout=600)
        bal = rpc(W_PORT, "get_balance")["result"]
        print(f"[fixed] A afterwards: balance {bal['balance']/1e11:.4f} XMZ", flush=True)
    stop(w)
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
