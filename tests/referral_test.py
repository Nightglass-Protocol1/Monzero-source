#!/usr/bin/env python3
"""Referral / affiliate programme: code derivation, attribution rules, and the fee waterfall that
keeps the reserve and the liquidity share whole before any referral is paid. No chain needed.

Run:  python3 tests/referral_test.py
"""
import json, os, sys, tempfile, unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import xmzpool as xp  # noqa: E402


def _write(tmp, obj):
    path = os.path.join(tmp, "c.json")
    with open(path, "w") as f:
        f.write(json.dumps(obj))
    return path


def make_pool(tmp, referral=None, liquidity=None):
    cfg = {
        "daemon_rpc": "http://127.0.0.1:1", "wallet_rpc": "http://127.0.0.1:1",
        "pool_address": "P", "database": os.path.join(tmp, "p.sqlite"),
        "stratum_ports": [{"port": 1, "difficulty": 100}],
        "payouts": {"enabled": True, "threshold_xmz": 1, "fee_reserve_xmz": 0},
    }
    if referral is not None:
        cfg["referral"] = referral
    if liquidity is not None:
        cfg["liquidity"] = liquidity
    return xp.Pool(xp.load_config(_write(tmp, cfg)), enable_payouts=True)


ON = {"enabled": True, "basis_points": 15, "reserve_basis_points": 2000, "allow_self": False}


class Codes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.pool = make_pool(self.tmp, referral=dict(ON))

    def test_deterministic_and_idempotent(self):
        a = self.pool.register_referral("ADDR_ONE")
        b = self.pool.register_referral("ADDR_ONE")
        self.assertEqual(a, b)
        self.assertTrue(a.startswith("mz"))
        # one row only, and it resolves back to the address
        self.assertEqual(self.pool.store.q("SELECT COUNT(*) FROM referrals")[0][0], 1)
        self.assertEqual(self.pool.resolve_referral(a), "ADDR_ONE")

    def test_distinct_addresses_distinct_codes(self):
        self.assertNotEqual(self.pool.register_referral("A" * 95), self.pool.register_referral("B" * 95))

    def test_collision_grows_the_code(self):
        # Force a collision: pre-seed the short derivation of X onto a different owner.
        short = xp.Pool._derive_code("X", 8)
        with self.pool.store.tx() as db:
            db.execute("INSERT INTO referrals(code,address,created) VALUES (?,?,0)", (short, "SOMEONE_ELSE"))
        code = self.pool.register_referral("X")
        self.assertNotEqual(code, short)
        self.assertGreater(len(code), len(short))
        self.assertEqual(self.pool.resolve_referral(code), "X")


