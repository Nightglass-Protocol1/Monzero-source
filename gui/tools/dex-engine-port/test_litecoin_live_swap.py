#!/usr/bin/env python3
"""LTC/XMZ or BTC/XMZ swap between two resident app engines on chains that advance by themselves.

--chain selects Litecoin (default) or Bitcoin; both run the same contract paths.
Stages:
  testnet  caller-run Litecoin testnet / Bitcoin testnet4 or signet node and XMZ testnet daemon (testnet coins only).
           The trade start is gated by a signed control document, as in the app.
  regtest  owned Litecoin regtest and offline XMZ fakechain, advanced by a background
           block clock, so every wait is a real wait (dry run of the testnet driver).

Blocks are never mined on demand, invalidated or popped here. The adversarial chain
checks (reorgs, premature spends, wrong sources) stay in test_litecoin_paired_chain.py.
Testnet trade folders are kept after the run: they hold the keys that recover coins.
"""
import argparse
import base64
import decimal
import hashlib
import json
import os
import pathlib
import secrets
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
import uuid
import requests
from nacl.signing import SigningKey
from test_litecoin_settlement import Node, NoRedirect, RpcError, write_private, require, atoms
from test_wallet_recovery import GENESIS as FAKECHAIN_GENESIS, port, ready, rpc
from test_litecoin_paired_chain import install_identity, TEST
from litecoin_app_fixture import LitecoinAppFixture, SplitLitecoinApp
from swap_chain import profile as chain_profile, regtest_node

KEEPALIVE_SECONDS = 240  # The resident app waits at most 600 s for its next command.


class LtcRpc:
    """Thread-safe cookie-authenticated Litecoin JSON-RPC (loopback only)."""
    def __init__(self, port_number, cookie):
        self.port, self.cookie = port_number, pathlib.Path(cookie)
        self.lock, self.request_id = threading.Lock(), 0
        self.transport = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    # Reads a busy node may drop (empty reply when its RPC work queue is full) are
    # retried; anything that changes node or wallet state is never repeated.
    READ_ONLY = frozenset({"getbalance","getblock","getblockchaininfo","getblockcount","getblockhash","getblockheader",
        "getrawmempool","getrawtransaction","gettransaction","listwallets","validateaddress"})

    def __call__(self, method, *params, wallet=None):
        for attempt in range(6 if method in self.READ_ONLY else 1):
            try:
                return self.call_once(method, *params, wallet=wallet)
            except (ValueError, urllib.error.URLError, ConnectionError, TimeoutError) as error:
                if method not in self.READ_ONLY or attempt == 5:
                    raise
                time.sleep(2 * (attempt + 1))

    def call_once(self, method, *params, wallet=None):
        with self.lock:
            self.request_id += 1
            request_id = self.request_id
        endpoint = f"http://127.0.0.1:{self.port}" + (f"/wallet/{wallet}" if wallet else "")
        request = urllib.request.Request(endpoint, data=json.dumps({"jsonrpc":"1.0","id":request_id,
            "method":method,"params":params}).encode(), headers={"Content-Type":"application/json",
            "Authorization":"Basic "+base64.b64encode(self.cookie.read_bytes().strip()).decode()})
        try:
            response = self.transport.open(request, timeout=60)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read(4_000_001)
        require(len(raw) <= 4_000_000, "Oversized Litecoin RPC response")
        value = json.loads(raw, parse_float=decimal.Decimal)
        require(value.get("id") == request_id, "Litecoin RPC response identity mismatch")
        if value.get("error"):
            raise RpcError(value["error"].get("code"), value["error"].get("message"))
        return value["result"]


class BlockClock(threading.Thread):
    """Regtest stage only: one block per interval on each chain, independent of the trade."""
    def __init__(self, ltc, ltc_address, xmz_port, xmz_address, ltc_seconds, xmz_seconds):
        super().__init__(daemon=True)
        self.ltc, self.ltc_address, self.xmz_port, self.xmz_address = ltc, ltc_address, xmz_port, xmz_address
        self.intervals = {"ltc":ltc_seconds, "xmz":xmz_seconds}
        self.stopped, self.error = threading.Event(), None

    def run(self):
        due = {name:time.monotonic()+interval for name, interval in self.intervals.items()}
        try:
            while not self.stopped.wait(0.5):
                now = time.monotonic()
                if now >= due["ltc"]:
                    self.ltc("generatetoaddress", 1, self.ltc_address); due["ltc"] = now+self.intervals["ltc"]
                if now >= due["xmz"]:
                    rpc(self.xmz_port, "generateblocks", {"wallet_address":self.xmz_address,"amount_of_blocks":1})
                    due["xmz"] = now+self.intervals["xmz"]
        except Exception as error:  # Surfaced by the driver's next wait.
            self.error = error


class LiveElectrumPeer:
    """Electrum view of a caller-run node for the local light wallet (testnet stage).

    The regtest peer in test_light_wallet_chain scans the whole chain on every
    history request; on public testnet only blocks from start_height (taken just
    before the light wallet is created) can hold its coins, so only those are scanned.

    Broadcast fault hooks keep the regtest peer's contract (accepted_broadcast_digests,
    hold_next_broadcast_reply with broadcast_held/release_broadcast_reply,
    drop_next_broadcast_reply, dropped_broadcast_replies). Connections are served
    concurrently here, so an armed hook is taken under a lock by exactly one
    broadcast, and only after sendrawtransaction returned (node acceptance).
    """
    def __new__(cls, ltc, fixture, workdir, start_height):
        import test_light_wallet_chain as chain

        class Node:
            rpc = staticmethod(ltc)
            def header(self, height):
                return ltc("getblockheader", ltc("getblockhash", height), False)

        class Peer(chain.ElectrumPeer):
            def __init__(self, *args):
                super().__init__(*args)
                self.broadcast_lock = threading.Lock()
                self.broadcast_hold_seconds = 180  # as the regtest peer's barrier
                self.held_broadcast_digests = []
                self.holding = 0

            # Dishonest-server faults (--light-seller-dishonest): "lag" withholds
            # the newest block; hidden txids are left out of every history.
            fault = None
            hidden = frozenset()

            def answer(self, method, params):
                if self.fault == "lag" and method in ("blockchain.headers.subscribe", "blockchain.block.headers"):
                    tip = ltc("getblockcount")-1
                    if method == "blockchain.headers.subscribe":
                        return {"height":tip, "hex":self.node.header(tip)}
                    start, count = params
                    headers = [self.node.header(h) for h in range(start, min(tip, start+count-1)+1)]
                    return {"count":len(headers), "hex":"".join(headers), "max":2016}
                result = super().answer(method, params)
                if method == "blockchain.scripthash.get_history" and self.hidden:
                    result = [entry for entry in result if entry["tx_hash"] not in self.hidden]
                return result

            def hold_next_broadcast(self):
                """Arm one hold with fresh barrier events (one hold at a time)."""
                with self.broadcast_lock:
                    require(not self.hold_next_broadcast_reply and not self.holding, "A broadcast hold is already armed")
                    self.broadcast_held.clear(); self.release_broadcast_reply.clear()
                    self.hold_next_broadcast_reply = True

            def drop_next_broadcast(self):
                with self.broadcast_lock:
                    self.drop_next_broadcast_reply = True

            def serve(self):
                # One thread per connection: a helper that leaves a connection open
                # must not make the next helper wait (the regtest peer is sequential).
                while not self.stop.is_set():
                    try:
                        connection, _ = self.listener.accept()
                    except socket.timeout:
                        continue
                    except OSError:
                        return
                    threading.Thread(target=self.handle, args=(connection,), daemon=True).start()

            def handle(self, connection):
                try:
                    connection.settimeout(120)
                    with self.context.wrap_socket(connection, server_side=True) as tls, tls.makefile("rwb") as stream:
                        while not self.stop.is_set():
                            line = stream.readline(1 << 20)
                            if not line:
                                return
                            request = json.loads(line); self.requests.append(request["method"])
                            result = self.answer(request["method"], request["params"])
                            if request["method"] == "blockchain.transaction.broadcast" \
                                    and not self.deliver_broadcast_reply(request["params"][0]):
                                return  # the node accepted it; this connection loses the reply
                            stream.write(json.dumps({"jsonrpc":"2.0","id":request["id"],"result":result}, default=str).encode()+b"\n")
                            stream.flush()
                except (ssl.SSLError, ConnectionError, socket.timeout):
                    pass  # the helper went away; it treats the outcome as unknown
                except Exception as error:  # noqa: BLE001 — reported in the evidence
                    self.errors.append(repr(error))

            def deliver_broadcast_reply(self, raw):
                """Runs only after sendrawtransaction returned; False suppresses the reply.
                A held reply waits on its own connection thread, so others are served."""
                digest = hashlib.sha256(bytes.fromhex(raw)).hexdigest()
                with self.broadcast_lock:
                    self.accepted_broadcast_digests.append(digest)
                    hold, drop = self.hold_next_broadcast_reply, False
                    if hold:
                        self.hold_next_broadcast_reply = False
                        self.held_broadcast_digests.append(digest); self.holding += 1
                    elif self.drop_next_broadcast_reply:
                        self.drop_next_broadcast_reply = False
                        self.dropped_broadcast_replies += 1; drop = True
                if not hold:
                    return not drop
                self.broadcast_held.set()
                released = self.release_broadcast_reply.wait(self.broadcast_hold_seconds)
                with self.broadcast_lock:
                    self.holding -= 1
                    if released:
                        self.dropped_broadcast_replies += 1
                if not released:
                    raise RuntimeError("Owned broadcast barrier timed out")
                return False

            def transactions(self):
                tip = ltc("getblockcount")
                for height in range(start_height, tip+1):
                    block = ltc("getblockhash", height)
                    if block not in self.blocks:
                        self.blocks[block] = ltc("getblock", block, 1)["tx"]
                    for txid in self.blocks[block]:
                        yield height, self.decoded(txid)
                for txid in ltc("getrawmempool"):
                    yield 0, self.decoded(txid)

        return Peer(Node(), fixture, workdir)


