#!/usr/bin/env python3
"""Share difficulty (vardiff): a CPU miner must stay near one share per target_share_seconds,
including after the pool stalls and answers a backlog of easy shares in a burst (2026-11-20: a
4 kH/s miner was pushed to max_difficulty, 10000M, and found no share for ten hours).

Run:  python3 tests/vardiff_test.py
"""
import json, os, sys, tempfile, unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import xmzpool as xp  # noqa: E402


def make_pool(tmp):
    cfg = {"daemon_rpc": "http://127.0.0.1:1", "wallet_rpc": "http://127.0.0.1:1",
           "pool_address": "P", "database": os.path.join(tmp, "p.sqlite"),
           "stratum_ports": [{"port": 1, "difficulty": 5000}],
           "payouts": {"enabled": False, "threshold_xmz": 1, "fee_reserve_xmz": 0}}
    path = os.path.join(tmp, "c.json")
    with open(path, "w") as f:
        json.dump(cfg, f)
    return xp.Pool(xp.load_config(path), enable_payouts=False)


class Clock:
    def __init__(self):
        self.t = 1_000_000.0

    def __call__(self):
        return self.t


class Vardiff(unittest.TestCase):
    def setUp(self):
        self.pool = make_pool(tempfile.mkdtemp())
        self.clock = Clock()
        p = mock.patch.object(xp.time, "time", self.clock)
        p.start()
        self.addCleanup(p.stop)
        self.m = xp.Miner(self.pool, None, "A", "w", 5000, False)
        self.target = self.pool.cfg["target_share_seconds"]

    def mine(self, hashrate, seconds):
        """Shares at the miner's current difficulty, at the pace its hashrate gives, answered at once"""
        end = self.clock.t + seconds
        while self.clock.t < end:
            self.clock.t += self.m.diff / hashrate
            self.pool.retarget(self.m, self.m.diff)

    def test_settles_near_target(self):
        self.mine(4000, 1800)
        self.assertLess(abs(self.m.diff - 4000 * self.target) / (4000 * self.target), 0.5)

    def test_no_new_jobs_does_not_compound(self):
        """2026-11-20 06:57-07:15: the pool sent no new job for 18 minutes, so the miner kept mining at
        the job's difficulty (7507, a share every ~2 s) while vardiff, counting shares, multiplied the
        miner's difficulty by 4 every minute; the next job carried 10000M."""
        self.m.diff = 7507
        for _ in range(18 * 30):            # 18 minutes of shares at the stale job's difficulty
            self.clock.t += 7507 / 4000
            self.pool.retarget(self.m, 7507)
        self.assertLess(self.m.diff, 3 * 4000 * self.target)

    def test_backlog_burst_does_not_explode(self):
        self.mine(4000, 1800)
        settled = self.m.diff
        # The pool stalls; the miner keeps finding easy shares and they are all answered in a burst
        self.m.diff = 7507
        self.clock.t += 120
        for _ in range(200):
            self.clock.t += 0.2
            self.pool.retarget(self.m, 7507)
        self.assertLess(self.m.diff, 4 * settled)
        self.mine(4000, 1800)
        self.assertLess(self.m.diff, 3 * 4000 * self.target)

    def test_stuck_at_max_comes_back_down(self):
        self.m.diff = self.pool.cfg["max_difficulty"]
        for _ in range(30):     # new jobs every 30 s, no shares (a 4 kH/s CPU cannot find one)
            self.clock.t += 30
            self.pool.ease_off(self.m)
        self.mine(4000, 1800)
        self.assertLess(self.m.diff, 3 * 4000 * self.target)
        self.assertGreater(self.m.diff, 4000 * self.target / 3)

    def test_fixed_difficulty_untouched(self):
        self.m.fixed = True
        self.clock.t += 3600
        self.pool.ease_off(self.m)
        self.pool.retarget(self.m, 5000)
        self.assertEqual(self.m.diff, 5000)


if __name__ == "__main__":
    unittest.main()
