#!/usr/bin/env python3
"""End-to-end check of the maker's funds re-check (helper_market::serve) with the real helper.

A maker posts an XMZ sell offer on a local Solana validator; the XMZ wallet and the relays are
stand-ins served here, so the maker's XMZ balance can be changed under it.

  1. take arrives while the maker's XMZ is gone  -> taker refused, offer withdrawn, relays told
  2. no take, balance gone, periodic check due    -> offer withdrawn on its own
  3. take arrives, wallet unreachable              -> letter left unanswered, offer stays open

  solana-test-validator --reset --bpf-program <ID> monzero_sol_swap.so --rpc-port 18899 ...
  python3 tests/maker_funds_recheck.py <helper binary> http://127.0.0.1:18899 <ID>
"""
import json, os, secrets, subprocess, sys, tempfile, threading, time, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HELPER, SOL_RPC, PROGRAM = sys.argv[1:4]
CHAIN = 501_000_103  # SolLocal
XMZ = 10**11

state = {"unlocked": 10**6 * XMZ, "wallet_fails": False, "listings": {}, "mail": {}, "cancels": []}


class Stub(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/v1/listings":
            return self.reply(200, {"listings": list(state["listings"].values())})
        if u.path == "/v1/mail":
            return self.reply(200, {"mail": state["mail"].get(q["to"][0].lower(), [])})
        self.reply(404, {"error": "not found"})

    def do_POST(self):
        u = urlparse(self.path)
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        if u.path == "/v1/listings":
            body.pop("solAccount", None)
            state["listings"][body["listing"]["id"]] = body
            return self.reply(200, {"ok": True})
        if u.path == "/v1/mail":
            state["mail"].setdefault(body["to"].lower(), []).append({"at": int(time.time()), "sealed": body["sealed"]})
            return self.reply(200, {"ok": True})
        if u.path == "/v1/cancel":
            state["cancels"].append(body)
            state["listings"].pop(body["cancel"]["id"], None)
            return self.reply(200, {"ok": True})
        if u.path.startswith("/wallet"):  # stand-in XMZ wallet RPC
            if state["wallet_fails"]:
                return self.reply(500, {"error": "down"})
            m = body.get("method")
            if m == "get_balance":
                return self.reply(200, {"jsonrpc": "2.0", "id": "0", "result": {"balance": state["unlocked"], "unlocked_balance": state["unlocked"]}})
            if m == "get_height":
                return self.reply(200, {"jsonrpc": "2.0", "id": "0", "result": {"height": 33000}})
            return self.reply(200, {"jsonrpc": "2.0", "id": "0", "result": {}})
        self.reply(404, {"error": "not found"})


srv = ThreadingHTTPServer(("127.0.0.1", 0), Stub)
threading.Thread(target=srv.serve_forever, daemon=True).start()
BASE = f"http://127.0.0.1:{srv.server_address[1]}"


def tick(seed):
    return {"evmRpc": SOL_RPC, "walletSeedHex": seed, "accountIndex": 0,
            "mainWallet": {"url": BASE + "/wallet"}, "swapWallet": {"url": BASE + "/wallet"}}


def helper(command, folder, seed, **extra):
    req = {"schema": "monzero-xmz-evm-helper-request-v1", "command": command, "tradeDir": folder,
           "tick": tick(seed), "relays": [BASE], "localSolProgram": PROGRAM, **extra}
    out = subprocess.run([HELPER], input=json.dumps(req).encode(), capture_output=True, timeout=180).stdout
    return json.loads(out)


def rpc(method, params):
    r = urllib.request.Request(SOL_RPC, json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode(),
                               {"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(r, timeout=30))


def airdrop(account, lamports):
    rpc("requestAirdrop", [account, lamports])
    for _ in range(60):
        if rpc("getBalance", [account, {"commitment": "confirmed"}])["result"]["value"] >= lamports:
            return
        time.sleep(0.5)
    raise SystemExit("airdrop did not arrive")


def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        global failed
        failed = True


failed = False
root = tempfile.mkdtemp(prefix="funds-recheck-")
maker_seed, taker_seed = secrets.token_hex(64), secrets.token_hex(64)


def post(name, lifetime=3600, renew=False):
    d = os.path.join(root, "maker", name)
    r = helper("post", d, maker_seed, offer={"chainId": CHAIN, "side": "sell-xmz", "valueWei": "50000000",
                                            "xmzAtoms": str(1000 * XMZ), "lifetimeSecs": lifetime, "renew": renew})
    assert "error" not in r or r.get("ok") is not False, r
    return d, r


def take(name, listing):
    d = os.path.join(root, "taker", name)
    r = helper("take", d, taker_seed, listing=listing)
    if r.get("ok") is False and "trade account" in r.get("error", ""):  # fund the taker's SOL trade account once
        account = r["error"].split("trade account ")[1].split(" ")[0]
        airdrop(account, 2_000_000_000)
        r = helper("take", d, taker_seed, listing=listing)
    return d, r


# 1. take arrives after the maker's XMZ is gone
state["unlocked"] = 10**6 * XMZ
d1, r = post("one")
check(r.get("ok") is not False and len(state["listings"]) == 1, f"offer posted ({r.get('error', 'ok')})")
listing = next(iter(state["listings"].values()))
t1, r = take("one", listing)
check(r.get("ok") is not False, f"taker sent its take ({r.get('error', 'ok')})")
state["unlocked"] = 10 * XMZ  # spent elsewhere
r = helper("serve", d1, maker_seed)
check(r.get("offerWithdrawn") is True, f"maker withdrew the offer: {r.get('reason')}")
check(os.path.isfile(os.path.join(d1, "withdrawn")), "withdrawn marker written")
check(len(state["cancels"]) == 1 and not state["listings"], "relays were told (signed cancel) and the offer left the board")
r = helper("take", t1, taker_seed)
check(isinstance(r.get("refused"), str) and "withdrawn" in r["refused"], f"taker learns it was refused: {r.get('refused')}")
r = helper("serve", d1, maker_seed)
check(r.get("offerWithdrawn") is True and len(state["cancels"]) == 1, "a later serve keeps it withdrawn, no second cancel")

# 2. periodic check with no take
state["unlocked"] = 10**6 * XMZ
d2, r = post("two")
check(len(state["listings"]) == 1, "second offer posted")
listing2 = next(iter(state["listings"].values()))
r = helper("serve", d2, maker_seed)
check(r.get("taken") is False and not r.get("offerWithdrawn"), "funded offer stays open")
state["unlocked"] = 0
r = helper("serve", d2, maker_seed)
check(not r.get("offerWithdrawn"), "within 10 minutes of the last check: not re-checked yet")
os.remove(os.path.join(d2, "funds-checked"))  # as if 10 minutes passed
r = helper("serve", d2, maker_seed)
check(r.get("offerWithdrawn") is True and not state["listings"], "periodic check withdrew the unfunded offer")
t2, r = take("two", listing2)
r = helper("serve", d2, maker_seed)
check(r.get("offerWithdrawn") is True and len(state["cancels"]) == 2, "a take after the withdrawal: still withdrawn, no extra cancel")
r = helper("take", t2, taker_seed)
check(isinstance(r.get("refused"), str) and "withdrawn" in r["refused"], f"late taker is refused too: {r.get('refused')}")

# 3. wallet unreachable when a take arrives
state["unlocked"] = 10**6 * XMZ
d3, r = post("three")
listing = next(iter(state["listings"].values()))
t3, r = take("three", listing)
state["wallet_fails"] = True
if os.path.exists(os.path.join(d3, "funds-checked")):
    os.remove(os.path.join(d3, "funds-checked"))
r = helper("serve", d3, maker_seed)
mail_before = sum(len(v) for v in state["mail"].values())
check(not r.get("offerWithdrawn") and not os.path.exists(os.path.join(d3, "withdrawn")) and len(state["listings"]) == 1,
      f"unknown balance: no withdrawal ({json.dumps(r)[:120]})")
check(sum(len(v) for v in state["mail"].values()) == mail_before, "unknown balance: the take is not answered yet")
state["wallet_fails"] = False
r = helper("serve", d3, maker_seed)
check(not r.get("offerWithdrawn") and r.get("answered") == 1, f"wallet back with the funds: the take is answered ({json.dumps(r)[:120]})")

# 4. auto-renew ("keep open")
state["listings"].clear()
d4, r = post("four", lifetime=150, renew=True)  # renewed in its last 60 s (after 90 s)
check(os.path.isfile(os.path.join(d4, "renew")), "keep-open offer posted with its renew mark")
first = next(iter(state["listings"].values()))
r = helper("serve", d4, maker_seed)
check(r.get("renew") is True and "renewed" not in r, "not renewed while more than a minute is left")
time.sleep(92)
r = helper("serve", d4, maker_seed)
nxt = (r.get("renewed") or {}).get("folder")
check(nxt and os.path.isfile(os.path.join(nxt, "listing.json")) and os.path.isfile(os.path.join(nxt, "renew")),
      f"renewed into a new folder that is kept open too ({nxt})")
new_l = json.load(open(os.path.join(nxt, "listing.json")))["listing"]["listing"]
old_l = first["listing"]
check(new_l["value"] == old_l["value"] and new_l["xmz"] == old_l["xmz"] and new_l["side"] == old_l["side"]
      and new_l["expiresAt"] - new_l["createdAt"] == old_l["expiresAt"] - old_l["createdAt"] and new_l["id"] != old_l["id"],
      "same side, price, amount and life; new id")
check(list(state["listings"]) == [new_l["id"]], "relays carry only the renewal (old one cancelled)")
t4, r = take("four-old", first)  # a taker still holding the old offer
r = helper("serve", d4, maker_seed)
check(r.get("renewed", {}).get("folder") == nxt, "old offer stays retired")
r = helper("take", t4, taker_seed)
check(isinstance(r.get("refused"), str) and "renewed" in r["refused"], f"a take of the old offer is refused: {r.get('refused')}")
r = helper("serve", nxt, maker_seed)
check(r.get("taken") is False and r.get("renew") is True and not r.get("offerWithdrawn"), "the renewal serves as a normal open offer")

# 5. renewal due but the coins are gone: withdrawn, not renewed
state["listings"].clear()
d5, r = post("five", lifetime=150, renew=True)
state["unlocked"] = 0
open(os.path.join(d5, "funds-checked"), "w").write(str(int(time.time())))  # the periodic check is not due
time.sleep(92)
r = helper("serve", d5, maker_seed)
check(r.get("offerWithdrawn") is True and "renewed" not in r and not state["listings"], "renewal due with no funds: withdrawn instead")
state["unlocked"] = 10**6 * XMZ

print("FAILED" if failed else "ALL PASSED")
sys.exit(1 if failed else 0)
