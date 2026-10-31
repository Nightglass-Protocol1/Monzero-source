#!/usr/bin/env python3
"""Monzero LTC/XMZ trade kit: set up one side of a two-party trade.

Each party runs this on their own machine. Only public files cross between
them: the identity card, the proposed terms and the two signatures.

  trade_kit.py identity --role ltc|xmz --dir DIR
      Create this side's identity key (encrypted in the app's identity format)
      and TLS certificate. Writes DIR/card.json, the public part to share.
  trade_kit.py terms --ltc-card CARD --xmz-card CARD --params PARAMS.json --out TERMS.json
      Build the quote terms from both cards and the agreed trade parameters.
  trade_kit.py sign --dir DIR --terms TERMS.json --out SIG.json
      Sign the terms with this side's identity (asks for its passphrase).
  trade_kit.py assemble --terms TERMS.json --ltc-sig SIG --xmz-sig SIG --out start-quote.json
      Combine both signatures into the signed quote and check them.
  trade_kit.py configure --dir DIR --quote start-quote.json --peer-card CARD --policy POLICY.json
                         --local LOCAL.json --out APPDIR
      Write this side's app configuration (for monzero-wallet-gui --dex-ltc-app
      APPDIR/app.json) from the signed quote, the other side's public card, the
      agreed policy and this side's own node, wallet and peer settings. APPDIR
      appears complete or not at all; an existing APPDIR is never replaced.

The quote format matches swap-machine's litecoin_quote (envelope v1, terms
schema monzero-native-ltc-xmz-quote-v2): compact sorted JSON, one Ed25519
signature per role over "MONZERO-DEX-OFFER-V1\\n" + length-prefixed domain
and payload. Nothing here creates a trade, reserves coins or moves funds.
"""
import argparse, ctypes, errno, getpass, hashlib, json, os, platform, secrets, stat, subprocess, sys, uuid
from pathlib import Path
from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_decrypt, crypto_aead_xchacha20poly1305_ietf_encrypt
from nacl.pwhash import argon2id
from nacl.signing import SigningKey, VerifyKey
from nacl.exceptions import BadSignatureError

SCHEMA = "monzero-native-ltc-xmz-quote-v2"
ENVELOPE = "monzero-ltc-signed-quote-envelope-v1"
HEADER = b"MONZERO-DEX-IDENTITY-ARGON2ID13-3-64M-XCHACHA20-V1\n"
FIELDS = ["schema", "quoteId", "swapId", "ltcFunder", "xmzFunder", "ltcGenesis", "xmzGenesis",
    "ltcNetwork", "xmzNetwork", "ltcAtoms", "xmzAtoms", "ltcPayout", "ltcRefund", "xmzPayout", "xmzRefund",
    "ltcFundingFeeMaxAtoms", "xmzNativeFeeMaxAtoms", "ltcCancelFeeAtoms", "ltcRefundFeeAtoms",
    "ltcRedeemFeeAtoms", "ltcPunishFeeAtoms", "ltcEarlyRefundFeeAtoms", "ltcConfirmations", "xmzConfirmations",
    "cancelBlocks", "punishBlocks", "expiresAt", "appFeeXmzAtoms", "ltcMinimumOutputAtoms",
    "ltcMinimumRelayFeeAtomsPerKb", "xmzPayoutAtoms", "xmzClaimFeeMaxAtoms", "xmzRefundFeeMaxAtoms"]
ROLE_NAMES = {"ltc": "litecoin-funder", "xmz": "native-funder"}
LOCAL_KEYS = {"local settings": ("journalHelper", "litecoin", "xmz", "peer"),
    "litecoin": ("rpcPort", "wallet", "anchor", "cookieFile"),
    "xmz": ("daemonPort", "claimWalletPort", "claimLoginFile", "walletPasswordFile"),
    "peer": ("address", "port")}
LOCAL_ROLE_KEYS = {"xmz": ("sourceWalletPort", "sourceLoginFile", "sourceAddress"),
    "ltc": ("observerWalletPort", "observerLoginFile")}
