"""BTC/XMZ offers through the open DEX relays (trade_kit.py offer | offers | take | serve | cancel).

The kit's two-party setup (cards, terms, two signatures) travels as sealed relay mail instead of
files handed over by hand, and the apps then negotiate through the relays too (`configure` with
LOCAL.json "peer": {"relays": [...]}), so neither trader needs an open port.

  offer   --dir DIR --relay URL --network NET --side sell-xmz|buy-xmz --btc SATS --xmz ATOMS
          --params PARAMS.json --policy POLICY.json [--hours H]
          Post a signed offer. PARAMS.json holds the maker's own terms: its two destinations, fees,
          confirmations, cancel window, networks and genesis hashes (no amounts, no other party).
  offers  --relay URL                               list the relays' checked BTC/XMZ offers
  take    --dir DIR --relay URL --offer ID --addresses ADDR.json [--wait S]
          Ask the maker for the trade (ADDR.json: our two destinations), check and sign the terms
          it answers with, and write DIR/trade/ (start-quote.json, peer-card.json, policy.json).
  serve   --dir DIR --relay URL [--wait S]        the maker: answer the first take, write DIR/trade/
  cancel  --dir DIR --relay URL                   withdraw the untaken offer

Mail format: as the app's (engine/src/mail.rs): a fresh X25519 sender key per letter, key
SHA-256("monzero-dex-mail/1" || shared || sender || recipient), ChaCha20-Poly1305, zero nonce.
Offers are signed with the identity key over "MONZERO-DEX-BTC-LISTING-V1\\n" + canonical JSON.
"""
import hashlib, json, secrets, time, urllib.error, urllib.request
from pathlib import Path
from nacl.bindings import (crypto_aead_chacha20poly1305_ietf_decrypt, crypto_aead_chacha20poly1305_ietf_encrypt,
                           crypto_scalarmult, crypto_scalarmult_base)
from nacl.exceptions import BadSignatureError, CryptoError
from nacl.signing import SigningKey, VerifyKey

LISTING_SCHEMA, CANCEL_SCHEMA, MAIL_SCHEMA = "monzero-dex-btc-listing/1", "monzero-dex-btc-cancel/1", "monzero-dex-mail/1"
LISTING_DOMAIN, CANCEL_DOMAIN = b"MONZERO-DEX-BTC-LISTING-V1\n", b"MONZERO-DEX-BTC-CANCEL-V1\n"
NETWORKS = ("btc-mainnet", "btc-signet", "btc-testnet4", "btc-regtest")
# Each role's own destinations: where its coin goes back on a refund, and where it is paid
OWN_ADDRESSES = {"ltc": ("ltcRefund", "xmzPayout"), "xmz": ("ltcPayout", "xmzRefund")}
VARIABLE = {"schema", "quoteId", "swapId", "ltcFunder", "xmzFunder", "appFeeXmzAtoms", "ltcAtoms", "xmzAtoms", "expiresAt"}
QUOTE_SECONDS = 3500  # the XMZ seller must lock before the quote expires (at most an hour)


def kit():
    import trade_kit
    return trade_kit


def fail(message):
    raise SystemExit("trade_kit: " + message)


# ---- relay HTTP ----

def relay_url(url):
    u = url.rstrip("/")
    if not (u.startswith("https://") or u.startswith("http://127.0.0.1:") or u.startswith("http://localhost:")):
        fail(f"{url}: relays are https, or http on this machine")
    return u


