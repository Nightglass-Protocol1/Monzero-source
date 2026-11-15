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
    d = Path(req["tradeDir"])
    part = scen.get("taken_xmz")
    if taken and part and int(part) < int(offer["xmzAtoms"]) and not d.name.endswith("-rest"):
        whole_v, whole_x, part = int(offer["valueWei"]), int(offer["xmzAtoms"]), int(part)
        v = -(-whole_v * part // whole_x)                     # rounded up for the maker, as the engine does
        (d / "taken.json").write_text(json.dumps({"valueWei": str(v), "xmzAtoms": str(part)}))
        rest = d.parent / (d.name + "-rest"); rest.mkdir(exist_ok=True)
        r = {**offer, "valueWei": str(whole_v - v), "xmzAtoms": str(whole_x - part)}
        (rest / "offer.json").write_text(json.dumps(r)); (rest / "listing.json").write_text(json.dumps(r))
        out({"taken": True, "takenXmz": str(part), "remainder": {"folder": str(rest), "listing": {"listing": {
            "xmz": r["xmzAtoms"], "value": hex(int(r["valueWei"])), "minXmz": offer.get("minXmzAtoms"), "expiresAt": 1}}}})
    out({"taken": taken and not (scen.get("rest_waits") and d.name.endswith("-rest")), "expired": False})
if cmd == "cancel":
    out({"cancelled": True})
tk = Path(req["tradeDir"]) / "taken.json"
part = json.loads(tk.read_text()) if tk.exists() else offer
value = scen.get("trade_value") or part.get("valueWei")
trade = {"role": scen.get("role", "claimer"), "commitment": "0x" + "ab" * 32, "stage": "Agreed",
         "offer": {"chainId": offer.get("chainId"), "value": value, "xmzAmount": part.get("xmzAtoms")}}
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
                                        # The figures below were worked out at the first $0.001 price
                                        "reference_usd": "0.001", "dynamic_price": {"floor_usd": "0.001"},
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
        self.assertEqual(self.bot.trades, {}, "both trades finished")
        self.assertEqual({s["state"] for s in self.bot.slots.values()}, {"open"}, "the slots offer again")
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


class PartialOffers(BotTest):
    def test_offers_allow_parts_from_the_minimum(self):
        self.bot.round()
        self.assertEqual({p["offer"]["minXmzAtoms"] for p in self.requests("post")}, {str(50 * lb.ATOMIC)})

    def test_whole_offers_when_switched_off_or_too_small(self):
        self.bot.cfg["min_part_xmz"] = "0"
        self.bot.round()
        self.assertTrue(all("minXmzAtoms" not in p["offer"] for p in self.requests("post")))
        self.assertEqual(self.bot.min_part(90 * lb.ATOMIC), 0, "a part of 50 must leave room for another")
        self.bot.cfg["min_part_xmz"] = "0.1"
        self.assertEqual(self.bot.min_part(2000 * lb.ATOMIC), lb.ATOMIC, "never below the DEX's 1 XMZ")

    def test_a_part_is_sold_and_the_rest_stays_on_offer(self):
        self.bot.cfg["ladder_percent"] = [2]
        self.bot.round()
        whole = self.bot.slots["ETH-0"]
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True, "taken_xmz": str(500 * lb.ATOMIC), "rest_waits": True})
        self.bot.round()
        # The part: a quarter of 2000 XMZ at 0.00204 ETH, rounded up for the bot
        (folder, t), = self.bot.trades.items()
        self.assertEqual(t["xmzAtoms"], 500 * lb.ATOMIC)
        self.assertEqual(t["valueAtoms"], -(-whole["valueAtoms"] * 500 // 2000))
        self.assertEqual(len(self.requests("approve")), 1, "the trade for the part is approved")
        slot = self.bot.slots["ETH-0"]
        self.assertEqual((slot["state"], slot["xmzAtoms"]), ("open", 1500 * lb.ATOMIC))
        self.assertTrue(slot["folder"].endswith("-rest"))
        self.assertEqual(slot["valueAtoms"] + t["valueAtoms"], whole["valueAtoms"])
        # The price moves by the part's share: a quarter of 2%
        self.assertEqual(self.bot.reference(), Decimal("0.001005"))
        taken = [json.loads(x) for x in self.bot.ledger_file.read_text().splitlines() if '"taken"' in x]
        self.assertEqual([e["valueAtoms"] for e in taken], [str(t["valueAtoms"])], "the day limit counts the part")
        # The rest is served (not posted again) while the part is swapped
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True, "rest_waits": True, "done": True})
        self.bot.round()
        self.assertEqual(self.bot.trades, {})
        self.assertEqual(len(self.requests("payout")), 1)
        self.assertEqual(len(self.requests("post")), 1)
        self.assertTrue(any(r["tradeDir"].endswith("-rest") for r in self.requests("serve")))

    def test_a_part_with_other_amounts_is_not_approved(self):
        self.bot.cfg["ladder_percent"] = [2]
        self.bot.round()
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True, "taken_xmz": str(500 * lb.ATOMIC), "rest_waits": True,
                       "trade_value": "1"})
        self.bot.round()
        self.assertEqual(self.requests("approve"), [])
        self.assertTrue(any("NOT approved" in a for a in self.bot.alerts))

    def test_whole_offers_from_before_are_replaced(self):
        self.bot.cfg["min_part_xmz"] = "0"
        self.bot.round()
        self.bot.cfg["min_part_xmz"] = "50"
        self.bot.round()
        self.assertEqual(len(self.requests("cancel")), 2)
        self.bot.round()
        self.assertEqual([p["offer"].get("minXmzAtoms") for p in self.requests("post")][2:], [str(50 * lb.ATOMIC)] * 2)

    def test_a_trade_running_in_a_slot_from_before_carries_on(self):
        self.bot.round()
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True})
        self.bot.cfg["ladder_percent"] = [2]
        s = self.bot.slots["ETH-0"]
        old = {"ETH-0": {**s, "state": "trading"}}
        self.bot.slots_file.write_text(json.dumps(old))
        self.bot.trades_file.unlink(missing_ok=True)
        bot = lb.Bot(self.bot.cfg)
        self.assertEqual(list(bot.trades), [s["folder"]])
        self.assertEqual(bot.slots["ETH-0"]["state"], "idle")