IDENTITY_FILES = ("tls.crt", "tls.key", "identity.bin", "identity.passphrase")  # referenced in place, never copied
RENAME_NOREPLACE = 1  # <linux/fs.h>
SYS_RENAMEAT2 = {"x86_64": 316, "aarch64": 276}  # used only if libc lacks renameat2()


def fail(message):
    raise SystemExit("trade_kit: " + message)


def write_new(path, data, mode=0o600):
    """Create a file that must not exist yet, readable by the owner only."""
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode), "wb") as out:
        out.write(data); out.flush(); os.fsync(out.fileno())


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()


def signing_bytes(payload, role):
    domain = SCHEMA + "-" + ("ltc-funder" if role == "ltc" else "xmz-funder")
    return b"MONZERO-DEX-OFFER-V1\n" + f"{len(domain)}:{domain}{len(payload)}:".encode() + payload


def passphrase(confirm):
    value = os.environ.get("MONZERO_KIT_PASSPHRASE")  # non-interactive tests only
    if value is None:
        value = getpass.getpass("Identity passphrase: ")
        if confirm and getpass.getpass("Repeat passphrase: ") != value:
            fail("passphrases differ")
    if len(value) < 12:
        fail("use a passphrase of at least 12 characters")
    return value.encode()


def seal(seed, password):
    salt, nonce = secrets.token_bytes(16), secrets.token_bytes(24)
    header = HEADER + salt + nonce
    key = argon2id.kdf(32, password, salt, opslimit=3, memlimit=64 * 1024 * 1024)
    return header + crypto_aead_xchacha20poly1305_ietf_encrypt(seed, header, nonce, key)


def unseal(blob, password):
    if not blob.startswith(HEADER) or len(blob) != len(HEADER) + 16 + 24 + 32 + 16:
        fail("not a Monzero DEX identity file")
    salt, nonce = blob[len(HEADER):len(HEADER) + 16], blob[len(HEADER) + 16:len(HEADER) + 40]
    key = argon2id.kdf(32, password, salt, opslimit=3, memlimit=64 * 1024 * 1024)
    try:
        return crypto_aead_xchacha20poly1305_ietf_decrypt(blob[len(HEADER) + 40:], blob[:len(HEADER) + 40], nonce, key)
    except Exception:
        fail("wrong passphrase or damaged identity file")


def cmd_identity(a):
    d = Path(a.dir)
    d.mkdir(mode=0o700, parents=True, exist_ok=False)
    seed = secrets.token_bytes(32)
    password = passphrase(confirm=True)
    write_new(d / "identity.bin", seal(seed, password))
    write_new(d / "identity.passphrase", password)  # the app reads it from a file; keep this folder private
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(d / "tls.key"),
        "-out", str(d / "tls.crt"), "-days", "30", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
        check=True, capture_output=True)
    os.chmod(d / "tls.key", 0o600); os.chmod(d / "tls.crt", 0o600)
    der = subprocess.run(["openssl", "x509", "-in", str(d / "tls.crt"), "-outform", "DER"], check=True, capture_output=True).stdout
    card = {"schema": "monzero-ltc-trade-card-v1", "role": a.role, "funderKey": SigningKey(seed).verify_key.encode().hex(),
            "tlsCertificateSha256": hashlib.sha256(der).hexdigest(),
            "tlsCertificatePem": (d / "tls.crt").read_text()}
    write_new(d / "card.json", canonical(card), 0o644)
    print(f"Created {d}. Share only {d / 'card.json'}.")
    print("Funder key:", card["funderKey"])


def load_card(path, role):
    card = json.loads(Path(path).read_text())
    if card.get("schema") != "monzero-ltc-trade-card-v1" or card.get("role") != role:
        fail(f"{path} is not a {role} trade card")
    VerifyKey(bytes.fromhex(card["funderKey"]))
    return card