def request(url, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json", "User-Agent": "monzero-trade-kit"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read(64 * 1024 * 1024))
    except urllib.error.HTTPError as e:
        try:
            why = json.loads(e.read()).get("error")
        except ValueError:
            why = None
        raise RuntimeError(why or f"HTTP {e.code}")


def post_all(relays, path, body):
    results = []
    for r in relays:
        try:
            request(r + path, body)
            results.append((r, "ok"))
        except (RuntimeError, OSError) as e:
            results.append((r, str(e)))
    if not any(status == "ok" for _, status in results):
        fail("no relay accepted it: " + "; ".join(f"{r}: {s}" for r, s in results))
    return results


# ---- sealed mail (compatible with the app's engine/src/mail.rs) ----

def mailbox_public(secret):
    return crypto_scalarmult_base(secret)


def message_key(shared, sender, recipient):
    return hashlib.sha256(MAIL_SCHEMA.encode() + shared + sender + recipient).digest()


def seal(to, plain):
    secret = secrets.token_bytes(32)
    sender = crypto_scalarmult_base(secret)
    shared = crypto_scalarmult(secret, to)
    if shared == bytes(32):
        fail("unusable mailbox key")
    return sender + crypto_aead_chacha20poly1305_ietf_encrypt(plain, None, bytes(12), message_key(shared, sender, to))


def open_sealed(secret, sealed):
    if len(sealed) < 48:
        return None
    sender, own = sealed[:32], crypto_scalarmult_base(secret)
    try:
        shared = crypto_scalarmult(secret, sender)
        if shared == bytes(32):
            return None
        return crypto_aead_chacha20poly1305_ietf_decrypt(sealed[32:], None, bytes(12), message_key(shared, sender, own))
    except CryptoError:
        return None


def send_letter(relays, to_hex, letter):
    sealed = seal(bytes.fromhex(to_hex), kit().canonical(letter))
    post_all(relays, "/v1/mail", {"to": "0x" + to_hex, "sealed": "0x" + sealed.hex()})


def read_letters(relays, secret):
    own = mailbox_public(secret).hex()
    seen, letters = set(), []
    for r in relays:
        try:
            mail = request(f"{r}/v1/mail?to=0x{own}")["mail"]
        except (RuntimeError, OSError, KeyError):
            continue
        for item in mail:
            raw = bytes.fromhex(item["sealed"].removeprefix("0x"))
            if raw in seen:
                continue
            seen.add(raw)
            plain = open_sealed(secret, raw)
            if plain is not None:
                try:
                    letters.append(json.loads(plain))
                except ValueError:
                    pass
    return letters


# ---- identity ----

class Identity:
    def __init__(self, directory):
        k = kit()
        self.dir = Path(directory)
        self.card = k.read_json(self.dir / "card.json", "identity card")
        self.role = self.card.get("role")
        if self.role not in k.ROLE_NAMES or not isinstance(self.card.get("mailbox"), str):
            fail(f"{self.dir} is not a trade identity with a mailbox key; make one with: trade_kit.py identity")
        self.mailbox = bytes.fromhex((self.dir / "mailbox.key").read_text().strip())
        if mailbox_public(self.mailbox).hex() != self.card["mailbox"]:
            fail(f"{self.dir / 'mailbox.key'} does not match the card")
        self._key = None

    def key(self):
        if self._key is None:
            k = kit()
            self._key = SigningKey(k.unseal((self.dir / "identity.bin").read_bytes(), k.passphrase(confirm=False)))
            if self._key.verify_key.encode().hex() != self.card["funderKey"]:
                fail("the identity key does not match the card")
        return self._key


def signed_listing(identity, fields):
    payload = kit().canonical(fields)
    return {"listing": fields, "signature": identity.key().sign(LISTING_DOMAIN + payload).signature.hex()}


def check_listing(signed, now=None):
    """The offer's fields if its maker signed it; nothing else is trusted from a relay."""
    k = kit()
    try:
        l, sig = signed["listing"], bytes.fromhex(signed["signature"])
        if set(l) != {"schema", "id", "pair", "network", "side", "btc", "xmz", "makerKey", "mailbox", "createdAt", "expiresAt"}:
            return None
        if l["schema"] != LISTING_SCHEMA or l["pair"] != "BTC/XMZ" or l["side"] not in ("sell-xmz", "buy-xmz") or l["network"] not in NETWORKS:
            return None
        VerifyKey(bytes.fromhex(l["makerKey"])).verify(LISTING_DOMAIN + k.canonical(l), sig)
        if now is not None and l["expiresAt"] <= now:
            return None
        return l
    except (KeyError, TypeError, ValueError, BadSignatureError):
        return None


def trade_dir(identity):
    d = identity.dir / "trade"
    if (d / "start-quote.json").exists():
        fail(f"{d} already holds an agreed trade")
    d.mkdir(mode=0o700, exist_ok=True)
    return d


def finish(identity, d, terms_payload, signatures, peer_card, policy):
    k = kit()
    envelope = k.canonical({"schema": k.ENVELOPE, "payload": terms_payload.decode(),
        "ltcSignature": signatures["ltc"], "xmzSignature": signatures["xmz"]})
    for name, data in (("peer-card.json", k.canonical(peer_card)), ("policy.json", k.canonical(policy)), ("start-quote.json", envelope)):
        path = d / name
        if path.exists():
            path.unlink()
        k.write_new(path, data)
    k.load_verified_quote(d / "start-quote.json")  # both signatures, checked once more
    print(f"Trade agreed. Next, write this side's app folder (LOCAL.json \"peer\": {{\"relays\": [...]}}):\n"
          f"  trade_kit.py configure --dir {identity.dir} --quote {d / 'start-quote.json'} --peer-card {d / 'peer-card.json'} "
          f"--policy {d / 'policy.json'} --local LOCAL.json --out APPDIR")


# ---- commands ----

def cmd_offer(a):
    k = kit()
    me = Identity(a.dir)
    relays = [relay_url(r) for r in a.relay]
    if (me.dir / "offer.json").exists():
        fail(f"{me.dir} already has an offer; one offer per identity")
    side_role = "xmz" if a.side == "sell-xmz" else "ltc"
    if side_role != me.role:
        fail(f"this identity is the {me.role} side: it can only offer to " + ("buy XMZ (buy-xmz)" if me.role == "ltc" else "sell XMZ (sell-xmz)"))
    params, policy = k.read_json(a.params, "parameters"), k.read_json(a.policy, "policy")
    missing = [f for f in k.FIELDS if f not in VARIABLE and f not in params and f not in OWN_ADDRESSES["xmz" if me.role == "ltc" else "ltc"]]
    if missing:
        fail("parameters lack " + ", ".join(missing))
    if params.get("ltcNetwork") != a.network:
        fail("the parameters are for another network")
    if a.btc <= 0 or a.xmz <= 0 or not 1 <= a.hours <= 24:
        fail("amounts must be positive and the offer open 1-24 hours")
    now = int(time.time())
    fields = {"schema": LISTING_SCHEMA, "id": "0x" + secrets.token_hex(32), "pair": "BTC/XMZ", "network": a.network,
        "side": a.side, "btc": str(a.btc), "xmz": str(a.xmz), "makerKey": me.card["funderKey"], "mailbox": me.card["mailbox"],
        "createdAt": now, "expiresAt": now + a.hours * 3600}
    signed = signed_listing(me, fields)
    k.write_new(me.dir / "offer.json", k.canonical({"signed": signed, "params": params, "policy": policy, "relays": relays}))
    for r, status in post_all(relays, "/v1/btc/listings", signed):
        print(f"{r}: {status}")
    print(f"Offer {fields['id']} posted. Keep `trade_kit.py serve --dir {me.dir} --relay ...` running to answer takes.")


def cmd_offers(a):
    now, seen = int(time.time()), set()
    for r in (relay_url(x) for x in a.relay):
        try:
            listings = request(r + "/v1/btc/listings")["listings"]
        except (RuntimeError, OSError, KeyError) as e:
            print(f"{r}: {e}")
            continue
        for signed in listings:
            l = check_listing(signed, now)
            if l is None or l["id"] in seen:
                continue
            seen.add(l["id"])
            verb = "sells" if l["side"] == "sell-xmz" else "buys"
            print(f"{l['id']}  {l['network']}  {verb} {int(l['xmz']) / 1e11:g} XMZ for {int(l['btc']) / 1e8:.8f} BTC"
                  f"  ({(l['expiresAt'] - now) // 60} min left)")


def build_terms(maker_role, listing, params, maker_card, taker_card, taker_addresses):
    k = kit()
    terms = {"schema": k.SCHEMA, "quoteId": str(uuid4()), "swapId": str(uuid4()), "appFeeXmzAtoms": "0",
        "ltcAtoms": listing["btc"], "xmzAtoms": listing["xmz"], "expiresAt": str(int(time.time()) + QUOTE_SECONDS)}
    cards = {maker_role: maker_card, ("xmz" if maker_role == "ltc" else "ltc"): taker_card}
    terms.update(ltcFunder=cards["ltc"]["funderKey"], xmzFunder=cards["xmz"]["funderKey"])
    for f in k.FIELDS:
        if f not in terms:
            value = taker_addresses.get(f) if f in taker_addresses else params.get(f)
            if value is None:
                fail(f"no value for {f}")
            terms[f] = str(value)
    return k.canonical(terms), terms


def uuid4():
    import uuid
    return uuid.uuid4()


def cmd_serve(a):
    k = kit()
    me = Identity(a.dir)
    state = k.read_json(me.dir / "offer.json", "offer")
    relays = [relay_url(r) for r in a.relay] or state["relays"]
    listing = check_listing(state["signed"])
    if listing is None or listing["makerKey"] != me.card["funderKey"]:
        fail("offer.json is not our signed offer")
    d = trade_dir(me)
    taker_role = "xmz" if me.role == "ltc" else "ltc"
    sent = d / "sent-terms.json"
    deadline = time.time() + a.wait
    while True:
        for letter in read_letters(relays, me.mailbox):
            if letter.get("listing") != listing["id"]:
                continue
            if letter.get("kind") == "btc-take" and sent.exists():
                # The taker asked again (our answer was lost): the same signed terms, to the same taker only
                record = k.read_json(sent, "sent terms")
                card = letter.get("card", {})
                if card.get("funderKey") == record["takerCard"]["funderKey"] and letter.get("replyTo") == record["takerCard"]["mailbox"]:
                    send_letter(relays, record["takerCard"]["mailbox"], {"kind": "btc-terms", "listing": listing["id"], "terms": record["terms"],
                        "signature": record["signature"], "makerCard": me.card, "policy": state["policy"]})
            elif letter.get("kind") == "btc-take":
                card, addresses, reply = letter.get("card", {}), letter.get("addresses", {}), letter.get("replyTo", "")
                if (card.get("role") != taker_role or card.get("schema") != "monzero-ltc-trade-card-v1" or card.get("funderKey") == me.card["funderKey"]
                        or set(addresses) != set(OWN_ADDRESSES[taker_role]) or len(reply) != 64 or card.get("mailbox") != reply):
                    continue
                payload, terms = build_terms(me.role, listing, state["params"], me.card, card, addresses)
                signature = me.key().sign(k.signing_bytes(payload, me.role)).signature.hex()
                k.write_new(sent, k.canonical({"terms": payload.decode(), "signature": signature, "takerCard": card}))
                send_letter(relays, reply, {"kind": "btc-terms", "listing": listing["id"], "terms": payload.decode(),
                    "signature": signature, "makerCard": me.card, "policy": state["policy"]})
                print(f"Answered a take from {card['funderKey'][:16]}… with signed terms; waiting for its signature.")
            elif letter.get("kind") == "btc-signature" and sent.exists():
                record = k.read_json(sent, "sent terms")
                payload = record["terms"].encode()
                try:
                    VerifyKey(bytes.fromhex(record["takerCard"]["funderKey"])).verify(k.signing_bytes(payload, taker_role),
                        bytes.fromhex(letter.get("signature", "")))
                except (ValueError, BadSignatureError):
                    continue
                finish(me, d, payload, {me.role: record["signature"], taker_role: letter["signature"]}, record["takerCard"], state["policy"])
                return
        if time.time() > deadline:
            fail("no completed take yet; run serve again")
        time.sleep(3)


def cmd_take(a):
    k = kit()
    me = Identity(a.dir)
    relays = [relay_url(r) for r in a.relay]
    listing = None
    for r in relays:
        try:
            listing = check_listing(request(f"{r}/v1/btc/listings/{a.offer}"), int(time.time()))
        except (RuntimeError, OSError):
            continue
        if listing:
            break
    if listing is None:
        fail("the offer is not on these relays, or has expired")
    maker_role = "xmz" if listing["side"] == "sell-xmz" else "ltc"
    if maker_role == me.role:
        fail(f"this offer needs the other side; this identity is the {me.role} side")
    addresses = k.read_json(a.addresses, "addresses")
    if set(addresses) != set(OWN_ADDRESSES[me.role]) or not all(isinstance(v, str) and v for v in addresses.values()):
        fail("the addresses file holds exactly: " + ", ".join(OWN_ADDRESSES[me.role]))
    d = trade_dir(me)
    asked = d / "take.json"
    if not asked.exists():
        k.write_new(asked, k.canonical({"listing": listing, "addresses": addresses}))
    send_letter(relays, listing["mailbox"], {"kind": "btc-take", "listing": listing["id"], "card": me.card,
        "addresses": addresses, "replyTo": me.card["mailbox"]})
    print(f"Asked the maker of {listing['id']}; waiting for its terms.")
    deadline = time.time() + a.wait
    while True:
        for letter in read_letters(relays, me.mailbox):
            if letter.get("kind") != "btc-terms" or letter.get("listing") != listing["id"]:
                continue
            maker_card = letter.get("makerCard", {})
            if maker_card.get("funderKey") != listing["makerKey"] or maker_card.get("mailbox") != listing["mailbox"]:
                continue
            payload = letter.get("terms", "").encode()
            _, terms = k.validate_terms(payload)
            # The terms must be exactly the offer, both keys and our own destinations
            expected = {"ltcAtoms": listing["btc"], "xmzAtoms": listing["xmz"], "ltcNetwork": listing["network"],
                "ltcFunder" if me.role == "ltc" else "xmzFunder": me.card["funderKey"],
                "ltcFunder" if maker_role == "ltc" else "xmzFunder": listing["makerKey"], **addresses}
            wrong = [f for f, v in expected.items() if terms.get(f) != v]
            policy = letter.get("policy")
            if wrong or not isinstance(policy, dict) or policy.get("ltc_network") != terms["ltcNetwork"]:
                fail("the maker's terms differ from its offer: " + ", ".join(wrong or ["policy"]))
            try:
                VerifyKey(bytes.fromhex(listing["makerKey"])).verify(k.signing_bytes(payload, maker_role), bytes.fromhex(letter["signature"]))
            except (ValueError, BadSignatureError):
                fail("the maker's signature does not verify")
            print(f"Terms: {int(terms['ltcAtoms']) / 1e8:.8f} BTC for {int(terms['xmzAtoms']) / 1e11:g} XMZ, cancel after "
                  f"{terms['cancelBlocks']} blocks, quote expires {terms['expiresAt']}. Signing.")
            signature = me.key().sign(k.signing_bytes(payload, me.role)).signature.hex()
            send_letter(relays, listing["mailbox"], {"kind": "btc-signature", "listing": listing["id"], "signature": signature})
            finish(me, d, payload, {me.role: signature, maker_role: letter["signature"]}, maker_card, policy)
            return
        if time.time() > deadline:
            fail("no answer from the maker yet; run take again")
        time.sleep(3)


def cmd_cancel(a):
    k = kit()
    me = Identity(a.dir)
    state = k.read_json(me.dir / "offer.json", "offer")
    relays = [relay_url(r) for r in a.relay] or state["relays"]
    fields = {"schema": CANCEL_SCHEMA, "id": state["signed"]["listing"]["id"], "makerKey": me.card["funderKey"], "at": int(time.time())}
    signed = {"cancel": fields, "signature": me.key().sign(CANCEL_DOMAIN + k.canonical(fields)).signature.hex()}
    for r, status in post_all(relays, "/v1/btc/cancel", signed):
        print(f"{r}: {status}")


def add_parsers(sub):
    p = sub.add_parser("offer")
    for flag in ("--dir", "--network", "--side", "--params", "--policy"):
        p.add_argument(flag, required=True)
    p.add_argument("--relay", action="append", required=True)
    p.add_argument("--btc", type=int, required=True)
    p.add_argument("--xmz", type=int, required=True)
    p.add_argument("--hours", type=int, default=6)
    p = sub.add_parser("offers")
    p.add_argument("--relay", action="append", required=True)
    p = sub.add_parser("take")
    for flag in ("--dir", "--offer", "--addresses"):
        p.add_argument(flag, required=True)
    p.add_argument("--relay", action="append", required=True)
    p.add_argument("--wait", type=int, default=600)
    p = sub.add_parser("serve")
    p.add_argument("--dir", required=True)
    p.add_argument("--relay", action="append", default=[])
    p.add_argument("--wait", type=int, default=3600)
    p = sub.add_parser("cancel")
    p.add_argument("--dir", required=True)
    p.add_argument("--relay", action="append", default=[])
    return {"offer": cmd_offer, "offers": cmd_offers, "take": cmd_take, "serve": cmd_serve, "cancel": cmd_cancel}
