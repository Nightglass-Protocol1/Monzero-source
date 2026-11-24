#!/usr/bin/env python3
"""Monzero DEX liquidity bot: sells XMZ from its own wallet on the DEX as a ladder of offers.

The pool sends it a share of the pool's own fee (xmzpool `liquidity`); the owner may add XMZ too.
For each enabled pair (SOL, ETH, BNB) it keeps a ladder of sell offers at steps above a reference
USD price, converted to the coin at its live price. The reference follows demand (`dynamic_price`):
each sale raises it, quiet periods lower it slowly, never below a floor; every change is logged. It runs the trades the same way the phone
wallet does (the same trade helper, `monzero-dex-evm-helper`), approves a trade only when it is
exactly the offer it posted, and after each trade sends what the trade account received to the
pair's vault address, keeping a little for the next trade's network fees.

Safety:
  * New offers only under a valid, unexpired control document signed with the pinned key
    (the DEX kill switch), within its per-trade and 24-hour limits.
  * Each ladder slot has its own trade account (its own key index), so trades never share funds.
  * Offers together never promise more XMZ than the wallet has unlocked.
  * A trade already started always runs to its end (claim or refund), whatever the document says.

  liquidity_bot.py CONFIG init        make the bot's keys and XMZ wallet (once)
  liquidity_bot.py CONFIG accounts    the XMZ address to fund and each slot's trade account
  liquidity_bot.py CONFIG once        one round (add --dry-run to post nothing)
  liquidity_bot.py CONFIG run         rounds forever (the service)
  liquidity_bot.py CONFIG status      what the bot holds and is doing

Standard library only.
"""
import argparse, hashlib, json, logging, os, secrets, signal, socket, subprocess, sys, time
import urllib.request
from decimal import Decimal, ROUND_DOWN
from pathlib import Path

log = logging.getLogger("liquidity")
ATOMIC = 10 ** 11
REQUEST_SCHEMA = "monzero-xmz-evm-helper-request-v1"
CONTROL_DOMAIN = b"monzero-dex-control-v1"
DAY = 86400
WALLET_NAME = "liquidity"

PAIR_DEFAULTS = {
    "SOL": {"chain_id": 501000101, "decimals": 9, "control": "sol-mainnet", "rpc": "https://solana-rpc.publicnode.com",
            "keep": "20000000", "key_base": 100, "solana": True},      # 0.02 SOL left for fees and the account minimum
    "ETH": {"chain_id": 1, "decimals": 18, "control": "eth-mainnet", "rpc": "https://ethereum-rpc.publicnode.com",
            "keep": "2000000000000000", "key_base": 200},    # 0.002 ETH
    "BNB": {"chain_id": 56, "decimals": 18, "control": "bnb-mainnet", "rpc": "https://bsc-rpc.publicnode.com",
            "keep": "4000000000000000", "key_base": 300},    # 0.004 BNB
    # USDT: a token on its chain. The offer names its pair; the price is a dollar; network fees
    # are the chain's coin (keep, gas_coin), and the payout sends the USDT first, then the coin over keep
    "USDT": {"chain_id": 501000101, "pair": "USDT/XMZ", "decimals": 6, "control": "sol-usdt-v2-mainnet",
             "rpc": "https://solana-rpc.publicnode.com", "keep": "20000000", "gas_coin": "SOL", "gas_decimals": 9,
             "usd_fixed": "1", "key_base": 400, "solana": True},
    "USDT-BSC": {"chain_id": 56, "pair": "USDT-BSC/XMZ", "decimals": 18, "control": "bnb-usdt-v2-mainnet",
                 "rpc": "https://bsc-rpc.publicnode.com", "keep": "4000000000000000", "gas_coin": "BNB", "gas_decimals": 18,
                 "usd_fixed": "1", "key_base": 500},
}
DEFAULTS = {
    "network": "mainnet",
    "relays": ["https://relay1.monzero.org", "https://relay2.monzero.org"],
    "price_api": "https://monzero.org/markets/api.php?only=coins",
    "control_base": "https://monzero.org/dex/control/",
    "control_key": "9eba2b1784ddfc23f028c03c78f862fecc81af81616722b56e671e6662e0a6fb",
    "daemon": "127.0.0.1:6175",
    "reference_usd": "0.01",
    # The reference price follows demand: +up% per sale, -down% after every down_every_hours
    # without a sale or change, never below floor_usd. Its state is in price.json.
    "dynamic_price": {"enabled": True, "up_percent_per_sale": 2, "down_percent": 1, "down_every_hours": 12, "floor_usd": "0.01"},
    "ladder_percent": [2, 5, 10, 20],
    "offer_xmz": "2000",
    # Buyers may take any part of an offer from this much XMZ (the rest stays on offer as a new
    # offer); "0" posts whole offers only
    "min_part_xmz": "50",
    "offer_hours": 6,
    "reprice_percent": 3,
    "keep_xmz": "0",
    "round_seconds": 30,
    "main_port": 18190,
    "pairs": {},
}