def cmd_terms(a):
    ltc, xmz = load_card(a.ltc_card, "ltc"), load_card(a.xmz_card, "xmz")
    if ltc["funderKey"] == xmz["funderKey"]:
        fail("both cards use the same key")
    params = json.loads(Path(a.params).read_text())
    terms = {"schema": SCHEMA, "quoteId": str(uuid.uuid4()), "swapId": str(uuid.uuid4()),
             "ltcFunder": ltc["funderKey"], "xmzFunder": xmz["funderKey"], "appFeeXmzAtoms": "0"}
    for key in FIELDS:
        if key not in terms:
            if key not in params:
                fail(f"parameters file lacks {key}")
            terms[key] = str(params[key])
    unknown = set(params) - set(FIELDS)
    if unknown:
        fail("unknown parameters: " + ", ".join(sorted(unknown)))
    write_new(Path(a.out), canonical(terms), 0o644)
    print(f"Wrote {a.out}. Both parties review it, then each runs: trade_kit.py sign")


def load_terms(path):
    payload = Path(path).read_bytes().strip()
    return validate_terms(payload)


def validate_terms(payload):
    terms = json.loads(payload)
    if not isinstance(terms, dict) or canonical(terms) != payload or sorted(terms) != sorted(FIELDS) or terms.get("schema") != SCHEMA:
        fail(f"not canonical {SCHEMA} terms")
    if any(not isinstance(v, str) for v in terms.values()):
        fail("every term must be a string")
    return payload, terms


def load_verified_quote(path):
    """Authenticate exchanged terms before creating any local app files."""
    try:
        envelope = json.loads(Path(path).read_text())
        if not isinstance(envelope, dict) or set(envelope) != {"schema", "payload", "ltcSignature", "xmzSignature"}:
            fail("invalid signed quote envelope fields")
        if envelope["schema"] != ENVELOPE or not isinstance(envelope["payload"], str):
            fail("invalid signed quote envelope schema or payload")
        payload, terms = validate_terms(envelope["payload"].encode())
        if terms["ltcFunder"] == terms["xmzFunder"]:
            fail("both quote parties use the same key")
        for role in ("ltc", "xmz"):
            key = bytes.fromhex(terms[role + "Funder"])
            signature = bytes.fromhex(envelope[role + "Signature"])
            VerifyKey(key).verify(signing_bytes(payload, role), signature)
        return terms
    except (ValueError, TypeError, KeyError, BadSignatureError):
        fail("invalid signed quote: malformed terms, key or signature")


def cmd_sign(a):
    payload, terms = load_terms(a.terms)
    d = Path(a.dir)
    card = json.loads((d / "card.json").read_text())
    role = card["role"]
    key = SigningKey(unseal((d / "identity.bin").read_bytes(), passphrase(confirm=False)))
    expected = terms["ltcFunder" if role == "ltc" else "xmzFunder"]
    if key.verify_key.encode().hex() != expected:
        fail("these terms name a different key for your role")
    print(f"Signing as the {ROLE_NAMES[role]}: {terms['ltcAtoms']} LTC atoms for {terms['xmzAtoms']} XMZ atoms, "
          f"swap {terms['swapId']}, expires {terms['expiresAt']}")
    signature = key.sign(signing_bytes(payload, role)).signature.hex()
    write_new(Path(a.out), canonical({"schema": "monzero-ltc-trade-signature-v1", "role": role,
        "termsSha256": hashlib.sha256(payload).hexdigest(), "signature": signature}), 0o644)
    print(f"Wrote {a.out}; send it to the other party.")


def cmd_assemble(a):
    payload, terms = load_terms(a.terms)
    signatures = {}
    for role, path in (("ltc", a.ltc_sig), ("xmz", a.xmz_sig)):
        sig = json.loads(Path(path).read_text())
        if sig.get("role") != role or sig.get("termsSha256") != hashlib.sha256(payload).hexdigest():
            fail(f"{path} is not the {role} signature over these terms")
        pin = terms["ltcFunder" if role == "ltc" else "xmzFunder"]
        VerifyKey(bytes.fromhex(pin)).verify(signing_bytes(payload, role), bytes.fromhex(sig["signature"]))
        signatures[role] = sig["signature"]
    envelope = canonical({"schema": ENVELOPE, "payload": payload.decode(),
        "ltcSignature": signatures["ltc"], "xmzSignature": signatures["xmz"]})
    write_new(Path(a.out), envelope)
    print(f"Wrote {a.out}: both signatures verified.")


