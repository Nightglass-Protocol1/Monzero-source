#!/usr/bin/env python3
"""Solo mining: who is solo (solo port or solo: login), a solo block pays its finder alone less the
solo fee, PPLNS never counts solo shares, the stats API states the solo side, and an existing
database gains the solo columns. No chain needed.

Run:  python3 tests/solo_test.py
"""
import asyncio, json, os, sqlite3, sys, tempfile, unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import xmzpool as xp  # noqa: E402

A, B = "A" + "a" * 94, "B" + "b" * 94
PORTS = [{"port": 1, "difficulty": 100, "desc": "CPUs"}, {"port": 2, "difficulty": 100, "desc": "Solo mining", "solo": True}]


def make_pool(tmp, solo=None):
    cfg = {"daemon_rpc": "http://127.0.0.1:1", "wallet_rpc": "http://127.0.0.1:1", "pool_address": "P",
           "database": os.path.join(tmp, "p.sqlite"), "stratum_ports": PORTS,
           "payouts": {"enabled": True, "threshold_xmz": 1, "fee_reserve_xmz": 0}}
    if solo is not None:
        cfg["solo"] = solo
    path = os.path.join(tmp, "c.json")
    open(path, "w").write(json.dumps(cfg))
    return xp.Pool(xp.load_config(path), enable_payouts=True)


class Writer:
    def __init__(self): self.out = []
    def write(self, b): self.out.append(json.loads(b))
    async def drain(self): pass


class Login(unittest.TestCase):
    def setUp(self):
        self.pool = make_pool(tempfile.mkdtemp())
        async def ok(address): return address in (A, B)
        async def job(miner): return {"job_id": "j"}
        self.pool.valid_address, self.pool.new_job = ok, job

    def login(self, login, port):
        w = Writer()
        m = asyncio.run(self.pool.login(w, 1, {"login": login, "pass": "x"}, PORTS[port]))
        return m, w.out[-1]

    def test_plain_login_is_pplns(self):
        m, _ = self.login(A, 0)
        self.assertFalse(m.solo)

    def test_solo_prefix_on_any_port(self):
        m, _ = self.login("solo:" + A + ".rig1", 0)
        self.assertTrue(m.solo)
        self.assertEqual((m.address, m.worker), (A, "rig1"))
        m2, _ = self.login("SOLO:" + B, 0)
        self.assertTrue(m2.solo)

    def test_solo_port(self):
        m, _ = self.login(A, 1)
        self.assertTrue(m.solo)

    def test_disabled_refuses_solo(self):
        self.pool.cfg["solo"]["enabled"] = False
        m, reply = self.login("solo:" + A, 0)
        self.assertIsNone(m)
        self.assertIn("not enabled", reply["error"]["message"])
        m, _ = self.login(A, 0)
        self.assertFalse(m.solo, "PPLNS logins still work")


class Credit(unittest.TestCase):
    def setUp(self):
        self.pool = make_pool(tempfile.mkdtemp())
        db = self.pool.store.db
        # A mines PPLNS, B mines solo, interleaved
        for i in range(4):
            db.execute("INSERT INTO shares(ts,address,worker,diff,height,solo) VALUES (0,?,'w',100,1,0)", (A,))
            db.execute("INSERT INTO shares(ts,address,worker,diff,height,solo) VALUES (0,?,'w',100,1,1)", (B,))

    def bal(self, a):
        r = self.pool.store.q("SELECT pending FROM balances WHERE address=?", (a,))
        return r[0][0] if r else 0

    def test_pplns_block_ignores_solo_shares(self):
        self.pool.store.db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts,finder,solo) VALUES (10,'h','pending',8,10000,0,?,0)", (A,))
        self.pool.credit(10, 1_000_000, 8, 10000)
        self.assertEqual(self.bal(A), 990_000, "all the PPLNS payout goes to the PPLNS miner")
        self.assertEqual(self.bal(B), 0, "a solo miner earns nothing from a shared block")
        self.assertEqual(self.pool.store.q("SELECT amount FROM pool_fee WHERE height=10")[0][0], 10_000)

    def test_solo_block_pays_its_finder_less_the_fee(self):
        self.pool.store.db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts,finder,solo) VALUES (11,'h','pending',8,10000,0,?,1)", (B,))
        self.pool.credit(11, 1_000_000, 8, 10000)
        self.assertEqual(self.bal(B), 990_000, "the solo finder gets the reward less 1%")
        self.assertEqual(self.bal(A), 0, "PPLNS miners get nothing from a solo block")
        self.assertEqual(self.pool.store.q("SELECT amount FROM pool_fee WHERE height=11")[0][0], 10_000)
        self.assertEqual(self.pool.store.q("SELECT status FROM blocks WHERE height=11")[0][0], "confirmed")

    def test_solo_fee_is_its_own_setting(self):
        self.pool.cfg["solo"]["fee_basis_points"] = 50
        self.pool.store.db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts,finder,solo) VALUES (12,'h','pending',8,10000,0,?,1)", (B,))
        self.pool.credit(12, 1_000_000, 8, 10000)
        self.assertEqual(self.bal(B), 995_000)

    def test_credited_once(self):
        self.pool.store.db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts,finder,solo) VALUES (13,'h','pending',8,10000,0,?,1)", (B,))
        self.pool.credit(13, 1_000_000, 8, 10000)
        self.pool.credit(13, 1_000_000, 8, 10000)
        self.assertEqual(self.bal(B), 990_000)


