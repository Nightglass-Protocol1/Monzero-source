#!/usr/bin/env python3
"""End-to-end pool test on a private, offline regtest chain (no real coins).

Starts its own monzerod (--regtest --offline --fixed-difficulty), two
wallet-rpcs (pool wallet; miner wallets) and the pool, then: stratum error
cases, two miners finding blocks, the stats API, an orphan, maturity and
PPLNS crediting, and a real payout that arrives in the miners' wallets.

    python3 tests/run_regtest.py --bin DIR_WITH_MONZEROD_AND_WALLET_RPC
"""
import argparse, json, os, shutil, sqlite3, subprocess, sys, tempfile, threading, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
from xmzpool import rpc, RpcError, ATOMIC  # noqa: E402
from stratum_miner import StratumMiner  # noqa: E402

P = {"p2p": 36174, "rpc": 36175, "zmq": 36176, "wpool": 36180, "wmine": 36181, "stratum": 36333, "api": 36117}
DAEMON = f"http://127.0.0.1:{P['rpc']}"
WPOOL, WMINE = f"http://127.0.0.1:{P['wpool']}/json_rpc", f"http://127.0.0.1:{P['wmine']}/json_rpc"
POOL_LOGIN = ("pool", "regtest-login-9f3")   # the pool wallet runs with a login, as in production
results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond)))
    print(f"{'PASS' if cond else 'FAIL'}  {name}{'' if cond else '  -> ' + str(detail)}", flush=True)


def d(method, params=None):
    return rpc(DAEMON + "/json_rpc", method, params, timeout=1800)