# --- configure: build everything in memory, then publish one directory atomically -----------------

def read_json(path, what):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError) as error:
        fail(f"cannot read the {what} {path}: {error}")


def light_seller(local, role):
    """The XMZ seller may reach Bitcoin/Litecoin through Electrum servers instead of a node."""
    ltc = local.get("litecoin") if isinstance(local, dict) else None
    return role == "xmz" and isinstance(ltc, dict) and "light" in ltc


def check_light(light):
    """Mirrors the journal helper's rule: at least two distinct servers and a bounded tip age."""
    if not isinstance(light, dict) or set(light) != {"servers", "maximumTipAge"}:
        fail("LOCAL.json litecoin.light must have exactly servers and maximumTipAge")
    servers = light["servers"]
    if not isinstance(servers, list) or not 2 <= len(servers) <= 8:
        fail("LOCAL.json litecoin.light.servers must list 2 to 8 independent Electrum servers")
    for server in servers:
        if (not isinstance(server, dict) or set(server) != {"host", "port"} or not isinstance(server["host"], str)
                or not server["host"] or type(server["port"]) is not int or not 1 <= server["port"] <= 65535):
            fail("each LOCAL.json litecoin.light server needs a host and an integer port")
    if len({(s["host"], s["port"]) for s in servers}) != len(servers):
        fail("LOCAL.json litecoin.light servers must be distinct")
    if type(light["maximumTipAge"]) is not int or not 600 <= light["maximumTipAge"] <= 3 * 3600:
        fail("LOCAL.json litecoin.light.maximumTipAge must be 600 to 10800 seconds")


def check_local(local, role):
    """Every LOCAL.json value the layout uses must be present before anything is written."""
    light = light_seller(local, role)
    if isinstance(local, dict) and isinstance(local.get("litecoin"), dict) and "light" in local["litecoin"] and not light:
        fail("only the XMZ seller can use light servers; the Bitcoin/Litecoin funder needs its node")
    for where, keys in LOCAL_KEYS.items():
        value = local if where == "local settings" else local[where]  # the top level was checked first
        if not isinstance(value, dict):
            fail(f"LOCAL.json {where} must be a JSON object")
        if where == "litecoin" and light:
            if set(value) != {"light"}:
                fail("LOCAL.json litecoin must be either node settings or light, not both")
            check_light(value["light"])
            continue
        keys = keys + (LOCAL_ROLE_KEYS[role] if where == "xmz" else ())
        missing = [key for key in keys if value.get(key) is None]
        if missing:
            fail(f"LOCAL.json {where} lacks " + ", ".join(missing))
    for section, keys in (("litecoin", () if light else ("rpcPort",)), ("xmz", ("daemonPort", "claimWalletPort") +
            (("sourceWalletPort",) if role == "xmz" else ("observerWalletPort",))), ("peer", ("port",))):
        for key in keys:
            value = local[section][key]
            if type(value) is not int or not 1 <= value <= 65535:
                fail(f"LOCAL.json {section}.{key} must be an integer port between 1 and 65535")
    if not isinstance(local["journalHelper"], str) or not local["journalHelper"]:
        fail("LOCAL.json journalHelper must be a path")


def final_path(out):
    """The absolute APPDIR path. Its parent must exist; APPDIR itself is never resolved or followed."""
    raw = Path(out)
    if raw.name in ("", ".", ".."):
        fail(f"--out {out} must name a new directory")
    try:
        parent = raw.parent.resolve(strict=True)
    except (OSError, RuntimeError):
        fail(f"the parent directory of --out {out} must already exist")
    if not parent.is_dir():
        fail(f"the parent of --out {out} is not a directory")
    return parent / raw.name