class Attribution(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.pool = make_pool(self.tmp, referral=dict(ON))
        self.op = "OP" + "p" * 93
        self.code = self.pool.register_referral(self.op)

    def test_valid_referral_recorded(self):
        miner = "M" + "m" * 94
        self.pool.set_miner_referrer(miner, self.code)
        self.assertEqual(self.pool.store.q("SELECT referrer FROM miner_referrer WHERE miner=?", (miner,))[0][0], self.op)

    def test_sticky_first_code_wins(self):
        miner, op2 = "M" + "m" * 94, "OP2" + "q" * 92
        code2 = self.pool.register_referral(op2)
        self.pool.set_miner_referrer(miner, self.code)
        self.pool.set_miner_referrer(miner, code2)      # later code must be ignored
        self.assertEqual(self.pool.store.q("SELECT referrer FROM miner_referrer WHERE miner=?", (miner,))[0][0], self.op)

    def test_self_referral_blocked_by_default(self):
        self.pool.set_miner_referrer(self.op, self.code)
        self.assertFalse(self.pool.store.q("SELECT 1 FROM miner_referrer WHERE miner=?", (self.op,)))

    def test_self_referral_allowed_when_configured(self):
        self.pool.cfg["referral"]["allow_self"] = True
        self.pool.set_miner_referrer(self.op, self.code)
        self.assertTrue(self.pool.store.q("SELECT 1 FROM miner_referrer WHERE miner=?", (self.op,)))

    def test_unknown_and_empty_codes_ignored(self):
        miner = "M" + "m" * 94
        self.pool.set_miner_referrer(miner, "mzdoesnotexist")
        self.pool.set_miner_referrer(miner, "")
        self.assertFalse(self.pool.store.q("SELECT 1 FROM miner_referrer WHERE miner=?", (miner,)))


class Waterfall(unittest.TestCase):
    """credit_referrals(db, height, fee, paid) pays from leftover fee only."""
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def _run(self, fee, paid, referrers, referral=None, liquidity=None):
        pool = make_pool(self.tmp, referral=referral or dict(ON), liquidity=liquidity)
        for miner, op in referrers.items():
            pool.register_referral(op)
            with pool.store.tx() as db:
                db.execute("INSERT INTO miner_referrer(miner,code,referrer,since) VALUES (?,?,?,0)",
                           (miner, pool.referral_code(op), op))
        with pool.store.tx() as db:
            total = pool.credit_referrals(db, 10, fee, paid)
        credited = dict(pool.store.q("SELECT referrer, COALESCE(SUM(amount),0) FROM referral_credits GROUP BY referrer"))
        return pool, total, credited

    def test_basic_payout_is_bps_of_referred_reward(self):
        # one referred miner earning 1_000_000; 15 bps -> 1500. Fee is ample.
        _, total, credited = self._run(1_000_000, {"MINER": 1_000_000}, {"MINER": "OP"})
        self.assertEqual(total, 1500)
        self.assertEqual(credited["OP"], 1500)

    def test_non_referred_miners_pay_nothing(self):
        _, total, credited = self._run(1_000_000, {"MINER": 1_000_000}, {})
        self.assertEqual(total, 0)
        self.assertEqual(credited, {})

    def test_reserve_and_liquidity_protected(self):
        # Fee is small so the leftover budget caps the referral below the raw target.
        # fee=1000, reserve 20% -> 200, liquidity share 100 bps(1%) -> 10, budget = 790.
        # target would be 15 bps of 1_000_000 = 1500, so it is capped to the 790 budget.
        _, total, credited = self._run(
            1000, {"MINER": 1_000_000}, {"MINER": "OP"},
            liquidity={"enabled": True, "address": "", "share_basis_points": 100})
        self.assertLessEqual(total, 790)
        self.assertEqual(total, 790)
        # the pool always keeps at least the reserve + liquidity share
        self.assertGreaterEqual(1000 - total, 200 + 10)

    def test_pro_rata_scale_down_between_referrers(self):
        # Two referrers want equal amounts but the budget only covers part; split must be pro-rata.
        _, total, credited = self._run(
            100, {"M1": 1_000_000, "M2": 1_000_000}, {"M1": "OP1", "M2": "OP2"})
        # fee 100, reserve 20% = 20, no liquidity -> budget 80; want 1500+1500=3000 -> each ~40
        self.assertLessEqual(total, 80)
        self.assertEqual(credited.get("OP1"), credited.get("OP2"))
        self.assertLessEqual(abs(credited["OP1"] + credited["OP2"] - total), 1)

    def test_disabled_pays_nothing(self):
        off = {"enabled": False, "basis_points": 15, "reserve_basis_points": 2000, "allow_self": False}
        _, total, credited = self._run(1_000_000, {"MINER": 1_000_000}, {"MINER": "OP"}, referral=off)
        self.assertEqual(total, 0)

    def test_zero_or_negative_fee_pays_nothing(self):
        _, total, _ = self._run(0, {"MINER": 1_000_000}, {"MINER": "OP"})
        self.assertEqual(total, 0)


class Integration(unittest.TestCase):
    """Full credit(): a referred miner credits the operator, and pool_fee stays the gross fee."""
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.pool = make_pool(self.tmp, referral=dict(ON))
        db = self.pool.store.db
        self.miner, self.op = "M" + "m" * 94, "O" + "o" * 94
        for _ in range(4):
            db.execute("INSERT INTO shares(ts,address,worker,diff,height) VALUES (0,?,?,100,1)", (self.miner, "w"))
        db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts) VALUES (10,'h','pending',4,200,0)")
        self.pool.register_referral(self.op)
        db.execute("INSERT INTO miner_referrer(miner,code,referrer,since) VALUES (?,?,?,0)",
                   (self.miner, self.pool.referral_code(self.op), self.op))

    def test_credit_pays_operator_and_keeps_gross_fee(self):
        reward = 1_000_000
        self.pool.credit(10, reward, 4, 200)
        fee = self.pool.store.q("SELECT amount FROM pool_fee")[0][0]
        miner_bal = self.pool.store.q("SELECT pending FROM balances WHERE address=?", (self.miner,))[0][0]
        op_bal = self.pool.store.q("SELECT COALESCE(SUM(pending),0) FROM balances WHERE address=?", (self.op,))[0][0]
        # pool_fee is the GROSS fee (referral is NOT subtracted from it -> liquidity base intact)
        self.assertEqual(miner_bal + fee, reward)
        # operator earned 15 bps of the miner's reward, drawn from the fee's leftover
        self.assertEqual(op_bal, miner_bal * 15 // 10000)
        self.assertGreater(op_bal, 0)
        # the pool still nets at least its reserve after paying the operator
        self.assertGreaterEqual(fee - op_bal, fee * 2000 // 10000)


class Config(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def _load(self, referral=None, liquidity=None):
        cfg = {"daemon_rpc": "x", "wallet_rpc": "x", "pool_address": "P",
               "database": os.path.join(self.tmp, "p.sqlite"), "stratum_ports": [{"port": 1, "difficulty": 100}]}
        if referral is not None:
            cfg["referral"] = referral
        if liquidity is not None:
            cfg["liquidity"] = liquidity
        return xp.load_config(_write(self.tmp, cfg))

    def test_defaults_disabled(self):
        self.assertFalse(self._load()["referral"]["enabled"])

    def test_basis_points_out_of_range_rejected(self):
        with self.assertRaises(SystemExit):
            self._load(referral={"basis_points": 500})

    def test_reserve_plus_liquidity_over_100pct_rejected(self):
        with self.assertRaises(SystemExit):
            self._load(referral={"reserve_basis_points": 9500},
                       liquidity={"enabled": True, "address": "", "share_basis_points": 600})


if __name__ == "__main__":
    unittest.main(verbosity=2)