def wait_for(fn, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = fn()
            if v:
                return v
        except Exception:  # noqa: BLE001 — services still starting
            pass
        time.sleep(1)
    raise SystemExit(f"timed out waiting for {what}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--net-difficulty", type=int, default=200)
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="xmzpool-regtest-")
    procs, logs = [], {}

    def start(name, cmd):
        logs[name] = open(os.path.join(work, name + ".log"), "w")
        procs.append(subprocess.Popen(cmd, stdout=logs[name], stderr=subprocess.STDOUT))

    try:
        start("monzerod", [f"{a.bin}/monzerod", "--regtest", "--offline", "--fixed-difficulty", str(a.net_difficulty),
                           "--data-dir", f"{work}/chain", "--p2p-bind-port", str(P["p2p"]), "--rpc-bind-port", str(P["rpc"]),
                           "--zmq-rpc-bind-port", str(P["zmq"]), "--non-interactive", "--log-level", "0"])
        wait_for(lambda: d("get_info")["status"] == "OK", 120, "monzerod")
        for name in ("wpool", "wmine"):
            os.makedirs(f"{work}/{name}", exist_ok=True)
            start(name, [f"{a.bin}/monzero-wallet-rpc", "--daemon-address", f"127.0.0.1:{P['rpc']}", "--trusted-daemon",
                         "--allow-mismatched-daemon-version", "--rpc-bind-port", str(P[name]), "--rpc-bind-ip", "127.0.0.1",
                         *(["--rpc-login", ":".join(POOL_LOGIN)] if name == "wpool" else ["--disable-rpc-login"]),
                         "--wallet-dir", f"{work}/{name}", "--non-interactive"])
        wait_for(lambda: rpc(WPOOL, "get_version", auth=POOL_LOGIN), 120, "pool wallet-rpc")
        wait_for(lambda: rpc(WMINE, "get_version"), 120, "miner wallet-rpc")

        addrs = {}
        for who in ("alice", "bob", "carol"):
            rpc(WMINE, "create_wallet", {"filename": who, "password": "", "language": "English"})
            addrs[who] = rpc(WMINE, "get_address", {"account_index": 0})["address"]
            rpc(WMINE, "close_wallet")
        rpc(WPOOL, "create_wallet", {"filename": "pool", "password": "", "language": "English"}, auth=POOL_LOGIN)
        pool_addr = rpc(WPOOL, "get_address", {"account_index": 0}, auth=POOL_LOGIN)["address"]

        # History for ring decoys, mined outside the pool.
        d("generateblocks", {"amount_of_blocks": 120, "wallet_address": addrs["carol"]})

        cfg = {"nettype": "mainnet", "daemon_rpc": DAEMON, "wallet_rpc": f"http://127.0.0.1:{P['wpool']}", "wallet_rpc_login": ":".join(POOL_LOGIN),
               "pool_address": pool_addr, "database": f"{work}/pool.sqlite", "bind": "127.0.0.1",
               "api_port": P["api"], "public_host": "127.0.0.1",
               "stratum_ports": [{"port": P["stratum"], "difficulty": 20, "desc": "test"}],
               "min_difficulty": 10, "confirmations": 60, "unlock_interval_seconds": 2, "fee_basis_points": 100,
               "pplns_window_factor": 2.0, "target_share_seconds": 3,
               "payouts": {"enabled": True, "threshold_xmz": 0.5, "interval_seconds": 3, "fee_reserve_xmz": 0.01}}
        open(f"{work}/pool.json", "w").write(json.dumps(cfg, indent=1))
        start("pool", [sys.executable, os.path.join(HERE, "..", "xmzpool.py"), f"{work}/pool.json", "--enable-payouts", "-v"])
        wait_for(lambda: urllib.request.urlopen(f"http://127.0.0.1:{P['api']}/api/stats", timeout=5).status == 200, 60, "pool API")

        # --- stratum error handling
        m = StratumMiner("127.0.0.1", P["stratum"], DAEMON)
        r = m.login("not-an-address")
        check("login with an invalid address is refused", r and r.get("error"), r)
        m.close()
        m = StratumMiner("127.0.0.1", P["stratum"], DAEMON)
        r = m.login(pool_addr)
        check("login with the pool's own address is refused", r and r.get("error"), r)
        m.close()
        alice = StratumMiner("127.0.0.1", P["stratum"], DAEMON)
        r = alice.login(addrs["alice"])
        check("valid login gets a session and an rx/0 job", r and r["result"]["status"] == "OK" and r["result"]["job"]["algo"] == "rx/0", r)
        job = alice.job
        r = alice.submit(job["job_id"], "zz", "00" * 32)
        check("malformed nonce rejected", r and r["error"], r)
        r = alice.submit("nope", "00000000", "00" * 32)
        check("unknown job rejected", r and r["error"], r)
        r = alice.submit(job["job_id"], "01000000", "ab" * 32)
        check("wrong result hash rejected", r and r["error"] and "Wrong result" in r["error"]["message"], r)
        # find one real share, then resubmit it
        found = {}
        alice.mine(lambda: "r" in found, start_nonce=1000, on_share=lambda rep: found.setdefault("r", rep))
        check("valid share accepted", found["r"] and found["r"]["result"]["status"] == "OK", found)

        # --- two miners until the pool has 2 blocks
        bob = StratumMiner("127.0.0.1", P["stratum"], DAEMON)
        bob.login(addrs["bob"] + ".rig2")
        db = sqlite3.connect(cfg["database"], check_same_thread=False)
        nblocks = lambda: db.execute("SELECT COUNT(*) FROM blocks").fetchone()[0]
        stop = lambda: nblocks() >= 2
        counts = {}
        threads = [threading.Thread(target=lambda n, mm, s: counts.__setitem__(n, mm.mine(stop, start_nonce=s)), args=(n, mm, s))
                   for n, mm, s in (("alice", alice, 5_000_000), ("bob", bob, 9_000_000))]
        t0 = time.time()
        for t in threads:
            t.start()
        for t in threads:
            t.join(900)
        check("pool found 2 blocks", nblocks() >= 2, f"{nblocks()} in {time.time() - t0:.0f}s")
        print(f"      shares accepted/rejected: {counts}  ({time.time() - t0:.0f}s)")
        check("no honest share was rejected", all(v[1] == 0 for v in counts.values()), counts)
        rows = db.execute("SELECT height, hash, status FROM blocks").fetchall()
        on_chain = all(d("get_block_header_by_height", {"height": h})["block_header"]["hash"] == bh for h, bh, _ in rows)
        check("found blocks are in the chain with the recorded hash", on_chain, rows)
        dup = alice.submit(alice.job["job_id"], "00000000", "00" * 32)
        r2 = alice.submit(alice.job["job_id"], "00000000", "00" * 32)
        check("duplicate nonce rejected", r2 and r2["error"] and "Duplicate" in r2["error"]["message"], (dup, r2))
        alice.close(); bob.close()

        s = json.load(urllib.request.urlopen(f"http://127.0.0.1:{P['api']}/api/stats", timeout=10))
        check("stats API: blocks, hash rate, ports, 11 decimals",
              s["pool"]["totalBlocks"] >= 2 and s["pool"]["hashrate"] > 0 and s["config"]["ports"][0]["port"] == P["stratum"]
              and s["config"]["coinDecimalPlaces"] == 11, s["pool"])

        # --- orphan: a pending block whose hash is not in the chain
        h0 = rows[0][0]
        db.execute("INSERT INTO blocks(height, hash, prev_hash, nonce, ts, finder, net_diff, last_share, reward, status) VALUES (?,?,?,?,?,?,?,?,?, 'pending')",
                   (5, "ff" * 32, "00" * 32, 0, time.time(), addrs["alice"], a.net_difficulty, 1, 1))
        db.commit()

        # --- maturity and PPLNS crediting
        d("generateblocks", {"amount_of_blocks": 61, "wallet_address": addrs["carol"]})
        wait_for(lambda: db.execute("SELECT COUNT(*) FROM blocks WHERE status='pending'").fetchone()[0] == 0, 60, "unlock")
        check("fake block marked orphaned", db.execute("SELECT status FROM blocks WHERE height=5").fetchone()[0] == "orphaned")
        check("orphan earns no credits", db.execute("SELECT COUNT(*) FROM credits WHERE height=5").fetchone()[0] == 0)
        exact = True
        for h, reward in db.execute("SELECT height, reward FROM blocks WHERE status='confirmed'"):
            hdr = d("get_block_header_by_height", {"height": h})["block_header"]
            credited = db.execute("SELECT COALESCE(SUM(amount),0) FROM credits WHERE height=?", (h,)).fetchone()[0]
            fee = db.execute("SELECT amount FROM pool_fee WHERE height=?", (h,)).fetchone()[0]
            exact &= credited + fee == reward == hdr["reward"] and fee >= reward // 100
        check("each confirmed block: miner credits + pool fee = exact chain reward", exact)
        shares = dict(db.execute("SELECT address, SUM(diff) FROM shares GROUP BY address").fetchall())
        cred = dict(db.execute("SELECT address, SUM(amount) FROM credits GROUP BY address").fetchall())
        check("both miners credited", set(cred) == {addrs["alice"], addrs["bob"]}, cred)
        print(f"      share difficulty {{alice: {shares.get(addrs['alice'])}, bob: {shares.get(addrs['bob'])}}}; credits XMZ "
              f"{{alice: {cred.get(addrs['alice'], 0) / ATOMIC:.5f}, bob: {cred.get(addrs['bob'], 0) / ATOMIC:.5f}}}")

        # --- payouts reach the miners' wallets
        rpc(WPOOL, "refresh", auth=POOL_LOGIN)
        sent = wait_for(lambda: db.execute("SELECT id, txid FROM payments WHERE status='sent'").fetchall(), 120, "payment")
        check("payment sent with a transaction id", sent and len(sent[0][1]) == 64, sent)
        d("generateblocks", {"amount_of_blocks": 12, "wallet_address": addrs["carol"]})
        ok = True
        for who in ("alice", "bob"):
            rpc(WMINE, "open_wallet", {"filename": who, "password": ""})
            rpc(WMINE, "refresh")
            got = rpc(WMINE, "get_balance", {"account_index": 0})["balance"]
            paid = db.execute("SELECT COALESCE(SUM(i.amount),0) FROM payment_items i JOIN payments p ON p.id=i.payment WHERE p.status='sent' AND i.address=?", (addrs[who],)).fetchone()[0]
            ok &= got == paid and paid > 0
            print(f"      {who}: wallet {got / ATOMIC:.5f} XMZ, pool says paid {paid / ATOMIC:.5f}")
            rpc(WMINE, "close_wallet")
        check("miners' wallets received exactly what the pool recorded", ok)
        bal = dict(db.execute("SELECT address, pending FROM balances").fetchall())
        check("balances fell by the amounts paid", all(v < 0.5 * ATOMIC for v in bal.values()), bal)
        m = json.load(urllib.request.urlopen(f"http://127.0.0.1:{P['api']}/api/miner?address={addrs['alice']}", timeout=10))
        check("miner API shows paid amount", m["paid"] > 0 and m["payments"], m)
    finally:
        for p in reversed(procs):
            p.terminate()
        for p in procs:
            try:
                p.wait(30)
            except subprocess.TimeoutExpired:
                p.kill()
        print(f"\nwork dir: {work}" if a.keep else "")
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    failed = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