def app_layout(final, role, terms, quote, identity_dir, own_certificate, peer, policy, local, helper):
    """APPDIR's contents as (directories, [(name, bytes, mode)]). Every path inside points at FINAL."""
    directories = ["meta", "secrets"] + (["reservations"] if role == "ltc" else [])
    files = []

    def add(name, data, mode=0o600):
        files.append((name, data, mode))
        return str(final / name)

    def write_config(name, value):
        return add(name, canonical(value))

    d = identity_dir
    journal = {"schema": "monzero-private-ltc-journal-v5",
        "helper": add("journal-helper", helper, 0o700),  # the app runs only a single-link, owner-only copy
        "helperDigest": hashlib.sha256(helper).hexdigest(),
        "metadataDirectory": str(final / "meta"), "snapshotDirectory": str(final / "secrets"),
        "tradeId": terms["swapId"], "role": ROLE_NAMES[role],
        "pins": [list(bytes.fromhex(terms["ltcFunder"])), list(bytes.fromhex(terms["xmzFunder"]))],
        "policy": policy, "startQuoteFile": str(quote)}
    journal_path = write_config("journal.json", journal)
    phase = {k: v for k, v in journal.items() if k != "startQuoteFile"}
    ltc, xmz = local["litecoin"], local["xmz"]
    ltc_service = ({"ltcLight": ltc["light"]} if light_seller(local, role) else
        {"ltcPort": ltc["rpcPort"], "ltcWallet": ltc["wallet"], "ltcAnchor": ltc["anchor"], "ltcCookieFile": ltc["cookieFile"]})
    native = {"nativeDaemonPort": xmz["daemonPort"], "nativeWalletPort": xmz["claimWalletPort"], "nativeLoginFile": xmz["claimLoginFile"]}
    observer = ({"walletPort": xmz["sourceWalletPort"], "loginFile": xmz["sourceLoginFile"]} if role == "xmz"
                else {"walletPort": xmz["observerWalletPort"], "loginFile": xmz["observerLoginFile"]})
    purpose = "refund" if role == "xmz" else "payout"
    payment_rpc = write_config("payment-rpc.json", dict(ltc_service, **native,
        schema=f"monzero-private-ltc-{purpose}-rpc-v4", preparationObserver=observer, walletPasswordFile=xmz["walletPasswordFile"]))
    payment = write_config("payment.json", dict(phase, schema="monzero-private-ltc-journal-v2" if role == "xmz"
        else "monzero-private-ltc-journal-v3", **{purpose + "RpcFile": payment_rpc}))
    manifest = {"schema": "monzero-private-ltc-app-v1", "journalConfigFile": journal_path, "paymentConfigFile": payment}
    if role == "xmz":
        funding_rpc = write_config("funding-rpc.json", dict(ltc_service, schema="monzero-private-ltc-funding-rpc-v2",
            nativeDaemonPort=xmz["daemonPort"], nativeWalletPort=xmz["sourceWalletPort"], nativeLoginFile=xmz["sourceLoginFile"],
            source={"wallet": xmz["sourceAddress"], "account": 0, "priority": 1}))
        settlement = write_config("funding.json", dict(phase, schema="monzero-private-ltc-journal-v4", fundingRpcFile=funding_rpc))
        manifest["fundingConfigFile"] = settlement
    else:
        reservation_rpc = write_config("reservation-rpc.json", dict(ltc_service, schema="monzero-private-ltc-reservation-rpc-v1",
            reservationDirectory=str(final / "reservations"), feeRateAtomsPerKb=local.get("feeRateAtomsPerKb", 1000)))
        manifest["reservationConfigFile"] = write_config("reservation.json", {"schema": "monzero-private-ltc-reservation-v1",
            "journalConfigFile": journal_path, "rpcFile": reservation_rpc})
        redeem_rpc = write_config("redeem-rpc.json", dict(ltc_service, schema="monzero-private-ltc-redeem-rpc-v1",
            nativeDaemonPort=xmz["daemonPort"], observerWalletPort=xmz["observerWalletPort"],
            observerLoginFile=xmz["observerLoginFile"], safetyBlocks=local.get("safetyBlocks", 2)))
        settlement = write_config("redeem.json", {"schema": "monzero-private-ltc-redeem-v1",
            "journalConfigFile": journal_path, "rpcFile": redeem_rpc})
    ca = add("ca.pem", (own_certificate + peer["tlsCertificatePem"]).encode())
    link = local["peer"]
    manifest["peerConfigFile"] = write_config("peer.json", {"schema": "monzero-private-ltc-peer-v2",
        "mode": "listen" if role == "xmz" else "connect", "address": link["address"], "port": link["port"], "peerName": "localhost",
        "peerCertificateSha256": peer["tlsCertificateSha256"], "certificateFile": str(d / "tls.crt"), "privateKeyFile": str(d / "tls.key"),
        "caFile": ca, "identityFile": str(d / "identity.bin"), "passphraseFile": str(d / "identity.passphrase"),
        "settlementConfigFile": settlement})
    write_config("app.json", manifest)
    return directories, files


