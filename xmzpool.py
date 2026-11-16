#!/usr/bin/env python3
"""Monzero (XMZ) mining pool: RandomX stratum, PPLNS, stats API.

Standard library only. The pool's own monzerod (unrestricted RPC on
localhost) builds a block template per miner job with a unique extra nonce and
checks every share with `calc_pow`, so no hashing code lives here.
monzero-wallet-rpc holds the pool wallet; it validates miner addresses and,
only when payouts are explicitly enabled, sends payments.

Money rules:
- Balances and payments are integers in atomic units (1 XMZ = 10**11).
- A block is credited once, after `confirmations` blocks, and only if the
  chain still holds the exact block the pool submitted.
- A payment is written to the database as "sending" (with the balances
  already deducted) before the wallet is called. A definite wallet error
  restores the balances; an uncertain result (timeout, lost reply) stops all
  payouts until the operator reconciles it. Nothing is ever retried blindly.
"""
import argparse, asyncio, hashlib, http.client, json, logging, os, secrets, sqlite3, sys, time, urllib.parse, urllib.request, urllib.error
from pathlib import Path

ATOMIC = 10**11
NONCE_OFFSET = 39          # bytes: major, minor, timestamp varints + prev hash
MAX_JOBS_PER_MINER = 4
log = logging.getLogger("xmzpool")


# ---------------------------------------------------------------- RPC helpers
class RpcError(Exception):
    """The server answered with an error: the call definitely did not succeed."""

class RpcUncertain(Exception):
    """No usable answer (timeout, connection lost): the outcome is unknown."""

def digest_header(challenges, user, password, path):
    """Answer an RFC 2617 MD5 qop=auth challenge. monero-wallet-rpc offers MD5
    and MD5-sess together, which urllib's digest handler fails on."""
    for c in challenges:
        if not c.lower().startswith("digest "):
            continue
        f = dict((k.strip().lower(), v.strip().strip('"')) for k, _, v in
                 (p.partition("=") for p in c[7:].split(",")))
        if f.get("algorithm", "MD5").upper() != "MD5" or "auth" not in f.get("qop", "").split(","):
            continue
        h = lambda s: hashlib.md5(s.encode()).hexdigest()
        cnonce, nc = secrets.token_hex(8), "00000001"
        realm, nonce = f["realm"], f["nonce"]
        ha1, ha2 = h(f"{user}:{realm}:{password}"), h(f"POST:{path}")
        resp = h(f"{ha1}:{nonce}:{nc}:{cnonce}:auth:{ha2}")
        return (f'Digest username="{user}", realm="{realm}", nonce="{nonce}", uri="{path}", '
                f'algorithm=MD5, qop=auth, nc={nc}, cnonce="{cnonce}", response="{resp}"')
    return None

def rpc(url, method, params=None, timeout=30, auth=None):
    body = json.dumps({"jsonrpc": "2.0", "id": "0", "method": method, "params": params or {}}).encode()
    headers = {"Content-Type": "application/json"}
    parts = urllib.parse.urlsplit(url)
    path = parts.path or "/"
    # monero-wallet-rpc keeps its digest challenge per connection, so the answer
    # must go back on the same keep-alive connection that received the 401.
    conn = http.client.HTTPConnection(parts.hostname, parts.port or 80, timeout=timeout)
    try:
        for attempt in (1, 2):
            conn.request("POST", path, body, headers)
            resp = conn.getresponse()
            data = resp.read()
            if resp.status == 401 and auth:
                # A 401 means the request was refused before it ran, so answering
                # the challenge and sending it again cannot repeat a payment.
                if attempt == 2:
                    raise RpcError(f"{method}: wallet RPC login rejected")
                header = digest_header(resp.headers.get_all("WWW-Authenticate") or [], *auth, path)
                if not header:
                    raise RpcError(f"{method}: no supported auth challenge")
                headers["Authorization"] = header
                continue
            if resp.status != 200:
                raise RpcError(f"{method}: HTTP {resp.status}")
            reply = json.loads(data)
            break
    except (http.client.HTTPException, TimeoutError, ConnectionError, OSError, json.JSONDecodeError) as e:
        raise RpcUncertain(f"{method}: {e}") from e
    finally:
        conn.close()
    if reply.get("error"):
        raise RpcError(f"{method}: {reply['error']}")
    return reply["result"]


# ---------------------------------------------------------------- PoW maths
def meets(hash_hex, difficulty):
    """Monero's check_hash: the hash, read little-endian, times difficulty fits in 256 bits."""
    return int.from_bytes(bytes.fromhex(hash_hex), "little") * difficulty < 1 << 256