# ---------------------------------------------------------------- ed25519 (RFC 8032, verify only)
_P = 2 ** 255 - 19
_L = 2 ** 252 + 27742317777372353535851937790883648493
_D = -121665 * pow(121666, _P - 2, _P) % _P
_I = pow(2, (_P - 1) // 4, _P)


def _inv(x):
    return pow(x, _P - 2, _P)


def _add(a, b):
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    A = (y1 - x1) * (y2 - x2) % _P
    B = (y1 + x1) * (y2 + x2) % _P
    C = t1 * 2 * _D * t2 % _P
    D = z1 * 2 * z2 % _P
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F % _P, G * H % _P, F * G % _P, E * H % _P)


def _mul(s, p):
    q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _equal(a, b):
    return (a[0] * b[2] - b[0] * a[2]) % _P == 0 and (a[1] * b[2] - b[1] * a[2]) % _P == 0


def _recover_x(y, sign):
    if y >= _P:
        return None
    x2 = (y * y - 1) * _inv(_D * y * y + 1)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P != 0:
        x = x * _I % _P
    if (x * x - x2) % _P != 0:
        return None
    if (x & 1) != sign:
        x = _P - x
    return x


def _decompress(s):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % _P)


_G = (lambda y: (_recover_x(y, 0), y, 1, _recover_x(y, 0) * y % _P))(4 * _inv(5) % _P)


def ed25519_verify(public, message, signature):
    if len(public) != 32 or len(signature) != 64:
        return False
    A, R = _decompress(public), _decompress(signature[:32])
    if A is None or R is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= _L:
        return False
    h = int.from_bytes(hashlib.sha512(signature[:32] + public + message).digest(), "little") % _L
    return _equal(_mul(s, _G), _add(R, _mul(h, A)))