def fsync(fd):
    os.fsync(fd)  # one place for tests to observe sync order


def write_all(fd, data):
    view = memoryview(data)
    while view:
        written = os.write(fd, view)
        if written <= 0:
            raise OSError(errno.EIO, "write made no progress")
        view = view[written:]


def noreplace_rename():
    """rename(old_dir_fd, old, new_dir_fd, new) that fails with EEXIST instead of replacing new.

    Linux renameat2(RENAME_NOREPLACE) checks and renames in one kernel step, so a target created
    at any moment, including a dangling symlink, is never replaced. Other platforms fail closed:
    there is no check-then-rename fallback."""
    if not sys.platform.startswith("linux"):
        fail(f"configure cannot publish safely on {sys.platform}; it needs Linux renameat2(RENAME_NOREPLACE)")
    libc = ctypes.CDLL(None, use_errno=True)
    try:
        native = libc.renameat2
        native.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        native.restype = ctypes.c_int
        call = lambda old_dir, old, new_dir, new: native(old_dir, old, new_dir, new, RENAME_NOREPLACE)
    except AttributeError:
        number = SYS_RENAMEAT2.get(platform.machine())
        if number is None:
            fail(f"configure cannot publish safely: no renameat2 for {platform.machine()} in this C library")
        syscall = libc.syscall
        syscall.restype = ctypes.c_long
        call = lambda old_dir, old, new_dir, new: syscall(ctypes.c_long(number), ctypes.c_long(old_dir), ctypes.c_char_p(old),
            ctypes.c_long(new_dir), ctypes.c_char_p(new), ctypes.c_long(RENAME_NOREPLACE))

    def rename(old_dir, old, new_dir, new):
        if call(old_dir, os.fsencode(old), new_dir, os.fsencode(new)) != 0:
            code = ctypes.get_errno()
            raise OSError(code, os.strerror(code), old, None, new)
    return rename


def entry(dir_fd, name):
    try:
        return os.stat(name, dir_fd=dir_fd, follow_symlinks=False)
    except FileNotFoundError:
        return None


def make_staging(parent_fd, final):
    """A new owner-only sibling of final, on the same filesystem so the rename stays atomic."""
    for _ in range(16):
        name = f".{final.name}.staging-{secrets.token_hex(8)}"
        try:
            os.mkdir(name, 0o700, dir_fd=parent_fd)
        except FileExistsError:
            continue
        except OSError as error:
            fail(f"cannot create a staging directory beside {final}: {error.strerror}")
        try:
            fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent_fd)
        except OSError as error:
            fail(f"staging directory {final.parent / name} changed before it could be opened ({error.strerror}); left for review")
        info = os.fstat(fd)
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid():
            os.close(fd)
            fail(f"staging directory {final.parent / name} is not ours; left for review")
        os.fchmod(fd, 0o700)
        return name, fd
    fail(f"cannot pick an unused staging name beside {final}")


def make_directory_at(dir_fd, name, created):
    created.append((name, True))  # recorded first so an interruption cannot leak it
    os.mkdir(name, 0o700, dir_fd=dir_fd)
    fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=dir_fd)
    try:
        os.fchmod(fd, 0o700)
        fsync(fd)
    finally:
        os.close(fd)


