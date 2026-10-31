#!/usr/bin/env python3
"""Disposable Litecoin regtest contract fixture; never a live trading adapter.

Reuses swap-core's actual adaptor transaction constructors through an explicitly
selected ignored Rust test. All RPC endpoints and test wallets are owned here.
"""
import argparse
import hashlib
import http.server
import threading
import base64
import decimal
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import uuid

GENESIS = "530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9"
SCHEMA = "monzero-ltc-contract-fixture-v3"
ATOMS = 100_000_000
PRINCIPAL = ATOMS
FEE = 10_000
CANCEL_BLOCKS = 10
PUNISH_BLOCKS = 7


class RpcError(RuntimeError):
    def __init__(self, code, message):
        self.code = code
        self.message = str(message)[:256]
        super().__init__(f"Litecoin fixture RPC rejected request ({code})")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        raise RuntimeError("Fixture RPC redirect refused")


def atoms(value):
    if isinstance(value, bool) or not isinstance(value, (str, int, decimal.Decimal)):
        raise ValueError("Expected exact coin amount")
    scaled = decimal.Decimal(value) * ATOMS
    if not scaled.is_finite() or scaled < 0 or scaled != scaled.to_integral_value():
        raise ValueError("Invalid atomic amount")
    return int(scaled)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_private(path, value):
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w") as out:
        json.dump(value, out, separators=(",", ":"))
        out.flush()
        os.fsync(out.fileno())


def signed_quote_fixture(terms):
    # Independent libsodium signatures with public test-only identity seeds.
    from nacl.signing import SigningKey
    payload = json.dumps(terms, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    signatures = []
    for seed, role in ((1, "ltc-funder"), (2, "xmz-funder")):
        domain = f"monzero-native-ltc-xmz-quote-v1-{role}".encode()
        message = b"MONZERO-DEX-OFFER-V1\n"+str(len(domain)).encode()+b":"+domain+str(len(payload)).encode()+b":"+payload
        signatures.append(SigningKey(bytes([seed])*32).sign(message).signature.hex())
    return {"payload":payload.decode(), "ltcSignature":signatures[0], "xmzSignature":signatures[1]}


class RelayProxy:
    """Owned loopback RPC proxy; optionally discard one actual relay reply."""
    def __init__(self, upstream, drop_reply=False, drop_request=False):
        self.counts = {}
        self.forwarded = {}
        self.mutex = threading.Lock()
        proxy = self
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def do_POST(self):
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= 2_000_000:
                    self.send_error(400)
                    return
                body = self.rfile.read(size)
                method = json.loads(body)["method"]
                with proxy.mutex:
                    proxy.counts[method] = proxy.counts.get(method, 0)+1
                    number = proxy.counts[method]
                if drop_request and method == "sendrawtransaction" and number == 1:
                    self.close_connection = True
                    self.connection.shutdown(socket.SHUT_RDWR)
                    return
                with proxy.mutex:
                    proxy.forwarded[method] = proxy.forwarded.get(method, 0)+1
                transport = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
                request = urllib.request.Request(f"http://127.0.0.1:{upstream}"+self.path, data=body,
                    headers={"Authorization":self.headers.get("Authorization", ""), "Content-Type":"application/json"})
                try:
                    response = transport.open(request, timeout=30)
                except urllib.error.HTTPError as error:
                    response = error
                with response:
                    code, payload = response.status, response.read(2_000_001)
                if drop_reply and method == "sendrawtransaction":
                    self.close_connection = True
                    self.connection.shutdown(socket.SHUT_RDWR)
                    return
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_port
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
    def __enter__(self):
        self.thread.start()
        return self
    def __exit__(self, *args):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)