def target_hex(difficulty):
    if difficulty < 1 << 32:
        return ((0xFFFFFFFF // difficulty) & 0xFFFFFFFF).to_bytes(4, "little").hex()
    return ((0xFFFFFFFFFFFFFFFF // difficulty) & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "little").hex()

# --- address keys
# A wallet's two public keys are always in the curve's prime-order group. An address made up by
# hand can pass the checksum without that, and the pool wallet then refuses to pay it (its own
# check finds the coins would not arrive): such an address cannot mine here.
_P = 2 ** 255 - 19
_D = -121665 * pow(121666, _P - 2, _P) % _P
_L = 2 ** 252 + 27742317777372353535851937790883648493
_B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
_B58_BLOCK = {2: 1, 3: 2, 5: 3, 6: 4, 7: 5, 9: 6, 10: 7, 11: 8}

def _point(key):
    y = int.from_bytes(key, "little"); sign = y >> 255; y &= (1 << 255) - 1
    if y >= _P:
        return None
    x2 = (y * y - 1) * pow(_D * y * y + 1, _P - 2, _P) % _P
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P:
        x = x * pow(2, (_P - 1) // 4, _P) % _P
    if (x * x - x2) % _P:
        return None
    return (_P - x if x & 1 != sign else x, y)

def _add(a, b):
    t = _D * a[0] * b[0] * a[1] * b[1] % _P
    return ((a[0] * b[1] + b[0] * a[1]) * pow(1 + t, _P - 2, _P) % _P, (a[1] * b[1] + a[0] * b[0]) * pow(1 - t, _P - 2, _P) % _P)

def _prime_order(key):
    point, acc, k = _point(key), (0, 1), _L
    if point is None or point == (0, 1):
        return False
    while k:
        if k & 1:
            acc = _add(acc, point)
        point = _add(point, point); k >>= 1
    return acc == (0, 1)

def address_keys_usable(address):
    """True when both public keys in the address are ones a wallet could have made."""
    try:
        raw = b""
        for i in range(0, len(address), 11):
            block, n = address[i:i + 11], 0
            for c in block:
                n = n * 58 + _B58.index(c)
            raw += n.to_bytes(_B58_BLOCK[len(block)], "big")
    except (ValueError, KeyError, OverflowError):
        return False
    if len(raw) not in (69, 77):       # prefix, spend key, view key, [payment id], checksum
        return False
    return _prime_order(raw[1:33]) and _prime_order(raw[33:65])

def with_nonce(blob_hex, nonce_hex):
    o = NONCE_OFFSET * 2
    return blob_hex[:o] + nonce_hex + blob_hex[o + 8:]


# ---------------------------------------------------------------- storage
SCHEMA = """
CREATE TABLE IF NOT EXISTS shares (id INTEGER PRIMARY KEY, ts REAL, address TEXT, worker TEXT, diff INTEGER, height INTEGER);
CREATE INDEX IF NOT EXISTS ix_shares_addr_ts ON shares(address, ts);
CREATE TABLE IF NOT EXISTS blocks (height INTEGER PRIMARY KEY, hash TEXT, prev_hash TEXT, nonce INTEGER, ts REAL,
  finder TEXT, net_diff INTEGER, last_share INTEGER, reward INTEGER, status TEXT, credited_at REAL);
CREATE TABLE IF NOT EXISTS credits (height INTEGER, address TEXT, amount INTEGER, PRIMARY KEY (height, address));
CREATE TABLE IF NOT EXISTS balances (address TEXT PRIMARY KEY, pending INTEGER NOT NULL DEFAULT 0, paid INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS payments (id INTEGER PRIMARY KEY, ts REAL, status TEXT, txid TEXT, fee INTEGER, detail TEXT);
CREATE TABLE IF NOT EXISTS payment_items (payment INTEGER, address TEXT, amount INTEGER);
CREATE TABLE IF NOT EXISTS pool_fee (height INTEGER PRIMARY KEY, amount INTEGER);
CREATE INDEX IF NOT EXISTS payment_items_address ON payment_items(address);
CREATE TABLE IF NOT EXISTS liquidity_state (key TEXT PRIMARY KEY, value INTEGER);
CREATE TABLE IF NOT EXISTS liquidity_transfers (id INTEGER PRIMARY KEY, ts REAL, status TEXT, txid TEXT, amount INTEGER, fee INTEGER, detail TEXT);
"""

class Store:
    def __init__(self, path):
        self.db = sqlite3.connect(path, isolation_level=None)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.executescript(SCHEMA)

    def tx(self):
        return _Tx(self.db)

    def q(self, sql, args=()):
        return self.db.execute(sql, args).fetchall()

class _Tx:
    def __init__(self, db): self.db = db
    def __enter__(self): self.db.execute("BEGIN IMMEDIATE"); return self.db
    def __exit__(self, exc, *_):
        self.db.execute("ROLLBACK" if exc else "COMMIT")


# ---------------------------------------------------------------- pool
class Miner:
    def __init__(self, pool, writer, address, worker, diff, fixed):
        self.id = secrets.token_hex(8)
        self.pool, self.writer = pool, writer
        self.address, self.worker = address, worker
        self.diff, self.fixed = diff, fixed
        self.jobs = {}             # job_id -> job dict (newest last)
        self.share_times = []
        self.last_retarget = time.time()
        self.invalid = 0
        self.last_seen = time.time()


class Pool:
    def __init__(self, cfg, enable_payouts):
        self.cfg = cfg
        self.daemon = cfg["daemon_rpc"].rstrip("/") + "/json_rpc"
        self.wallet = cfg["wallet_rpc"].rstrip("/") + "/json_rpc"
        self.wallet_auth = tuple(cfg["wallet_rpc_login"].split(":", 1)) if cfg.get("wallet_rpc_login") else None
        self.store = Store(cfg["database"])
        self.miners = {}
        self.top = None            # (height, prev_hash) of the current template
        self.net = {}
        self.address_ok = {}
        self.payouts_enabled = enable_payouts and cfg["payouts"]["enabled"]
        self.halt_file = Path(cfg["database"]).with_suffix(".payouts-halted")
        self.held = {}             # address -> time until which its payout is skipped (the wallet refused it)
        self.started = time.time()

    # --- daemon / wallet
    async def d(self, method, params=None, timeout=30):
        return await asyncio.to_thread(rpc, self.daemon, method, params, timeout)

    async def w(self, method, params=None, timeout=60):
        return await asyncio.to_thread(rpc, self.wallet, method, params, timeout, self.wallet_auth)

    async def valid_address(self, address):
        if address in self.address_ok:
            return self.address_ok[address]
        try:
            r = await self.w("validate_address", {"address": address, "any_net_type": False})
        except (RpcError, RpcUncertain):
            return False
        ok = bool(r.get("valid")) and r.get("nettype") == self.cfg["nettype"] and address != self.cfg["pool_address"]
        ok = ok and await asyncio.to_thread(address_keys_usable, address)
        if len(self.address_ok) < 100000:
            self.address_ok[address] = ok
        return ok

    async def new_job(self, miner):
        extra = secrets.token_hex(8)
        t = await self.d("get_block_template", {"wallet_address": self.cfg["pool_address"], "extra_nonce": extra})
        job = {"id": secrets.token_hex(6), "height": t["height"], "prev": t["prev_hash"], "seed": t["seed_hash"],
               "hblob": t["blockhashing_blob"], "tblob": t["blocktemplate_blob"],
               "net_diff": int(t.get("wide_difficulty") or hex(t["difficulty"]), 16), "diff": miner.diff,
               "nonces": set(), "made": time.time()}
        miner.jobs[job["id"]] = job
        while len(miner.jobs) > MAX_JOBS_PER_MINER:
            miner.jobs.pop(next(iter(miner.jobs)))
        return {"blob": job["hblob"], "job_id": job["id"], "target": target_hex(miner.diff),
                "height": job["height"], "seed_hash": job["seed"], "algo": "rx/0"}

    async def send(self, miner, obj):
        try:
            miner.writer.write((json.dumps(obj) + "\n").encode())
            await miner.writer.drain()
        except (ConnectionError, RuntimeError):
            pass

    async def push_jobs(self, reason):
        for m in list(self.miners.values()):
            try:
                await self.send(m, {"jsonrpc": "2.0", "method": "job", "params": await self.new_job(m)})
            except (RpcError, RpcUncertain) as e:
                log.warning("job push failed (%s): %s", reason, e)

    # --- loops
    async def watch_chain(self):
        last_refresh = 0
        while True:
            try:
                info = await self.d("get_info", timeout=10)
                self.net = {"height": info["height"], "difficulty": info["difficulty"],
                            "top": info["top_block_hash"], "ts": time.time()}
                top = (info["height"], info["top_block_hash"])
                if top != self.top:
                    self.top = top
                    await self.push_jobs("new block")
                    last_refresh = time.time()
                elif time.time() - last_refresh > self.cfg["job_refresh_seconds"]:
                    await self.push_jobs("refresh")
                    last_refresh = time.time()
            except (RpcError, RpcUncertain) as e:
                log.warning("daemon unavailable: %s", e)
            await asyncio.sleep(1)

    async def unlocker(self):
        while True:
            try:
                await self.unlock_once()
            except (RpcError, RpcUncertain) as e:
                log.warning("unlocker: %s", e)
            await asyncio.sleep(self.cfg["unlock_interval_seconds"])

    async def unlock_once(self):
        for height, bhash, nonce, last_share, net_diff in self.store.q(
                "SELECT height, hash, nonce, last_share, net_diff FROM blocks WHERE status='pending' ORDER BY height"):
            h = (await self.d("get_block_header_by_height", {"height": height}))["block_header"]
            if h["hash"] != bhash:
                with self.store.tx() as db:
                    db.execute("UPDATE blocks SET status='orphaned' WHERE height=? AND status='pending'", (height,))
                log.warning("block %d orphaned (chain has %s)", height, h["hash"][:16])
                continue
            if h["depth"] < self.cfg["confirmations"]:
                continue
            self.credit(height, h["reward"], last_share, net_diff)

    def credit(self, height, reward, last_share, net_diff):
        window = max(1, int(net_diff * self.cfg["pplns_window_factor"]))
        totals, acc = {}, 0
        for addr, diff in self.store.q("SELECT address, diff FROM shares WHERE id<=? ORDER BY id DESC", (last_share,)):
            take = min(diff, window - acc)
            totals[addr] = totals.get(addr, 0) + take
            acc += take
            if acc >= window:
                break
        fee = reward * self.cfg["fee_basis_points"] // 10000
        pay = reward - fee
        shares_sum = sum(totals.values())
        with self.store.tx() as db:
            if db.execute("SELECT status FROM blocks WHERE height=?", (height,)).fetchone()[0] != "pending":
                return
            given = 0
            for addr, d in sorted(totals.items()):
                amount = pay * d // shares_sum if shares_sum else 0
                if amount <= 0:
                    continue
                given += amount
                db.execute("INSERT INTO credits VALUES (?,?,?)", (height, addr, amount))
                db.execute("INSERT INTO balances(address, pending) VALUES (?,?) ON CONFLICT(address) DO UPDATE SET pending=pending+excluded.pending", (addr, amount))
            db.execute("INSERT INTO pool_fee VALUES (?,?)", (height, reward - given))
            db.execute("UPDATE blocks SET status='confirmed', reward=?, credited_at=? WHERE height=?", (reward, time.time(), height))
        log.info("block %d confirmed: reward %.5f XMZ to %d miners, pool keeps %.5f", height, reward / ATOMIC, len(totals), (reward - given) / ATOMIC)

    async def payer(self):
        while True:
            await asyncio.sleep(self.cfg["payouts"]["interval_seconds"])
            try:
                await self.pay_round()
            except (RpcError, RpcUncertain) as e:
                log.warning("payouts: %s", e)
            try:
                await self.liquidity_once()   # after the miners, and only from what they are not owed
            except (RpcError, RpcUncertain) as e:
                log.warning("liquidity: %s", e)

    # --- the DEX liquidity share: a part of the pool's own fee, never the miners' coins
    def liquidity_owed(self):
        """Atoms of the pool fee earned since the share was switched on that are still to be sent."""
        lq = self.cfg["liquidity"]
        start = lq.get("from_height")
        if start is None:
            row = self.store.q("SELECT value FROM liquidity_state WHERE key='from_height'")
            if row:
                start = row[0][0]
            else:   # first run: only fees from blocks credited from now on, not the pool's history
                start = self.store.q("SELECT COALESCE(MAX(height),0) FROM pool_fee")[0][0]
                with self.store.tx() as db:
                    db.execute("INSERT OR IGNORE INTO liquidity_state VALUES ('from_height', ?)", (start,))
        earned = self.store.q("SELECT COALESCE(SUM(amount),0) FROM pool_fee WHERE height>?", (start,))[0][0]
        entitled = earned * int(lq["share_basis_points"]) // 10000
        used = self.store.q("SELECT COALESCE(SUM(amount),0) FROM liquidity_transfers WHERE status IN ('sending','sent','uncertain')")[0][0]
        return max(0, entitled - used)

    async def liquidity_once(self):
        """Send the liquidity share to the DEX liquidity wallet when the pool wallet can spare it:
        its unlocked balance must still cover every miner's whole pending balance (paid out or
        not yet due), the payout fee reserve and this transfer's own fee."""
        lq = self.cfg["liquidity"]
        if not lq["enabled"] or not self.payouts_enabled or self.halt_file.exists():
            return None
        if not await self.valid_address(lq["address"]):
            log.error("liquidity: the address in the config is not a usable %s address; nothing sent", self.cfg["nettype"])
            return None
        owed = self.liquidity_owed()
        if owed < int(lq["min_transfer_xmz"] * ATOMIC):
            return None
        if self.store.q("SELECT COUNT(*) FROM payments WHERE status IN ('sending','uncertain')")[0][0]:
            return None
        bal = await self.w("get_balance", {"account_index": 0})
        miners = self.store.q("SELECT COALESCE(SUM(pending),0) FROM balances")[0][0]
        room = bal["unlocked_balance"] - miners - int(self.cfg["payouts"]["fee_reserve_xmz"] * ATOMIC) - int(lq["tx_fee_margin_xmz"] * ATOMIC)
        amount = min(owed, room)
        if amount < int(lq["min_transfer_xmz"] * ATOMIC):
            log.info("liquidity: %.5f XMZ owed, waiting: unlocked %.5f XMZ must first cover the miners' %.5f XMZ",
                     owed / ATOMIC, bal["unlocked_balance"] / ATOMIC, miners / ATOMIC)
            return None
        with self.store.tx() as db:   # record intent first, as for miner payments
            tid = db.execute("INSERT INTO liquidity_transfers(ts, status, amount) VALUES (?, 'sending', ?)", (time.time(), amount)).lastrowid
        try:
            r = await self.w("transfer", {"destinations": [{"address": lq["address"], "amount": amount}], "priority": 0, "account_index": 0}, timeout=180)
        except RpcError as e:
            with self.store.tx() as db:
                db.execute("UPDATE liquidity_transfers SET status='failed', detail=? WHERE id=?", (str(e)[:500], tid))
            log.error("liquidity transfer %d refused by wallet: %s", tid, e)
            return None
        except RpcUncertain as e:
            with self.store.tx() as db:
                db.execute("UPDATE liquidity_transfers SET status='uncertain', detail=? WHERE id=?", (str(e)[:500], tid))
            self.halt_file.write_text(f"liquidity transfer {tid} outcome unknown: {e}\nCheck the wallet's outgoing transfers, then fix the liquidity_transfers table and delete this file.\n")
            log.error("liquidity transfer %d outcome unknown; payouts halted", tid)
            return None
        with self.store.tx() as db:
            db.execute("UPDATE liquidity_transfers SET status='sent', txid=?, fee=? WHERE id=?", (r["tx_hash"], r["fee"], tid))
        log.info("liquidity transfer %d sent: %s, %.5f XMZ to the DEX liquidity wallet", tid, r["tx_hash"], amount / ATOMIC)
        return r["tx_hash"]

    async def pay_round(self):
        """Pay everyone who is due: one transaction holds at most max_destinations miners (a
        transaction has 16 outputs: 15 payments and the change), so a round sends several."""
        sent = []
        for _ in range(max(1, int(self.cfg["payouts"]["max_batches"]))):
            held = len(self.held)
            txid = await self.pay_once()
            if txid:
                sent.append(txid)
            elif len(self.held) == held:   # nothing sent and nobody newly set aside: nothing more to do now
                break
        return sent

    async def pay_once(self):
        p = self.cfg["payouts"]
        # Whoever has waited longest goes first (never paid, then least recently paid). Largest
        # balance first starved small miners: the big ones refilled the batch every round.
        due = self.store.q(
            "SELECT b.address, b.pending FROM balances b LEFT JOIN (SELECT i.address AS address, MAX(p.ts) AS last"
            " FROM payment_items i JOIN payments p ON p.id=i.payment WHERE p.status!='failed' GROUP BY i.address) l"
            " ON l.address=b.address WHERE b.pending>=? ORDER BY COALESCE(l.last,0) ASC, b.pending DESC, b.address LIMIT ?",
            (int(p["threshold_xmz"] * ATOMIC), p["max_destinations"] + len(self.held)))
        now = time.time()
        self.held = {a: t for a, t in self.held.items() if t > now}
        due = [(a, amount) for a, amount in due if a not in self.held][:p["max_destinations"]]
        if not due:
            return None
        if not self.payouts_enabled:
            log.info("payouts disabled; would pay %d miners %.5f XMZ", len(due), sum(a for _, a in due) / ATOMIC)
            return None
        if self.halt_file.exists():
            log.error("payouts halted: reconcile %s and delete it", self.halt_file)
            return None
        bal = await self.w("get_balance", {"account_index": 0})
        # Pay as many as the unlocked balance covers, in order; the rest lead the next batch
        room, affordable = bal["unlocked_balance"] - int(p["fee_reserve_xmz"] * ATOMIC), []
        for addr, amount in due:
            if amount > room:
                break
            affordable.append((addr, amount)); room -= amount
        if not affordable:
            log.warning("payouts waiting: unlocked %.5f XMZ < next due %.5f XMZ", bal["unlocked_balance"] / ATOMIC, due[0][1] / ATOMIC)
            return None
        due = affordable
        txid, refusal = await self.send_payment(due)
        if txid or refusal is None or len(due) == 1:
            if refusal is not None and len(due) == 1 and "not enough" not in refusal.lower():
                self.hold(due[0][0], refusal)
            return txid
        # The wallet refused the whole batch. One address in it can be the cause, and it must not
        # stop the others: pay each alone, and set aside for a while whoever the wallet still refuses.
        last = None
        for one in due:
            txid, refusal = await self.send_payment([one])
            if txid:
                last = txid
            elif refusal is None or "not enough" in refusal.lower():   # halted, or out of unlocked coins: stop here
                break
            else:
                self.hold(one[0], refusal)
        return last

    def hold(self, address, why):
        self.held[address] = time.time() + self.cfg["payouts"]["hold_seconds"]
        log.error("payout to %s set aside for %d s (balance kept): %s", address, self.cfg["payouts"]["hold_seconds"], why)

    async def send_payment(self, due):
        """One transaction to these miners. Returns (txid, None) when sent, (None, reason) when the
        wallet refused it (balances restored), (None, None) when the outcome is unknown (payouts halt)."""
        total = sum(a for _, a in due)
        with self.store.tx() as db:   # record intent and deduct first
            pid = db.execute("INSERT INTO payments(ts, status) VALUES (?, 'sending')", (time.time(),)).lastrowid
            for addr, amount in due:
                db.execute("INSERT INTO payment_items VALUES (?,?,?)", (pid, addr, amount))
                db.execute("UPDATE balances SET pending=pending-? WHERE address=? AND pending>=?", (amount, addr, amount))
        dests = [{"address": a, "amount": amt} for a, amt in due]
        try:
            r = await self.w("transfer", {"destinations": dests, "priority": 0, "account_index": 0}, timeout=180)
        except RpcError as e:   # wallet refused: nothing was sent
            with self.store.tx() as db:
                for addr, amount in due:
                    db.execute("UPDATE balances SET pending=pending+? WHERE address=?", (amount, addr))
                db.execute("UPDATE payments SET status='failed', detail=? WHERE id=?", (str(e)[:500], pid))
            log.error("payment %d refused by wallet, balances restored: %s", pid, e)
            return None, str(e)
        except RpcUncertain as e:  # may or may not have been sent
            with self.store.tx() as db:
                db.execute("UPDATE payments SET status='uncertain', detail=? WHERE id=?", (str(e)[:500], pid))
            self.halt_file.write_text(f"payment {pid} outcome unknown: {e}\nCheck the wallet's outgoing transfers, then fix the payments table and delete this file.\n")
            log.error("payment %d outcome unknown; payouts halted", pid)
            return None, None
        with self.store.tx() as db:
            db.execute("UPDATE payments SET status='sent', txid=?, fee=? WHERE id=?", (r["tx_hash"], r["fee"], pid))
            for addr, amount in due:
                db.execute("UPDATE balances SET paid=paid+? WHERE address=?", (amount, addr))
        log.info("payment %d sent: %s, %d miners, %.5f XMZ, fee %.5f", pid, r["tx_hash"], len(due), total / ATOMIC, r["fee"] / ATOMIC)
        return r["tx_hash"], None

    async def janitor(self):
        while True:
            await asyncio.sleep(300)
            now = time.time()
            for m in list(self.miners.values()):
                if now - m.last_seen > self.cfg["miner_timeout_seconds"]:
                    m.writer.close()
            # Old shares go only while no block waiting for credit could still need them.
            keep = self.cfg["share_retention_days"] * 86400
            oldest_pending = self.store.q("SELECT MIN(ts) FROM blocks WHERE status='pending'")[0][0]
            if oldest_pending is None or oldest_pending > now - keep / 2:
                with self.store.tx() as db:
                    db.execute("DELETE FROM shares WHERE ts<?", (now - keep,))

    # --- stratum
    async def handle(self, reader, writer, port_cfg):
        peer = writer.get_extra_info("peername")
        miner = None
        try:
            while True:
                line = await asyncio.wait_for(reader.readline(), self.cfg["miner_timeout_seconds"])
                if not line:
                    break
                if len(line) > 10000:
                    break
                try:
                    msg = json.loads(line)
                    mid, method, params = msg.get("id"), msg.get("method"), msg.get("params") or {}
                except (ValueError, AttributeError):
                    break
                if method == "login":
                    miner = await self.login(writer, mid, params, port_cfg)
                    if miner is None:
                        break
                elif miner is None or params.get("id") != miner.id:
                    await self.reply(writer, mid, error="Unauthenticated")
                    break
                elif method == "submit":
                    miner.last_seen = time.time()
                    ok, err = await self.submit(miner, params)
                    await self.reply(writer, mid, result={"status": "OK"} if ok else None, error=err)
                    if miner.invalid > self.cfg["max_invalid_shares"]:
                        log.warning("dropping %s after %d invalid shares", peer, miner.invalid)
                        break
                elif method == "keepalived":
                    miner.last_seen = time.time()
                    await self.reply(writer, mid, result={"status": "KEEPALIVED"})
                elif method == "getjob":
                    await self.reply(writer, mid, result=await self.new_job(miner))
                else:
                    await self.reply(writer, mid, error="Unknown method")
        except (asyncio.TimeoutError, ConnectionError, asyncio.IncompleteReadError, RpcError, RpcUncertain) as e:
            log.debug("connection %s closed: %s", peer, e)
        finally:
            if miner:
                self.miners.pop(miner.id, None)
            writer.close()

    async def reply(self, writer, mid, result=None, error=None):
        obj = {"id": mid, "jsonrpc": "2.0", "error": {"code": -1, "message": error} if error else None, "result": result}
        writer.write((json.dumps(obj) + "\n").encode())
        await writer.drain()

    async def login(self, writer, mid, params, port_cfg):
        login = str(params.get("login", "")).strip()
        diff, fixed = port_cfg["difficulty"], False
        if "+" in login:
            login, fixed_part = login.split("+", 1)
            if fixed_part.isdigit():
                diff, fixed = max(self.cfg["min_difficulty"], min(int(fixed_part), self.cfg["max_difficulty"])), True
        address, _, worker = login.partition(".")
        worker = (worker or str(params.get("pass") or "x"))[:32]
        if not await self.valid_address(address):
            await self.reply(writer, mid, error="Invalid address: use your Monzero (XMZ) wallet address as the login")
            return None
        if len(self.miners) >= self.cfg["max_miners"]:
            await self.reply(writer, mid, error="Pool is full")
            return None
        miner = Miner(self, writer, address, worker, diff, fixed)
        self.miners[miner.id] = miner
        job = await self.new_job(miner)
        await self.reply(writer, mid, result={"id": miner.id, "job": job, "extensions": ["algo", "keepalive"], "status": "OK"})
        log.info("login %s… worker %s diff %d", address[:12], worker, diff)
        return miner

    async def submit(self, miner, params):
        job = miner.jobs.get(params.get("job_id"))
        nonce = str(params.get("nonce", "")).lower()
        if job is None:
            return False, "Stale or unknown job"
        if len(nonce) != 8 or any(c not in "0123456789abcdef" for c in nonce):
            miner.invalid += 1
            return False, "Malformed nonce"
        if nonce in job["nonces"]:
            miner.invalid += 1
            return False, "Duplicate share"
        job["nonces"].add(nonce)
        blob = with_nonce(job["hblob"], nonce)
        major = int(blob[:2], 16)
        pow_hash = await self.d("calc_pow", {"major_version": major, "height": job["height"], "block_blob": blob, "seed_hash": job["seed"]})
        if str(params.get("result", "")).lower() != pow_hash:
            miner.invalid += 1
            return False, "Wrong result hash"
        if not meets(pow_hash, job["diff"]):
            miner.invalid += 1
            return False, "Low difficulty share"
        with self.store.tx() as db:
            share_id = db.execute("INSERT INTO shares(ts, address, worker, diff, height) VALUES (?,?,?,?,?)",
                                  (time.time(), miner.address, miner.worker, job["diff"], job["height"])).lastrowid
        self.retarget(miner)
        if meets(pow_hash, job["net_diff"]):
            await self.found_block(miner, job, nonce, share_id)
        return True, None

    async def found_block(self, miner, job, nonce, share_id):
        block = with_nonce(job["tblob"], nonce)
        try:
            await self.d("submit_block", [block])
        except RpcError as e:
            log.error("block at %d rejected: %s", job["height"], e)
            return
        except RpcUncertain as e:
            log.error("block at %d submission uncertain: %s (the unlocker will check the chain)", job["height"], e)
        h = None
        for _ in range(10):
            try:
                h = (await self.d("get_block_header_by_height", {"height": job["height"]}))["block_header"]
                break
            except RpcError:
                await asyncio.sleep(1)
        if not h or h["prev_hash"] != job["prev"] or h["nonce"] != int.from_bytes(bytes.fromhex(nonce), "little"):
            log.warning("block at %d did not make the chain", job["height"])
            return
        with self.store.tx() as db:
            db.execute("INSERT OR IGNORE INTO blocks(height, hash, prev_hash, nonce, ts, finder, net_diff, last_share, reward, status) VALUES (?,?,?,?,?,?,?,?,?,'pending')",
                       (job["height"], h["hash"], job["prev"], h["nonce"], time.time(), miner.address, job["net_diff"], share_id, h["reward"]))
        log.info("BLOCK FOUND %d %s by %s…", job["height"], h["hash"], miner.address[:12])
        self.top = None   # force new jobs for everyone

    def retarget(self, miner):
        now = time.time()
        miner.share_times.append(now)
        miner.share_times = miner.share_times[-20:]
        if miner.fixed or now - miner.last_retarget < self.cfg["retarget_seconds"] or len(miner.share_times) < 3:
            return
        span = miner.share_times[-1] - miner.share_times[0]
        avg = span / (len(miner.share_times) - 1)
        target = self.cfg["target_share_seconds"]
        if 0.5 * target <= avg <= 1.5 * target:
            return
        factor = max(0.25, min(4.0, target / max(avg, 0.01)))
        miner.diff = int(max(self.cfg["min_difficulty"], min(self.cfg["max_difficulty"], miner.diff * factor)))
        miner.share_times, miner.last_retarget = [now], now

    # --- stats
    def hashrate(self, address=None, window=600):
        since = time.time() - window
        if address:
            s = self.store.q("SELECT COALESCE(SUM(diff),0) FROM shares WHERE ts>=? AND address=?", (since, address))[0][0]
        else:
            s = self.store.q("SELECT COALESCE(SUM(diff),0) FROM shares WHERE ts>=?", (since,))[0][0]
        return s / window

    def stats(self):
        c = self.cfg
        blocks = self.store.q("SELECT height, hash, ts, reward, status, finder FROM blocks ORDER BY height DESC LIMIT 50")
        last = self.store.q("SELECT MAX(ts) FROM blocks WHERE status!='orphaned'")[0][0]
        round_start = self.store.q("SELECT COALESCE(MAX(last_share),0) FROM blocks")[0][0]
        round_hashes = self.store.q("SELECT COALESCE(SUM(diff),0) FROM shares WHERE id>?", (round_start,))[0][0]
        payments = self.store.q("SELECT ts, txid, fee, (SELECT SUM(amount) FROM payment_items WHERE payment=payments.id), (SELECT COUNT(*) FROM payment_items WHERE payment=payments.id) FROM payments WHERE status='sent' ORDER BY id DESC LIMIT 50")
        return {
            "config": {"coin": "monzero", "symbol": "XMZ", "algo": "rx/0", "coinUnits": ATOMIC, "coinDecimalPlaces": 11,
                       "coinDifficultyTarget": 120, "poolHost": c["public_host"],
                       "ports": [{"port": p["port"], "difficulty": p["difficulty"], "desc": p.get("desc", "")} for p in c["stratum_ports"]],
                       "fee": c["fee_basis_points"] / 100, "paymentScheme": "PPLNS", "pplnsWindowFactor": c["pplns_window_factor"],
                       "minPaymentThreshold": int(c["payouts"]["threshold_xmz"] * ATOMIC), "depth": c["confirmations"],
                       "paymentsEnabled": self.payouts_enabled},
            "network": {"height": self.net.get("height"), "difficulty": self.net.get("difficulty"),
                        "hashrate": (self.net.get("difficulty") or 0) / 120},
            "pool": {"hashrate": self.hashrate(), "miners": len({m.address for m in self.miners.values()}),
                     "workers": len(self.miners), "roundHashes": round_hashes,
                     "lastBlockFound": int(last * 1000) if last else 0,
                     "totalBlocks": self.store.q("SELECT COUNT(*) FROM blocks WHERE status!='orphaned'")[0][0],
                     "blocks": [{"height": h, "hash": bh, "timestamp": int(ts), "reward": r, "status": st,
                                 "finder": f[:8] + "…" + f[-4:]} for h, bh, ts, r, st, f in blocks],
                     "payments": [{"timestamp": int(ts), "txid": tx, "fee": fee, "amount": amt, "miners": n} for ts, tx, fee, amt, n in payments]},
            "liquidity": self.liquidity_stats(),
            "uptime": int(time.time() - self.started),
        }

    def liquidity_stats(self):
        lq = self.cfg["liquidity"]
        if not lq["enabled"]:
            return {"enabled": False}
        sent = self.store.q("SELECT ts, txid, amount FROM liquidity_transfers WHERE status='sent' ORDER BY id DESC LIMIT 20")
        return {"enabled": True, "shareOfPoolFeePercent": lq["share_basis_points"] / 100, "address": lq["address"],
                "sent": self.store.q("SELECT COALESCE(SUM(amount),0) FROM liquidity_transfers WHERE status='sent'")[0][0],
                "transfers": [{"timestamp": int(ts), "txid": tx, "amount": a} for ts, tx, a in sent]}

    # Per-worker stats for a miner, from the shares table (so they include workers that are
    # currently offline but mined within the retention window). Hashrate over several windows,
    # total shares and last-seen, plus which workers are connected right now.
    WORKER_WINDOWS = (("m5", 300), ("h1", 3600), ("d1", 86400), ("w1", 604800))

    def worker_stats(self, address):
        online = {m.worker: m.diff for m in self.miners.values() if m.address == address}
        workers = {}
        for key, win in self.WORKER_WINDOWS:
            for name, s in self.store.q(
                    "SELECT worker, COALESCE(SUM(diff),0) FROM shares WHERE address=? AND ts>=? GROUP BY worker",
                    (address, time.time() - win)):
                workers.setdefault(name, {})[key] = (s or 0) / win
        for name, total, last in self.store.q(
                "SELECT worker, COALESCE(SUM(diff),0), COALESCE(MAX(ts),0) FROM shares WHERE address=? GROUP BY worker",
                (address,)):
            w = workers.setdefault(name, {})
            w["shares"], w["last"] = int(total or 0), int(last or 0)
        for name in online:
            workers.setdefault(name, {})
        out = [{"name": name, "online": name in online,
                "hashrate": {k: w.get(k, 0) for k, _ in self.WORKER_WINDOWS},
                "shares": w.get("shares", 0), "last": w.get("last", 0),
                "difficulty": online.get(name, 0)}
               for name, w in workers.items()]
        out.sort(key=lambda x: (not x["online"], -x["hashrate"]["m5"], -x["shares"]))
        return out

    def miner_stats(self, address):
        bal = self.store.q("SELECT pending, paid FROM balances WHERE address=?", (address,))
        items = self.store.q("SELECT p.ts, p.txid, i.amount FROM payment_items i JOIN payments p ON p.id=i.payment WHERE i.address=? AND p.status='sent' ORDER BY p.id DESC LIMIT 50", (address,))
        workers = self.worker_stats(address)
        total_shares = self.store.q("SELECT COALESCE(SUM(diff),0) FROM shares WHERE address=?", (address,))[0][0] or 0
        return {"address": address, "pending": bal[0][0] if bal else 0, "paid": bal[0][1] if bal else 0,
                "hashrate": self.hashrate(address),
                "hashrates": {k: self.hashrate(address, win) for k, win in self.WORKER_WINDOWS},
                "shares": int(total_shares), "workersOnline": sum(1 for w in workers if w["online"]), "workers": workers,
                "payments": [{"timestamp": int(ts), "txid": tx, "amount": a} for ts, tx, a in items]}

    async def http(self, reader, writer):
        try:
            head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 10)
            path = head.split(b" ", 2)[1].decode("latin-1")
            route, _, query = path.partition("?")
            args = dict(kv.partition("=")[::2] for kv in query.split("&") if kv)
            status, ctype = "200 OK", "application/json"
            if route == "/api/stats":
                body = json.dumps(self.stats())
            elif route == "/api/miner":
                addr = args.get("address", "")
                body = json.dumps(self.miner_stats(addr)) if 90 <= len(addr) <= 110 and addr.isalnum() else '{"error":"address required"}'
            elif route in ("/", "/index.html"):
                body, ctype = (Path(__file__).parent / "web" / "index.html").read_text(), "text/html; charset=utf-8"
            else:
                status, body = "404 Not Found", '{"error":"not found"}'
            data = body.encode()
            writer.write(f"HTTP/1.0 {status}\r\nContent-Type: {ctype}\r\nContent-Length: {len(data)}\r\n"
                         f"Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n\r\n".encode() + data)
            await writer.drain()
        except (asyncio.TimeoutError, asyncio.IncompleteReadError, asyncio.LimitOverrunError, ConnectionError, IndexError):
            pass
        finally:
            writer.close()

    async def run(self):
        servers = []
        for p in self.cfg["stratum_ports"]:
            servers.append(await asyncio.start_server(lambda r, w, p=p: self.handle(r, w, p), self.cfg["bind"], p["port"]))
            log.info("stratum on %s:%d (difficulty %d)", self.cfg["bind"], p["port"], p["difficulty"])
        servers.append(await asyncio.start_server(self.http, self.cfg["api_bind"], self.cfg["api_port"]))
        log.info("stats API on %s:%d; payouts %s", self.cfg["api_bind"], self.cfg["api_port"], "ENABLED" if self.payouts_enabled else "disabled")
        await asyncio.gather(self.watch_chain(), self.unlocker(), self.payer(), self.janitor())


DEFAULTS = {
    "nettype": "mainnet", "bind": "0.0.0.0", "api_bind": "127.0.0.1", "api_port": 8117, "public_host": "pool.monzero.org",
    "fee_basis_points": 100, "pplns_window_factor": 2.0, "confirmations": 60,
    "min_difficulty": 100, "max_difficulty": 10_000_000_000, "target_share_seconds": 15, "retarget_seconds": 60,
    "job_refresh_seconds": 30, "miner_timeout_seconds": 600, "max_invalid_shares": 50, "max_miners": 5000,
    "unlock_interval_seconds": 60, "share_retention_days": 7,
    "payouts": {"enabled": False, "threshold_xmz": 1.0, "interval_seconds": 3600, "max_destinations": 15, "fee_reserve_xmz": 0.1,
                "max_batches": 8, "hold_seconds": 21600},
    # A share of the pool's own fee for the DEX liquidity wallet (see liquidity_once)
    "liquidity": {"enabled": False, "address": "", "share_basis_points": 100, "min_transfer_xmz": 10.0,
                  "tx_fee_margin_xmz": 0.1, "from_height": None},
}

def load_config(path):
    cfg = json.loads(json.dumps(DEFAULTS))
    user = json.loads(Path(path).read_text())
    payouts = {**cfg["payouts"], **user.pop("payouts", {})}
    liquidity = {**cfg["liquidity"], **user.pop("liquidity", {})}
    cfg.update(user)
    cfg["payouts"] = payouts
    cfg["liquidity"] = liquidity
    if not 0 <= int(liquidity["share_basis_points"]) <= 10000:
        sys.exit("config: liquidity.share_basis_points must be 0..10000 (100 = 1% of the pool fee)")
    for key in ("daemon_rpc", "wallet_rpc", "pool_address", "database", "stratum_ports"):
        if key not in cfg:
            sys.exit(f"config: missing {key}")
    return cfg

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("config")
    ap.add_argument("--enable-payouts", action="store_true",
                    help="actually send payments (also needs payouts.enabled in the config)")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    asyncio.run(Pool(load_config(a.config), a.enable_payouts).run())

if __name__ == "__main__":
    main()