class DynamicPrice(BotTest):
    def ref(self):
        return self.bot.reference()

    def test_a_sale_raises_the_price_and_reprices_the_ladder(self):
        self.bot.cfg["ladder_percent"] = [2]
        self.bot.round()
        self.assertEqual(self.ref(), Decimal("0.001"))
        self.scenario({"unlocked": 100000 * lb.ATOMIC, "taken": True, "done": True})
        self.bot.round()                       # taken, finished, paid out
        self.assertEqual(self.ref(), Decimal("0.00102"))
        self.scenario({"unlocked": 100000 * lb.ATOMIC})
        self.bot.round()                       # the slot posts again, 2% higher
        posts = self.requests("post")
        self.assertEqual([p["offer"]["valueWei"] for p in posts], [str(1020000000000000), str(1040400000000000)])
        events = [json.loads(x) for x in self.bot.ledger_file.read_text().splitlines()]
        self.assertEqual([e["referenceUsd"] for e in events if e["event"] == "price"], ["0.001", "0.001020000"])

    def test_open_offers_follow_a_price_change(self):
        self.bot.round()
        self.bot.set_reference(Decimal("0.00102"), "test")
        self.bot.round()
        self.assertEqual(len(self.requests("cancel")), 2, "both open offers withdrawn to re-post at the new price")
        self.bot.round()
        self.assertEqual(len(self.requests("post")), 4)

    def test_quiet_periods_lower_it_down_to_the_floor(self):
        self.bot.cfg["dynamic_price"]["floor_usd"] = "0.00097"
        self.bot.reference()
        self.bot.set_reference(Decimal("0.001"), "test")
        t0 = self.bot.price["changedAt"]
        self.bot.price_decay(t0 + 11 * 3600)
        self.assertEqual(self.ref(), Decimal("0.001"), "not before 12 hours")
        self.bot.price_decay(t0 + 12 * 3600)
        self.assertEqual(self.ref(), Decimal("0.00099"))
        self.bot.price_decay(t0 + 12 * 3600 + 3600)
        self.assertEqual(self.ref(), Decimal("0.00099"), "the clock restarts after a change")
        self.bot.price_decay(t0 + 100 * 3600)
        self.assertEqual(self.ref(), Decimal("0.00097"), "never below the floor")
        self.bot.price_decay(t0 + 200 * 3600)
        self.assertEqual(self.ref(), Decimal("0.00097"))

    def test_fixed_when_switched_off(self):
        self.bot.cfg["dynamic_price"]["enabled"] = False
        self.bot.price_after_sale("ETH")
        self.bot.price_decay(time.time() + 99 * 3600)
        self.assertEqual(self.ref(), Decimal("0.001"))
        self.assertFalse(self.bot.price_file.exists())


class Ed25519(unittest.TestCase):
    def test_rfc8032_vector_1(self):
        pub = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
        sig = bytes.fromhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")
        self.assertTrue(lb.ed25519_verify(pub, b"", sig))
        self.assertFalse(lb.ed25519_verify(pub, b"x", sig))


if __name__ == "__main__":
    unittest.main()
