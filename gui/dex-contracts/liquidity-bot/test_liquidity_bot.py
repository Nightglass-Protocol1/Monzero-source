#!/usr/bin/env python3
"""Liquidity bot tests: pricing, limits, the offer -> trade -> vault flow and approval checks,
against a stand-in trade helper (no chain, no wallet). Run: python3 test_liquidity_bot.py"""
import json, os, sys, tempfile, time, unittest
from decimal import Decimal
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import liquidity_bot as lb  # noqa: E402

FAKE_HELPER = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
req = json.load(sys.stdin)
state_dir = Path(os.environ["FAKE_STATE"])
log = state_dir / "requests.jsonl"
with open(log, "a") as f:
    f.write(json.dumps(req) + "\n")
scen = json.loads((state_dir / "scenario.json").read_text())
cmd = req["command"]
def out(v): print(json.dumps({"schema": "x", "ok": True, **v})); sys.exit()
if cmd == "xmzWallet":
    out({"address": "F" + "W" * 94, "balanceAtoms": str(scen["unlocked"]), "unlockedAtoms": str(scen["unlocked"]), "height": 1})
if cmd == "accounts":
    i = req["tick"]["accountIndex"]
    out({"evm": "0x%040x" % i, "sol": "Sol%d" % i})
if cmd == "post":
    d = Path(req["tradeDir"]); d.mkdir(parents=True)
    (d / "listing.json").write_text(json.dumps(req["offer"]))
    (d / "offer.json").write_text(json.dumps(req["offer"]))
    out({"listing": {"listing": {"id": "0x1"}}, "posted": [{"ok": True}]})
offer = json.loads((Path(req["tradeDir"]) / "offer.json").read_text()) if (Path(req["tradeDir"]) / "offer.json").exists() else {}
taken = scen.get("taken", False)
if cmd == "serve":
    out({"taken": taken, "expired": False})
if cmd == "cancel":
    out({"cancelled": True})
value = scen.get("trade_value") or offer.get("valueWei")
trade = {"role": scen.get("role", "claimer"), "commitment": "0x" + "ab" * 32, "stage": "Agreed",
         "offer": {"chainId": offer.get("chainId"), "value": value, "xmzAmount": offer.get("xmzAtoms")}}
approved = (Path(req["tradeDir"]) / "approved").exists()
if cmd == "status":
    out({**trade, "done": scen.get("done", False), "approved": approved})
if cmd == "tick":
    out({**trade, "done": scen.get("done", False), "approved": approved, "awaitingApproval": not approved, "awaitingFunding": None})
if cmd == "approve":
    (Path(req["tradeDir"]) / "approved").write_text("")
    out({**trade, "approved": True})
if cmd == "payout":
    out({**trade, "done": True, "stage": "Swapped", "payout": {"to": req["payoutAddress"], "tx": "0xfeed", "amountWei": value}})
if cmd == "report":
    out({})