def write_file_at(dir_fd, name, data, mode, created):
    if "/" in name or name in ("", ".", ".."):
        raise ValueError(f"bad app file name {name!r}")
    created.append((name, False))
    fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600, dir_fd=dir_fd)
    try:
        os.fchmod(fd, mode)  # exact mode whatever the umask; never wider than owner-only
        write_all(fd, data)
        fsync(fd)
    finally:
        os.close(fd)


def locate(parent_fd, staging, final, staging_fd):
    """Where our staging directory is now: 'staging', 'final' or None if neither name holds it.

    A directory has exactly one name, so this decides whether the rename happened."""
    ours = os.fstat(staging_fd)
    for name, where in ((staging, "staging"), (final.name, "final")):
        info = entry(parent_fd, name)
        if info is not None and (info.st_dev, info.st_ino) == (ours.st_dev, ours.st_ino):
            return where
    return None


def remove_staging(parent_fd, staging, staging_fd, created):
    """Remove only what this run created, through the descriptor of our own unpublished directory."""
    for name, is_directory in reversed(created):
        try:
            (os.rmdir if is_directory else os.unlink)(name, dir_fd=staging_fd)
        except FileNotFoundError:
            pass
    info, ours = entry(parent_fd, staging), os.fstat(staging_fd)
    if info is not None and (info.st_dev, info.st_ino) == (ours.st_dev, ours.st_ino):
        os.rmdir(staging, dir_fd=parent_fd)


def abandon(final, parent_fd, staging, staging_fd, created):
    """After a failure or interruption: clean up an unpublished staging directory, never final."""
    try:
        where = locate(parent_fd, staging, final, staging_fd)
    except OSError:
        where = None
    if where == "final":
        return (f"{final} had already been published complete and was left in place, "
                f"but its parent directory was not synced; run sync before relying on it")
    if where == "staging":
        try:
            remove_staging(parent_fd, staging, staging_fd, created)
            return f"nothing was published at {final}"
        except OSError as error:
            return f"nothing was published at {final}; remove the private staging directory {final.parent / staging} ({error.strerror})"
    return (f"could not tell whether {final} was published; left {final} and {final.parent / staging} "
            f"untouched for review")