def light_helper_call(args, configuration, mode, **fields):
    request = dict(schema=f"monzero-{args.chain}-light-{mode}-request-v1", requestId=str(uuid.uuid4()), **fields)
    result = subprocess.run([str(args.light_helper), "--"+mode+"-stdio-v1", str(configuration)],
        input=json.dumps(request).encode(), capture_output=True, timeout=600,
        env=dict(os.environ, SSL_CERT_FILE=str(args.light_wallet_tls/"root.pem")))
    require(result.returncode == 0, "Light wallet helper failed: "+mode)
    response = json.loads(result.stdout)
    require(response["requestId"] == request["requestId"] and response["executable"] is False,
        "Light wallet helper response context mismatch")
    return response


def xmz_transaction(daemon_port, txid):
    with requests.Session() as client:
        client.trust_env = False
        response = client.post(f"http://127.0.0.1:{daemon_port}/get_transactions",
            json={"txs_hashes":[txid]}, timeout=30, allow_redirects=False)
    require(response.status_code == 200 and len(response.content) <= 1_000_000, "XMZ transaction lookup failed")
    value = response.json()
    found = value.get("txs") or []
    return found[0] if found else None


def stop_processes(processes):
    """Attempt every owned process even when a remote transport fails."""
    failures = []
    for process in reversed(processes):
        try:
            if process.poll() is not None:
                continue
            if getattr(process, "monzero_owned_group", False):
                os.killpg(process.pid, signal.SIGTERM)
            else:
                process.terminate()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
        except Exception as error:
            failures.append(type(process).__name__ + ":" + type(error).__name__)
    return failures


def read_light_funding(resident, operation, maximum_attempts=3):
    require(operation in ("observe", "send-review"), "Only read operations may be retried")
    for _ in range(maximum_attempts):
        state = resident.call("bob", "light-"+operation)["lightFunding"]
        if state["state"] == "complete":
            return state
        require(state["state"] == "outcome-unconfirmed", "Unexpected light funding read state")
    raise RuntimeError("Light funding read remains uncertain: "+operation)


def reconciled_light_review(resident, txid, note, maximum_attempts=3):
    for attempt in range(maximum_attempts):
        observed = read_light_funding(resident, "observe")
        require(observed["result"]["txid"] == txid, "Could not reconcile retained light funding")
        if observed["result"].get("observation", {}).get("position") == "included":
            note("light-funding-confirmed-after-uncertain-send", txid=txid)
            return None, None, observed["result"]
        status = observed["result"]["broadcastStatus"]
        require(status in ("no-local-attempt", "outcome-unknown", "server-acknowledged"),
            "Unknown retained broadcast status")
        operation = "send" if status == "no-local-attempt" else "retry"
        note("light-funding-reconciled", txid=txid, broadcastStatus=status, attempt=attempt+1)
        state = resident.call("bob", "light-"+operation+"-review")["lightFunding"]
        if state["state"] == "outcome-unconfirmed":
            continue  # Re-observe rather than assuming the failed review is current.
        require(state["state"] == "complete" and state["review"], "Fresh funding review unavailable")
        return operation, state["review"], None
    raise RuntimeError("Fresh funding review remains uncertain after bounded reconciliation")


def send_retained_light_funding(resident, txid, review, note, maximum_attempts=3):
    """Reconcile uncertain results and obtain fresh consent for the same signed bytes.

    The fixture acts as the test user. No signing operation or replacement
    transaction is allowed here, and an absent network report is not treated
    as proof that broadcast never occurred.
    """
    original_digest = review["details"]["signedDigest"]
    operation = "send"
    for attempt in range(maximum_attempts):
        require(review["details"]["txid"] == txid and review["details"]["signedDigest"] == original_digest,
            "Fresh funding review changed signed transaction bytes")
        require(review["details"]["action"] == operation, "Funding review action changed")
        state = resident.call("bob", "light-"+operation, commitment=review["commitment"])["lightFunding"]
        if state["state"] == "complete":
            require(state["result"]["txid"] == txid, "Funding submission changed transaction")
            return state["result"]
        require(state["state"] == "outcome-unconfirmed", "Unexpected light funding submission state")
        if attempt+1 == maximum_attempts:
            observed = read_light_funding(resident, "observe")
            require(observed["result"]["txid"] == txid, "Reconciliation changed transaction")
            if observed["result"].get("observation", {}).get("position") == "included":
                return observed["result"]
            break
        operation, review, included = reconciled_light_review(resident, txid, note)
        if included is not None:
            return included
    raise RuntimeError("Light funding remains unconfirmed after bounded fresh approvals")


def retry_or_confirm_light_funding(resident, txid, original_digest, note, maximum_attempts=3):
    """Retry with fresh consent while pending; inclusion closes the retry window."""
    for attempt in range(maximum_attempts):
        observed = read_light_funding(resident, "observe")
        require(observed["state"] == "complete" and observed["result"]["txid"] == txid,
            "Could not observe retained funding for retry")
        if observed["result"]["observation"]["position"] == "included":
            note("light-funding-already-included", txid=txid)
            return False
        state = resident.call("bob", "light-retry-review")["lightFunding"]
        if state["state"] == "outcome-unconfirmed":
            continue  # A block may have arrived during review; observe again.
        require(state["state"] == "complete" and state["review"], "Funding retry review unavailable")
        review = state["review"]
        require(review["details"]["action"] == "retry" and review["details"]["txid"] == txid
            and review["details"]["signedDigest"] == original_digest, "Retry review changed signed transaction bytes")
        state = resident.call("bob", "light-retry", commitment=review["commitment"])["lightFunding"]
        if state["state"] == "complete":
            require(state["result"]["txid"] == txid, "Retry changed transaction")
            return True
        require(state["state"] == "outcome-unconfirmed", "Unexpected light funding retry state")
        note("light-funding-retry-needs-reconciliation", txid=txid, attempt=attempt+1)
    raise RuntimeError("Light funding retry remains uncertain after bounded fresh reviews")


def recovered_native_balance(wallet, vm_native, role):
    if vm_native and role == "bob":
        return vm_native.claim_balance()
    wallet("refresh", claim=True)
    return wallet("get_balance", claim=True)["balance"]


def run(args):
    if args.remote_host:
        from vm_lab_lease import LabVmLease
        with LabVmLease(exclusive=args.vm_bob_restart_stage == "powerloss-signed") as lease:
            return _run(args, lease)
    return _run(args)