# ---------------------------------------------------------------- small helpers
def http_json(url, body=None, timeout=20):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, headers={"content-type": "application/json", "user-agent": "monzero-liquidity-bot"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        raw = r.read(1 << 20)
    return json.loads(raw)


def atomic_write(path, text, mode=0o600):
    path = Path(path)
    tmp = path.with_name(path.name + ".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode)
    with os.fdopen(fd, "w") as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def read_json(path, default=None):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return default


def units(atoms, places):
    return format((Decimal(int(atoms)) / (Decimal(10) ** places)).normalize(), "f")


def load_config(path):
    cfg = json.loads(json.dumps(DEFAULTS))
    user = json.loads(Path(path).read_text())
    pairs = user.pop("pairs", {})
    dynamic = {**cfg["dynamic_price"], **user.pop("dynamic_price", {})}
    cfg.update(user)
    cfg["dynamic_price"] = dynamic
    if not (0 <= float(dynamic["up_percent_per_sale"]) <= 50 and 0 <= float(dynamic["down_percent"]) <= 50
            and float(dynamic["down_every_hours"]) >= 1 and Decimal(dynamic["floor_usd"]) > 0):
        sys.exit("config: dynamic_price needs up/down 0..50 %, down_every_hours >= 1 and a positive floor_usd")
    cfg["pairs"] = {}
    for name, p in pairs.items():
        if name not in PAIR_DEFAULTS:
            sys.exit(f"config: unknown pair {name} (SOL, ETH, BNB, USDT or USDT-BSC)")
        cfg["pairs"][name] = {**PAIR_DEFAULTS[name], **p}
    for key in ("helper", "wallet_rpc_bin", "state_dir"):
        if not cfg.get(key):
            sys.exit(f"config: missing {key}")
    if not cfg["ladder_percent"] or any(not 0 <= float(x) <= 500 for x in cfg["ladder_percent"]):
        sys.exit("config: ladder_percent must be a list of steps between 0 and 500")
    if Decimal(cfg["reference_usd"]) <= 0 or Decimal(cfg["offer_xmz"]) <= 0:
        sys.exit("config: reference_usd and offer_xmz must be positive")
    if not 1 <= int(cfg["offer_hours"]) <= 24:
        sys.exit("config: offer_hours must be 1..24")
    for name, p in cfg["pairs"].items():
        if p.get("enabled", True) and not p.get("vault"):
            sys.exit(f"config: pair {name} needs its vault address (where the {name} it receives goes)")
    return cfg


# ---------------------------------------------------------------- wallet RPC services
class WalletService:
    """One monzero-wallet-rpc on a loopback port, with its own login file (as the phone runs them)."""

    def __init__(self, cfg, folder, port):
        self.cfg, self.dir, self.port = cfg, Path(folder), port
        self.proc = None

    @property
    def login_file(self):
        return self.dir / f"monzero-wallet-rpc.{self.port}.login"

    def endpoint(self):
        return {"url": f"http://127.0.0.1:{self.port}", "loginFile": str(self.login_file)}

    def listening(self):
        if not self.login_file.is_file():
            return False
        with socket.socket() as s:
            s.settimeout(1)
            return s.connect_ex(("127.0.0.1", self.port)) == 0

    def start(self):
        if self.proc and self.proc.poll() is None and self.listening():
            return True
        self.dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        (self.dir / "wallets").mkdir(exist_ok=True, mode=0o700)
        with socket.socket() as s:   # someone else's service on our port: leave it (and its login) alone
            s.settimeout(1)
            if s.connect_ex(("127.0.0.1", self.port)) == 0:
                log.error("port %d is already in use (is the bot service running?); not starting a second wallet service", self.port)
                return False
        for old in self.dir.glob("monzero-wallet-rpc.*.login"):
            old.unlink()
        args = [self.cfg["wallet_rpc_bin"]]
        if self.cfg["network"] == "testnet":
            args.append("--testnet")
        args += ["--wallet-dir", str(self.dir / "wallets"), "--daemon-address", self.cfg["daemon"],
                 "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(self.port), "--non-interactive",
                 "--max-concurrency", "2", "--log-file", str(self.dir / "rpc.log"),
                 "--max-log-file-size", "1048576", "--max-log-files", "2"]
        self.proc = subprocess.Popen(args, cwd=self.dir, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, env={"LANG": "C", "HOME": str(self.dir)})
        for _ in range(120):
            if self.listening():
                return True
            if self.proc.poll() is not None:
                break
            time.sleep(0.5)
        log.error("wallet service in %s did not start (see %s)", self.dir, self.dir / "rpc.log")
        self.stop()
        return False

    def stop(self):
        p, self.proc = self.proc, None
        if p and p.poll() is None:
            p.send_signal(signal.SIGTERM)
            try:
                p.wait(30)
            except subprocess.TimeoutExpired:
                p.kill()


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ---------------------------------------------------------------- the bot
class Bot:
    def __init__(self, cfg, dry_run=False):
        self.cfg, self.dry_run = cfg, dry_run
        self.state_dir = Path(cfg["state_dir"])
        self.trades_dir = self.state_dir / "trades"
        self.seed_file = self.state_dir / "keys" / "seed.hex"
        self.password_file = self.state_dir / "keys" / "wallet-password"
        self.ledger_file = self.state_dir / "ledger.jsonl"
        self.slots_file = self.state_dir / "slots.json"
        self.main = WalletService(cfg, self.state_dir / "main-wallet", cfg["main_port"])
        self.trade_services = {}
        self.controls = {}      # control network -> newest valid payload
        self.prices = {}
        self.prices_at = 0
        self.slots = read_json(self.slots_file, {}) or {}
        # Running trades by folder, apart from the offers: with partial offers a slot can have its
        # offer's rest open while a part of it is being swapped
        self.trades_file = self.state_dir / "trades.json"
        self.trades = read_json(self.trades_file, {}) or {}
        for key, s in list(self.slots.items()):    # state from before partial offers: one trade per slot
            if s.get("state") in ("trading", "payout") and s.get("folder"):
                pair, slot = key.rsplit("-", 1)
                self.trades.setdefault(s["folder"], {"pair": pair, "slot": int(slot), "state": s["state"], "valueAtoms": s["valueAtoms"],
                                                     "xmzAtoms": s["xmzAtoms"], "account": s.get("account"), "note": s.get("note", "")})
                self.slots[key] = {"state": "idle", "account": s.get("account"), "note": ""}
        self.price_file = self.state_dir / "price.json"
        self.price = read_json(self.price_file, None)
        self.alerts = []

    # --- helper
    def helper(self, request, timeout=150):
        request = {"schema": REQUEST_SCHEMA, **request}
        try:
            r = subprocess.run([self.cfg["helper"]], input=json.dumps(request).encode(), capture_output=True,
                               timeout=timeout, cwd=self.state_dir)
        except subprocess.TimeoutExpired:
            return {"ok": False, "error": f"the helper took longer than {timeout} s"}
        for line in reversed(r.stdout.decode(errors="replace").strip().splitlines()):
            try:
                v = json.loads(line)
            except ValueError:
                continue
            if isinstance(v, dict):
                return v
        return {"ok": False, "error": "no answer from the helper: " + r.stderr.decode(errors="replace")[-300:]}

    def seed_hex(self):
        return self.seed_file.read_text().strip()

    def tick_config(self, pair, slot, swap=None):
        p = self.cfg["pairs"][pair]
        main = self.main.endpoint()
        return {"evmRpc": p["rpc"], "walletSeedHex": self.seed_hex(), "accountIndex": p["key_base"] + slot,
                "mainWallet": main, "swapWallet": swap or main}

    # --- setup
    def init(self):
        (self.state_dir / "keys").mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(self.state_dir, 0o700)
        if not self.seed_file.exists():
            atomic_write(self.seed_file, secrets.token_bytes(64).hex() + "\n")
            print(f"made the trade-account keys: {self.seed_file} (back it up: it holds every slot's account)")
        if not self.password_file.exists():
            atomic_write(self.password_file, secrets.token_urlsafe(32) + "\n")
        w = self.open_wallet()
        if "address" not in w:
            sys.exit(f"the XMZ wallet could not be opened: {w.get('error')}")
        print("XMZ wallet address (the pool's liquidity.address; send XMZ here to add stock):")
        print("  " + w["address"])
        print(f"back up {self.state_dir / 'keys'} and {self.main.dir / 'wallets'} together; they hold the bot's coins")

    def open_wallet(self):
        if not self.main.start():
            return {"error": "wallet service did not start"}
        r = self.helper({"command": "xmzWallet", "tradeDir": str(self.state_dir),
                         "xmzWallet": {"wallet": self.main.endpoint(), "action": "open", "name": WALLET_NAME,
                                       "passwordFile": str(self.password_file)}}, timeout=300)
        return r if r.get("ok") else {"error": r.get("error")}

    def wallet_status(self):
        r = self.helper({"command": "xmzWallet", "tradeDir": str(self.state_dir),
                         "xmzWallet": {"wallet": self.main.endpoint(), "action": "status", "name": WALLET_NAME,
                                       "passwordFile": str(self.password_file)}}, timeout=300)
        return r if r.get("ok") else None

    def account(self, pair, slot):
        r = self.helper({"command": "accounts", "tradeDir": str(self.state_dir), "tick": self.tick_config(pair, slot)})
        if not r.get("ok"):
            return None
        return r["sol"] if self.cfg["pairs"][pair].get("solana") else r["evm"]

    # --- outside information
    def refresh_prices(self):
        if time.time() - self.prices_at < 120:
            return
        try:
            d = http_json(self.cfg["price_api"])
            self.prices = {c["symbol"]: Decimal(str(c["usd"])) for c in d["coins"] if c.get("usd")}
            self.prices_at = time.time()
        except (OSError, ValueError, KeyError) as e:
            log.warning("prices: %s", e)
            if time.time() - self.prices_at > 1800:
                self.prices = {}   # half an hour old: too stale to price new offers

    def control(self, network):
        """The control payload for a network, or (None, why)."""
        # The website, then the relays' mirrored copies when it does not answer with a valid one
        sources = [self.cfg["control_base"]] + [r.rstrip("/") + "/v1/control/" for r in self.cfg["relays"]]
        for source in sources:
            try:
                req = urllib.request.Request(source + network + ".json", headers={"user-agent": "monzero-liquidity-bot"})
                with urllib.request.urlopen(req, timeout=20) as r:
                    raw = r.read(4097)
                why = self.check_control(raw, network)
                if not why:
                    break
                log.warning("control %s from %s: %s", network, source, why)
            except OSError as e:
                log.warning("control %s from %s: %s", network, source, e)
        doc = self.controls.get(network)
        now = time.time()
        if doc is None:
            return None, "no valid control document"
        if doc["expiresAt"] <= now:
            return None, "the control document expired"
        if not doc["newTradesEnabled"]:
            return None, "new trades are paused by the operator"
        return doc, None

    def check_control(self, raw, network):
        if len(raw) > 4096:
            return "malformed control document"
        try:
            env = json.loads(raw)
            if set(env) != {"payload", "signature"} or not isinstance(env["payload"], str):
                return "malformed control document"
            sig = bytes.fromhex(env["signature"])
            msg = CONTROL_DOMAIN + b"\x00" + env["payload"].encode()
            if not ed25519_verify(bytes.fromhex(self.cfg["control_key"]), msg, sig):
                return "control signature invalid"
            p = json.loads(env["payload"])
        except (ValueError, TypeError):
            return "malformed control document"
        if p.get("schema") != "monzero-dex-control-v1" or p.get("network") != network or not isinstance(p.get("newTradesEnabled"), bool):
            return "malformed control payload"
        for k in ("maxLtcAtomsPerTrade", "maxXmzAtomsPerTrade", "maxLtcAtomsPerDay"):
            if not str(p.get(k, "")).isdigit():
                return "malformed control payload"
        now = time.time()
        if not (p["issuedAt"] < p["expiresAt"] and p["expiresAt"] - p["issuedAt"] <= 30 * DAY and p["issuedAt"] <= now + 300):
            return "control document validity window rejected"
        kept = self.controls.get(network)
        if kept and p["sequence"] < kept["sequence"]:
            return "older control document ignored"
        self.controls[network] = p
        return None

    def gas_balance(self, pair, address):
        p = self.cfg["pairs"][pair]
        try:
            if p.get("solana"):
                r = http_json(p["rpc"], {"jsonrpc": "2.0", "id": 1, "method": "getBalance", "params": [address, {"commitment": "confirmed"}]})
                return int(r["result"]["value"])
            r = http_json(p["rpc"], {"jsonrpc": "2.0", "id": 1, "method": "eth_getBalance", "params": [address, "latest"]})
            return int(r["result"], 16)
        except (OSError, ValueError, KeyError, TypeError) as e:
            log.warning("%s balance of %s: %s", pair, address, e)
            return None

    # --- book-keeping
    def save_slots(self):
        atomic_write(self.slots_file, json.dumps(self.slots, indent=1, sort_keys=True))
        atomic_write(self.trades_file, json.dumps(self.trades, indent=1, sort_keys=True))

    def record(self, event):
        event = {"ts": int(time.time()), **event}
        with open(self.ledger_file, "a") as f:
            f.write(json.dumps(event, sort_keys=True) + "\n")

    def sold_today(self, pair):
        total, since = 0, time.time() - DAY
        try:
            with open(self.ledger_file) as f:
                for line in f:
                    e = json.loads(line)
                    if e.get("event") == "taken" and e.get("pair") == pair and e["ts"] >= since:
                        total += int(e["valueAtoms"])
        except (OSError, ValueError):
            pass
        return total

    def committed_today(self, pair):
        """Sold in the last 24 hours plus every open offer of the pair (any of them may be taken)."""
        return self.sold_today(pair) + sum(s["valueAtoms"] for k, s in self.slots.items()
                                           if k.startswith(pair + "-") and s.get("state") == "open")

    def alert(self, text):
        log.warning(text)
        self.alerts.append(text)

    # --- pricing
    def reference(self):
        """The USD price per XMZ the ladder starts from."""
        if not self.cfg["dynamic_price"]["enabled"]:
            return Decimal(self.cfg["reference_usd"])
        if self.price is None:
            self.price = {"referenceUsd": str(Decimal(self.cfg["reference_usd"])), "changedAt": int(time.time()), "sales": 0}
            self.save_price("start")
        return Decimal(self.price["referenceUsd"])

    def save_price(self, why):
        atomic_write(self.price_file, json.dumps(self.price, indent=1), mode=0o644)
        self.record({"event": "price", "referenceUsd": self.price["referenceUsd"], "why": why})
        log.info("reference price %s USD per XMZ (%s)", self.price["referenceUsd"], why)

    def set_reference(self, value, why):
        floor = Decimal(self.cfg["dynamic_price"]["floor_usd"])
        value = max(floor, value).quantize(Decimal("1e-9"))
        self.price.update(referenceUsd=str(value), changedAt=int(time.time()))
        self.save_price(why)

    def price_after_sale(self, pair, share=Decimal(1)):
        """+up% for a whole offer sold; a part of one moves it by its share (so small buys do not
        push the price up as fast as whole offers)."""
        d = self.cfg["dynamic_price"]
        if not d["enabled"]:
            return
        self.reference()
        self.price["sales"] = int(self.price.get("sales", 0)) + 1
        share = min(Decimal(1), max(Decimal(0), share))
        why = f"sale on {pair}" if share == 1 else f"sale on {pair} ({float(share) * 100:.0f}% of an offer)"
        self.set_reference(Decimal(self.price["referenceUsd"]) * (1 + Decimal(str(d["up_percent_per_sale"])) / 100 * share), why)

    def price_decay(self, now=None):
        """-down% for each full quiet period since the last change (no sale, no change)."""
        d = self.cfg["dynamic_price"]
        if not d["enabled"]:
            return
        ref = self.reference()
        now = now or time.time()
        period = float(d["down_every_hours"]) * 3600
        steps = int((now - self.price["changedAt"]) // period)
        if steps <= 0:
            return
        floor = Decimal(self.cfg["dynamic_price"]["floor_usd"])
        new = max(floor, ref * (1 - Decimal(str(d["down_percent"])) / 100) ** steps)
        if new == ref:   # at the floor already: only the clock moves on
            self.price["changedAt"] = int(self.price["changedAt"] + steps * period)
            atomic_write(self.price_file, json.dumps(self.price, indent=1), mode=0o644)
            return
        self.set_reference(new, f"{steps * float(d['down_every_hours']):g} h without a sale")
        self.price["changedAt"] = int(now)

    def offer_for(self, pair, step, doc):
        """(value in the coin's atoms, XMZ atoms) for one ladder step, inside the document's limits."""
        p = self.cfg["pairs"][pair]
        coin_usd = Decimal(p["usd_fixed"]) if p.get("usd_fixed") else self.prices.get(pair)
        if not coin_usd:
            return None
        price_usd = self.reference() * (1 + Decimal(str(step)) / 100)
        xmz = Decimal(self.cfg["offer_xmz"])
        xmz = min(xmz, Decimal(int(doc["maxXmzAtomsPerTrade"])) / ATOMIC)
        per_trade = Decimal(int(doc["maxLtcAtomsPerTrade"]))
        scale = Decimal(10) ** p["decimals"]
        value = xmz * price_usd / coin_usd * scale
        if value > per_trade:   # the coin limit binds: a smaller offer at the same price
            xmz = per_trade / scale * coin_usd / price_usd
            value = per_trade
        xmz_atoms = int((xmz * ATOMIC).to_integral_value(ROUND_DOWN))
        value_atoms = int(value.to_integral_value(ROUND_DOWN))
        if xmz_atoms < ATOMIC or value_atoms <= 0:
            return None
        return value_atoms, xmz_atoms

    # --- one round
    def round(self):
        self.alerts = []
        self.price_decay()
        self.refresh_prices()
        wallet = self.wallet_status()
        if wallet is None:
            self.alert("the XMZ wallet does not answer; nothing new is posted")
        unlocked = int(wallet["unlockedAtoms"]) if wallet else 0
        for folder, t in list(self.trades.items()):
            try:
                self.trade(folder, t) if t["state"] == "trading" else self.payout(folder, t)
            except Exception as e:   # one trade's trouble never stops the rest
                log.exception("trade %s: %s", Path(folder).name, e)
            self.save_slots()
        # Open offers may be taken, and a running trade's XMZ may not be locked yet: neither is spare
        promised = sum(s["xmzAtoms"] for s in self.slots.values() if s.get("state") == "open")
        promised += sum(t["xmzAtoms"] for t in self.trades.values())
        spare = unlocked - promised - int(Decimal(self.cfg["keep_xmz"]) * ATOMIC)
        for pair, p in self.cfg["pairs"].items():
            if not p.get("enabled", True):
                continue
            doc, why = self.control(p["control"])
            for slot, step in enumerate(self.cfg["ladder_percent"]):
                key = f"{pair}-{slot}"
                s = self.slots.setdefault(key, {"state": "idle"})
                try:
                    spare = self.step_slot(pair, slot, step, s, doc, why, spare)
                except Exception as e:   # one slot's trouble never stops the others
                    log.exception("%s: %s", key, e)
                self.save_slots()
        self.write_status(wallet)

    def step_slot(self, pair, slot, step, s, doc, why, spare):
        key = f"{pair}-{slot}"
        state = s["state"]
        if state == "open":
            return self.serve(pair, slot, step, s, doc, spare)
        # idle: post a new offer when everything allows it
        if doc is None:
            s["note"] = why
            return spare
        offer = self.offer_for(pair, step, doc)
        if offer is None:
            s["note"] = "no live price for " + pair
            return spare
        value, xmz = offer
        if xmz > spare:
            s["note"] = "waiting for unlocked XMZ"
            return spare
        if self.committed_today(pair) + value > int(doc["maxLtcAtomsPerDay"]):
            s["note"] = "the 24-hour limit is reached"
            return spare
        account = s.get("account") or self.account(pair, slot)
        if not account:
            s["note"] = "trade account unknown"
            return spare
        s["account"] = account
        gas = self.gas_balance(pair, account)
        if gas is None or gas < int(self.cfg["pairs"][pair]["keep"]):
            gp = self.cfg["pairs"][pair]
            s["note"] = f"its trade account {account} needs {units(gp['keep'], gp.get('gas_decimals', gp['decimals']))} {gp.get('gas_coin', pair)} for network fees"
            self.alert(f"{key}: " + s["note"])
            return spare
        folder = self.trades_dir / f"{key}-{int(time.time())}"
        if self.dry_run:
            log.info("dry run: %s would offer %s XMZ for %s %s", key, units(xmz, 11), units(value, self.cfg["pairs"][pair]["decimals"]), pair)
            return spare - xmz
        self.trades_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        offer = {"chainId": self.cfg["pairs"][pair]["chain_id"], "side": "sell-xmz", "valueWei": str(value),
                 "xmzAtoms": str(xmz), "lifetimeSecs": int(self.cfg["offer_hours"]) * 3600}
        min_part = self.min_part(xmz)
        if min_part:
            offer["minXmzAtoms"] = str(min_part)
        if self.cfg["pairs"][pair].get("pair"):
            offer["pair"] = self.cfg["pairs"][pair]["pair"]
        r = self.helper({"command": "post", "tradeDir": str(folder), "relays": self.cfg["relays"], "tick": self.tick_config(pair, slot),
                         "offer": offer})
        listing = (r.get("listing") or {}).get("listing") if r.get("ok") else None
        if not listing and not (folder / "listing.json").exists():
            s["note"] = "post refused: " + str(r.get("error"))[:200]
            log.warning("%s: %s", key, s["note"])
            return spare
        s.update({"state": "open", "folder": str(folder), "valueAtoms": value, "xmzAtoms": xmz, "step": step, "note": "",
                  "refUsd": str(self.reference()), "fullXmzAtoms": xmz, "minXmzAtoms": min_part,
                  "postedAt": int(time.time()), "expiresAt": int(time.time()) + int(self.cfg["offer_hours"]) * 3600})
        self.record({"event": "posted", "pair": pair, "slot": slot, "valueAtoms": str(value), "xmzAtoms": str(xmz), "folder": folder.name})
        log.info("%s: offered %s XMZ for %s %s", key, units(xmz, 11), units(value, self.cfg["pairs"][pair]["decimals"]), pair)
        return spare - xmz

    def serve(self, pair, slot, step, s, doc, spare):
        key = f"{pair}-{slot}"
        r = self.helper({"command": "serve", "tradeDir": s["folder"], "relays": self.cfg["relays"], "tick": self.tick_config(pair, slot)})
        if not r.get("ok"):
            s["note"] = "serve: " + str(r.get("error"))[:200]
            return spare
        if r.get("taken"):
            return self.taken(pair, slot, s, r, spare)
        if r.get("expired"):
            self.close(pair, slot, s, "expired")
            return spare
        # An offer from before partial offers (or from another setting): replace it
        if not self.dry_run and bool(self.min_part(s["xmzAtoms"])) != bool(s.get("minXmzAtoms")):
            c = self.helper({"command": "cancel", "tradeDir": s["folder"], "relays": self.cfg["relays"], "tick": self.tick_config(pair, slot)})
            if c.get("ok"):
                self.close(pair, slot, s, "cancelled to change partial fills")
            return spare
        # Re-price when the coin moved, or when the operator paused new trades
        fresh = self.offer_for(pair, step, doc) if doc else None
        # ...or when the reference price changed since it was posted (a sale or a quiet period)
        posted_ref = Decimal(s.get("refUsd") or self.reference())
        ref_moved = abs(self.reference() - posted_ref) * 1000 >= posted_ref * 9   # 0.9 % or more
        if doc is None or ref_moved or (fresh and abs(fresh[0] - s["valueAtoms"]) * 100 > s["valueAtoms"] * float(self.cfg["reprice_percent"])):
            if self.dry_run:
                return spare
            c = self.helper({"command": "cancel", "tradeDir": s["folder"], "relays": self.cfg["relays"], "tick": self.tick_config(pair, slot)})
            if c.get("ok"):
                self.close(pair, slot, s, "cancelled to re-price" if doc else "cancelled: new trades paused")
            else:
                s["note"] = "cancel: " + str(c.get("error"))[:200]
        return spare

    def min_part(self, xmz):
        """The smallest part buyers may take of an offer of `xmz` atoms; 0 for a whole offer"""
        m = int(Decimal(str(self.cfg.get("min_part_xmz", "0"))) * ATOMIC)
        if m <= 0:
            return 0
        m = max(m, ATOMIC)            # the DEX's floor: 1 XMZ
        return m if m * 2 <= xmz else 0   # a part must leave room for another; smaller offers stay whole

    def taken(self, pair, slot, s, r, spare):
        """All or part of the slot's offer was taken: the part becomes a trade; any rest is the
        slot's open offer from now on (the helper posted it in its own folder)."""
        key = f"{pair}-{slot}"
        part = int(r.get("takenXmz") or s["xmzAtoms"])
        if part <= 0 or part > s["xmzAtoms"]:
            self.alert(f"{key}: the helper reports {part} XMZ atoms taken of an offer of {s['xmzAtoms']}; nothing approved")
            return spare
        # The coin for the part at the offer's price, rounded up for us as the helper does
        value = s["valueAtoms"] if part == s["xmzAtoms"] else -(-s["valueAtoms"] * part // s["xmzAtoms"])
        self.trades[s["folder"]] = {"pair": pair, "slot": slot, "state": "trading", "valueAtoms": value, "xmzAtoms": part,
                                    "account": s.get("account"), "note": ""}
        self.record({"event": "taken", "pair": pair, "slot": slot, "valueAtoms": str(value), "xmzAtoms": str(part),
                     "of": str(s["xmzAtoms"]), "folder": Path(s["folder"]).name})
        log.info("%s: %s XMZ of %s taken", key, units(part, 11), units(s["xmzAtoms"], 11))
        self.price_after_sale(pair, Decimal(part) / Decimal(s.get("fullXmzAtoms") or s["xmzAtoms"]))
        rest = r.get("remainder") or {}
        listing = (rest.get("listing") or {}).get("listing") or {}
        if rest.get("folder") and listing:
            rest_xmz, rest_value = int(listing["xmz"]), int(str(listing["value"]), 0)
            self.slots[key] = {**s, "folder": rest["folder"], "xmzAtoms": rest_xmz, "valueAtoms": rest_value,
                               "minXmzAtoms": int(listing.get("minXmz") or 0), "expiresAt": int(listing.get("expiresAt") or s.get("expiresAt", 0)),
                               "note": ""}
            self.record({"event": "rest", "pair": pair, "slot": slot, "valueAtoms": str(rest_value), "xmzAtoms": str(rest_xmz),
                         "folder": Path(rest["folder"]).name})
            log.info("%s: %s XMZ stay on offer", key, units(rest_xmz, 11))
        else:
            self.slots[key] = {"state": "idle", "account": s.get("account"), "note": ""}
        self.trade(s["folder"], self.trades[s["folder"]])
        return spare

    def close(self, pair, slot, s, why):
        self.record({"event": "closed", "pair": pair, "slot": slot, "why": why, "folder": Path(s["folder"]).name})
        log.info("%s-%d: offer %s", pair, slot, why)
        self.slots[f"{pair}-{slot}"] = {"state": "idle", "account": s.get("account"), "note": why}

    def trade_service(self, folder, s):
        svc = self.trade_services.get(folder)
        if svc is None:
            port_file = Path(folder) / "swap-service.port"
            port = int(port_file.read_text()) if port_file.exists() else free_port()
            atomic_write(port_file, str(port))
            svc = self.trade_services[folder] = WalletService(self.cfg, Path(folder) / "swap-service", port)
        return svc if svc.start() else None

    def trade(self, folder, s):
        pair, slot = s["pair"], s["slot"]
        key = f"{pair}-{slot} {Path(folder).name}"
        status = self.helper({"command": "status", "tradeDir": folder})
        if status.get("ok") and status.get("done"):
            s["state"] = "payout"
            return self.payout(folder, s)
        svc = self.trade_service(folder, s)
        if svc is None:
            s["note"] = "the trade's wallet service did not start"
            return
        r = self.helper({"command": "tick", "tradeDir": folder, "tick": self.tick_config(pair, slot, svc.endpoint())}, timeout=330)
        if not r.get("ok"):
            s["note"] = "tick: " + str(r.get("error"))[:200]
            log.warning("%s: %s", key, s["note"])
            return
        s["note"] = r.get("stage", "")
        if r.get("awaitingFunding"):
            self.alert(f"{key}: the trade account {r.get('account')} needs {units(r['awaitingFunding']['missingWei'], self.cfg['pairs'][pair]['decimals'])} more {pair} for this trade's fees")
        if r.get("awaitingApproval") and not r.get("approved"):
            offer = r.get("offer") or {}
            # Only the trade we offered: our side, our chain, our amounts
            # A USDT pair's trade moves the token, a coin pair's the coin (asset zero)
            token = any(c not in "0x" for c in str(offer.get("asset", "0x0")))
            if (r.get("role") == "claimer" and int(offer.get("chainId", 0)) == self.cfg["pairs"][pair]["chain_id"]
                    and token == bool(self.cfg["pairs"][pair].get("pair"))
                    and int(offer.get("value", "0"), 0) == s["valueAtoms"] and int(offer.get("xmzAmount", "0"), 0) == s["xmzAtoms"]):
                a = self.helper({"command": "approve", "tradeDir": folder, "commitment": r["commitment"]})
                log.info("%s: approved the trade%s", key, "" if a.get("ok") else ": " + str(a.get("error")))
            else:
                self.alert(f"{key}: the trade does not match the offer; NOT approved (role {r.get('role')}, {offer})")
        if r.get("done"):
            s["state"] = "payout"
            self.payout(folder, s)

    def payout(self, folder, s):
        pair, slot = s["pair"], s["slot"]
        key = f"{pair}-{slot} {Path(folder).name}"
        p = self.cfg["pairs"][pair]
        svc = self.trade_services.pop(folder, None)
        if svc:
            svc.stop()
        r = self.helper({"command": "payout", "tradeDir": folder, "tick": self.tick_config(pair, slot),
                         "payoutAddress": p["vault"], "keepWei": str(p["keep"])})
        if not r.get("ok"):
            s["note"] = "payout: " + str(r.get("error"))[:200]
            log.warning("%s: %s", key, s["note"])
            return
        paid = r.get("payout") or {}
        self.helper({"command": "report", "tradeDir": folder, "relays": self.cfg["relays"]})
        self.record({"event": "finished", "pair": pair, "slot": slot, "stage": r.get("stage"), "payoutTx": paid.get("tx"),
                     "paidAtoms": paid.get("amountWei"), "folder": Path(folder).name})
        log.info("%s: trade finished (%s); %s %s to the vault%s", key, r.get("stage"),
                 units(paid.get("amountWei", 0), p["decimals"]), pair, f" ({paid['tx']})" if paid.get("tx") else "")
        del self.trades[folder]

    def write_status(self, wallet):
        atomic_write(self.state_dir / "status.json", json.dumps({
            "updatedAt": int(time.time()), "dryRun": self.dry_run, "referenceUsd": str(self.reference()),
            "ladderUsd": [str((self.reference() * (1 + Decimal(str(x)) / 100)).quantize(Decimal("1e-8"))) for x in self.cfg["ladder_percent"]],
            "xmz": {"address": (wallet or {}).get("address"), "balance": units((wallet or {}).get("balanceAtoms", 0), 11),
                    "unlocked": units((wallet or {}).get("unlockedAtoms", 0), 11)},
            "prices": {k: str(v) for k, v in self.prices.items()},
            "slots": {k: {"state": v.get("state"), "xmz": units(v.get("xmzAtoms", 0), 11) if v.get("state") == "open" else None,
                          "fromXmz": units(v["minXmzAtoms"], 11) if v.get("state") == "open" and v.get("minXmzAtoms") else None,
                          "note": v.get("note", "")} for k, v in sorted(self.slots.items())},
            "trades": {Path(f).name: {"pair": t["pair"], "state": t["state"], "xmz": units(t["xmzAtoms"], 11), "note": t.get("note", "")}
                       for f, t in sorted(self.trades.items())},
            "alerts": self.alerts}, indent=1), mode=0o644)

    def shutdown(self):
        for svc in self.trade_services.values():
            svc.stop()
        self.main.stop()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("config")
    ap.add_argument("command", choices=["init", "accounts", "once", "run", "status"])
    ap.add_argument("--dry-run", action="store_true", help="post nothing; show what would be offered")
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    cfg = load_config(a.config)
    bot = Bot(cfg, dry_run=a.dry_run)
    if a.command == "status":
        print(json.dumps(read_json(bot.state_dir / "status.json", {}), indent=1))
        return
    try:
        if a.command == "init":
            bot.init()
        elif a.command == "accounts":
            w = bot.open_wallet()
            print("XMZ wallet:", w.get("address", w.get("error")))
            for pair, p in cfg["pairs"].items():
                for slot, _ in enumerate(cfg["ladder_percent"]):
                    acct = bot.account(pair, slot)
                    gas = bot.gas_balance(pair, acct) if acct else None
                    have = units(gas, p["decimals"]) if gas is not None else "?"
                    print(f"{pair}-{slot}: {acct}  holds {have} {pair}, needs at least {units(p['keep'], p['decimals'])}")
            # The offers' maker addresses (EVM form for every pair): the website's Telegram feed
            # leaves these out (quiet_makers in monzero-private/dex-status.php)
            makers = []
            for pair in cfg["pairs"]:
                for slot, _ in enumerate(cfg["ladder_percent"]):
                    r = bot.helper({"command": "accounts", "tradeDir": str(bot.state_dir), "tick": bot.tick_config(pair, slot)})
                    if r.get("ok"):
                        makers.append(r["evm"])
            print("quiet_makers for the website's dex-status.php:")
            print("  " + json.dumps(makers))
        elif a.command == "once":
            if "error" in bot.open_wallet():
                sys.exit("the XMZ wallet could not be opened")
            bot.round()
            print(json.dumps(read_json(bot.state_dir / "status.json"), indent=1))
        else:
            stop = []
            signal.signal(signal.SIGTERM, lambda *_: stop.append(1))
            while not stop:
                if "error" in bot.open_wallet():
                    log.error("the XMZ wallet could not be opened; trying again in a minute")
                    time.sleep(60)
                    continue
                bot.round()
                for _ in range(int(cfg["round_seconds"])):
                    if stop:
                        break
                    time.sleep(1)
    finally:
        bot.shutdown()


if __name__ == "__main__":
    main()