class Stats(unittest.TestCase):
    def setUp(self):
        self.pool = make_pool(tempfile.mkdtemp())
        db = self.pool.store.db
        now = xp.time.time()
        db.execute("INSERT INTO shares(ts,address,worker,diff,height,solo) VALUES (?,?,'w',6000,1,0)", (now, A))
        db.execute("INSERT INTO shares(ts,address,worker,diff,height,solo) VALUES (?,?,'w',3000,1,1)", (now, B))
        db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts,finder,reward,solo) VALUES (20,?, 'confirmed',1,1,?,?,5,0)", ("1" * 64, now, A))
        db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts,finder,reward,solo) VALUES (21,?, 'pending',2,1,?,?,5,1)", ("2" * 64, now, B))
        db.execute("INSERT INTO blocks(height,hash,status,last_share,net_diff,ts,finder,reward,solo) VALUES (22,?, 'orphaned',2,1,?,?,5,1)", ("3" * 64, now, B))
        self.pool.net = {}

    def test_stats_state_the_solo_side(self):
        s = self.pool.stats()
        self.assertEqual([p["solo"] for p in s["config"]["ports"]], [False, True])
        self.assertEqual(s["config"]["paymentScheme"], "PPLNS")
        self.assertAlmostEqual(s["pool"]["hashrate"], 6000 / 600, msg="the shared side counts PPLNS shares only")
        solo = s["solo"]
        self.assertTrue(solo["enabled"])
        self.assertEqual((solo["fee"], solo["paymentScheme"], solo["ports"]), (1.0, "SOLO", [2]))
        self.assertAlmostEqual(solo["hashrate"], 3000 / 600)
        self.assertEqual(solo["totalBlocks"], 1, "orphaned solo blocks are not counted")
        self.assertEqual(solo["minPaymentThreshold"], xp.ATOMIC)
        self.assertEqual({b["height"]: b["solo"] for b in s["pool"]["blocks"]}, {20: False, 21: True, 22: True})
        json.dumps(s)

    def test_disabled_says_so(self):
        self.pool.cfg["solo"]["enabled"] = False
        self.assertEqual(self.pool.stats()["solo"], {"enabled": False})

    def test_round_hashes_skip_solo(self):
        self.assertEqual(self.pool.stats()["pool"]["roundHashes"], 0, "round starts after the last PPLNS block (share 1)")


class Upgrade(unittest.TestCase):
    def test_existing_database_gains_solo_columns(self):
        tmp = tempfile.mkdtemp()
        db = sqlite3.connect(os.path.join(tmp, "p.sqlite"))
        db.execute("CREATE TABLE shares (id INTEGER PRIMARY KEY, ts REAL, address TEXT, worker TEXT, diff INTEGER, height INTEGER)")
        db.execute("CREATE TABLE blocks (height INTEGER PRIMARY KEY, hash TEXT, prev_hash TEXT, nonce INTEGER, ts REAL, finder TEXT, net_diff INTEGER, last_share INTEGER, reward INTEGER, status TEXT, credited_at REAL)")
        db.execute("INSERT INTO shares(ts,address,worker,diff,height) VALUES (0,'X','w',1,1)")
        db.execute("INSERT INTO blocks(height,hash,status) VALUES (5,'h','confirmed')")
        db.commit(); db.close()
        pool = make_pool(tmp)
        self.assertEqual(pool.store.q("SELECT solo FROM shares")[0][0], 0)
        self.assertEqual(pool.store.q("SELECT solo FROM blocks")[0][0], 0)
        make_pool(tmp)   # a second start leaves it as it is


class Config(unittest.TestCase):
    def test_defaults_on_at_one_percent(self):
        p = make_pool(tempfile.mkdtemp())
        self.assertEqual(p.cfg["solo"], {"enabled": True, "fee_basis_points": 100})

    def test_fee_out_of_range_rejected(self):
        with self.assertRaises(SystemExit):
            make_pool(tempfile.mkdtemp(), solo={"fee_basis_points": 10001})


if __name__ == "__main__":
    unittest.main(verbosity=1)