def _run(args, vm_lease=None):
    require(not args.report.exists(), "Report already exists")
    testnet = args.stage == "testnet"
    require(args.btc_test_network == "testnet4" or (args.chain == "btc" and args.stage == "testnet"),
        "--btc-test-network applies to the Bitcoin testnet stage only")
    chain = chain_profile(args.chain, "signet" if args.btc_test_network == "signet" else args.stage)
    if testnet:
        require(args.ltc_rpc_port and args.ltc_cookie and args.xmz_daemon_port and args.xmz_funder_wallet_port,
            "Testnet stage needs the caller-run node ports and cookie")
    else:
        require(args.litecoind and args.daemon, "Regtest stage needs the owned node binaries (--litecoind or --bitcoind)")
    require(not args.light_helper or args.light_wallet_tls, "Light wallet funding needs --light-wallet-tls")
    require(not args.light_seller or args.light_wallet_tls, "A light-wallet seller needs --light-wallet-tls")
    if args.light_wallet_tls:
        # The fixture's server certificate lasts 7 days (generate_litecoin_tls_fixture.py);
        # an expired one otherwise shows up only as a stalled light-wallet step.
        from cryptography import x509
        expires = x509.load_der_x509_certificate((args.light_wallet_tls/"server.der").read_bytes()).not_valid_after_utc.timestamp()
        require(expires > time.time()+args.trade_timeout, "The light-wallet TLS fixture expires before this trade "
            "could finish; regenerate it with generate_litecoin_tls_fixture.py")
    require(not args.light_seller_dishonest or (args.light_seller and args.mode == "success"),
        "--light-seller-dishonest needs --light-seller and --mode success")
    require(not args.remote_wallet_rpc or args.remote_host, "VM wallets require --remote-host")
    require(not args.remote_light_helper or (args.remote_host and args.light_helper),
        "VM light helper requires remote trader and local reference helper")
    require(not args.vm_bob_restart_stage or (args.remote_host and args.remote_light_helper and args.remote_wallet_rpc),
        "VM restart test requires VM-owned light and native wallets")
    if args.remote_host:
        require(not args.light_helper or args.remote_light_helper, "VM light wallet requires --remote-light-helper")
        require(args.kit_config and args.remote_binary and args.remote_helper,
            "VM trader requires --kit-config, --remote-binary and --remote-helper")
        require(not args.listen_address and not args.peer_address,
            "First VM fixture uses loopback SSH tunnels; omit address overrides")
    require(not args.hand_over_gui or (args.kit_config and args.split_processes and not args.remote_host and testnet
        and args.control_seed_file), "Hand-over needs --kit-config --split-processes, a testnet stage and --control-seed-file")
    if args.vm_bob_restart_stage == 'powerloss-signed':
        from recovery_acceptance_storage import require_recovery_storage
        args.temp_parent=require_recovery_storage(args.temp_parent,args.keep)
    root = pathlib.Path(tempfile.mkdtemp(prefix=f"ltc-xmz-live-{args.stage}-", dir=args.temp_parent)).resolve()
    print("Trade workspace: "+str(root), flush=True)
    processes, owned_ltc, clock, resident, light_peer = [], None, None, None, None
    seller_peers = []
    remote = vm_native = vm_light = None
    evidence = {"stage":args.stage,"mode":args.mode,"workspace":str(root),"timeline":[],"lightSeller":args.light_seller}
    started = time.monotonic()

    def note(event, **fields):
        entry = dict(event=event, elapsedSeconds=round(time.monotonic()-started), unix=int(time.time()), **fields)
        evidence["timeline"].append(entry)
        print(json.dumps(entry, sort_keys=True), flush=True)

    passed = handed_over = False
    try:
        if args.remote_host:
            from remote_trade_workspace import RemoteTradeWorkspace
            remote = RemoteTradeWorkspace(args.remote_host, root, args.remote_binary, args.remote_helper, processes)
            remote_digest = remote.run(["python3", "-c",
                "import hashlib,pathlib,sys; print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())",
                remote.helper]).decode().strip()
            require(remote_digest == hashlib.sha256(args.journal_helper.read_bytes()).hexdigest(),
                "VM journal helper differs from the locally verified helper")
            # A matching hash does not establish CPU/loader compatibility. An
            # argument-free invocation cannot touch a journal or send funds.
            probe = json.loads(remote.run(["python3", "-c",
                "import json,subprocess,sys; p=subprocess.run([sys.argv[1]],input=b'',capture_output=True,env={'LANG':'C'},timeout=15); "
                "print(json.dumps({'code':p.returncode,'error':p.stderr.decode(errors='replace')}))", remote.helper]))
            require(probe == {"code":1,"error":"Litecoin request failed; refresh the saved journal and reconcile before retrying\n"},
                "VM helper cannot execute correctly (CPU/loader preflight): " + str(probe))
            evidence.update(remoteTrader="bob", remoteHost=args.remote_host, remoteWorkspace=str(remote.root),
                remoteHelperSha256=remote_digest,
                rpcServices="host test nodes over loopback SSH tunnels")
        if remote and args.remote_light_helper:
            from remote_light_wallet import RemoteLightWallet
            vm_light = RemoteLightWallet(remote, args.remote_light_helper, args.light_helper,
                args.light_wallet_tls, chain["light"], coin=args.chain)
            vm_light.install()  # verify CPU/loader and digest before chain funding
        # ---- Litecoin service ----
        if testnet:
            ltc = LtcRpc(args.ltc_rpc_port, args.ltc_cookie)
            ltc_cookie = pathlib.Path(args.ltc_cookie)
            info = ltc("getblockchaininfo")
            require(info["chain"] == chain["core_chain"] and ltc("getblockhash", 0) == chain["genesis"]
                and not info["initialblockdownload"] and info["blocks"] == info["headers"],
                chain["coin"]+" test-network node is not synchronized")
            if "fixture" not in ltc("listwallets"):
                try:
                    ltc("loadwallet", "fixture")
                except RpcError:
                    ltc("createwallet", "fixture")
        else:
            (root/"ltc").mkdir(mode=0o700)
            owned_ltc = regtest_node(args.chain, args.litecoind, root/"ltc")
            owned_ltc.start()
            ltc_cookie = root/"ltc/regtest/.cookie"
            ltc = LtcRpc(owned_ltc.port, ltc_cookie)
            ltc("createwallet", "fixture")
        ltc_port = ltc.port
        anchor = ltc("getnewaddress", "", "bech32", wallet="fixture")
        if not testnet:
            ltc("generatetoaddress", 440, anchor)  # Setup coins only; the trade waits on the clock.
        ltc_payout = ltc("getnewaddress", "", "bech32", wallet="fixture")
        ltc_refund = ltc("getnewaddress", "", "bech32", wallet="fixture")
        ltc_atoms = args.ltc_atoms
        spendable = atoms(ltc("getbalance", wallet="fixture"))
        need = ltc_atoms + (args.light_funding_margin + 20000 if args.light_helper else 100000)
        if args.light_helper:
            # Light selection takes no change or change >= minimum_ltc_output (100000) after the 1000 lock fee.
            change = args.light_funding_margin - 1000
            require(change == 0 or change >= 100000,
                f"--light-funding-margin {args.light_funding_margin} leaves {change} change; use 1000 or >= 101000")
        require(spendable >= need, f"{chain['coin']} fixture wallet holds {spendable} atoms; needs {need}")
        note("litecoin-ready", height=ltc("getblockcount"), spendableAtoms=str(spendable))

        # ---- XMZ service and wallets ----
        source_port, claim_port, observer_port = port(), port(), port()
        if testnet:
            daemon_port = args.xmz_daemon_port
            info = rpc(daemon_port, "get_info")
            require(info["nettype"] == "testnet" and info["synchronized"], "XMZ daemon is not a synchronized testnet")
            xmz_genesis = rpc(daemon_port, "get_block_header_by_height", {"height":0})["block_header"]["hash"]
        else:
            daemon_port, p2p_port = port(), port()
            daemon = subprocess.Popen([str(args.daemon), "--regtest", "--keep-fakechain", "--fixed-difficulty", "1",
                "--offline", "--no-igd", "--hide-my-port", "--no-zmq", "--non-interactive",
                "--data-dir", str(root/"native-chain"), "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(daemon_port),
                "--p2p-bind-ip", "127.0.0.1", "--p2p-bind-port", str(p2p_port), "--log-file", str(root/"native-node.log")],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            processes.append(daemon)
            info = ready(daemon, daemon_port, "get_info")
            require(info["nettype"] == "fakechain" and info["offline"], "Owned native node not isolated")
            xmz_genesis = FAKECHAIN_GENESIS
        if remote and args.remote_wallet_rpc:
            from remote_native_wallets import RemoteNativeWallets
            remote.forward([daemon_port])
            vm_native = RemoteNativeWallets(remote, args.remote_wallet_rpc, daemon_port, args.stage)
            vm_native.start()
            evidence["rpcServices"] = "VM-owned Bob native wallets; host test nodes over loopback SSH tunnels"
            note("vm-native-wallets-ready")
        for name, rpc_port in (("source-wallets", source_port), ("claim-wallets", claim_port), ("observer-wallets", observer_port)):
            (root/name).mkdir(mode=0o700)
            command = [str(args.wallet_rpc), "--wallet-dir", str(root/name),
                "--daemon-address", f"127.0.0.1:{daemon_port}", "--trusted-daemon", "--allow-mismatched-daemon-version",
                "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(rpc_port), "--non-interactive",
                "--max-concurrency", "2", "--log-file", str(root/(name+".log"))]
            if testnet:
                command.insert(1, "--testnet")
            process = subprocess.Popen(command, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            processes.append(process)
            ready(process, rpc_port, "get_version", root/f"monzero-wallet-rpc.{rpc_port}.login")

        def wallet(method, params=None, claim=False, observer=False):
            rpc_port = observer_port if observer else (claim_port if claim else source_port)
            return rpc(rpc_port, method, params, root/f"monzero-wallet-rpc.{rpc_port}.login")

        addresses = {}
        for name in ("mining", "payout", "reserve", "source"):
            if vm_native and name == "payout":
                vm_native.create_recipient(name)
                addresses[name] = vm_native.recipient_address()
                vm_native.close_recipient()
                continue
            wallet("create_wallet", {"filename":name,"password":"","language":"English"})
            addresses[name] = wallet("get_address")["address"]
            wallet("close_wallet")
        wallet("open_wallet", {"filename":"source","password":""})
        xmz_atoms = 200000000000
        if testnet:
            funder = lambda method, params=None: rpc(args.xmz_funder_wallet_port, method, params)
            funder("refresh")
            sent = funder("transfer", {"destinations":[{"amount":2*xmz_atoms,"address":addresses["source"]}],"priority":1})
            note("xmz-source-funded", txid=sent["tx_hash"], atoms=str(2*xmz_atoms))
        else:
            rpc(daemon_port, "generateblocks", {"wallet_address":addresses["source"],"amount_of_blocks":131})

        # ---- Shared waiting, never mining ----
        def keepalive():
            if resident is not None:
                resident.call("alice", "status")

        def wait_until(label, predicate, timeout):
            deadline, last = time.monotonic()+timeout, time.monotonic()
            note("wait", label=label)
            while True:
                if clock and clock.error:
                    raise RuntimeError("Block clock failed: "+str(clock.error))
                value = predicate()
                if value:
                    note("waited", label=label)
                    return value
                require(time.monotonic() < deadline, "Timed out waiting for "+label)
                if time.monotonic()-last >= KEEPALIVE_SECONDS:
                    keepalive(); last = time.monotonic()
                time.sleep(5 if testnet else 1)

        def ltc_depth(txid):
            try:
                return int(ltc("getrawtransaction", txid, True).get("confirmations", 0))
            except RpcError:
                return 0

        def xmz_depth(txid):
            found = xmz_transaction(daemon_port, txid)
            if not found or found.get("in_pool") or "block_height" not in found:
                return 0
            return rpc(daemon_port, "get_info")["height"] - int(found["block_height"])

        def confirmed_by_app(label, check, timeout=1800):
            # The app's own read-only check; repeated like a user pressing "check"
            # again. The attempt count is evidence of how often a live chain races it.
            attempts = []
            def attempt():
                result = check()
                attempts.append(bool(result))
                if not result:
                    time.sleep(15)
                return result
            value = wait_until(label, attempt, timeout)
            note("app-confirmed", label=label, attempts=len(attempts))
            return value

        if not testnet:
            clock = BlockClock(ltc, anchor, daemon_port, addresses["mining"], args.ltc_block_seconds, args.xmz_block_seconds)
            clock.start()
        def source_ready():
            wallet("refresh")
            return wallet("get_balance")["unlocked_balance"] >= xmz_atoms + 10000000000
        wait_until("xmz source unlocked", source_ready, 3*3600)
        native_source_before = wallet("get_balance")["balance"]

        # ---- Local light wallet (Litecoin funder), funded from the node's fixture wallet ----
        light_peer, light_setup = None, None
        if args.light_helper:
            from litecoin_app_light_fixture import create_light_wallet
            light_funding_atoms = ltc_atoms+args.light_funding_margin
            def fund(address):
                txid = ltc("sendtoaddress", address, str(decimal.Decimal(light_funding_atoms)/100000000), wallet="fixture")
                note("light-wallet-funded", address=address, txid=txid, atoms=str(light_funding_atoms))
                wait_until("light wallet funding confirmation", lambda: ltc_depth(txid) >= 1, args.ltc_wait)
                evidence.update(lightWalletFundingTxid=txid, lightWalletFundingAtoms=str(light_funding_atoms))
            (root/"light-electrum").mkdir(mode=0o700)
            peer = LiveElectrumPeer(ltc, args.light_wallet_tls, root/"light-electrum", ltc("getblockcount"))
            if vm_light:
                light_peer = peer.__enter__()
                remote.forward([light_peer.port])
                light_setup = vm_light.create(light_peer.port)
                require(vm_light.refresh() == {"observedConfirmedAtomic":"0", "pendingReceivedAtomic":"0"},
                    "New VM light wallet is not empty")
                fund(light_setup["address"])
            else:
                # The app requires a single-link, owner-only helper copy.
                installed = root/"light-helper"
                with os.fdopen(os.open(installed, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o700), "wb") as out:
                    out.write(args.light_helper.read_bytes()); out.flush(); os.fsync(out.fileno())
                args.light_helper = installed
                args.gui_light_recovery_helper = args.light_helper
                light_peer, light_setup = create_light_wallet(args, root, ltc, anchor, network=chain["light"], tag=args.chain,
                    maximum_height=10_000_000, peer=peer, fund=fund)
            ltc_refund = light_setup["address"]
            note("light-wallet-ready", address=ltc_refund, helperSha256=light_setup["helperSha256"])

        # ---- Signed terms ----
        policy = {"ltc_network":chain["quote"],
            "xmz_network":"xmz-testnet" if testnet else "xmz-mainnet","xmz_genesis":xmz_genesis,
            "ltc_confirmations":args.ltc_confirmations,"xmz_confirmations":10,"cancel_blocks":args.cancel_blocks,"punish_blocks":10,
            "maximum_ltc_atoms":1000000000,"maximum_xmz_atoms":200000000000,
            "maximum_ltc_funding_fee":100000,"maximum_ltc_recovery_fee":20000,
            "maximum_xmz_fee":10000000000,"minimum_ltc_output":100000,"minimum_relay_fee_atoms_per_kb":1000}
        terms = {"schema":"monzero-native-ltc-xmz-quote-v2","quoteId":str(uuid.uuid4()),"swapId":str(uuid.uuid4()),
            "ltcFunder":SigningKey(bytes([1])*32).verify_key.encode().hex(),
            "xmzFunder":SigningKey(bytes([2])*32).verify_key.encode().hex(),
            "ltcGenesis":ltc("getblockhash", 0),"xmzGenesis":xmz_genesis,
            "ltcNetwork":policy["ltc_network"],"xmzNetwork":policy["xmz_network"],
            "ltcAtoms":str(ltc_atoms),"xmzAtoms":str(xmz_atoms),"xmzPayoutAtoms":"100000000000",
            "xmzClaimFeeMaxAtoms":"10000000000","xmzRefundFeeMaxAtoms":"10000000000",
            "ltcPayout":ltc_payout,"ltcRefund":ltc_refund,"xmzPayout":addresses["payout"],"xmzRefund":addresses["reserve"],
            "ltcFundingFeeMaxAtoms":"100000","xmzNativeFeeMaxAtoms":"10000000000",
            "ltcCancelFeeAtoms":"10000","ltcRefundFeeAtoms":"10000","ltcRedeemFeeAtoms":"10000",
            "ltcPunishFeeAtoms":"10000","ltcEarlyRefundFeeAtoms":"10000","ltcConfirmations":str(args.ltc_confirmations),"xmzConfirmations":"10",
            "cancelBlocks":str(args.cancel_blocks),"punishBlocks":"10","expiresAt":"0","appFeeXmzAtoms":"0",
            "ltcMinimumOutputAtoms":"100000","ltcMinimumRelayFeeAtomsPerKb":"1000"}
        request = {"policy":policy,"terms":terms,"ltc_port":ltc_port,"native_port":daemon_port,
            "wallet_port":source_port,"claim_wallet_port":claim_port,"observer_wallet_port":observer_port,
            "anchor":anchor,"native_source":addresses["source"],"password":secrets.token_hex(16)}
        if testnet:
            request["ltc_cookie"] = str(ltc_cookie)

        def invoke(action):
            request["action"] = action
            path = root/"request.json"
            if path.exists():
                path.unlink()
            write_private(path, request)
            completed = subprocess.run([str(args.engine_test_binary), "--exact", TEST, "--ignored"],
                env=dict(os.environ, MONZERO_PAIRED_CHAIN_FIXTURE=str(root)), capture_output=True, timeout=1800)
            if completed.returncode:
                (root/(action+".failure.log")).write_bytes(completed.stdout+completed.stderr)
            require(completed.returncode == 0 and b"1 passed" in completed.stdout, "Engine fixture stage failed: "+action)
            return json.loads((root/(action+"-result.json")).read_text())

        def install_journal(payout, label):
            executable = args.journal_helper.read_bytes()
            helper = root/(label+"-helper")
            with os.fdopen(os.open(helper, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o700), "wb") as out:
                out.write(executable); out.flush(); os.fsync(out.fileno())
            config = {"schema":"monzero-private-ltc-journal-v5","helper":str(helper),
                "helperDigest":hashlib.sha256(helper.read_bytes()).hexdigest(),
                "metadataDirectory":str(root/("bob-meta" if payout else "alice-meta")),
                "snapshotDirectory":str(root/("bob-secrets" if payout else "alice-secrets")),
                "tradeId":terms["swapId"],"role":"litecoin-funder" if payout else "native-funder",
                "pins":[list(bytes.fromhex(terms["ltcFunder"])),list(bytes.fromhex(terms["xmzFunder"]))],
                "policy":policy,"startQuoteFile":str(root/"start-quote.json")}
            path = root/(label+"-config.json")
            write_private(path, json.loads(json.dumps(config, sort_keys=True)))
            return helper, config, path

        # Sign as late as possible: native funding must be sent before quote expiry (<= 1 h).
        terms["expiresAt"] = str(int(time.time())+3500)
        if args.quote_kit:
            # Two-party setup as the trade kit does it: each side's own random
            # identity, terms built from their public cards, two separate signatures.
            kit = pathlib.Path(__file__).resolve().parent.parent/"dex-trade-kit"/"trade_kit.py"
            kit_env = dict(os.environ, MONZERO_KIT_PASSPHRASE=secrets.token_hex(16))
            def run_kit(*kit_args):
                done = subprocess.run([sys.executable, str(kit), *kit_args], env=kit_env, capture_output=True, text=True)
                require(done.returncode == 0, "Trade kit failed: "+done.stderr[-2000:])
            for side in ("ltc", "xmz"):
                if remote and side == "ltc":
                    remote.kit("identity", "--role", "ltc", "--dir", remote.path("kit-ltc"))
                    evidence["remoteIdentityGeneratedOnVm"] = True
                    (root/"kit-ltc").mkdir(mode=0o700)
                    (root/"kit-ltc/card.json").write_bytes(remote.read("kit-ltc/card.json"))
                    continue
                run_kit("identity", "--role", side, "--dir", str(root/("kit-"+side)))
            params = {k:v for k,v in terms.items() if k not in ("schema","quoteId","swapId","ltcFunder","xmzFunder","appFeeXmzAtoms")}
            write_private(root/"kit-params.json", params)
            run_kit("terms", "--ltc-card", str(root/"kit-ltc/card.json"), "--xmz-card", str(root/"kit-xmz/card.json"),
                "--params", str(root/"kit-params.json"), "--out", str(root/"kit-terms.json"))
            for side in ("ltc", "xmz"):
                if remote and side == "ltc":
                    remote.upload(root/"kit-terms.json", "kit-terms.json")
                    remote.kit("sign", "--dir", remote.path("kit-ltc"), "--terms", remote.path("kit-terms.json"),
                        "--out", remote.path("kit-ltc.sig"))
                    (root/"kit-ltc.sig").write_bytes(remote.read("kit-ltc.sig"))
                    continue
                run_kit("sign", "--dir", str(root/("kit-"+side)), "--terms", str(root/"kit-terms.json"), "--out", str(root/("kit-"+side+".sig")))
            for path in ("bob-meta","bob-secrets","alice-meta","alice-secrets","reservations"):
                (root/path).mkdir(mode=0o700)
            run_kit("assemble", "--terms", str(root/"kit-terms.json"), "--ltc-sig", str(root/"kit-ltc.sig"),
                "--xmz-sig", str(root/"kit-xmz.sig"), "--out", str(root/"start-quote.json"))
            terms.clear(); terms.update(json.loads((root/"kit-terms.json").read_text()))
        else:
            invoke("prepare_start")
        note("quote-signed", swapId=terms["swapId"], expiresAt=terms["expiresAt"])
        startup_configs = {}
        for payout in (False, True):
            helper, config, path = install_journal(payout, "start-bob" if payout else "start-alice")
            startup_configs[config["role"]] = (helper, config, path)

        session_request = {}
        for label, role in (("alice","native-funder"),("bob","litecoin-funder")):
            if remote and label == "bob":
                continue  # Bob's identity/passphrase never leave the VM; kit-config needs neither here.
            if args.quote_kit:
                side = root/("kit-"+("ltc" if role == "litecoin-funder" else "xmz"))
                identity, password = side/"identity.bin", (side/"identity.passphrase").read_bytes()
            else:
                identity, password, _ = install_identity(root, role)
            session_request[label+"Config"] = str(startup_configs[role][2])
            session_request[label+"Identity"] = str(identity)
            session_request[label+"Passphrase"] = password.decode()

        def light_lock(resident, setup):
            """Lock from the local light wallet: the app reviews, signs and sends through
            the light helper; no node wallet takes part. Public mempools are shared, so
            checks look for this transaction rather than an empty pool."""
            from litecoin_app_light_fixture import attach_light_wallet
            if vm_light:
                vm_light.attach(resident)
            else:
                attach_light_wallet(args, root, resident, setup)
            resident.call("bob", "light-prepare", feeAtomic="1000")
            resident.call("bob", "wait-recovery")
            state = resident.call("bob", "light-review")["lightFunding"]
            require(state["state"] == "complete" and state["reviewOperation"] == "review" and state["review"],
                "Peer recovery did not authorize local signature review")
            def call(operation, **fields):
                value = resident.call("bob", "light-"+operation, **fields)["lightFunding"]
                if fields.get("accepted", True):
                    require(value["state"] == "complete", "Local app funding failed: "+operation)
                return value
            sign = call("review")["review"]["commitment"]
            call("sign", accepted=False, commitment="0"*64)
            signed = call("sign", commitment=sign)["result"]
            txid = signed["txid"]
            require(signed["signed"] and not signed["broadcast"] and txid not in ltc("getrawmempool"),
                "Local signing broadcast a transaction")
            if args.vm_bob_restart_stage in ("signed", "powerloss-signed"):
                from remote_light_recovery import restart_bob
                if args.vm_bob_restart_stage == "powerloss-signed":
                    from remote_vm_recovery import restart_bob_after_power_loss
                    recovered = restart_bob_after_power_loss(resident, remote, vm_light, vm_native,
                        root/"vm-powerloss-checkpoint.json", [daemon_port, light_peer.port, ltc_port, kit_port],
                        args.trade_timeout*1000, vm_lease=vm_lease)
                else:
                    recovered = restart_bob(resident, remote, vm_light, args.trade_timeout*1000)
                resident.call("bob", "light-sign", accepted=False, commitment=sign)
                resident.call("bob", "light-send", accepted=False, commitment=sign)
                require(txid not in ltc("getrawmempool"), "Restart broadcast without fresh consent")
                evidence.update(vmBobRestartStage=args.vm_bob_restart_stage, vmBobRestart=recovered)
                note("vm-bob-restarted", stage=args.vm_bob_restart_stage, txid=txid,
                    quoteSecondsRemaining=int(terms["expiresAt"])-int(time.time()))
            send_review = read_light_funding(resident, "send-review")["review"]
            send = send_review["commitment"]
            call("send", accepted=False, commitment=sign)
            require(txid not in ltc("getrawmempool"), "Signing consent authorized broadcast")
            if args.vm_bob_restart_stage == "accepted":
                from remote_light_recovery import interrupt_bob_after_acceptance
                recovered = interrupt_bob_after_acceptance(resident, remote, vm_light, light_peer,
                    send, args.trade_timeout*1000)
                evidence.update(vmBobRestartStage="accepted", vmBobRestart=recovered)
                note("vm-bob-restarted", stage="node-accepted-broadcast", txid=txid,
                    quoteSecondsRemaining=int(terms["expiresAt"])-int(time.time()))
                # The node accepted these bytes, but Bob received no reply. Observe
                # before any fresh retry review; never reuse the old send approval.
                sent = read_light_funding(resident, "observe")["result"]
            else:
                sent = send_retained_light_funding(resident, txid, send_review, note)
            require(sent["txid"] == txid and ltc("getrawtransaction", txid, True)["txid"] == txid,
                "Local funding not accepted by the node")
            note("ltc-lock-sent", txid=txid, source="light-wallet")
            call("send", accepted=False, commitment=send)
            call("retry", accepted=False, commitment=send)
            retried = retry_or_confirm_light_funding(resident, txid, send_review["details"]["signedDigest"], note)
            if args.vm_bob_restart_stage == "accepted":
                digest = send_review["details"]["signedDigest"]
                require(light_peer.accepted_broadcast_digests
                    and all(item == digest for item in light_peer.accepted_broadcast_digests),
                    "Broadcast recovery accepted different funding bytes")
                require(light_peer.dropped_broadcast_replies == 1,
                    "Broadcast interruption did not lose exactly one reply")
                evidence.update(lightAcceptedBroadcastCount=len(light_peer.accepted_broadcast_digests),
                    lightAcceptedBroadcastDigests=light_peer.accepted_broadcast_digests[:],
                    lightDroppedBroadcastReplies=light_peer.dropped_broadcast_replies)
            def included():
                # Ask the node first: each app observe spends one of the resident
                # app's 256 commands, which a stalled testnet4 exhausts in ~20 min.
                if ltc_depth(txid) < 1:
                    return None
                found = read_light_funding(resident, "observe")["result"]["observation"]
                return found if found["position"] == "included" and found["confirmations"] >= 1 else None
            found = wait_until("light wallet sees its lock in a block", included, args.ltc_wait)
            require(found["blockHash"] == ltc("getrawtransaction", txid, True)["blockhash"], "Light wallet saw a different block")
            if vm_light:
                if args.vm_bob_restart_stage:
                    from remote_light_recovery import release_password
                    release_password(remote, vm_light)
                else:
                    vm_light.release_password()
            else:
                (resident.directory/"light-password").unlink()
            evidence.update(lightLockObservedBlock=found["blockHash"], lightRetryVerified=retried,
                lightIncludedBeforeRetry=not retried)
            return txid

        # ---- One app setup holding every phase (as test_litecoin_paired_chain prepare_app_setup) ----
        app_directory = root/"app-setup"; app_directory.mkdir(mode=0o700)
        app_request = dict(session_request, appDirectory=str(app_directory))
        ltc_service = {"ltcPort":ltc_port,"ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(ltc_cookie)}
        seller_service = ltc_service
        if args.light_seller:
            # The XMZ seller reaches the chain only through two Electrum servers
            # (SELLER_LIGHT_REDEEM.md). Both read the same node here, so this
            # proves the plumbing and rules, not server independence.
            start = ltc("getblockcount")
            for index in range(2):
                directory = root/f"seller-electrum-{index}"; directory.mkdir(mode=0o700)
                seller_peers.append(LiveElectrumPeer(ltc, args.light_wallet_tls, directory, start).__enter__())
            seller_service = {"ltcLight":{"servers":[{"host":"localhost","port":peer.port} for peer in seller_peers],
                "maximumTipAge":args.light_seller_tip_age}}
            note("light-seller-servers", ports=[peer.port for peer in seller_peers], startHeight=start)
        funding_rpc = root/"app-funding-rpc.json"
        write_private(funding_rpc, dict(seller_service, schema="monzero-private-ltc-funding-rpc-v2",
            nativeDaemonPort=daemon_port, nativeWalletPort=source_port,
            nativeLoginFile=str(root/f"monzero-wallet-rpc.{source_port}.login"),
            source={"wallet":addresses["source"],"account":0,"priority":1}))
        funding = dict(startup_configs["native-funder"][1]); funding.pop("startQuoteFile")
        funding.update(schema="monzero-private-ltc-journal-v4", fundingRpcFile=str(funding_rpc))
        funding_path = root/"app-funding.json"; write_private(funding_path, json.loads(json.dumps(funding, sort_keys=True)))
        app_request["aliceSettlementConfig"] = str(funding_path)
        reservation_rpc = root/"app-reservation-rpc.json"
        write_private(reservation_rpc, dict(ltc_service, schema="monzero-private-ltc-reservation-rpc-v1",
            reservationDirectory=str(root/"reservations"), feeRateAtomsPerKb=1000))
        reservation = root/"app-reservation.json"
        write_private(reservation, {"journalConfigFile":str(startup_configs["litecoin-funder"][2]),
            "rpcFile":str(reservation_rpc),"schema":"monzero-private-ltc-reservation-v1"})
        app_request["reservationConfig"] = str(reservation)
        for role, label, purpose in (("native-funder","alice","refund"),("litecoin-funder","bob","payout")):
            password_path = root/("app-"+label+"-wallet-password")
            with os.fdopen(os.open(password_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as out:
                out.write(request["password"].encode())
            observer = source_port if purpose == "refund" else observer_port
            rpc_path = root/("app-"+label+"-payment-rpc.json")
            write_private(rpc_path, dict(seller_service if purpose == "refund" else ltc_service,
                schema="monzero-private-ltc-"+purpose+"-rpc-v4",
                nativeDaemonPort=daemon_port, nativeWalletPort=claim_port,
                nativeLoginFile=str(root/f"monzero-wallet-rpc.{claim_port}.login"),
                preparationObserver={"walletPort":observer,"loginFile":str(root/f"monzero-wallet-rpc.{observer}.login")},
                walletPasswordFile=str(password_path)))
            config = dict(startup_configs[role][1]); config.pop("startQuoteFile")
            config["schema"] = "monzero-private-ltc-journal-v2" if purpose == "refund" else "monzero-private-ltc-journal-v3"
            config[purpose+"RpcFile"] = str(rpc_path)
            payment_path = root/("app-"+label+"-payment.json")
            write_private(payment_path, json.loads(json.dumps(config, sort_keys=True)))
            app_request[label+"PaymentConfig"] = str(payment_path)
        redeem_rpc = root/"app-redeem-rpc.json"
        write_private(redeem_rpc, dict(ltc_service, schema="monzero-private-ltc-redeem-rpc-v1",
            nativeDaemonPort=daemon_port, observerWalletPort=observer_port,
            observerLoginFile=str(root/f"monzero-wallet-rpc.{observer_port}.login"), safetyBlocks=2))
        wrapper = root/"app-redeem.json"
        write_private(wrapper, {"journalConfigFile":str(startup_configs["litecoin-funder"][2]),
            "rpcFile":str(redeem_rpc),"schema":"monzero-private-ltc-redeem-v1"})
        app_request["bobSettlementConfig"] = str(wrapper)

        resident_directory = root/"resident-app"; resident_directory.mkdir(mode=0o700)
        startup_request = dict(app_request, initialStartup=True, residentDirectory=str(resident_directory))
        if testnet:
            # Throwaway testnet-only operator key, created and kept inside this trade workspace.
            # Throwaway per-run key, or the prototype key a hand-over app was built with.
            control_key = SigningKey(bytes.fromhex(args.control_seed_file.read_text().strip())) \
                if args.control_seed_file else SigningKey.generate()
            now = int(time.time())
            payload = json.dumps({"schema":"monzero-dex-control-v1","network":chain["quote"],"sequence":1,
                "issuedAt":now,"expiresAt":now+86400,"newTradesEnabled":True,
                "maxLtcAtomsPerTrade":str(ltc_atoms),"maxXmzAtomsPerTrade":str(xmz_atoms),
                "maxLtcAtomsPerDay":str(4*ltc_atoms)}, sort_keys=True, separators=(",",":"))
            signature = control_key.sign(b"monzero-dex-control-v1\0"+payload.encode()).signature.hex()
            control_file = root/"control.json"
            with os.fdopen(os.open(control_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w") as out:
                out.write(json.dumps({"payload":payload,"signature":signature}, sort_keys=True, separators=(",",":")))
            startup_request.update(ltcNetwork=chain["app_stage"], controlPublicKey=control_key.verify_key.encode().hex(),
                controlFile=str(control_file))
        elif args.chain != "ltc":
            startup_request.update(ltcNetwork=chain["app_stage"])  # the app defaults to Litecoin regtest
        if args.listen_address or args.peer_address:
            startup_request.update(listenAddress=args.listen_address or "127.0.0.1", peerAddress=args.peer_address or "127.0.0.1")
        tls_root = args.light_wallet_tls/"root.pem" if args.light_helper or args.light_seller else None
        if args.kit_config:
            probe = socket.socket(); probe.bind(("127.0.0.1", 0)); kit_port = probe.getsockname()[1]; probe.close()
            if remote:
                # Services remain bound to loopback. Bob reaches Alice through the peer tunnel too.
                remote.forward([ltc_port, kit_port] if vm_native else [ltc_port, daemon_port, claim_port, observer_port, kit_port])
        if args.split_processes:
            # One trader per process; they reach each other over the peer address.
            fixtures, directories = {}, {}
            for role in ("alice", "bob"):
                directories[role] = root/("resident-"+role); directories[role].mkdir(mode=0o700)
                fixtures[role] = root/("session-"+role+".json")
                if args.kit_config:
                    # Each trader starts from the app folder the kit wrote from that
                    # side's own settings; nothing else from this driver reaches it.
                    side = "xmz" if role == "alice" else "ltc"
                    login = lambda port: str(root/f"monzero-wallet-rpc.{port}.login")
                    local = {"journalHelper":str(args.journal_helper),
                        "litecoin":{"rpcPort":ltc_port,"wallet":"fixture","anchor":anchor,"cookieFile":str(ltc_cookie)},
                        "xmz":{"daemonPort":daemon_port,"claimWalletPort":claim_port,"claimLoginFile":login(claim_port),
                            "sourceWalletPort":source_port,"sourceLoginFile":login(source_port),"sourceAddress":addresses["source"],
                            "observerWalletPort":observer_port,"observerLoginFile":login(observer_port),
                            "walletPasswordFile":str(root/("app-"+role+"-wallet-password"))},
                        "peer":{"address":(args.listen_address if role == "alice" else args.peer_address) or "127.0.0.1","port":kit_port}}
                    write_private(root/("kit-"+side+"-local.json"), local)
                    write_private(root/"kit-policy.json", policy) if not (root/"kit-policy.json").exists() else None
                    if remote and role == "bob":
                        remote.upload(root/"start-quote.json", "start-quote.json")
                        remote.upload(root/"kit-xmz/card.json", "peer-card.json")
                        remote.upload(root/"kit-policy.json", "policy.json")
                        remote.upload(ltc_cookie, "ltc.cookie")
                        local["journalHelper"] = remote.helper
                        local["litecoin"]["cookieFile"] = remote.path("ltc.cookie")
                        if vm_native:
                            local["xmz"] = vm_native.config()
                        else:
                            for rpc_port in (claim_port, observer_port):
                                remote.upload(root/f"monzero-wallet-rpc.{rpc_port}.login", f"wallet-{rpc_port}.login")
                            remote.upload(root/"app-bob-wallet-password", "wallet-password")
                            local["xmz"] = {"daemonPort":daemon_port,"claimWalletPort":claim_port,
                                "claimLoginFile":remote.path(f"wallet-{claim_port}.login"),
                                "observerWalletPort":observer_port,"observerLoginFile":remote.path(f"wallet-{observer_port}.login"),
                                "walletPasswordFile":remote.path("wallet-password")}
                        remote.put("local.json", json.dumps(local).encode())
                        remote.kit("configure", "--dir", remote.path("kit-ltc"), "--quote", remote.path("start-quote.json"),
                            "--peer-card", remote.path("peer-card.json"), "--policy", remote.path("policy.json"),
                            "--local", remote.path("local.json"), "--out", remote.path("app"))
                        kit_request = {k:v for k,v in startup_request.items() if k in ("ltcNetwork","controlPublicKey")}
                        if testnet:
                            remote.upload(control_file, "control.json")
                            kit_request["controlFile"] = remote.path("control.json")
                        remote.put("resident/.keep", b"")
                        remote.put("session.json", json.dumps(dict(kit_request, appConfigFile=remote.path("app/app.json"),
                            residentRole="bob", residentDirectory=remote.path("resident"))).encode())
                        continue
                    run_kit("configure", "--dir", str(root/("kit-"+side)), "--quote", str(root/"start-quote.json"),
                        "--peer-card", str(root/("kit-"+("ltc" if side == "xmz" else "xmz")+"/card.json")),
                        "--policy", str(root/"kit-policy.json"), "--local", str(root/("kit-"+side+"-local.json")),
                        "--out", str(root/("kit-"+side+"-app")))
                    kit_request = {k:v for k,v in startup_request.items() if k in ("ltcNetwork","controlPublicKey","controlFile")}
                    write_private(fixtures[role], dict(kit_request, appConfigFile=str(root/("kit-"+side+"-app/app.json")),
                        residentRole=role, residentDirectory=str(directories[role])))
                    continue
                write_private(fixtures[role], dict(startup_request, residentRole=role, residentDirectory=str(directories[role])))
            def remote_bob():
                from litecoin_remote_fixture import RemoteLitecoinAppFixture
                return RemoteLitecoinAppFixture(remote.host, remote.binary, remote.path("session.json"),
                    remote.path("resident"), processes, directories["bob"], multiplex=False,
                    tls_root=vm_light.tls_root if vm_light else None,
                    function_timeout_ms=args.trade_timeout*1000)
            if args.hand_over_gui:
                from gui_hand_over import hand_over
                evidence.update(hand_over(args, root, fixtures, light_setup, daemon_port, note, args.trade_timeout))
                evidence.update(schema="monzero-ltc-xmz-hand-over-v1", chain=args.chain, swapId=terms["swapId"],
                    policy=policy, durationSeconds=round(time.monotonic()-started), executable=False)
                passed = handed_over = True
                return
            resident = SplitLitecoinApp(args.gui_test_binary, fixtures, directories, processes, tls_root,
                function_timeout_ms=args.trade_timeout*1000, remote_bob=remote_bob if remote else None)
            ledger_dirs = list(directories.values())
        else:
            fixture = root/"session.json"
            write_private(fixture, startup_request)
            resident = LitecoinAppFixture(args.gui_test_binary, fixture, resident_directory, processes,
                tls_root, function_timeout_ms=args.trade_timeout*1000)
            ledger_dirs = [resident_directory]
        require(resident.start() == {"schema":"monzero-ltc-resident-startup-fixture-v1",
            "startedAfterApproval":True,"executable":False}, "Resident startup evidence differs")
        if testnet:
            if remote:
                (directories["bob"]/"bob-started-trades.json").write_bytes(remote.read("resident/bob-started-trades.json"))
            ledgers = [p for d in ledger_dirs for p in d.glob("*-started-trades.json")]
            note("trade-started", ledger=sorted(p.name for p in ledgers))
            require(len(ledgers) == 2, "Kill-switch ledger did not record both starts")
        if args.quote_kit:
            # The engine fixture knows only its test identities; read the agreed
            # trade from the apps themselves instead.
            resident.call("bob", "wait-negotiation")
            negotiated = {"nativeAddress": None}
            note("negotiated", source="apps")
        else:
            negotiated = invoke("begin_negotiated")
            require(negotiated["startApprovalBound"] is True, "Trade setup did not use role-bound start approval")
            note("negotiated", contract=negotiated["contract"], nativeAddress=negotiated["nativeAddress"])

        # ---- Litecoin lock (Litecoin funder) ----
        if args.light_helper:
            lock_txid = light_lock(resident, light_setup)
        else:
            resident.call("bob", "reserve")
            resident.call("bob", "wait-recovery")
            state = resident.call("bob", "lock-review")["reservation"]
            review = state["fundingReview"]
            require(review["details"]["action"] == "send", "Unexpected Litecoin lock approval action")
            resident.call("bob", "lock-submit", accepted=False, commitment="0"*64)
            sent = resident.call("bob", "lock-submit", commitment=review["commitment"])["reservation"]
            lock_txid = review["details"]["txid"]
            require(sent["fundingSubmission"]["txid"] == lock_txid, "Submitted Litecoin lock differs from approval")
            note("ltc-lock-sent", txid=lock_txid)
        wait_until("ltc lock confirmations", lambda: ltc_depth(lock_txid) >= args.ltc_confirmations, args.ltc_wait)
        if args.light_helper:
            decoded = ltc("getrawtransaction", lock_txid, True)
            spent_atoms = sum(atoms(ltc("getrawtransaction", vin["txid"], True)["vout"][vin["vout"]]["value"]) for vin in decoded["vin"])
            lock_fee = spent_atoms-sum(atoms(vout["value"]) for vout in decoded["vout"])
        else:
            lock_fee = atoms(-ltc("gettransaction", lock_txid, wallet="fixture")["fee"])
        require(0 < lock_fee <= int(terms["ltcFundingFeeMaxAtoms"]), "Litecoin lock fee exceeds signed ceiling")
        require(time.time() < int(terms["expiresAt"]), "Quote expired before native funding; rerun with faster blocks")

        # ---- XMZ lock (native funder) ----
        resident.call("alice", "funding-refresh")
        prepared = resident.call("alice", "funding-prepare")["funding"]["fundingReview"]
        native_txid = prepared["details"]["txid"]
        state = resident.call("alice", "funding-review", retry=False)["funding"]
        consent = state["fundingReview"]["commitment"]
        resident.call("alice", "funding-submit", accepted=False, commitment="0"*64)
        sent = resident.call("alice", "funding-submit", commitment=consent)["funding"]
        require(sent["snapshot"]["nativeFunding"]["acknowledged"], "Native funding not acknowledged")
        note("xmz-lock-sent", txid=native_txid)
        wait_until("xmz lock confirmations", lambda: xmz_depth(native_txid) >= 10, args.xmz_wait)
        def funding_confirmed():
            resident.call("alice", "funding-refresh")
            checked = resident.call("alice", "funding-check")["funding"]
            return checked["fundingCheckState"] == "confirmed" and checked["fundingConfirmation"]["txid"] == native_txid
        confirmed_by_app("app xmz lock check", funding_confirmed)
        def drop_connection(stage):
            # The apps must rebuild the session, the recovery agreement and any
            # settlement step already under way, without a restart.
            if args.drop_connection is None or args.drop_stage != stage:
                return
            before = resident.call("bob", "status")["reconnects"]+resident.call("alice", "status")["reconnects"]
            resident.call(args.drop_connection, "drop-connection")
            note("connection-dropped", role=args.drop_connection, stage=stage)
            resident.call("bob", "wait-reconnected", accepted=None, reconnects=before, recovery=True)
            peers = {role:{key:resident.last[role]["peer"].get(key) for key in ("state","complete","recoveryComplete",
                "settlementState","transportAuthenticated","failedDuring")} for role in ("alice","bob")}
            require(resident.last["accepted"], "Apps did not reconnect: "+json.dumps(peers))
            after = resident.call("bob", "status")["reconnects"]+resident.call("alice", "status")["reconnects"]
            note("reconnected", reconnects=after-before)
            evidence.update(connectionDropRecovered=True, connectionDropRole=args.drop_connection,
                connectionDropStage=stage, reconnects=after-before)
        drop_connection("locks")  # both chains locked: the worst moment to lose the peer
        wallet("refresh")
        native_funding = wallet("get_transfer_by_txid", {"txid":native_txid})["transfer"]
        require(native_funding["type"] == "out" and native_funding["amount"] == xmz_atoms
            and 0 < native_funding["fee"] <= int(terms["xmzNativeFeeMaxAtoms"]), "Native funding amount or fee differs")
        evidence.update(ltcLockTxid=lock_txid, ltcLockFeeAtoms=str(lock_fee), nativeLockTxid=native_txid,
            nativeLockFeeAtoms=str(native_funding["fee"]))

        def restore_wallet(role):
            resident.call(role, "payment-refresh")
            state = resident.call(role, "wallet-restore")["payment"]
            require(state["walletRecoveryState"] == "verified"
                and negotiated["nativeAddress"] in (None, state["recoveredWallet"]["address"]), "Recovered swap wallet differs")
            require(recovered_native_balance(wallet, vm_native, role) == xmz_atoms,
                "Recovered swap wallet does not hold the lock")

        def pay(name):
            role = "bob" if name == "payout" else "alice"
            resident.call(role, "payment-refresh")
            review = resident.call(role, name+"-prepare")["payment"][name+"Review"]
            require(review, "Native "+name+" was not prepared")
            resident.submit_payment(name)  # send, then an exact retry of the same transaction
            txid = review["details"]["txid"]
            note("xmz-"+name+"-sent", txid=txid)
            wait_until("xmz "+name+" confirmations", lambda: xmz_depth(txid) >= 10, args.xmz_wait)
            def paid():
                resident.call(role, "payment-refresh")
                state = resident.call(role, name+"-check")["payment"]
                return state[name+"CheckState"] == "confirmed" and state[name+"Confirmation"]["txid"] == txid
            confirmed_by_app("app xmz "+name+" check", paid)
            return review["details"]

        def received(name, txid):
            if vm_native and name == "payout":
                vm_native.open_recipient(name)
                try:
                    vm_native.refresh_recipient()
                    incoming = vm_native.recipient_transfer(txid)
                    balance = vm_native.recipient_balance()["balance"]
                finally:
                    vm_native.close_recipient()
            else:
                wallet("open_wallet", {"filename":name,"password":""})
                wallet("refresh", {"start_height":0})
                incoming = wallet("get_transfer_by_txid", {"txid":txid})["transfer"]
                balance = wallet("get_balance")["balance"]
                wallet("close_wallet")
            require(incoming["type"] == "in" and incoming["amount"] == balance, "Unexpected native "+name+" balance")
            return balance

        if args.mode == "success":
            resident.call("alice", "share-proof")
            drop_connection("proof")
            peer = resident.call("bob", "wait-release")["peer"]
            require(peer["nativeFundingTxid"] == native_txid, "Proof changed native funding identity")
            resident.call("bob", "release-redemption", accepted=False, txid="0"*64)
            depth = ltc_depth(lock_txid)
            note("release-window", lockDepth=depth, cancelBlocks=args.cancel_blocks,
                remainingBeforeSafety=args.cancel_blocks-2-depth)
            resident.call("bob", "release-redemption", txid=native_txid)
            drop_connection("release")
            resident.call("alice", "wait-settlement")
            if args.light_seller_dishonest:
                # Review only reads saved data; the chain gate is at submit. One
                # server withholds the newest block: the approved submission must
                # be refused before anything is recorded or broadcast.
                review = resident.call("alice", "redemption-review")["redemption"]["review"]
                seller_peers[1].fault = "lag"
                refused = resident.call("alice", "redemption-submit", commitment=review["commitment"])["redemption"]
                seller_peers[1].fault = None
                require(not refused["submission"] and refused["state"] not in ("acknowledged", "uncertain", "mempool"),
                    "Seller sent a redemption while a server withheld the newest block")
                require(not any(vin.get("txid") == lock_txid for txid in ltc("getrawmempool")
                    for vin in ltc("getrawtransaction", txid, True)["vin"]),
                    "A redemption was broadcast while a server withheld the newest block")
                note("dishonest-lag-refused", state=refused["state"])
            review = resident.call("alice", "redemption-review")["redemption"]["review"]
            resident.call("alice", "redemption-submit", accepted=False, commitment="0"*64)
            state = resident.call("alice", "redemption-submit", commitment=review["commitment"])["redemption"]
            redeem_txid = review["details"]["txid"]
            require(state["submission"]["txid"] == redeem_txid, "Submitted redemption differs from approval")
            note("ltc-redeem-sent", txid=redeem_txid)
            wait_until("ltc redemption confirmations", lambda: ltc_depth(redeem_txid) >= 2, args.ltc_wait)
            if args.light_seller_dishonest:
                # One server hides the confirmed redemption: the app must not
                # report it confirmed while the servers disagree.
                seller_peers[1].hidden = frozenset({redeem_txid})
                hidden = resident.call("alice", "redemption-check", kind="redeem")["redemption"]
                seller_peers[1].hidden = frozenset()
                require(hidden["checkState"] != "confirmed" and not hidden["observation"],
                    "Seller confirmed a redemption one server did not report")
                note("dishonest-hide-refused", checkState=hidden["checkState"])
                evidence.update(dishonestServerRefusals=["lag","hide-redeem"])
            def redeemed():
                state = resident.call("alice", "redemption-check", kind="redeem")["redemption"]
                return state["checkState"] == "confirmed" and state["observation"]["txid"] == redeem_txid
            confirmed_by_app("app ltc redemption check", redeemed)
            transaction = ltc("getrawtransaction", redeem_txid, True)
            require(len(transaction["vout"]) == 1
                and atoms(transaction["vout"][0]["value"]) == ltc_atoms-int(terms["ltcRedeemFeeAtoms"])
                and transaction["vout"][0]["scriptPubKey"]["hex"] == ltc("validateaddress", ltc_payout)["scriptPubKey"], "Wrong Litecoin redemption amount or recipient")
            restore_wallet("bob")
            details = pay("payout")
            wallet("close_wallet")  # source; recipients are opened one at a time
            require(int(details["payoutAtoms"]) == int(terms["xmzPayoutAtoms"]), "Native payout differs from signed terms")
            got = received("payout", details["txid"])
            reserve = received("reserve", details["txid"])
            require(got == int(terms["xmzPayoutAtoms"]) and got+reserve+int(details["feeAtoms"]) == xmz_atoms,
                "Native payout accounting differs from the locked amount")
            evidence.update(ltcRedeemTxid=redeem_txid, ltcRedeemAtoms=str(ltc_atoms-int(terms["ltcRedeemFeeAtoms"])),
                nativePayoutTxid=details["txid"], nativePayoutAtoms=str(got), nativeReserveReturnAtoms=str(reserve),
                nativePayoutFeeAtoms=details["feeAtoms"], successPathVerified=True)
        else:
            def recovery_spend(role, kind):
                review = resident.call(role, "recovery-review", kind=kind)["reservation"]["recoveryReview"]
                require(review["details"]["action"] == "send", "Unexpected recovery approval action")
                resident.call(role, "recovery-submit", accepted=False, commitment="0"*64)
                state = resident.call(role, "recovery-submit", commitment=review["commitment"])["reservation"]
                txid = review["details"]["txid"]
                require(state["recoverySubmission"]["txid"] == txid, "Submitted recovery differs from approval")
                note("ltc-"+kind+"-sent", role=role, txid=txid)
                wait_until("ltc "+kind+" confirmations", lambda: ltc_depth(txid) >= 2, args.ltc_wait)
                def recovered():
                    state = resident.call(role, "recovery-check", kind=kind)["reservation"]
                    return state["recoveryCheckState"] == "confirmed" and state["recoveryObservation"]["txid"] == txid
                confirmed_by_app("app ltc "+kind+" check", recovered)
                return txid
            wait_until("cancellation timelock", lambda: ltc_depth(lock_txid) >= args.cancel_blocks, args.ltc_wait*8)
        if args.mode == "punish":
            # The BTC buyer goes silent after both locks: the XMZ seller cancels,
            # then takes the BTC once the punish timelock passes. With
            # --light-seller every seller step goes through the Electrum servers.
            cancel_txid = recovery_spend("alice", "cancel")
            wait_until("punish timelock", lambda: ltc_depth(cancel_txid) >= int(terms["punishBlocks"]), args.ltc_wait*8)
            punish_txid = recovery_spend("alice", "punish")
            transaction = ltc("getrawtransaction", punish_txid, True)
            punished = ltc_atoms-int(terms["ltcCancelFeeAtoms"])-int(terms["ltcPunishFeeAtoms"])
            require(len(transaction["vin"]) == 1 and transaction["vin"][0]["txid"] == cancel_txid
                and len(transaction["vout"]) == 1 and atoms(transaction["vout"][0]["value"]) == punished
                and transaction["vout"][0]["scriptPubKey"]["hex"] == ltc("validateaddress", ltc_payout)["scriptPubKey"],
                "Punishment spent a different output or changed the agreed amount/recipient")
            require(ltc("gettxout", cancel_txid, 0) is None, "Punishment did not consume the cancellation output")
            evidence.update(ltcCancelTxid=cancel_txid, ltcPunishTxid=punish_txid, ltcPunishAtoms=str(punished),
                punishPathVerified=True)
        elif args.mode == "refund":
            spent = {kind:recovery_spend("bob", kind) for kind in ("cancel", "refund")}
            transaction = ltc("getrawtransaction", spent["refund"], True)
            refunded = ltc_atoms-int(terms["ltcCancelFeeAtoms"])-int(terms["ltcRefundFeeAtoms"])
            require(len(transaction["vout"]) == 1 and atoms(transaction["vout"][0]["value"]) == refunded
                and transaction["vout"][0]["scriptPubKey"]["hex"] == ltc("validateaddress", ltc_refund)["scriptPubKey"], "Wrong Litecoin refund amount or destination")
            restore_wallet("alice")
            details = pay("refund")
            wallet("close_wallet")
            got = received("reserve", details["txid"])
            require(got == int(details["refundAtoms"]) and got+int(details["feeAtoms"]) == xmz_atoms,
                "Native refund accounting differs from the locked amount")
            evidence.update(ltcCancelTxid=spent["cancel"], ltcRefundTxid=spent["refund"], ltcRefundAtoms=str(refunded),
                nativeRefundTxid=details["txid"], nativeRefundAtoms=str(got), nativeRefundFeeAtoms=details["feeAtoms"],
                refundPathVerified=True)
        resident.stop(); resident = None
        if args.light_helper:
            if vm_light:
                observed = vm_light.refresh()
            else:
                configuration = light_setup["configurationFile"]
                light_helper_call(args, configuration, "sync", maximumDownloadHeaders=100000)
                observed = light_helper_call(args, configuration, "wallet-refresh",
                    walletScan=dict(gap=2, addressesPerBranch=3, historyEntriesPerBranch=50))["wallet"]
            expected = light_funding_atoms-ltc_atoms-lock_fee
            if args.mode == "refund":
                expected += ltc_atoms-int(terms["ltcCancelFeeAtoms"])-int(terms["ltcRefundFeeAtoms"])
            require(int(observed["observedConfirmedAtomic"]) == expected and observed["pendingReceivedAtomic"] == "0",
                f"Light wallet balance {observed['observedConfirmedAtomic']} differs from the expected {expected}")
            evidence.update(localLightWalletFunding=True, lightWalletAddress=light_setup["address"],
                lightHelperSha256=light_setup["helperSha256"], lightWalletFinalAtoms=str(expected),
                lightElectrumRequests=len(light_peer.requests))
        if remote:
            require(sorted(p.name for p in (root/"kit-ltc").iterdir()) == ["card.json"],
                "Host received unexpected private VM identity files")
            evidence["hostHasOnlyRemotePublicCard"] = True
        if vm_light:
            require(not (root/"local-light-wallet").exists() and not list(root.rglob("light-password")),
                "Host contains unexpected VM light-wallet secrets")
            evidence.update(remoteLightWallet=True, lightWalletCreatedOnVm=True,
                hostHasNoRemoteLightWalletFiles=True)
        if vm_native:
            require(not (root/"source-wallets/payout").exists() and not (root/"source-wallets/payout.keys").exists(),
                "VM payout wallet unexpectedly exists on host")
            evidence.update(remoteNativeWallets=True, remoteNativePayoutAddress=addresses["payout"])
        evidence.update(schema="monzero-ltc-xmz-live-swap-v1", chain=args.chain, swapId=terms["swapId"], policy=policy,
            residentAppSettlement=True, fullCrossChainSettlement=True,
            killSwitchGatedStart=testnet, chainControlledByDriver=not testnet,
            chainAdvanceMode="caller-run-testnet" if testnet else "owned-block-clock",
            durationSeconds=round(time.monotonic()-started), executable=False)
        # Both seller servers must have carried real work: lock checks and the spend.
        require(all("blockchain.scripthash.get_history" in peer.requests for peer in seller_peers),
            "A light-seller server was never asked about the contract")
        passed = True
    finally:
        if clock:
            clock.stopped.set(); clock.join(timeout=10)
        cleanup_errors = stop_processes(processes)
        if owned_ltc:
            try:
                owned_ltc.stop()
            except Exception as error:
                cleanup_errors.append("Litecoin:" + type(error).__name__)
        if light_peer is not None:
            try:
                light_peer.stop.set(); light_peer.release_broadcast_reply.set()  # wake any held broadcast reply
                light_peer.listener.close(); light_peer.thread.join(10)
                evidence["lightElectrumErrors"] = light_peer.errors
            except Exception as error:
                cleanup_errors.append("LightPeer:" + type(error).__name__)
        for index, peer in enumerate(seller_peers):
            try:
                peer.stop.set(); peer.listener.close(); peer.thread.join(10)
                evidence[f"sellerElectrum{index}Errors"] = peer.errors
                evidence[f"sellerElectrum{index}Requests"] = len(peer.requests)
            except Exception as error:
                cleanup_errors.append("SellerPeer:" + type(error).__name__)
        evidence["cleanupVerified"] = not cleanup_errors
        if cleanup_errors:
            evidence["cleanupErrors"] = cleanup_errors
            if passed:
                write_private(pathlib.Path(str(args.report)+".failed.json"), evidence)
                raise RuntimeError("Swap settled but process cleanup could not be verified")
        if not passed:
            write_private(pathlib.Path(str(args.report)+".failed.json"), evidence)
        if passed and not testnet and not args.keep:
            shutil.rmtree(root)
        if handed_over:
            write_private(args.report, evidence)  # after cleanup, so the report records it
    note("passed")
    write_private(args.report, evidence)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    path = lambda value: pathlib.Path(value).resolve()
    parser.add_argument("--stage", choices=("regtest","testnet"), required=True)
    parser.add_argument("--chain", choices=("ltc","btc"), default="ltc", help="UTXO chain traded against XMZ")
    parser.add_argument("--btc-test-network", choices=("testnet4","signet"), default="testnet4",
        help="Bitcoin public test network for the testnet stage")
    parser.add_argument("--mode", choices=("success","refund","punish"), required=True)
    parser.add_argument("--light-seller-dishonest", action="store_true",
        help="with --light-seller --mode success: one seller server withholds a block, then hides the redemption; the app must refuse both")
    for flag in ("wallet-rpc","engine-test-binary","journal-helper","gui-test-binary","temp-parent","report"):
        parser.add_argument("--"+flag, required=True, type=path)
    parser.add_argument("--litecoind", "--bitcoind", type=path, help="regtest stage: the chain's Core daemon")
    parser.add_argument("--daemon", type=path, help="regtest stage: monzerod for the owned fakechain")
    parser.add_argument("--ltc-rpc-port", type=int, help="testnet stage")
    parser.add_argument("--ltc-cookie", type=path, help="testnet stage")
    parser.add_argument("--xmz-daemon-port", type=int, help="testnet stage")
    parser.add_argument("--xmz-funder-wallet-port", type=int, help="testnet stage: wallet-rpc with an open funded wallet")
    parser.add_argument("--light-helper", type=path, help="fund the Litecoin lock from a disposable local light wallet")
    parser.add_argument("--light-wallet-tls", type=path, help="Electrum TLS fixture directory (server.der, server-key.der, root.pem)")
    parser.add_argument("--light-seller", action="store_true", help="the XMZ seller uses two Electrum servers instead of the node")
    parser.add_argument("--light-seller-tip-age", type=int, default=5400, help="seconds the seller accepts its best header being old")
    parser.add_argument("--ltc-atoms", type=int, default=100000000)
    parser.add_argument("--ltc-confirmations", type=int, default=2, help="lock confirmations bound into the quote")
    parser.add_argument("--light-funding-margin", type=int, default=5000000,
        help="atoms paid to the light wallet beyond the lock amount (covers the funding fee and change)")
    parser.add_argument("--cancel-blocks", type=int, default=20)
    parser.add_argument("--ltc-block-seconds", type=float, default=20, help="regtest clock")
    parser.add_argument("--xmz-block-seconds", type=float, default=30, help="regtest clock")
    parser.add_argument("--ltc-wait", type=int, default=3600, help="seconds allowed for 2 Litecoin confirmations")
    parser.add_argument("--xmz-wait", type=int, default=7200, help="seconds allowed for 10 XMZ confirmations")
    parser.add_argument("--trade-timeout", type=int, default=12*3600, help="whole resident app run, seconds")
    parser.add_argument("--drop-connection", choices=("alice","bob"), help="cut the peer connection after both locks; the apps must reconnect")
    parser.add_argument("--drop-stage", choices=("locks","proof","release"), default="locks",
        help="when to cut it: after both locks, after the funding proof, or after the redemption release")
    parser.add_argument("--quote-kit", action="store_true", help="set up identities and the signed quote with tools/dex-trade-kit")
    parser.add_argument("--kit-config", action="store_true",
        help="with --quote-kit --split-processes: start each trader only from the app folder trade_kit.py configure writes")
    parser.add_argument("--split-processes", action="store_true", help="run each trader in its own app process")
    parser.add_argument("--vm-bob-restart-stage", choices=("signed", "accepted", "powerloss-signed"), help="interrupt VM Bob at a retained transaction boundary; powerloss-signed hard-powers off the designated lab VM")
    parser.add_argument("--remote-light-helper", help="VM light helper binary; keeps Litecoin funding keys on VM")
    parser.add_argument("--remote-wallet-rpc", help="VM wallet RPC binary; keeps Bob native wallets on the VM")
    parser.add_argument("--remote-host", help="SSH alias for the lab VM running Bob (requires --kit-config)")
    parser.add_argument("--remote-binary", help="absolute VM path to the Qt dex_tests binary")
    parser.add_argument("--remote-helper", help="absolute VM path to the journal helper")
    parser.add_argument("--listen-address", help="address the native-funder app listens on (testnet allows non-loopback)")
    parser.add_argument("--peer-address", help="address the Litecoin-funder app connects to")
    parser.add_argument("--hand-over-gui", type=path, help="open the real app for each trader and let a person run the trade (needs --kit-config --split-processes)")
    parser.add_argument("--control-seed-file", type=path, help="testnet control key seed (hex) matching the app build's pinned control key")
    parser.add_argument("--keep", action="store_true", help="keep the regtest workspace after success")
    parsed = parser.parse_args()
    if parsed.kit_config and not (parsed.quote_kit and parsed.split_processes):
        parser.error("--kit-config needs --quote-kit and --split-processes")
    run(parsed)
