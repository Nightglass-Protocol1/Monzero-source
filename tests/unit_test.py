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

    def test_disabled_sends_nothing(self):
        self.pool.payouts_enabled = False
        calls = []
        self.run_with(lambda p: calls.append(p))
        self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main()
