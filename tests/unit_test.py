#!/usr/bin/env python3
"""Unit tests: PoW maths, PPLNS crediting and payout failure handling (no chain needed)."""
import asyncio, os, sys, tempfile, unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import xmzpool as xp  # noqa: E402


def make_pool(tmp, enabled=True):
    cfg = xp.load_config(_write(tmp, {"daemon_rpc": "http://127.0.0.1:1", "wallet_rpc": "http://127.0.0.1:1",
                                      "pool_address": "P", "database": os.path.join(tmp, "p.sqlite"),
                                      "stratum_ports": [{"port": 1, "difficulty": 100}],
                                      "payouts": {"enabled": enabled, "threshold_xmz": 1, "fee_reserve_xmz": 0}}))
    return xp.Pool(cfg, enable_payouts=True)


def _write(tmp, obj):
    import json
    path = os.path.join(tmp, "c.json")
    open(path, "w").write(json.dumps(obj))
    return path


class Maths(unittest.TestCase):
    def test_meets(self):
        self.assertTrue(xp.meets("00" * 32, 10**12))
        self.assertFalse(xp.meets("ff" * 32, 2))
        self.assertTrue(xp.meets("ff" * 32, 1))
        # 2**255 as a little-endian hash meets difficulty 1 but not 2
        h = (1 << 255).to_bytes(32, "little").hex()
        self.assertTrue(xp.meets(h, 1)); self.assertFalse(xp.meets(h, 2))

    def test_target(self):
        self.assertEqual(xp.target_hex(1), "ffffffff")
        self.assertEqual(int.from_bytes(bytes.fromhex(xp.target_hex(1000)), "little"), 0xFFFFFFFF // 1000)
        self.assertEqual(len(xp.target_hex(1 << 33)), 16)

    def test_nonce(self):
        blob = "aa" * 39 + "00000000" + "bb" * 37
        self.assertEqual(xp.with_nonce(blob, "01020304"), "aa" * 39 + "01020304" + "bb" * 37)


class Credit(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.pool = make_pool(self.tmp)
        db = self.pool.store.db
        for addr, diff in [("A", 100)] * 3 + [("B", 100)] * 1 + [("A", 100)] * 2:
            db.execute("INSERT INTO shares(ts,address,worker,diff,height) VALUES (0,?,?,?,1)", (addr, "w", diff))
        db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts) VALUES (10,'h','pending',6,200,0)")

    def test_pplns_window_and_exact_sum(self):
        # window = 2 x 200 = 400: the last four shares (A,A,B,A) count, A 300 / B 100
        self.pool.credit(10, 1_000_003, 6, 200)
        c = dict(self.pool.store.q("SELECT address, amount FROM credits"))
        fee = self.pool.store.q("SELECT amount FROM pool_fee")[0][0]
        pay = 1_000_003 - 1_000_003 // 100
        self.assertEqual(c, {"A": pay * 300 // 400, "B": pay * 100 // 400})
        self.assertEqual(sum(c.values()) + fee, 1_000_003)

    def test_credit_once(self):
        self.pool.credit(10, 1000, 6, 200)
        first = self.pool.store.q("SELECT SUM(amount), COUNT(*) FROM credits")[0]
        self.pool.credit(10, 1000, 6, 200)
        self.assertEqual(self.pool.store.q("SELECT SUM(amount), COUNT(*) FROM credits")[0], first)


class AddressKeys(unittest.TestCase):
    GOOD = "FTBxy6FD1erFBgYKLgz9sAQvJa4K12tTPFuPfoKZcTQMfHEe7kDTmQdS92ehNFoWkq2D6jQCkUzELVsCGLtYa8G2D2avcNm"
    # Passes the wallet's validate_address, but its keys are not wallet keys: the pool wallet refused to pay it (12 Nov 2026)
    MADE_UP = "FWSHPXgK5N3Rw8HJMFDMq9Ar1B1vPBcsa65hcjU7GbQd5CoKoqQTyHm8Gi57YsivHwMhA584Xq6Jbg3yiWufkpEhE3LPVDg"

    def test_wallet_address_is_usable(self):
        self.assertTrue(xp.address_keys_usable(self.GOOD))

    def test_made_up_address_is_not(self):
        self.assertFalse(xp.address_keys_usable(self.MADE_UP))

    def test_garbage_is_not(self):
        for bad in ("", "abc", "0" * 95, self.GOOD[:-1], self.GOOD + "1"):
            self.assertFalse(xp.address_keys_usable(bad), bad)

    def test_login_refuses_a_made_up_address(self):
        tmp = tempfile.mkdtemp(); pool = make_pool(tmp)
        async def w(method, params=None, timeout=60):
            return {"valid": True, "nettype": "mainnet"}
        pool.w = w
        self.assertTrue(asyncio.run(pool.valid_address(self.GOOD)))
        self.assertFalse(asyncio.run(pool.valid_address(self.MADE_UP)))


class Payouts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.pool = make_pool(self.tmp)
        self.pool.store.db.execute("INSERT INTO balances(address,pending) VALUES ('A', ?), ('B', ?)", (2 * xp.ATOMIC, 3 * xp.ATOMIC))

    def run_with(self, transfer):
        async def w(method, params=None, timeout=60):
            if method == "get_balance":
                return {"unlocked_balance": 100 * xp.ATOMIC}
            return transfer(params)
        self.pool.w = w
        return asyncio.run(self.pool.pay_once())

    def balances(self):
        return dict(self.pool.store.q("SELECT address, pending FROM balances"))

    def test_refused_restores_balances(self):
        def refuse(_): raise xp.RpcError("not enough money")
        self.run_with(refuse)
        self.assertEqual(self.balances(), {"A": 2 * xp.ATOMIC, "B": 3 * xp.ATOMIC})
        self.assertEqual(self.pool.store.q("SELECT status FROM payments")[0][0], "failed")

    def test_uncertain_halts_and_keeps_deduction(self):
        def lost(_): raise xp.RpcUncertain("timed out")
        self.run_with(lost)
        self.assertEqual(self.balances(), {"A": 0, "B": 0})
        self.assertEqual(self.pool.store.q("SELECT status FROM payments")[0][0], "uncertain")
        self.assertTrue(self.pool.halt_file.exists())
        calls = []
        self.run_with(lambda p: calls.append(p))
        self.assertEqual(calls, [], "no payment while halted")

    def test_success_records_txid(self):
        seen = []
        def ok(p):
            seen.append(p); return {"tx_hash": "ab" * 32, "fee": 123}
        self.assertEqual(self.run_with(ok), "ab" * 32)
        self.assertEqual(sorted(x["amount"] for x in seen[0]["destinations"]), [2 * xp.ATOMIC, 3 * xp.ATOMIC])
        self.assertEqual(self.pool.store.q("SELECT SUM(paid) FROM balances")[0][0], 5 * xp.ATOMIC)

    def fill(self, miners):
        self.pool.store.db.execute("DELETE FROM balances")
        self.pool.store.db.executemany("INSERT INTO balances(address,pending) VALUES (?,?)", miners)

    def test_small_miners_are_not_starved(self):
        # 20 miners, room for 15 per transaction: the 5 smallest were never reached before
        self.fill([("M%02d" % i, (40 - i) * xp.ATOMIC // 10) for i in range(20)])
        sent = []
        def ok(p):
            sent.append([d["address"] for d in p["destinations"]]); return {"tx_hash": "%064x" % len(sent), "fee": 1}
        self.run_with(ok)                                   # one transaction
        self.assertEqual(len(sent[0]), 15)
        left = {a for a, v in self.balances().items() if v}
        self.assertEqual(left, {"M%02d" % i for i in range(15, 20)})
        # the big miners earn again before the next round; the waiting ones still go first
        for i in range(15):
            self.pool.store.db.execute("UPDATE balances SET pending=? WHERE address=?", (5 * xp.ATOMIC, "M%02d" % i))
        self.run_with(ok)
        self.assertEqual(sent[1][:5], ["M%02d" % i for i in range(15, 20)])
        self.assertEqual(len(sent[1]), 15)

    def test_round_pays_everyone_in_batches(self):
        self.fill([("M%02d" % i, 2 * xp.ATOMIC) for i in range(32)])
        sent = []
        def ok(p):
            sent.append(len(p["destinations"])); return {"tx_hash": "%064x" % len(sent), "fee": 1}
        async def w(method, params=None, timeout=60):
            return {"unlocked_balance": 100 * xp.ATOMIC} if method == "get_balance" else ok(params)
        self.pool.w = w
        txids = asyncio.run(self.pool.pay_round())
        self.assertEqual(sent, [15, 15, 2])
        self.assertEqual(len(txids), 3)
        self.assertEqual(set(self.balances().values()), {0})
        self.assertEqual(self.pool.store.q("SELECT SUM(paid) FROM balances")[0][0], 64 * xp.ATOMIC)

    def test_pays_what_the_unlocked_balance_covers(self):
        self.fill([("A", 60 * xp.ATOMIC), ("B", 30 * xp.ATOMIC), ("C", 30 * xp.ATOMIC)])
        sent = []
        def ok(p):
            sent.append([d["address"] for d in p["destinations"]]); return {"tx_hash": "cd" * 32, "fee": 1}
        self.run_with(ok)                                   # 100 XMZ unlocked: A and B fit, C waits
        self.assertEqual(sent, [["A", "B"]])
        self.assertEqual(self.balances()["C"], 30 * xp.ATOMIC)

    def test_round_stops_after_a_refusal(self):
        self.fill([("M%02d" % i, 2 * xp.ATOMIC) for i in range(32)])
        calls = []
        def refuse(p):
            calls.append(1); raise xp.RpcError("not enough unlocked money")
        async def w(method, params=None, timeout=60):
            return {"unlocked_balance": 100 * xp.ATOMIC} if method == "get_balance" else refuse(params)
        self.pool.w = w
        self.assertEqual(asyncio.run(self.pool.pay_round()), [])
        self.assertEqual(len(calls), 2, "the batch, then one alone; out of coins stops the round")
        self.assertEqual(set(self.balances().values()), {2 * xp.ATOMIC})
        self.assertEqual(self.pool.held, {})

    def test_one_refused_address_does_not_stop_the_others(self):
        # The wallet refuses any transaction that pays BAD (as on 12 Nov 2026: its own sanity check)
        self.fill([("M%02d" % i, 2 * xp.ATOMIC) for i in range(20)] + [("BAD", 9 * xp.ATOMIC)])
        sent = []
        def transfer(p):
            if any(d["address"] == "BAD" for d in p["destinations"]):
                raise xp.RpcError("transfer: {'code': -4, 'message': 'Total received by BAD: 0, expected 9'}")
            sent.append([d["address"] for d in p["destinations"]]); return {"tx_hash": "%064x" % len(sent), "fee": 1}
        async def w(method, params=None, timeout=60):
            return {"unlocked_balance": 1000 * xp.ATOMIC} if method == "get_balance" else transfer(params)
        self.pool.w = w
        asyncio.run(self.pool.pay_round())
        left = {a: v for a, v in self.balances().items() if v}
        self.assertEqual(left, {"BAD": 9 * xp.ATOMIC}, "everyone else is paid; the refused balance is kept")
        self.assertIn("BAD", self.pool.held)
        self.assertEqual(self.pool.store.q("SELECT SUM(paid) FROM balances")[0][0], 40 * xp.ATOMIC)
        before = len(sent)
        asyncio.run(self.pool.pay_round())                  # next round: BAD is skipped, not retried
        self.assertEqual(len(sent), before)
        self.pool.held["BAD"] = 0                            # after the hold it is tried again
        asyncio.run(self.pool.pay_round())
        self.assertIn("BAD", self.pool.held)

    def test_disabled_sends_nothing(self):
        self.pool.payouts_enabled = False
        calls = []
        self.run_with(lambda p: calls.append(p))
        self.assertEqual(calls, [])


class WalletLogin(unittest.TestCase):
    """monero-wallet-rpc offers MD5 and MD5-sess digest challenges together."""

    def setUp(self):
        import hashlib, http.server, json, threading
        calls = self.calls = []

        class Wallet(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a): pass
            def do_POST(self):
                self.rfile.read(int(self.headers["Content-Length"]))
                a = self.headers.get("Authorization", "")
                f = dict((k.strip(), v.strip().strip('"')) for k, _, v in (p.partition("=") for p in a[7:].split(",")))
                h = lambda s: hashlib.md5(s.encode()).hexdigest()
                ok = a.startswith("Digest ") and f.get("response") == h(
                    f"{h('pool:monero-rpc:s3cret')}:{f.get('nonce')}:{f.get('nc')}:{f.get('cnonce')}:auth:{h('POST:/json_rpc')}")
                calls.append(ok)
                if not ok:
                    self.send_response(401)
                    for alg in ("MD5", "MD5-sess"):
                        self.send_header("WWW-authenticate", f'Digest qop="auth",algorithm={alg},realm="monero-rpc",nonce="bm9uY2U=",stale=false')
                    self.send_header("Content-Length", "0"); self.end_headers()
                    return
                out = json.dumps({"id": "0", "jsonrpc": "2.0", "result": {"valid": True}}).encode()
                self.send_response(200); self.send_header("Content-Length", str(len(out))); self.end_headers()
                self.wfile.write(out)

        self.srv = http.server.HTTPServer(("127.0.0.1", 0), Wallet)
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.srv.server_port}/json_rpc"

    def tearDown(self):
        self.srv.shutdown(); self.srv.server_close()

    def test_answers_md5_challenge(self):
        self.assertEqual(xp.rpc(self.url, "validate_address", {}, 5, ("pool", "s3cret")), {"valid": True})
        self.assertEqual(self.calls, [False, True])

    def test_wrong_login_is_definite_error(self):
        with self.assertRaises(xp.RpcError):
            xp.rpc(self.url, "validate_address", {}, 5, ("pool", "wrong"))
        self.assertEqual(self.calls, [False, False])


if __name__ == "__main__":
    unittest.main()