class Node:
    def __init__(self, executable, directory):
        self.executable = executable
        self.directory = directory
        self.process = None
        self.log = None
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]
        self.transport = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
        self.request_id = 0

    def rpc(self, method, *params, wallet=None):
        self.request_id += 1
        cookie = (self.directory / "regtest/.cookie").read_bytes().strip()
        endpoint = f"http://127.0.0.1:{self.port}" + (f"/wallet/{wallet}" if wallet else "")
        request = urllib.request.Request(endpoint, data=json.dumps({
            "jsonrpc": "1.0", "id": self.request_id, "method": method, "params": params,
        }).encode(), headers={"Authorization": "Basic " + base64.b64encode(cookie).decode(),
                              "Content-Type": "application/json"})
        try:
            response = self.transport.open(request, timeout=30)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read(2_000_001)
        require(len(raw) <= 2_000_000, "Oversized RPC response")
        value = json.loads(raw, parse_float=decimal.Decimal)
        require(value.get("id") == self.request_id, "RPC response identity mismatch")
        if value.get("error"):
            raise RpcError(value["error"].get("code"), value["error"].get("message"))
        return value["result"]

    def mine(self, blocks, address):
        try:
            return self.rpc("generatetoaddress", blocks, address)
        except RpcError as error:
            # This RPC has no secret parameters; expose its bounded rejection
            # reason without dumping any other wallet/transaction response.
            raise RuntimeError("Owned node mining failed: " + error.message) from None

    def start(self):
        self.log = open(self.directory / "daemon.log", "ab")
        self.process = subprocess.Popen([
            str(self.executable), f"-datadir={self.directory}", "-regtest", "-server=1",
            "-listen=0", "-connect=0", "-dnsseed=0", "-discover=0", "-upnp=0",
            "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1", f"-rpcport={self.port}",
            # Transparent-chain fixture only; MWEB validation is a separate milestone.
            "-vbparams=mweb:0:0:1000000:2000000",
            "-txindex=1", "-fallbackfee=0.001", "-printtoconsole=1",
        ], stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            require(self.process.poll() is None, "Owned Litecoin daemon exited during startup")
            try:
                info = self.rpc("getblockchaininfo")
                require(info["chain"] == "regtest" and self.rpc("getblockhash", 0) == GENESIS,
                        "Unexpected Litecoin chain identity")
                require(self.rpc("getconnectioncount") == 0, "Fixture node has external peers")
                return
            except (FileNotFoundError, urllib.error.URLError, ConnectionError, RpcError):
                time.sleep(0.1)
        raise RuntimeError("Owned Litecoin node startup timed out")

    def stop(self):
        if self.process and self.process.poll() is None:
            try:
                self.rpc("stop")
            except (OSError, RuntimeError, ValueError):
                self.process.terminate()
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        if self.log:
            self.log.close()
            self.log = None


class Fixture:
    def __init__(self, root, binary):
        self.root, self.binary, self.sequence = root, binary, 0

    def call(self, value, reject=False):
        self.sequence += 1
        request = self.root / f"request-{self.sequence}.json"
        response = self.root / f"response-{self.sequence}.json"
        write_private(request, value)
        environment = dict(os.environ, MONZERO_LTC_FIXTURE_REQUEST=str(request),
                           MONZERO_LTC_FIXTURE_RESPONSE=str(response))
        checked = subprocess.run([str(self.binary), "--exact", "export_litecoin_settlement", "--ignored"],
                                 env=environment, capture_output=True, timeout=30)
        if reject:
            require(checked.returncode != 0 and not response.exists(), "Unsafe fixture request accepted")
            return
        require(checked.returncode == 0 and b"1 passed" in checked.stdout and response.is_file(),
                "Rust Litecoin transaction fixture failed")
        require(response.stat().st_mode & 0o077 == 0, "Fixture result is not private")
        result = json.loads(response.read_text())
        require(result.get("schema") == SCHEMA and result.get("executable") is False,
                "Wrong Rust fixture response schema")
        return result


def run(daemon, binary, parent, report=None, wallet_binary=None, redeem_binary=None):
    with tempfile.TemporaryDirectory(prefix="monzero-ltc-contract-", dir=parent) as temp:
        root = pathlib.Path(temp)
        node_dir = root / "node"
        node_dir.mkdir(mode=0o700)
        node = Node(daemon, node_dir)
        fixture = Fixture(root, binary)
        try:
            node.start()
            node.rpc("createwallet", "fixture")
            mining = node.rpc("getnewaddress", "", "bech32", wallet="fixture")
            require(mining.startswith("rltc1"), "Expected Litecoin regtest address")
            node.mine(440, mining)
            forks = node.rpc("getblockchaininfo")["softforks"]
            require(forks["csv"]["active"], "CSV is not active")
            require(forks["mweb"]["active"] is False
                    and forks["mweb"]["bip8"]["start_height"] == 1_000_000,
                    "Unexpected MWEB parameters in transparent-chain fixture")
            description = fixture.call({"mode": "describe"})
            decoded = node.rpc("decodescript", description["witnessScript"])
            addresses = decoded["segwit"]["addresses"]
            require(isinstance(addresses, list) and len(addresses) == 1, "Ambiguous lock address")
            lock_address = addresses[0]
            require(lock_address.startswith("rltc1"), "Lock uses wrong address network")
            require(description.get("lockAddress") == lock_address
                    and description.get("chain") == node.rpc("getblockchaininfo")["chain"]
                    and description.get("genesis") == node.rpc("getblockhash", 0),
                    "Rust Litecoin address or chain identity differs from node")
            require(node.rpc("validateaddress", lock_address)["scriptPubKey"] == description["scriptPubKey"],
                    "Node lock script differs from engine descriptor")
            reservation_dir = root / "reservations"
            reservation_dir.mkdir(mode=0o700)
            wallet_sequence = 0
            def wallet_call(request, reject=False):
                nonlocal wallet_sequence
                wallet_sequence += 1
                request_path = root / f"wallet-request-{wallet_sequence}.json"
                response_path = root / f"wallet-response-{wallet_sequence}.json"
                write_private(request_path, request)
                env = dict(os.environ, MONZERO_LTC_WALLET_REQUEST=str(request_path),
                           MONZERO_LTC_WALLET_RESPONSE=str(response_path),
                           MONZERO_LTC_WALLET_COOKIE=str(node_dir / "regtest/.cookie"),
                           MONZERO_LTC_RESERVATION_DIRECTORY=str(reservation_dir))
                action = request.get("action", "preview")
                # Log only operation labels, never the request, cookie or key material.
                print(f"Wallet check {wallet_sequence}: {action} ({'reject' if reject else 'accept'})", flush=True)
                started = time.monotonic()
                try:
                    result = subprocess.run([str(wallet_binary), "--exact", "export_litecoin_wallet_rpc", "--ignored"],
                                            env=env, capture_output=True, timeout=600)
                except subprocess.TimeoutExpired:
                    raise RuntimeError(f"Wallet check {wallet_sequence} ({action}) timed out after 600s") from None
                print(f"Wallet check {wallet_sequence}: exit={result.returncode}, elapsed={time.monotonic()-started:.1f}s", flush=True)
                if reject:
                    require(result.returncode != 0 and not response_path.exists(), "Unsafe wallet preview accepted")
                    return
                if (result.returncode != 0 or not response_path.is_file()) and report is not None:
                    # Keep test diagnostics private and outside the disposable node
                    # directory. Never print request bodies or credentials.
                    diagnostic = pathlib.Path(str(report) + f".check-{wallet_sequence}.failure.log")
                    fd = os.open(diagnostic, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                    with os.fdopen(fd, "wb") as output:
                        output.write(result.stdout)
                        output.write(result.stderr)
                require(result.returncode == 0 and response_path.is_file(), f"Rust Litecoin wallet adapter failed at check {wallet_sequence} ({action}), exit={result.returncode}")
                value = json.loads(response_path.read_text())
                require(value.get("schema") == "monzero-ltc-wallet-fixture-v1" and value.get("executable") is False,
                        "Wrong wallet adapter response")
                return value
            outcomes = {}
            for mode in ("redeem", "refund", "earlyRefund", "punish"):
                print(f"Starting Litecoin {mode} settlement checks", flush=True)
                redeem_address = node.rpc("getnewaddress", "", "bech32", wallet="fixture")
                refund_address = node.rpc("getnewaddress", "", "bech32", wallet="fixture")
                redeem_script = node.rpc("validateaddress", redeem_address)["scriptPubKey"]
                refund_script = node.rpc("validateaddress", refund_address)["scriptPubKey"]
                if wallet_binary:
                    wallet_request = {"port": node.port, "wallet": "fixture", "anchor": mining,
                                      "contract": lock_address, "amountAtoms": PRINCIPAL,
                                      "feeRateAtomsPerKb": 100_000, "maximumFeeAtoms": 100_000}
                    if mode == "redeem":
                        node.rpc("createwallet", "unrelated")
                        foreign = node.rpc("getnewaddress", "", "bech32", wallet="unrelated")
                        wallet_call(dict(wallet_request, anchor=foreign), reject=True)
                        wallet_call(dict(wallet_request, maximumFeeAtoms=1), reject=True)
                        wallet_call(dict(wallet_request, wallet="unrelated"), reject=True)
                    before_balance = node.rpc("getbalance", wallet="fixture")
                    before_pool = node.rpc("getrawmempool")
                    before_locked = node.rpc("listlockunspent", wallet="fixture")
                    funded = wallet_call(wallet_request)
                    require(node.rpc("getbalance", wallet="fixture") == before_balance
                            and node.rpc("getrawmempool") == before_pool
                            and node.rpc("listlockunspent", wallet="fixture") == before_locked,
                            "Funding preview changed balances, locked coins or broadcast")
                    trade = str(uuid.uuid4())
                    funded = wallet_call(dict(wallet_request, action="reserve", trade=trade))
                    require(funded["reserved"] is True, "Reservation was not established")
                    reserved_inputs = node.rpc("listlockunspent", wallet="fixture")
                    require(bool(reserved_inputs), "Reservation left inputs unlocked")
                    resumed = wallet_call(dict(wallet_request, action="resume", trade=trade))
                    require(resumed["psbt"] == funded["psbt"], "Resume changed exact funding PSBT")
                    wallet_call(dict(wallet_request, action="resume", trade=trade, wallet="unrelated"), reject=True)
                    wallet_call(dict(wallet_request, action="reserve", trade=trade, amountAtoms=PRINCIPAL+1), reject=True)
                    conflicting_trade = str(uuid.uuid4())
                    conflicting_intent = json.loads((reservation_dir / f"{trade}.intent").read_text())
                    conflicting_intent["trade"] = conflicting_trade
                    write_private(reservation_dir / f"{conflicting_trade}.intent", conflicting_intent)
                    wallet_call(dict(wallet_request, action="resume", trade=conflicting_trade), reject=True)
                    require(node.rpc("listlockunspent", wallet="fixture") == reserved_inputs,
                            "Conflicting trade changed original input locks")
                    node.stop()
                    node.start()
                    if "fixture" not in node.rpc("listwallets"):
                        node.rpc("loadwallet", "fixture")
                    require(node.rpc("listlockunspent", wallet="fixture") == [], "Fixture expected volatile Core coin locks")
                    resumed = wallet_call(dict(wallet_request, action="resume", trade=trade))
                    require(resumed["psbt"] == funded["psbt"]
                            and node.rpc("listlockunspent", wallet="fixture") == reserved_inputs,
                            "Restart failed to restore original funding reservation")
                else:
                    funded = node.rpc("walletcreatefundedpsbt", [], [{lock_address: "1.00000000"}], 0,
                                      {"change_type": "bech32", "feeRate": "0.001"}, True, wallet="fixture")
                request = {"mode": "build", "chain": node.rpc("getblockchaininfo")["chain"],
                           "genesis": node.rpc("getblockhash", 0),
                           "redeemAddress": redeem_address, "refundAddress": refund_address,
                           "lockPsbt": funded["psbt"], "amountAtoms": str(PRINCIPAL),
                           "redeemScript": redeem_script, "refundScript": refund_script,
                           "feeAtoms": str(FEE), "cancelBlocks": CANCEL_BLOCKS, "punishBlocks": PUNISH_BLOCKS}
                for bad in ({"chain": "main"}, {"genesis": "0"*64},
                            {"redeemAddress": refund_address}, {"refundAddress": "bcrt1invalid"},
                            {"amountAtoms": str(PRINCIPAL + 1)}, {"feeAtoms": str(PRINCIPAL)},
                            {"cancelBlocks": 0}, {"punishBlocks": 0}, {"punishBlocks": 65536}, {"redeemScript": "51"}):
                    fixture.call(dict(request, **bad), reject=True)
                transactions = fixture.call(request)
                require(transactions.get("adaptorSecretRecovered") is True, "Adaptor recovery not verified")
                require(transactions.get("refundAdaptorSecretRecovered") is True,
                        "Refund adaptor recovery not verified")
                def funding_observation(expected):
                    observed = wallet_call(dict(wallet_request, action="observeLock", trade=trade))["observation"]
                    require(observed["state"] == expected
                            and observed["txid"] == transactions["lock"]["txid"], "Wrong funding reconciliation state")
                    return observed
                if wallet_binary:
                    require(funding_observation("unobserved")["inputs_available"] is True,
                            "Unbroadcast reservation inputs unavailable")
                if wallet_binary:
                    policy = {"key_a":description["keyA"], "key_b":description["keyB"],
                              "refund_address":refund_address, "cancel_blocks":CANCEL_BLOCKS,
                              "cancel_fee":FEE, "refund_fee":FEE,
                              "minimum_relay_fee_atoms_per_kb":1000, "minimum_output_atoms":100_000}
                    recovery = {"policy":policy, "cancel":transactions["cancel"]["rawHex"],
                                "refund":transactions["refund"]["rawHex"]}
                    from nacl.signing import SigningKey
                    xmz_test_address = "FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H"
                    quote_terms = {"schema":"monzero-native-ltc-xmz-quote-v1", "quoteId":str(uuid.uuid4()), "swapId":trade,
                        "ltcFunder":SigningKey(bytes([1])*32).verify_key.encode().hex(),
                        "xmzFunder":SigningKey(bytes([2])*32).verify_key.encode().hex(),
                        "ltcGenesis":GENESIS, "xmzGenesis":"a"*64, "ltcNetwork":"ltc-regtest", "xmzNetwork":"xmz-mainnet",
                        "ltcAtoms":str(PRINCIPAL), "xmzAtoms":"500000000", "ltcPayout":redeem_address, "ltcRefund":refund_address,
                        "xmzPayout":xmz_test_address, "xmzRefund":xmz_test_address, "ltcFundingFeeMaxAtoms":"100000",
                        "xmzNativeFeeMaxAtoms":"100000", "ltcCancelFeeAtoms":str(FEE), "ltcRefundFeeAtoms":str(FEE),
                        "ltcRedeemFeeAtoms":str(FEE), "ltcPunishFeeAtoms":str(FEE), "ltcEarlyRefundFeeAtoms":str(FEE), "ltcConfirmations":"1", "xmzConfirmations":"10",
                        "cancelBlocks":str(CANCEL_BLOCKS), "punishBlocks":str(PUNISH_BLOCKS), "expiresAt":str(int(time.time())+3500), "appFeeXmzAtoms":"0",
                        "ltcMinimumOutputAtoms":"100000", "ltcMinimumRelayFeeAtomsPerKb":"1000"}
                    quote = signed_quote_fixture(quote_terms)
                    sign_request = dict(wallet_request, action="signExchange", trade=trade, recovery=recovery, quote=quote)
                    for bad in ({"ltcAtoms":str(PRINCIPAL+1)}, {"expiresAt":str(int(time.time())-1)}, {"ltcGenesis":"b"*64}):
                        wallet_call(dict(sign_request, quote=signed_quote_fixture(dict(quote_terms, **bad))), reject=True)
                    wallet_call(dict(sign_request, quote=dict(quote, ltcSignature="00"*64)), reject=True)
                    for bad in ({"cancel_blocks":CANCEL_BLOCKS+1}, {"refund_fee":FEE+1},
                                {"refund_address":mining}, {"key_a":description["keyB"]},
                                {"minimum_output_atoms":PRINCIPAL}, {"minimum_relay_fee_atoms_per_kb":100_000_000}):
                        wallet_call(dict(sign_request, recovery=dict(recovery, policy=dict(policy, **bad))), reject=True)
                    wallet_call(dict(sign_request, recovery=dict(recovery, refund="00")), reject=True)
                    cancel_script = node.rpc("decoderawtransaction", recovery["cancel"])["vin"][0]["txinwitness"][-1]
                    bad_cancel = recovery["cancel"].replace(cancel_script, cancel_script[:-2]+"00", 1)
                    require(bad_cancel != recovery["cancel"], "Fixture failed to mutate cancellation script")
                    wallet_call(dict(sign_request, recovery=dict(recovery, cancel=bad_cancel)), reject=True)
                    refund_witness = node.rpc("decoderawtransaction", recovery["refund"])["vin"][0]["txinwitness"][0]
                    bad_refund = recovery["refund"].replace(refund_witness, refund_witness[:-2]+"02", 1)
                    wallet_call(dict(sign_request, recovery=dict(recovery, refund=bad_refund)), reject=True)
                    corrupt_signature = refund_witness[:-4]+f"{int(refund_witness[-4:-2],16)^1:02x}"+refund_witness[-2:]
                    invalid_signature_refund = recovery["refund"].replace(refund_witness, corrupt_signature, 1)
                    require(invalid_signature_refund != recovery["refund"], "Fixture failed to corrupt signature")
                    wallet_call(dict(sign_request, recovery=dict(recovery, refund=invalid_signature_refund)), reject=True)
                    require(not (reservation_dir / f"{trade}.signing").exists(),
                            "Rejected recovery package created signing authority")
                    pool_before_sign = node.rpc("getrawmempool")
                    first_signed = wallet_call(sign_request)
                    # Verify the real execution controller committed both peers;
                    # merely supplying a wallet binary is not evidence of this.
                    expected_quote = hashlib.sha256(b"monzero-native-ltc-xmz-quote-v1\0" + quote["payload"].encode()).digest()
                    protocol = reservation_dir / f"execution-protocol-{trade}"
                    for actor, role in (("bob", 0), ("alice", 1)):
                        head_path = protocol / f"{actor}-metadata" / f"{trade}.ltc-execution-head"
                        head = json.loads(head_path.read_text())
                        require(head["schema"] == "monzero-ltc-execution-head-v1" and head["trade"] == trade
                                and head["step"] == 2 and head["quote"] == list(expected_quote),
                                "Owned execution did not commit the expected completed exchange")
                        snapshot = protocol / f"{actor}-secrets" / f"{trade}.ltc-protocol-frame-{role}-{bytes(head['checkpoint']).hex()}"
                        require(hashlib.sha256(snapshot.read_bytes()).digest() == bytes(head["checkpoint"]),
                                "Execution head points to the wrong protocol snapshot")
                        require(head_path.stat().st_mode & 0o777 == 0o600 and snapshot.stat().st_mode & 0o777 == 0o600,
                                "Execution state is not private")
                        metadata = protocol / f"{actor}-metadata"
                        intent_path = metadata / f"{trade}.ltc-recovery-intent"
                        intent_bytes = intent_path.read_bytes()
                        require(hashlib.sha256(intent_bytes).digest() == bytes(head["recovery_intent"]),
                                "Execution head does not commit the recovery intent")
                        intent = json.loads(intent_bytes)
                        require(intent["psbt"] == funded["psbt"] and intent["checkpoint"] == head["checkpoint"],
                                "Recovery uses another lock or key checkpoint")
                        from nacl.signing import VerifyKey
                        for direction, signer_role in (("outgoing", role), ("incoming", 1-role)):
                            record_path = metadata / f"{trade}.ltc-recovery-{direction}"
                            record = json.loads(record_path.read_bytes())
                            frame_bytes = bytes(record["bytes"])
                            frame = json.loads(frame_bytes)
                            require(frame["role"] == signer_role and frame["binding"]["quote"] == list(expected_quote)
                                    and frame["binding"]["cancel"] == transactions["cancel"]["txid"]
                                    and frame["binding"]["refund"] == transactions["refund"]["txid"],
                                    "Recovery signature frame does not bind the expected trade")
                            VerifyKey(SigningKey(bytes([signer_role+1])*32).verify_key.encode()).verify(
                                b"monzero-ltc-xmz-recovery-v1\0" + len(frame_bytes).to_bytes(8, "big") + frame_bytes,
                                bytes(record["signature"]))
                            require(record_path.stat().st_mode & 0o777 == 0o600,
                                    "Recovery signature record is not private")
                        completed_path = metadata / f"{trade}.ltc-recovery-completed"
                        completed = json.loads(completed_path.read_bytes())
                        require(completed["refund"] is not None if role == 0 else completed["punish"] is not None,
                                "Missing role-specific completed recovery")
                        require(intent_path.stat().st_mode & 0o777 == 0o600
                                and completed_path.stat().st_mode & 0o777 == 0o600,
                                "Recovery state is not private")
                    bob_head = protocol / "bob-metadata" / f"{trade}.ltc-execution-head"
                    saved_head = bob_head.with_suffix(".missing-test")
                    bob_head.rename(saved_head)
                    wallet_call(sign_request, reject=True)
                    require(not bob_head.exists(), "Missing execution head caused a new attempt")
                    saved_head.rename(bob_head)
                    # A saved wallet signature must not bypass missing protocol recovery.
                    recovery_intent = protocol / "bob-metadata" / f"{trade}.ltc-recovery-intent"
                    saved_intent = recovery_intent.with_suffix(".missing-test")
                    signed_before = (reservation_dir / f"{trade}.signed").read_bytes()
                    recovery_intent.rename(saved_intent)
                    wallet_call(sign_request, reject=True)
                    require(not recovery_intent.exists()
                            and (reservation_dir / f"{trade}.signed").read_bytes() == signed_before,
                            "Missing committed recovery was replaced or changed signed funding")
                    saved_intent.rename(recovery_intent)
                    # Simulate an older quote-only signing record. Reject adding
                    # an exchange without leaving that old record unrelayable.
                    binding_path = reservation_dir / f"{trade}.exchange"
                    signing_path = reservation_dir / f"{trade}.signing"
                    original_binding = binding_path.read_bytes()
                    original_signing = signing_path.read_bytes()
                    legacy_signing = json.loads(original_signing)
                    legacy_signing.pop("exchange_record_hash")
                    signing_path.write_text(json.dumps(legacy_signing, separators=(",", ":")))
                    binding_path.unlink()
                    wallet_call(sign_request, reject=True)
                    require(not binding_path.exists(), "Rejected exchange upgrade mutated legacy recovery")
                    signing_path.write_bytes(original_signing)
                    binding_path.write_bytes(original_binding)
                    binding_path.chmod(0o600)
                    repriced = signed_quote_fixture(dict(quote_terms, xmzAtoms="500000001"))
                    wallet_call(dict(sign_request, quote=repriced), reject=True)
                    saved_quote = json.loads((reservation_dir / f"{trade}.quote").read_text())
                    require(saved_quote["terms"] == quote_terms, "Durable quote differs from signed commercial terms")
                    # Simulate interruption after the durable signing context,
                    # before receipt persistence. Re-sign only the same txid.
                    (reservation_dir / f"{trade}.signed").unlink()
                    repeated = wallet_call(sign_request)
                    require(repeated["txid"] == first_signed["txid"], "Interrupted signing changed funding identity")
                    first_signed = repeated
                    changed_valid = dict(policy, minimum_output_atoms=policy["minimum_output_atoms"]+1)
                    wallet_call(dict(sign_request, action="sign", recovery=dict(recovery, policy=changed_valid)), reject=True)
                    require(node.rpc("getrawmempool") == pool_before_sign, "Signing broadcast a payment")
                    for suffix in ("quote", "exchange", "signing", "signed"):
                        require((reservation_dir / f"{trade}.{suffix}").stat().st_mode & 0o777 == 0o600,
                                "Signed funding record permissions are not private")
                    node.stop()
                    node.start()
                    if "fixture" not in node.rpc("listwallets"):
                        node.rpc("loadwallet", "fixture")
                    finalized = wallet_call(sign_request)
                    require(finalized == first_signed, "Signing recovery changed persisted transaction bytes")
                    require(node.rpc("listlockunspent", wallet="fixture") == reserved_inputs,
                            "Reopening signed funding did not restore coin locks")
                else:
                    signed = node.rpc("walletprocesspsbt", funded["psbt"], wallet="fixture")
                    finalized = node.rpc("finalizepsbt", signed["psbt"])
                    require(finalized["complete"], "Lock signing incomplete")
                if wallet_binary:
                    relay_request = dict(wallet_request, action="relay", trade=trade,
                                         expectedTxid=transactions["lock"]["txid"])
                    wallet_call(dict(relay_request, expectedTxid="0"*64), reject=True)
                    quote_file = reservation_dir / f"{trade}.quote"
                    saved_quote_file = reservation_dir / f"{trade}.quote-backup"
                    quote_file.rename(saved_quote_file)
                    wallet_call(relay_request, reject=True)
                    saved_quote_file.rename(quote_file)
                    exchange_file = reservation_dir / f"{trade}.exchange"
                    saved_exchange = exchange_file.read_bytes()
                    exchange_file.unlink()
                    wallet_call(relay_request, reject=True)
                    exchange_file.write_bytes(saved_exchange + b" ")
                    exchange_file.chmod(0o600)
                    wallet_call(relay_request, reject=True)
                    exchange_file.write_bytes(saved_exchange)
                    require(not (reservation_dir / f"{trade}.relay").exists(), "Wrong identity consumed relay intent")
                    if mode == "punish":
                        (reservation_dir / f"{trade}.relay-receipt").mkdir(mode=0o700)
                    with RelayProxy(node.port, drop_reply=mode == "earlyRefund", drop_request=mode == "refund") as proxy:
                        proxy_request = dict(relay_request, port=proxy.port)
                        outcome = wallet_call(proxy_request)["outcome"]
                        expected_state = "uncertain" if mode in ("refund", "earlyRefund") else "submitted"
                        require(outcome["state"] == expected_state and outcome["txid"] == transactions["lock"]["txid"],
                                "Wrong relay acknowledgement state")
                        if expected_state == "submitted":
                            require(outcome["receipt_persisted"] == (mode != "punish"),
                                    "Relay receipt persistence was misreported")
                        require((reservation_dir / f"{trade}.relay").is_file(), "Relay intent was not retained")
                        require(proxy.counts.get("sendrawtransaction") == 1, "Relay did not issue exactly one send")
                        replay = wallet_call(proxy_request)["outcome"]
                        require(replay["state"] == "reconcile" and proxy.counts.get("sendrawtransaction") == 1,
                                "Existing relay intent caused duplicate send")
                        node.stop()
                        node.start()
                        if "fixture" not in node.rpc("listwallets"):
                            node.rpc("loadwallet", "fixture")
                        replay = wallet_call(proxy_request)["outcome"]
                        require(replay["state"] == "reconcile" and proxy.counts.get("sendrawtransaction") == 1,
                                "Restart caused duplicate funding send")
                        original_intent = (reservation_dir / f"{trade}.relay").read_bytes()
                        recovered = wallet_call(dict(proxy_request, action="resumeRelay"))["outcome"]
                        if mode == "refund":
                            require(recovered["state"] == "submitted" and proxy.counts.get("sendrawtransaction") == 2,
                                    "Explicit recovery did not resend the original unsent lock")
                            require(proxy.counts.get("lockunspent", 0) >= 1,
                                    "Unsent relay recovery did not restore wallet locks")
                        else:
                            require(recovered["state"] == "observed" and recovered["observation"]["state"] == "mempool"
                                    and proxy.counts.get("sendrawtransaction") == 1,
                                    "Accepted funding was not reconciled without a second send")
                        require(proxy.forwarded.get("sendrawtransaction") == 1
                                and (reservation_dir / f"{trade}.relay").read_bytes() == original_intent,
                                "Relay recovery changed intent or duplicated an upstream send")
                        lock_txid = outcome["txid"]
                else:
                    lock_txid = node.rpc("sendrawtransaction", finalized["hex"])
                require(lock_txid == transactions["lock"]["txid"], "Lock transaction ID changed while signing")
                if wallet_binary:
                    wallet_call(dict(wallet_request, action="resume", trade=trade), reject=True)
                    funding_observation("mempool")
                    live_request = dict(wallet_request, action="liveLock",
                                        observe=f"{lock_txid}:{transactions['lock']['vout']}")
                    wallet_call(live_request, reject=True)
                lock_block = node.mine(1, mining)[0]
                if wallet_binary:
                    require(funding_observation("confirmed")["block"] == lock_block,
                            "Funding block identity mismatch")
                    node.rpc("invalidateblock", lock_block)
                    funding_observation("mempool")
                    wallet_call(live_request, reject=True)
                    lock_miner = node.rpc("getnewaddress", "", "bech32", wallet="fixture")
                    lock_block = node.mine(1, lock_miner)[0]
                    require(funding_observation("confirmed")["block"] == lock_block,
                            "Reorged funding failed to reconfirm")
                    if mode == "redeem":
                        proof = node.rpc("gettxoutproof", [lock_txid], lock_block)
                        node.rpc("removeprunedfunds", lock_txid, wallet="fixture")
                        funding_observation("confirmedOutput")
                        node.rpc("importprunedfunds", finalized["hex"], proof, wallet="fixture")
                        funding_observation("confirmed")
                    outpoint = f"{lock_txid}:{transactions['lock']['vout']}"
                    wallet_call(dict(wallet_request, action="resume", trade=trade), reject=True)
                    observed_lock = wallet_call(dict(wallet_request, observe=outpoint))["output"]
                    require(observed_lock["amountAtoms"] == str(PRINCIPAL)
                            and observed_lock["scriptPubKey"] == description["scriptPubKey"]
                            and observed_lock["confirmations"] >= 1, "Wallet adapter lock observation mismatch")
                    live = wallet_call(live_request)
                    require(live["confirmations"] == observed_lock["confirmations"] and live["tip"] == observed_lock["tip"],
                            "Live lock evidence differs from confirmed funding")
                if mode in ("redeem", "earlyRefund"):
                    raw, txid, expected = transactions[mode]["rawHex"], transactions[mode]["txid"], PRINCIPAL-FEE
                    script = redeem_script if mode == "redeem" else refund_script
                else:
                    early = node.rpc("testmempoolaccept", [transactions["cancel"]["rawHex"]])[0]
                    require(early.get("allowed") is False and early.get("reject-reason") == "non-BIP68-final",
                            "Immature cancellation was not rejected for its relative timelock")
                    node.mine(CANCEL_BLOCKS-2, mining)
                    if wallet_binary:
                        wallet_call(live_request, reject=True)
                    early = node.rpc("testmempoolaccept", [transactions["cancel"]["rawHex"]])[0]
                    require(early.get("allowed") is False and early.get("reject-reason") == "non-BIP68-final",
                            "Cancellation became valid one block too early")
                    node.mine(1, mining)
                    require(node.rpc("sendrawtransaction", transactions["cancel"]["rawHex"]) == transactions["cancel"]["txid"],
                            "Cancellation transaction ID mismatch")
                    cancel_block = node.mine(1, mining)[0]
                    cancellation = node.rpc("getrawtransaction", transactions["cancel"]["txid"], True)
                    require(cancellation["hex"] == transactions["cancel"]["rawHex"] and cancellation["confirmations"] >= 1,
                            "Cancellation witness or confirmation mismatch")
                    if mode == "punish":
                        for advance in (0, PUNISH_BLOCKS-2):
                            if advance:
                                node.mine(advance, mining)
                            early = node.rpc("testmempoolaccept", [transactions["punish"]["rawHex"]])[0]
                            require(early.get("allowed") is False and early.get("reject-reason") == "non-BIP68-final",
                                    "Punishment accepted before its relative timelock")
                        node.mine(1, mining)
                    raw, txid, expected = transactions[mode]["rawHex"], transactions[mode]["txid"], PRINCIPAL-2*FEE
                    script = redeem_script if mode == "punish" else refund_script
                if mode == "redeem" and redeem_binary:
                    def submit_redemption(port, tag, reject=False):
                        request_path, response_path = root / (tag+"-request.json"), root / (tag+"-response.json")
                        write_private(request_path, {"port":port,"anchor":redeem_address,"transaction":raw})
                        env = dict(os.environ, MONZERO_LTC_REDEEM_REQUEST=str(request_path),
                            MONZERO_LTC_REDEEM_RESPONSE=str(response_path),
                            MONZERO_LTC_REDEEM_COOKIE=str(node_dir / "regtest/.cookie"))
                        result = subprocess.run([str(redeem_binary), "--exact",
                            "litecoin_rpc::redeem_submission::tests::actual_completed_redeem_transport", "--ignored"],
                            env=env, capture_output=True, timeout=60)
                        if reject:
                            require(result.returncode != 0 and not response_path.exists(), "Repeated redemption unexpectedly accepted")
                            return
                        require(result.returncode == 0 and response_path.exists(), "Redemption transport fixture failed")
                        value = json.loads(response_path.read_text())
                        require(value["schema"] == "monzero-ltc-redeem-transport-fixture-v1"
                            and value["txid"] == txid and value["executable"] is False, "Wrong redemption transport result")
                        return value
                    with RelayProxy(node.port, drop_request=True) as proxy:
                        require(submit_redemption(proxy.port, "redeem-dropped-request")["acknowledged"] is False,
                                "Dropped redemption request claimed acknowledgement")
                        require(proxy.counts.get("sendrawtransaction") == 1 and not proxy.forwarded.get("sendrawtransaction"),
                                "Dropped request was not exercised")
                    require(txid not in node.rpc("getrawmempool"), "Dropped redemption reached the node")
                    with RelayProxy(node.port, drop_reply=True) as proxy:
                        require(submit_redemption(proxy.port, "redeem-dropped-reply")["acknowledged"] is False,
                                "Lost redemption reply claimed acknowledgement")
                        require(proxy.forwarded.get("sendrawtransaction") == 1, "Redemption was not forwarded exactly once")
                    require(txid in node.rpc("getrawmempool"), "Uncertain redemption absent from node mempool")
                    require(node.rpc("getrawtransaction", txid) == raw, "Retried redemption changed signed bytes")
                    submit_redemption(node.port, "redeem-already-pooled", reject=True)
                else:
                    require(node.rpc("sendrawtransaction", raw) == txid, "Settlement transaction ID mismatch")
                if wallet_binary and mode in ("redeem", "earlyRefund"):
                    require(wallet_call(dict(wallet_request, observe=outpoint))["output"] is not None,
                            "Mempool spend changed confirmed-chain observation")
                    wallet_call(live_request, reject=True)
                block = node.mine(1, mining)[0]
                observed = node.rpc("getrawtransaction", txid, True)
                require(observed["hex"] == raw, "Confirmed witness differs from engine transaction")
                require(observed["confirmations"] >= 1 and len(observed["vout"]) == 1,
                        "Settlement transaction is not confirmed")
                output = observed["vout"][0]
                require(atoms(output["value"]) == expected and output["scriptPubKey"]["hex"] == script,
                        "Settlement destination or exact amount mismatch")
                alternatives = ("redeem", "earlyRefund", "cancel") if mode in ("redeem", "earlyRefund") else ("refund", "punish", "redeem", "earlyRefund")
                for other in alternatives:
                    if other != mode:
                        rejection = node.rpc("testmempoolaccept", [transactions[other]["rawHex"]])[0]
                        require(rejection.get("allowed") is False
                                and rejection.get("reject-reason") == "missing-inputs",
                                f"Competing {other} after {mode}: {str(rejection.get('reject-reason'))[:120]}")
                if wallet_binary:
                    require(wallet_call(dict(wallet_request, observe=outpoint))["output"] is None,
                            "Wallet adapter retained spent lock output")
                    funding_observation("confirmed")
                before = node.rpc("getbestblockhash")
                node.stop()
                node.start()
                if "fixture" not in node.rpc("listwallets"):
                    node.rpc("loadwallet", "fixture")
                require(node.rpc("getbestblockhash") == before, "Chain did not survive daemon restart")
                require(node.rpc("getrawtransaction", txid, True)["confirmations"] >= 1,
                        "Settlement did not survive daemon restart")
                if wallet_binary:
                    funding_observation("confirmed")
                node.rpc("invalidateblock", block)
                require(node.rpc("getrawtransaction", txid, True).get("confirmations", 0) == 0,
                        "Reorg did not remove confirmation evidence")
                if wallet_binary and mode in ("redeem", "earlyRefund"):
                    require(wallet_call(dict(wallet_request, observe=outpoint))["output"] is not None,
                            "Reorg failed to restore confirmed-chain lock observation")
                # A new coinbase destination prevents reproducing the identical
                # block hash that invalidateblock has explicitly marked invalid.
                replacement_miner = node.rpc("getnewaddress", "", "bech32", wallet="fixture")
                replacement = node.mine(1, replacement_miner)[0]
                require(replacement != block, "Reorg replacement reused invalidated block")
                require(node.rpc("getrawtransaction", txid, True)["confirmations"] >= 1,
                        "Settlement did not reconfirm")
                if mode == "punish":
                    # Removing the cancellation anchor also removes punishment.
                    # Reconfirm cancel first, then prove its relative lock resets.
                    node.rpc("invalidateblock", cancel_block)
                    require(node.rpc("getrawtransaction", txid, True).get("confirmations", 0) == 0,
                            "Cancellation reorg retained punishment confirmation")
                    reanchor_miner = node.rpc("getnewaddress", "", "bech32", wallet="fixture")
                    node.mine(1, reanchor_miner)
                    require(node.rpc("getrawtransaction", transactions["cancel"]["txid"], True)["confirmations"] == 1,
                            "Cancellation did not reconfirm at the new anchor")
                    for advance in (0, PUNISH_BLOCKS-2):
                        if advance:
                            node.mine(advance, reanchor_miner)
                        immature = node.rpc("testmempoolaccept", [raw])[0]
                        require(immature.get("allowed") is False
                                and immature.get("reject-reason") == "non-BIP68-final",
                                "Reanchored punishment did not regain its relative timelock")
                    node.mine(1, reanchor_miner)
                    require(node.rpc("sendrawtransaction", raw) == txid,
                            "Reanchored punishment transaction changed")
                    node.mine(1, reanchor_miner)
                    require(node.rpc("getrawtransaction", txid, True)["confirmations"] >= 1,
                            "Reanchored punishment did not confirm")
                outcomes[mode] = {"txid": txid, "receivedAtoms": str(expected), "restartVerified": True,
                                  "reorgVerified": True}
                print(f"PASS: Litecoin {mode}, exact output, daemon restart and reorg/reconfirmation", flush=True)
            if wallet_binary:
                # Spend reserved inputs to a different destination before the
                # intended lock is ever broadcast. Input absence is not funding.
                conflict_trade = str(uuid.uuid4())
                unused = wallet_call(dict(wallet_request, action="reserve", trade=conflict_trade))
                decoded = node.rpc("decodepsbt", unused["psbt"])
                inputs = [{"txid":i["txid"], "vout":i["vout"]} for i in decoded["tx"]["vin"]]
                total = sum(atoms(i["witness_utxo"]["amount"]) for i in decoded["inputs"])
                competing = node.rpc("createrawtransaction", inputs, {mining: str(decimal.Decimal(total-FEE)/ATOMS)})
                competing = node.rpc("signrawtransactionwithwallet", competing, wallet="fixture")
                require(competing["complete"], "Competing fixture signing incomplete")
                node.rpc("sendrawtransaction", competing["hex"])
                for mine in (False, True):
                    if mine:
                        node.mine(1, mining)
                    observed = wallet_call(dict(wallet_request, action="observeLock", trade=conflict_trade))["observation"]
                    require(observed["state"] == "unobserved" and observed["inputs_available"] is False,
                            "Competing input spend was misidentified as our funding")
                print("PASS: unrelated reserved-input spend does not establish funding", flush=True)
            summary = {"schema": "monzero-ltc-contract-results-v3", "chain": "ltc-regtest", "genesis": GENESIS,
                       "engineFixture": SCHEMA, "outcomes": outcomes, "executable": False,
                       "fullCrossChainSettlement": False, "mwebActive": False,
                       "walletAdapterVerified": wallet_binary is not None,
                       "reservationRecoveryVerified": wallet_binary is not None,
                       "fundingReconciliationVerified": wallet_binary is not None,
                       "durableSigningVerified": wallet_binary is not None,
                       "durableRelayVerified": wallet_binary is not None,
                       "signedQuoteBindingVerified": wallet_binary is not None,
                       "negotiatedKeyBindingVerified": wallet_binary is not None,
                       "ownedExecutionVerified": wallet_binary is not None,
                       "durablePeerRecoveryVerified": wallet_binary is not None,
                       "liveLockSafetyVerified": wallet_binary is not None}
            summary["redemptionTransportUncertaintyVerified"] = bool(redeem_binary)
            summary["fullCrossChainSettlement"] = False
            if report:
                write_private(report, summary)
            print("PASS: Litecoin contract primitives accepted; not yet a negotiated LTC/XMZ swap", flush=True)
        finally:
            node.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--litecoind", required=True, type=pathlib.Path)
    parser.add_argument("--engine-test-binary", required=True, type=pathlib.Path)
    parser.add_argument("--temp-parent", required=True, type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--wallet-test-binary", type=pathlib.Path)
    parser.add_argument("--redeem-transport-test-binary", type=pathlib.Path)
    args = parser.parse_args()
    run(args.litecoind.resolve(strict=True), args.engine_test_binary.resolve(strict=True),
        args.temp_parent.resolve(strict=True), args.report,
        args.wallet_test_binary.resolve(strict=True) if args.wallet_test_binary else None,
        args.redeem_transport_test_binary.resolve(strict=True) if args.redeem_transport_test_binary else None)