print(json.dumps({"ok": False, "error": "unknown " + cmd}))
'''

CONTROL = {"newTradesEnabled": True, "expiresAt": int(time.time()) + 3600, "sequence": 1,
           "maxLtcAtomsPerTrade": str(10 ** 18), "maxXmzAtomsPerTrade": str(25000 * lb.ATOMIC), "maxLtcAtomsPerDay": str(10 ** 19)}


class BotTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        helper = self.tmp / "helper"
        helper.write_text(FAKE_HELPER)
        helper.chmod(0o755)
        os.environ["FAKE_STATE"] = str(self.tmp)
        self.scenario({"unlocked": 100000 * lb.ATOMIC})
        cfg_path = self.tmp / "c.json"
        cfg_path.write_text(json.dumps({"helper": str(helper), "wallet_rpc_bin": "/bin/false", "state_dir": str(self.tmp / "state"),
                                        "ladder_percent": [2, 10], "offer_xmz": "2000",
                                        "pairs": {"ETH": {"vault": "0x" + "d0" * 20}}}))
        self.bot = lb.Bot(lb.load_config(cfg_path))
        (self.tmp / "state" / "keys").mkdir(parents=True)
        self.bot.seed_file.write_text("00" * 64)
        self.bot.main.start = lambda: True
        self.bot.main.endpoint = lambda: {"url": "http://127.0.0.1:1", "loginFile": "/x"}
        lb.WalletService.start = lambda svc: True
        self.bot.control = lambda network: (dict(CONTROL), None)
        self.bot.prices = {"ETH": Decimal("2000")}
        self.bot.prices_at = time.time()
        self.bot.gas_balance = lambda pair, addr: 10 ** 18

    def scenario(self, s):
        (self.tmp / "scenario.json").write_text(json.dumps(s))

    def requests(self, cmd=None):
        p = self.tmp / "requests.jsonl"
        rows = [json.loads(x) for x in p.read_text().splitlines()] if p.exists() else []
        return [r for r in rows if cmd is None or r["command"] == cmd]

    def test_ladder_prices(self):
        self.bot.round()
        posts = self.requests("post")
        self.assertEqual(len(posts), 2)
        # 2000 XMZ at $0.00102 and $0.0011 with ETH at $2000
        self.assertEqual([p["offer"]["valueWei"] for p in posts], [str(1020000000000000), str(1100000000000000)])
        self.assertEqual({p["offer"]["side"] for p in posts}, {"sell-xmz"})
        self.assertEqual([p["tick"]["accountIndex"] for p in posts], [200, 201], "each slot its own trade account")
        self.bot.round()
        self.assertEqual(len(self.requests("post")), 2, "an open offer is not posted again")

    def test_coin_limit_shrinks_the_offer(self):
        doc = dict(CONTROL, maxLtcAtomsPerTrade=str(5 * 10 ** 14))   # 0.0005 ETH = $1
        value, xmz = self.bot.offer_for("ETH", 0, doc)
        self.assertEqual(value, 5 * 10 ** 14)
        self.assertEqual(xmz, 1000 * lb.ATOMIC)   # $1 at $0.001

    def test_not_more_xmz_than_unlocked(self):
        self.scenario({"unlocked": 3000 * lb.ATOMIC})
        self.bot.round()
        self.assertEqual(len(self.requests("post")), 1, "2 x 2000 XMZ do not fit in 3000")

    def test_paused_posts_nothing_and_withdraws_open_offers(self):
        self.bot.round()
        self.bot.control = lambda network: (None, "new trades are paused by the operator")
        self.bot.round()
        self.assertEqual(len(self.requests("cancel")), 2)
        self.assertEqual(len(self.requests("post")), 2)

    def test_reprice_when_the_coin_moves(self):
        self.bot.round()
        self.bot.prices = {"ETH": Decimal("2200")}
        self.bot.round()
        self.assertEqual(len(self.requests("cancel")), 2)
        self.bot.round()
        self.assertEqual(len(self.requests("post")), 4)

    def test_taken_offer_runs_to_the_vault(self):
        self.bot.round()
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True})
        self.bot.round()                 # taken -> tick -> approve
        self.assertEqual(len(self.requests("approve")), 2)
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True, "done": True})
        self.bot.round()                 # done -> payout
        payouts = self.requests("payout")
        self.assertEqual(len(payouts), 2)
        self.assertEqual({p["payoutAddress"] for p in payouts}, {"0x" + "d0" * 20})
        self.assertEqual({p["keepWei"] for p in payouts}, {"2000000000000000"}, "fees for the next trade stay")
        self.assertEqual({s["state"] for s in self.bot.slots.values()}, {"idle"})
        events = [json.loads(x)["event"] for x in self.bot.ledger_file.read_text().splitlines()]
        self.assertEqual(events.count("taken"), 2)
        self.assertEqual(events.count("finished"), 2)

    def test_wrong_amount_is_not_approved(self):
        self.bot.round()
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True, "trade_value": "1"})
        self.bot.round()
        self.assertEqual(self.requests("approve"), [])
        self.assertTrue(any("NOT approved" in a for a in self.bot.alerts))

    def test_wrong_role_is_not_approved(self):
        self.bot.round()
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True, "role": "owner"})
        self.bot.round()
        self.assertEqual(self.requests("approve"), [])

    def test_day_limit(self):
        self.bot.control = lambda network: (dict(CONTROL, maxLtcAtomsPerDay=str(1500000000000000)), None)
        self.bot.round()
        self.assertEqual(len(self.requests("post")), 1)
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True, "done": True})
        self.bot.round()
        self.bot.round()
        # 0.00102 ETH sold of 0.0015 a day: the next 0.00102 offer would pass the limit
        self.assertEqual(len(self.requests("post")), 1)

    def test_no_gas_no_offer(self):
        self.bot.gas_balance = lambda pair, addr: 0
        self.bot.round()
        self.assertEqual(self.requests("post"), [])
        self.assertTrue(self.bot.alerts)

    def test_dry_run_posts_nothing(self):
        self.bot.dry_run = True
        self.bot.round()
        self.assertEqual(self.requests("post"), [])


class Ed25519(unittest.TestCase):
    def test_rfc8032_vector_1(self):
        pub = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
        sig = bytes.fromhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")
        self.assertTrue(lb.ed25519_verify(pub, b"", sig))
        self.assertFalse(lb.ed25519_verify(pub, b"x", sig))


if __name__ == "__main__":
    unittest.main()