def publish_directory(final, directories, files):
    """Create final with exactly these contents, or fail without creating or replacing anything.

    The tree is built in a private sibling staging directory, every file and directory is fsynced,
    then renameat2(RENAME_NOREPLACE) gives it the final name and the parent is fsynced."""
    rename = noreplace_rename()
    parent_fd = os.open(final.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        if entry(parent_fd, final.name) is not None:
            fail(f"{final} already exists; it is never replaced")
        staging, staging_fd = make_staging(parent_fd, final)
        created, renaming = [], False
        try:
            for name in directories:
                make_directory_at(staging_fd, name, created)
            for name, data, mode in files:
                write_file_at(staging_fd, name, data, mode, created)
            fsync(staging_fd)
            renaming = True
            rename(parent_fd, staging, parent_fd, final.name)
        except BaseException as error:
            outcome = abandon(final, parent_fd, staging, staging_fd, created)
            if not isinstance(error, OSError):
                print(f"trade_kit: stopped by {type(error).__name__}; {outcome}", file=sys.stderr)
                raise
            if renaming and error.errno == errno.EEXIST:
                fail(f"{final} appeared while configuring and was not replaced; {outcome}")
            if renaming and error.errno in (errno.EINVAL, errno.ENOSYS, errno.EOPNOTSUPP):
                fail(f"the filesystem holding {final.parent} does not support no-replace rename; {outcome}")
            fail(f"could not write {final} ({error.strerror or error}); {outcome}")
        finally:
            os.close(staging_fd)
        try:
            fsync(parent_fd)
        except OSError as error:
            fail(f"{final} was published complete, but syncing {final.parent} failed ({error.strerror}). It was left in "
                 f"place. Its name may not survive a crash or power loss, and a repeated sync cannot prove it was saved. "
                 f"Run sync, then before starting the app confirm {final / 'app.json'} exists; if it is gone after a "
                 f"restart, inspect only this attempt's staging path {final.parent / staging} before rerunning configure. "
                 f"Do not delete other staging directories or an existing destination.")
        except BaseException:
            print(f"trade_kit: interrupted after publishing {final} complete; run sync before relying on it", file=sys.stderr)
            raise
    finally:
        os.close(parent_fd)


def cmd_configure(a):
    """One side's app files. Paths in LOCAL.json are this machine's own services."""
    final = final_path(a.out)
    noreplace_rename()  # refuse an unsupported platform before reading anything
    d = Path(a.dir).resolve()
    card = read_json(d / "card.json", "identity card")
    role = card.get("role") if isinstance(card, dict) else None
    if role not in ROLE_NAMES:
        fail(f"{d / 'card.json'} is not a trade card")
    card = load_card(d / "card.json", role)
    for name in IDENTITY_FILES:
        try:
            if not stat.S_ISREG(os.stat(d / name).st_mode):
                fail(f"{d / name} is not a regular file")
        except OSError as error:
            fail(f"{d / name} is missing ({error.strerror}); run configure on the machine that holds this identity")
    peer = load_card(a.peer_card, "xmz" if role == "ltc" else "ltc")
    if not isinstance(peer.get("tlsCertificatePem"), str) or not isinstance(peer.get("tlsCertificateSha256"), str):
        fail(f"{a.peer_card} lacks the peer's TLS certificate")
    quote = Path(a.quote).resolve()
    terms = load_verified_quote(quote)
    if terms["ltcFunder" if role == "ltc" else "xmzFunder"] != card["funderKey"]:
        fail("the signed quote names a different key for your role")
    if terms["xmzFunder" if role == "ltc" else "ltcFunder"] != peer["funderKey"]:
        fail("the signed quote names a different key for the other party")
    policy = read_json(a.policy, "policy")
    if not isinstance(policy, dict):
        fail("policy must be a JSON object")
    local = read_json(a.local, "local settings")
    check_local(local, role)
    try:
        helper = Path(local["journalHelper"]).read_bytes()
        own_certificate = (d / "tls.crt").read_text()
    except (OSError, ValueError) as error:
        fail(f"cannot read an input file: {error}")
    if not helper:
        fail(f"journal helper {local['journalHelper']} is empty")
    try:
        directories, files = app_layout(final, role, terms, quote, d, own_certificate, peer, policy, local, helper)
    except (TypeError, ValueError) as error:  # e.g. text that cannot be encoded
        fail(f"cannot build the app configuration: {error}")
    publish_directory(final, directories, files)
    stage = {"ltc-testnet": "testnet", "btc-testnet4": "btc-testnet4", "btc-signet": "btc-signet", "btc-regtest": "btc-regtest"}.get(terms["ltcNetwork"], "regtest")
    print(f"Wrote {final}. Start the app with: --dex-ltc-network {stage} --dex-ltc-app {final / 'app.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("identity"); p.add_argument("--role", choices=("ltc", "xmz"), required=True); p.add_argument("--dir", required=True)
    p = sub.add_parser("terms")
    for flag in ("--ltc-card", "--xmz-card", "--params", "--out"): p.add_argument(flag, required=True)
    p = sub.add_parser("sign")
    for flag in ("--dir", "--terms", "--out"): p.add_argument(flag, required=True)
    p = sub.add_parser("assemble")
    for flag in ("--terms", "--ltc-sig", "--xmz-sig", "--out"): p.add_argument(flag, required=True)
    p = sub.add_parser("configure")
    for flag in ("--dir", "--quote", "--peer-card", "--policy", "--local", "--out"): p.add_argument(flag, required=True)
    a = parser.parse_args()
    {"identity": cmd_identity, "terms": cmd_terms, "sign": cmd_sign, "assemble": cmd_assemble,
     "configure": cmd_configure}[a.command](a)


if __name__ == "__main__":
    main()
