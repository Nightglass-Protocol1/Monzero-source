#!/usr/bin/env python3
"""Disposable local fakechain key-restoration test, NOT an atomic swap.

Explicit daemon/wallet binaries required. Test keys never leave loopback or get
printed. All fixture data is removed after owned processes have stopped.
"""
import argparse
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import time
import uuid
import stat
import http.client
import http.server
import threading
import sys
import requests
from requests.auth import HTTPDigestAuth

GENESIS = "84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4"


def verify_spend_journal(root, txid, amount, fee, wallet, destination, lock_txid):
    records = {}
    identities = set()
    for kind in ("intent", "prepared", "receipt"):
        paths = list(root.glob(f"*.{kind}"))
        assert len(paths) == 1
        metadata = paths[0].lstat()
        assert stat.S_ISREG(metadata.st_mode) and metadata.st_mode & 0o077 == 0
        assert metadata.st_size <= 4096
        records[kind] = json.loads(paths[0].read_text(encoding="ascii"))
        identities.add(paths[0].stem)
    assert len(identities) == 1
    intent = records["intent"]
    assert intent["schema"] == "monzero-native-spend-intent-v1"
    assert intent["genesis"] == GENESIS and intent["lockTxid"] == lock_txid
    assert intent["wallet"] == wallet and intent["destination"] == destination
    assert intent["amountAtoms"] == str(amount) and int(intent["maximumFeeAtoms"]) >= fee
    for kind in ("prepared", "receipt"):
        record = records[kind]
        assert set(record) == {"schema", "txid", "feeAtoms", "weight"}
        assert record["schema"] == f"monzero-native-spend-{kind}-v1"
        assert record["txid"] == txid and record["feeAtoms"] == str(fee)
        assert type(record["weight"]) is int and record["weight"] > 0
    assert records["prepared"]["weight"] == records["receipt"]["weight"]
    return intent


class LostRelayReply:
    """Owned loopback fixture proxy: drop the body only after real relay succeeds.

    Keep Digest challenge/response on the same upstream connection. Never log
    headers, parameters, replies or credentials. No production use.
    """
    def __init__(self, wallet_port):
        self.accepted = []
        self.relay_calls = 0
        self.failed = False
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def setup(self):
                super().setup()
                self.connection.settimeout(20)
                self.upstream = http.client.HTTPConnection("127.0.0.1", wallet_port, timeout=15)

            def finish(self):
                self.upstream.close()
                super().finish()

            def do_POST(self):
                try:
                    length = int(self.headers["Content-Length"])
                    if self.path != "/json_rpc" or not 0 < length <= 1_048_576:
                        raise ValueError("invalid fixture request")
                    body = self.rfile.read(length)
                    request = json.loads(body)
                    headers = {"Content-Type": "application/json"}
                    if "Authorization" in self.headers:
                        headers["Authorization"] = self.headers["Authorization"]
                    self.upstream.request("POST", "/json_rpc", body, headers)
                    response = self.upstream.getresponse()
                    payload = response.read(1_048_577)
                    if len(payload) > 1_048_576:
                        raise ValueError("oversized fixture response")
                    drop = request.get("method") == "relay_tx" and response.status == 200
                    if drop:
                        owner.relay_calls += 1
                        result = json.loads(payload)
                        txid = result.get("result", {}).get("tx_hash")
                        if "error" in result or not isinstance(txid, str) or len(txid) != 64:
                            raise ValueError("relay was not acknowledged upstream")
                        owner.accepted.append(txid)
                    self.send_response(response.status)
                    challenge = response.getheader("WWW-Authenticate")
                    if challenge is not None:
                        self.send_header("WWW-Authenticate", challenge)
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    if drop:
                        # Headers arrive, but the promised JSON body never does.
                        self.wfile.flush()
                        self.connection.shutdown(socket.SHUT_RDWR)
                        self.close_connection = True
                        return
                    self.wfile.write(payload)
                except Exception:
                    owner.failed = True
                    self.close_connection = True

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        assert not self.thread.is_alive()


def verify_exact_net_relay(wallet_rpc, node_port, mining_address, root, environment, executable,
                           lose_reply=False):
    """Real disposable payment, not authenticated protocol top-up or live trading."""
    addresses = {}
    prefix = "lost-relay" if lose_reply else "exact-relay"
    names = {kind: f"{prefix}-{kind}" for kind in ("source", "payout", "return")}
    for name in names.values():
        wallet_rpc("create_wallet", {"filename": name, "password": "", "language": "English"})
        addresses[name] = wallet_rpc("get_address")["address"]
        wallet_rpc("close_wallet")
    assert len(set(addresses.values())) == 3
    source = addresses[names["source"]]
    wallet_rpc("open_wallet", {"filename": "restored", "password": ""})
    txids = []
    for amount in (100_000_000_000, 10_000_000_000):
        wallet_rpc("refresh")
        txids.append(wallet_rpc("transfer", {"destinations": [{"address": source, "amount": amount}],
                                             "priority": 1})["tx_hash"])
        rpc(node_port, "generateblocks", {"wallet_address": mining_address, "amount_of_blocks": 10})
    assert txids[0] != txids[1]
    wallet_rpc("close_wallet")
    wallet_rpc("open_wallet", {"filename": names["source"], "password": ""})
    wallet_rpc("scan_tx", {"txids": txids})
    wallet_rpc("refresh")
    assert wallet_rpc("get_balance")["unlocked_balance"] == 110_000_000_000
    assert rpc(node_port, "get_info")["tx_pool_size"] == 0
    directory = root / f"{prefix}-journal"
    directory.mkdir(mode=0o700)
    context = dict(environment, MONZERO_TEST_TOP_UP_WALLET=source,
        MONZERO_TEST_TOP_UP_DESTINATION=addresses[names["payout"]],
        MONZERO_TEST_TOP_UP_RETURN=addresses[names["return"]],
        MONZERO_TEST_TOP_UP_LOCK=txids[0], MONZERO_TEST_TOP_UP_TXID=txids[1],
        MONZERO_TEST_TOP_UP_JOURNAL=str(directory), MONZERO_TEST_TOP_UP_RELAY="1",
        MONZERO_TEST_TOP_UP_RECOVER_ONLY="0", MONZERO_TEST_TOP_UP_CHECK_UNCONFIRMED="0")
    command = [str(executable), "--exact", "native_exact_net_top_up_preview", "--ignored"]
    accepted = None
    if lose_reply:
        with LostRelayReply(int(environment["MONZERO_TEST_WALLET_RPC_PORT"])) as proxy:
            checked = subprocess.run(command, env=dict(context,
                MONZERO_TEST_WALLET_RPC_PORT=str(proxy.server.server_port),
                MONZERO_TEST_TOP_UP_EXPECT_UNKNOWN="1"),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        assert not proxy.failed and proxy.relay_calls == 1 and len(proxy.accepted) == 1
        accepted = proxy.accepted[0]
    else:
        checked = subprocess.run(command, env=dict(context, MONZERO_TEST_TOP_UP_EXPECT_UNKNOWN="0"),
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if checked.returncode != 0 or b"1 passed" not in checked.stdout:
        raise RuntimeError("Journal-backed exact-net relay failed: " +
                           (checked.stdout + checked.stderr).decode("utf-8", errors="replace")[-2000:])
    prepared_paths = list(directory.glob("*.prepared"))
    assert len(prepared_paths) == 1
    prepared = json.loads(prepared_paths[0].read_text(encoding="ascii"))
    if lose_reply:
        assert accepted == prepared["txid"]
    assert prepared["lockTxid"] == txids[0] and prepared["topUpTxid"] == txids[1]
    assert rpc(node_port, "get_info")["tx_pool_size"] == 1
    rpc(node_port, "generateblocks", {"wallet_address": mining_address, "amount_of_blocks": 10})
    wallet_rpc("refresh")
    assert wallet_rpc("get_balance")["balance"] == 0
    outgoing = wallet_rpc("get_transfer_by_txid", {"txid": prepared["txid"]})["transfer"]
    assert outgoing["type"] == "out" and outgoing["confirmations"] >= 10
    assert outgoing["fee"] == int(prepared["feeAtoms"])
    assert outgoing["amount"] + outgoing["fee"] == 110_000_000_000
    retained = {p.name: p.read_bytes() for p in directory.iterdir()}
    checked = subprocess.run(command, env=dict(context, MONZERO_TEST_TOP_UP_RELAY="0",
        MONZERO_TEST_TOP_UP_RECOVER_ONLY="1", MONZERO_TEST_TOP_UP_CHECK_CONFIRMED="1",
        MONZERO_TEST_TOP_UP_EXPECTED_TXID=prepared["txid"]),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if checked.returncode != 0 or b"1 passed" not in checked.stdout:
        raise RuntimeError("Confirmed exact-net reconciliation failed: " +
                           (checked.stdout + checked.stderr).decode("utf-8", errors="replace")[-2000:])
    assert {p.name: p.read_bytes() for p in directory.iterdir()} == retained
    assert not list(directory.glob("*.receipt"))
    assert rpc(node_port, "get_info")["tx_pool_size"] == 0
    wallet_rpc("close_wallet")
    for name, expected in ((names["payout"], 100_000_000_000),
                           (names["return"], int(prepared["reserveReturnAtoms"]))):
        wallet_rpc("open_wallet", {"filename": name, "password": ""})
        wallet_rpc("refresh")
        assert wallet_rpc("get_balance")["balance"] == expected
        received = wallet_rpc("get_transfer_by_txid", {"txid": prepared["txid"]})["transfer"]
        assert received["type"] == "in" and received["amount"] == expected and received["confirmations"] >= 10
        wallet_rpc("close_wallet")
    assert int(prepared["reserveReturnAtoms"]) + int(prepared["feeAtoms"]) == 10_000_000_000
    print("PASS: journal-backed exact-net relay confirms full principal and reserve return, zero source residual; "
          "fresh-process reconciliation succeeds without a receipt (isolated payment, not full protocol settlement)")
    if lose_reply:
        print("PASS: real exact-net relay accepted once with reply dropped; native outcome unknown, "
              "reservation reuse rejected, fresh process confirms original transaction without rebroadcast")


def verify_topup_dispatch_and_claim(wallet_rpc, node_port, mining_address, root, environment, executable):
    """Real disposable payments proving the delayed-top-up dispatch and two-source
    exact-net claim primitives end to end. Native mechanics only: the recovered
    swap wallet's keys and lock funding are locally minted stand-ins, not an
    authenticated protocol negotiation or Bitcoin-observed redemption."""
    wallet_rpc("create_wallet", {"filename": "claim-swap", "password": "", "language": "English"})
    swap_address = wallet_rpc("get_address")["address"]
    swap_spend = wallet_rpc("query_key", {"key_type": "spend_key"})["key"]
    swap_view = wallet_rpc("query_key", {"key_type": "view_key"})["key"]
    restore_height = rpc(node_port, "get_info")["height"]
    wallet_rpc("close_wallet")

    addresses = {}
    for name in ("claim-buyer", "claim-payout", "claim-return"):
        wallet_rpc("create_wallet", {"filename": name, "password": "", "language": "English"})
        addresses[name] = wallet_rpc("get_address")["address"]
        wallet_rpc("close_wallet")
    assert len({swap_address, *addresses.values()}) == 4

    lock_amount, reserve_amount = 100_000_000_000, 10_000_000_000
    wallet_rpc("open_wallet", {"filename": "restored", "password": ""})
    wallet_rpc("refresh")
    lock_txid = wallet_rpc("transfer", {"destinations": [
        {"address": swap_address, "amount": lock_amount}], "priority": 1})["tx_hash"]
    rpc(node_port, "generateblocks", {"wallet_address": mining_address, "amount_of_blocks": 10})
    wallet_rpc("refresh")
    buyer_txid = wallet_rpc("transfer", {"destinations": [
        {"address": addresses["claim-buyer"], "amount": reserve_amount * 2}], "priority": 1})["tx_hash"]
    rpc(node_port, "generateblocks", {"wallet_address": mining_address, "amount_of_blocks": 10})
    wallet_rpc("close_wallet")

    wallet_rpc("open_wallet", {"filename": "claim-buyer", "password": ""})
    wallet_rpc("scan_tx", {"txids": [buyer_txid]})
    wallet_rpc("refresh")
    assert wallet_rpc("get_balance")["unlocked_balance"] == reserve_amount * 2

    topup_journal = root / "topup-dispatch-journal"
    topup_journal.mkdir(mode=0o700)
    topup_result = root / "topup-dispatch-result"
    topup_environment = dict(environment,
        MONZERO_TEST_TOPUP_BUYER_WALLET=addresses["claim-buyer"], MONZERO_TEST_TOPUP_DESTINATION=swap_address,
        MONZERO_TEST_TOPUP_LOCK=lock_txid, MONZERO_TEST_TOPUP_RESERVE_ATOMS=str(reserve_amount),
        MONZERO_TEST_TOPUP_MAX_FEE_ATOMS=str(1_000_000_000), MONZERO_TEST_TOPUP_JOURNAL=str(topup_journal),
        MONZERO_TEST_TOPUP_RESULT=str(topup_result))
    checked = subprocess.run([str(executable), "--exact", "native_topup_dispatch", "--ignored"],
        env=topup_environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if checked.returncode != 0 or b"1 passed" not in checked.stdout:
        raise RuntimeError("Top-up dispatch fixture failed: " +
            (checked.stdout + checked.stderr).decode("utf-8", errors="replace")[-2000:])
    if not topup_result.is_file():
        raise RuntimeError("Missing top-up dispatch result marker")
    topup_txid = topup_result.read_text(encoding="ascii").strip()
    assert len(topup_txid) == 64 and all(c in "0123456789abcdef" for c in topup_txid)
    wallet_rpc("close_wallet")
    rpc(node_port, "generateblocks", {"wallet_address": mining_address, "amount_of_blocks": 10})
    print("PASS: buyer-funded top-up dispatch delivers the reserve to the recovered swap destination "
          "with a durable journal receipt (native mechanics only, not protocol-authenticated top-up)")

    claim_journal = root / "exact-net-claim-journal"
    claim_journal.mkdir(mode=0o700)
    claim_result = root / "exact-net-claim-result"
    claim_environment = dict(environment,
        MONZERO_TEST_CLAIM_WALLET=swap_address, MONZERO_TEST_CLAIM_DESTINATION=addresses["claim-payout"],
        MONZERO_TEST_CLAIM_RETURN=addresses["claim-return"], MONZERO_TEST_CLAIM_SPEND_KEY=swap_spend,
        MONZERO_TEST_CLAIM_VIEW_KEY=swap_view, MONZERO_TEST_CLAIM_RESTORE_HEIGHT=str(restore_height),
        MONZERO_TEST_CLAIM_LOCK_TXID=lock_txid, MONZERO_TEST_CLAIM_TOPUP_TXID=topup_txid,
        MONZERO_TEST_CLAIM_LOCKED_ATOMS=str(lock_amount + reserve_amount),
        MONZERO_TEST_CLAIM_PRINCIPAL_ATOMS=str(lock_amount), MONZERO_TEST_CLAIM_MAX_FEE_ATOMS=str(2_000_000_000),
        MONZERO_TEST_CLAIM_JOURNAL=str(claim_journal), MONZERO_TEST_CLAIM_RESULT=str(claim_result))
    checked = subprocess.run([str(executable), "--exact", "native_exact_net_claim", "--ignored"],
        env=claim_environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if checked.returncode != 0 or b"1 passed" not in checked.stdout:
        raise RuntimeError("Two-source exact-net claim fixture failed: " +
            (checked.stdout + checked.stderr).decode("utf-8", errors="replace")[-2000:])
    if not claim_result.is_file():
        raise RuntimeError("Missing exact-net claim result marker")
    claim_txid = claim_result.read_text(encoding="ascii").strip()
    assert len(claim_txid) == 64 and all(c in "0123456789abcdef" for c in claim_txid)
    rpc(node_port, "generateblocks", {"wallet_address": mining_address, "amount_of_blocks": 10})

    wallet_rpc("open_wallet", {"filename": "claim-payout", "password": ""})
    wallet_rpc("refresh")
    payout_balance = wallet_rpc("get_balance")
    assert payout_balance["balance"] == payout_balance["unlocked_balance"] == lock_amount
    received = wallet_rpc("get_transfer_by_txid", {"txid": claim_txid})["transfer"]
    assert received["type"] == "in" and received["amount"] == lock_amount and received["confirmations"] >= 10
    wallet_rpc("close_wallet")

    wallet_rpc("open_wallet", {"filename": "claim-return", "password": ""})
    wallet_rpc("refresh")
    assert wallet_rpc("get_balance")["balance"] > 0
    wallet_rpc("close_wallet")

    wallet_rpc("open_wallet", {"filename": "claim-swap", "password": ""})
    wallet_rpc("refresh")
    assert wallet_rpc("get_balance")["balance"] == 0
    wallet_rpc("close_wallet")

    print("PASS: two-source exact-net claim delivers the exact quoted principal and returns unused "
          "reserve; recovered swap wallet has zero residual (native mechanics only, not full "
          "delayed-top-up protocol negotiation)")


def verify_raw_restart_candidate(wallet_rpc, node_port, mining_address, root,
                                 sealed=False, environment=None):
    """Research only: compare raw replay with encrypted pending metadata.

    Does not issue a native engine recovery capability or change its journal.
    Observe wallet bookkeeping explicitly rather than assuming raw relay is a
    drop-in replacement for wallet relay_tx/commit_tx. The sealed variant keeps
    its temporary key in the parent, not durable production storage.
    """
    addresses = {}
    prefix = "sealed" if sealed else "raw"
    names = {kind: f"{prefix}-{kind}" for kind in ("source", "payout", "return")}
    for name in names.values():
        wallet_rpc("create_wallet", {"filename": name, "password": "", "language": "English"})
        addresses[name] = wallet_rpc("get_address")["address"]
        wallet_rpc("close_wallet")
    wallet_rpc("open_wallet", {"filename": "restored", "password": ""})
    wallet_rpc("refresh")
    funding = wallet_rpc("transfer", {"destinations": [
        {"address": addresses[names["source"]], "amount": 110_000_000_000}], "priority": 1})["tx_hash"]
    rpc(node_port, "generateblocks", {"wallet_address": mining_address, "amount_of_blocks": 10})
    wallet_rpc("close_wallet")
    wallet_rpc("open_wallet", {"filename": names["source"], "password": ""})
    wallet_rpc("scan_tx", {"txids": [funding]})
    wallet_rpc("refresh")
    prepared = wallet_rpc("transfer", {"destinations": [
        {"address": addresses[names["payout"]], "amount": 100_000_000_000},
        {"address": addresses[names["return"]], "amount": 10_000_000_000}],
        "priority": 1, "subtract_fee_from_outputs": [1], "do_not_relay": True,
        "get_tx_hex": not sealed, "get_tx_metadata": sealed, "get_tx_key": False})
    assert prepared["tx_key"] == ""
    assert prepared["tx_blob" if sealed else "tx_metadata"] == ""
    assert 0 < prepared["fee"] < 10_000_000_000
    assert rpc(node_port, "get_info")["tx_pool_size"] == 0
    blob = prepared.pop("tx_metadata" if sealed else "tx_blob")
    assert 0 < len(blob) <= 1_048_576 and len(blob) % 2 == 0
    assert all(c in "0123456789abcdef" for c in blob)
    key = None
    aad = None
    if sealed:
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        key = AESGCM.generate_key(bit_length=256)
        nonce = os.urandom(12)
        aad = json.dumps({"schema": "fixture-only-pending-v1", "txid": prepared["tx_hash"],
                          "wallet": addresses[names["source"]], "genesis": GENESIS,
                          "fundingTxid": funding, "destination": addresses[names["payout"]],
                          "principalAtoms": "100000000000",
                          "reserveReturnDestination": addresses[names["return"]],
                          "reserveReturnAtoms": str(10_000_000_000 - prepared["fee"]),
                          "feeAtoms": str(prepared["fee"])}, sort_keys=True).encode("ascii")
        artifact = nonce + AESGCM(key).encrypt(nonce, blob.encode("ascii"), aad)
    else:
        artifact = blob.encode("ascii")
    saved = root / f"{prefix}-restart-transaction"
    with open(os.open(saved, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as out:
        out.write(artifact)
        out.flush()
        os.fsync(out.fileno())
    del blob
    wallet_rpc("close_wallet")
    # Each invocation is a new process, with only the same immutable raw bytes.
    script = """
import pathlib, requests, sys
with requests.Session() as client:
    client.trust_env = False
    response = client.post('http://127.0.0.1:' + sys.argv[1] + '/sendrawtransaction',
        json={'tx_as_hex': pathlib.Path(sys.argv[2]).read_text(encoding='ascii'),
              'do_not_relay': False}, timeout=15, allow_redirects=False)
    assert response.status_code == 200
    assert response.json()['status'] == 'OK'
"""
    if sealed:
        # Test-only key transport: no key in argv, environment, journal or logs.
        # Parent memory survives this experiment; production key storage is unresolved.
        wallet_rpc("open_wallet", {"filename": names["source"], "password": ""})
        sealed_script = """
import pathlib, sys
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.exceptions import InvalidTag
sys.path.insert(0, sys.argv[5])
from test_wallet_recovery import rpc
artifact = pathlib.Path(sys.argv[1]).read_bytes()
key = sys.stdin.buffer.read(33)
assert len(key) == 32
try:
    metadata = AESGCM(key).decrypt(artifact[:12], artifact[12:], sys.argv[4].encode('ascii'))
except InvalidTag:
    sys.exit(42)  # Expected authentication failure, before reaching any RPC.
result = rpc(int(sys.argv[2]), 'relay_tx', {'hex': metadata.decode('ascii')}, login=pathlib.Path(sys.argv[3]))
assert result['tx_hash'] == sys.argv[6]
"""
        command = [sys.executable, "-c", sealed_script, str(saved),
                   environment["MONZERO_TEST_WALLET_RPC_PORT"], environment["MONZERO_TEST_WALLET_RPC_LOGIN"],
                   aad.decode("ascii"), str(pathlib.Path(__file__).resolve().parent), prepared["tx_hash"]]
        wrong_key = bytes([key[0] ^ 1]) + key[1:]
        rejected = subprocess.run(command, input=wrong_key, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, timeout=20)
        assert rejected.returncode == 42 and rpc(node_port, "get_info")["tx_pool_size"] == 0
        retained_balance = wallet_rpc("get_balance")["balance"]
        for field in json.loads(aad):
            changed = json.loads(aad)
            changed[field] = "altered-fixture-context"
            altered_command = list(command)
            altered_command[6] = json.dumps(changed, sort_keys=True)
            rejected = subprocess.run(altered_command, input=key, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL, timeout=20)
            assert rejected.returncode == 42, "changed context did not fail authentication"
        for label, corrupted in (("nonce", bytes([artifact[0] ^ 1]) + artifact[1:]),
                                  ("ciphertext", artifact[:-1] + bytes([artifact[-1] ^ 1])),
                                  ("truncated", artifact[:-1])):
            damaged = root / f"sealed-restart-{label}"
            with open(os.open(damaged, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as out:
                out.write(corrupted)
            altered_command = list(command)
            altered_command[3] = str(damaged)
            rejected = subprocess.run(altered_command, input=key, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL, timeout=20)
            assert rejected.returncode == 42, "changed artifact did not fail authentication"
        assert rpc(node_port, "get_info")["tx_pool_size"] == 0
        assert wallet_rpc("get_balance")["balance"] == retained_balance
        assert saved.read_bytes() == artifact
        # A populated different wallet has a valid saved index but a different
        # key image. RPC must reject before the daemon sees the transaction.
        wallet_rpc("close_wallet")
        wallet_rpc("open_wallet", {"filename": "raw-source", "password": ""})
        wrong_wallet_balance = wallet_rpc("get_balance")["balance"]
        rejected = subprocess.run(command, input=key, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, timeout=20)
        assert rejected.returncode != 0, "different wallet accepted prepared metadata"
        assert rpc(node_port, "get_info")["tx_pool_size"] == 0
        assert wallet_rpc("get_balance")["balance"] == wrong_wallet_balance
        wallet_rpc("close_wallet")
        wallet_rpc("open_wallet", {"filename": names["source"], "password": ""})
        checked = subprocess.run(command, input=key, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, timeout=20)
        assert checked.returncode == 0, "sealed restart candidate submission rejected"
        assert rpc(node_port, "get_info")["tx_pool_size"] == 1
        assert saved.read_bytes() == artifact
        wallet_rpc("close_wallet")
    else:
        for _ in range(2):
            checked = subprocess.run([sys.executable, "-c", script, str(node_port), str(saved)],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
            assert checked.returncode == 0, "raw restart candidate submission rejected"
            assert rpc(node_port, "get_info")["tx_pool_size"] == 1
    rpc(node_port, "generateblocks", {"wallet_address": mining_address, "amount_of_blocks": 10})
    wallet_rpc("open_wallet", {"filename": names["source"], "password": ""})
    wallet_rpc("refresh")
    assert wallet_rpc("get_balance")["balance"] == 0
    outgoing = wallet_rpc("get_transfer_by_txid", {"txid": prepared["tx_hash"]})["transfer"]
    assert outgoing["type"] == "out" and outgoing["fee"] == prepared["fee"]
    destinations = outgoing.get("destinations", [])
    expected = {addresses[names["payout"]]: 100_000_000_000,
                addresses[names["return"]]: 10_000_000_000 - prepared["fee"]}
    bookkeeping = {d["address"]: d["amount"] for d in destinations} == expected
    wallet_rpc("close_wallet")
    for name in (names["payout"], names["return"]):
        wallet_rpc("open_wallet", {"filename": name, "password": ""})
        wallet_rpc("refresh")
        assert wallet_rpc("get_balance")["balance"] == expected[addresses[name]]
        received = wallet_rpc("get_transfer_by_txid", {"txid": prepared["tx_hash"]})["transfer"]
        assert received["amount"] == expected[addresses[name]] and received["confirmations"] >= 10
        wallet_rpc("close_wallet")
    if sealed:
        assert bookkeeping, "sealed metadata failed to preserve exact destination bookkeeping"
        print("RESEARCH: encrypted pending metadata restored in fresh worker after wallet reopen; "
              "wrong key, altered context/ciphertext and wrong wallet block broadcast; "
              "original payment confirms with both destinations retained. "
              "Temporary parent-held key only; not production key storage or native recovery authorization.")
    else:
        print("RESEARCH: same raw transaction submitted by two fresh processes confirms once; "
              f"wallet destination bookkeeping retained={bookkeeping}. Not native restart authorization.")


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def rpc(number, method, params=None, login=None):
    credentials = None
    if login is not None:
        metadata = login.lstat()
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_mode & 0o077 or metadata.st_size > 1024:
            raise RuntimeError("Nonprivate fixture credentials")
        credentials = login.read_text(encoding="ascii").split(":", 1)
        if len(credentials) != 2 or not all(credentials):
            raise RuntimeError("Invalid fixture credentials")
    with requests.Session() as client:
        client.trust_env = False
        if credentials is not None:
            client.auth = HTTPDigestAuth(*credentials)
        with client.post(f"http://127.0.0.1:{number}/json_rpc",
                json={"jsonrpc": "2.0", "id": "fixture", "method": method, "params": params or {}},
                timeout=15, allow_redirects=False, stream=True) as response:
            if response.status_code != 200:
                raise RuntimeError("Fixture RPC HTTP failure")
            body = bytearray()
            for chunk in response.iter_content(4096):
                body.extend(chunk)
                if len(body) > 1048576:
                    raise RuntimeError("Fixture RPC response too large")
    value = json.loads(body)
    if value.get("id") != "fixture" or "error" in value or "result" not in value:
        # Never include responses/parameters, which may contain disposable keys.
        detail = ""
        if method in {"refresh", "transfer", "scan_tx", "sweep_all"} and isinstance(value.get("error"), dict):
            detail = ": " + str(value["error"].get("message", ""))[:180]
        raise RuntimeError(f"Fixture RPC {method} failed{detail}")
    return value["result"]


def ready(process, number, method, login=None):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("Fixture process exited before readiness")
        try:
            return rpc(number, method, login=login)
        except (OSError, RuntimeError, requests.RequestException):
            time.sleep(0.1)
    raise RuntimeError("Fixture readiness timeout")


def run(daemon, wallet, parent, address_vectors=None, engine_test_binary=None,
        engine_test_name="tests::calculate_full_refund_transaction_weights", fee_preview_helper=None,
        qt_fee_driver=None, native_spendable=False):
    if native_spendable and (engine_test_binary is None or engine_test_name not in {
            "engine_redeem_is_accepted_on_regtest",
            "engine_cancel_and_full_refund_are_accepted_on_regtest"}):
        raise ValueError("Native spending requires real Bitcoin settlement and recovered keys")
    vectors = None
    if address_vectors is not None:
        if address_vectors.stat().st_size > 2048:
            raise RuntimeError("Address fixture too large")
        vectors = address_vectors.read_text(encoding="ascii").splitlines()
        if len(vectors) != 9 or any(not line or len(line) > 110 for line in vectors):
            raise RuntimeError("Expected nine bounded public address vectors")
    with tempfile.TemporaryDirectory(prefix="xmz-recovery-", dir=parent) as directory:
        root = pathlib.Path(directory)
        processes = []
        try:
            node_port, wallet_port, p2p_port = port(), port(), port()
            if len({node_port, wallet_port, p2p_port}) != 3:
                raise RuntimeError("Ephemeral port collision; rerun fixture")
            login = root / f"monzero-wallet-rpc.{wallet_port}.login"
            def wallet_rpc(method, params=None):
                return rpc(wallet_port, method, params, login=login)
            engine_keys = None
            engine_request = None
            node = subprocess.Popen([str(daemon), "--regtest", "--keep-fakechain",
                "--fixed-difficulty", "1", "--offline", "--no-igd", "--hide-my-port",
                "--no-zmq", "--non-interactive", "--data-dir", str(root / "chain"),
                "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(node_port),
                "--p2p-bind-ip", "127.0.0.1", "--p2p-bind-port", str(p2p_port),
                "--log-file", str(root / "node.log")], stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL)
            processes.append(node)
            info = ready(node, node_port, "get_info")
            assert info["nettype"] == "fakechain" and info["height"] == 1
            assert info["incoming_connections_count"] == info["outgoing_connections_count"] == 0
            assert rpc(node_port, "get_block", {"height": 0})["block_header"]["hash"] == GENESIS
            (root / "wallets").mkdir(mode=0o700)
            server = subprocess.Popen([str(wallet), "--wallet-dir", str(root / "wallets"),
                "--daemon-address", f"127.0.0.1:{node_port}", "--trusted-daemon",
                # Only after verifying our owned offline fakechain above. Fakechain
                # uses HF16 at height 1 rather than the normal wallet fork schedule.
                "--allow-mismatched-daemon-version",
                "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(wallet_port),
                "--non-interactive", "--max-concurrency", "2",
                "--log-file", str(root / "wallet.log")], cwd=root, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL)
            processes.append(server)
            ready(server, wallet_port, "get_version", login=login)
            if vectors is not None:
                for index, vector in enumerate(vectors):
                    result = wallet_rpc("validate_address", {
                        "address": vector, "any_net_type": True, "allow_openalias": False})
                    assert result["valid"]
                    assert result["nettype"] == ("mainnet", "testnet", "stagenet")[index // 3]
                    assert result["integrated"] == (index % 3 == 1)
                    assert result["subaddress"] == (index % 3 == 2)
                    damaged = vector[:-1] + ("1" if vector[-1] != "1" else "2")
                    assert not wallet_rpc("validate_address", {
                        "address": damaged, "any_net_type": True, "allow_openalias": False})["valid"]
                print("PASS: native wallet validates nine engine address vectors and rejects damaged checksums")
            wallet_rpc("create_wallet", {"filename": "original", "password": "",
                                               "language": "English"})
            address = wallet_rpc("get_address")["address"]
            spend = wallet_rpc("query_key", {"key_type": "spend_key"})["key"]
            view = wallet_rpc("query_key", {"key_type": "view_key"})["key"]
            rpc(node_port, "generateblocks", {"wallet_address": address, "amount_of_blocks": 1})
            wallet_rpc("refresh", {"start_height": 0})
            before = wallet_rpc("get_balance")
            assert type(before["balance"]) is int and before["balance"] > 0
            wallet_rpc("close_wallet")
            wallet_rpc("generate_from_keys", {"filename": "restored", "password": "",
                "address": address, "spendkey": spend, "viewkey": view, "restore_height": 0})
            wallet_rpc("refresh", {"start_height": 0})
            assert wallet_rpc("get_address")["address"] == address
            after = wallet_rpc("get_balance")
            assert after["balance"] == before["balance"]
            assert after["unlocked_balance"] == before["unlocked_balance"]
            wallet_rpc("close_wallet")
            wallet_rpc("open_wallet", {"filename": "restored", "password": ""})
            assert wallet_rpc("get_balance")["balance"] == before["balance"]
            wallet_rpc("close_wallet")
            print("PASS: fakechain key restoration preserves address and balances across wallet reopen")
            # Fund a separate address with a normal (non-coinbase) transaction.
            wallet_rpc("create_wallet", {"filename": "recipient", "password": "",
                                               "language": "English"})
            recipient = wallet_rpc("get_address")["address"]
            recipient_spend = wallet_rpc("query_key", {"key_type": "spend_key"})["key"]
            recipient_view = wallet_rpc("query_key", {"key_type": "view_key"})["key"]
            wallet_rpc("close_wallet")
            # Both final destinations exist before negotiation. The shared lock
            # remains protocol-derived; these addresses are for later proceeds.
            destinations = {}
            for name in ("payout", "refund"):
                wallet_rpc("create_wallet", {"filename": name, "password": "",
                                             "language": "English"})
                destinations[name] = wallet_rpc("get_address")["address"]
                wallet_rpc("close_wallet")
            payout_address, refund_address = destinations["payout"], destinations["refund"]
            assert payout_address != refund_address
            destination_name = ("refund" if engine_test_name ==
                "engine_cancel_and_full_refund_are_accepted_on_regtest" else "payout")
            destination = destinations[destination_name]
            if engine_test_binary is not None:
                exported = root / "engine-recovery.txt"
                lock_file = root / "engine-lock.txt"
                release_file = root / "engine-release"
                quote_checked = root / "engine-quote-checked.json"
                environment = dict(os.environ, MONZERO_TEST_RECOVERY_EXPORT=str(exported),
                    MONZERO_TEST_LOCK_EXPORT=str(lock_file), MONZERO_TEST_RELEASE=str(release_file),
                    MONZERO_TEST_DAEMON_RPC_PORT=str(node_port), MONZERO_TEST_WALLET_RPC_PORT=str(wallet_port),
                    MONZERO_TEST_WALLET_RPC_LOGIN=str(login),
                    MONZERO_TEST_QUOTE_CHECKED=str(quote_checked),
                    MONZERO_TEST_XMZ_PAYOUT=payout_address, MONZERO_TEST_XMZ_REFUND=refund_address)
                engine = subprocess.Popen([str(engine_test_binary), "--exact",
                    engine_test_name], env=environment,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                processes.append(engine)
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    if engine.poll() is not None:
                        raise RuntimeError("Engine exited before funding request")
                    if lock_file.exists() and lock_file.stat().st_size <= 256:
                        candidate = lock_file.read_text(encoding="ascii").splitlines()
                        if len(candidate) == 3 and lock_file.read_bytes().endswith(b"\n"):
                            engine_request = candidate
                            break
                    time.sleep(0.02)
                if engine_request is None:
                    raise RuntimeError("Engine funding request timeout")
                atoms = engine_request[1]
                if len(engine_request[2]) != 64 or any(c not in "0123456789abcdef" for c in engine_request[2]):
                    raise RuntimeError("Invalid engine observer view key")
                if not atoms.isascii() or not atoms.isdecimal() or atoms.startswith("0") or int(atoms) > 2**64 - 1:
                    raise RuntimeError("Invalid engine fixture amount")
                recipient = engine_request[0]
                assert recipient not in destinations.values(), "Shared lock is a final destination"
                if environment.get("MONZERO_TEST_QT_QUOTE_DRIVER"):
                    assert quote_checked.is_file() and quote_checked.stat().st_size <= 1024
                    checked_quote = json.loads(quote_checked.read_text(encoding="ascii"))
                    assert checked_quote["xmzPayout"] == payout_address
                    assert checked_quote["xmzRefund"] == refund_address
                    assert len(checked_quote["commitment"]) == 64
                    destination = checked_quote["xmzRefund" if destination_name == "refund" else "xmzPayout"]
                    print("PASS: authenticated peer negotiation and both Qt-reserved quote preflights completed before Bitcoin lock")
                print("PASS: distinct payout/refund wallets exist before engine negotiation")
            # Mature the original reward and supply decoys, keeping RPC work bounded.
            for _ in range(13):
                rpc(node_port, "generateblocks", {"wallet_address": address, "amount_of_blocks": 10})
            wallet_rpc("open_wallet", {"filename": "restored", "password": ""})
            wallet_rpc("refresh")
            amount = 200_000_000_000  # 2 XMZ, preserving native atoms.
            if engine_request is not None:
                amount = int(engine_request[1])
                assert not exported.exists(), "Recovery key exposed before funding"
            # Construction is not read-only: exercise only this owned disposable wallet.
            # Do not export a signed blob, transaction key or relay metadata for a preview.
            prior_balance = wallet_rpc("get_balance")
            prior_chain = rpc(node_port, "get_info")
            assert prior_chain["tx_pool_size"] == 0
            estimate = wallet_rpc("transfer", {"destinations": [{"address": recipient,
                "amount": amount}], "priority": 1, "do_not_relay": True,
                "get_tx_key": False, "get_tx_hex": False, "get_tx_metadata": False})
            assert type(estimate["fee"]) is int and 0 < estimate["fee"] <= 2**64 - 1
            assert type(estimate["weight"]) is int and estimate["weight"] > 0
            assert estimate["amount"] == amount
            for field in ("tx_key", "tx_blob", "tx_metadata", "multisig_txset", "unsigned_txset"):
                assert estimate[field] == "", "Preview exported signing material"
            following_balance = wallet_rpc("get_balance")
            assert following_balance["balance"] == prior_balance["balance"]
            assert following_balance["unlocked_balance"] == prior_balance["unlocked_balance"]
            following_chain = rpc(node_port, "get_info")
            assert following_chain["height"] == prior_chain["height"]
            assert following_chain["tx_pool_size"] == 0
            # The final transfer is constructed separately and may have a different fee.
            del estimate
            print("PASS: non-relayed construction returns fee/weight without exported signing artifacts or balance/pool changes")
            if engine_test_binary is not None:
                fee_result = root / "fee-estimation-result"
                fee_preview = root / "fee-preview.json"
                fee_request_id = str(uuid.uuid4())
                fee_environment = dict(environment, MONZERO_TEST_FEE_WALLET=address,
                    MONZERO_TEST_FEE_DESTINATION=recipient, MONZERO_TEST_FEE_ATOMS=str(amount),
                    MONZERO_TEST_FEE_RESULT=str(fee_result), MONZERO_TEST_FEE_PREVIEW=str(fee_preview),
                    MONZERO_TEST_FEE_REQUEST_ID=fee_request_id)
                checked = subprocess.run([str(engine_test_binary), "--exact", "native_fee_estimation", "--ignored"],
                    env=fee_environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=40)
                if checked.returncode != 0:
                    raise RuntimeError("Rust native fee estimator fixture failed")
                if not fee_result.is_file() or fee_result.stat().st_size != 24:
                    raise RuntimeError("Rust native fee estimator did not produce its completion marker")
                if fee_result.read_bytes() != b"fee-estimation-verified\n":
                    raise RuntimeError("Invalid fee estimator completion marker")
                if not fee_preview.is_file() or fee_preview.stat().st_size > 2048:
                    raise RuntimeError("Missing or oversized fee preview")
                preview = json.loads(fee_preview.read_bytes())
                assert preview["schema"] == "monzero-native-fee-preview-v1"
                assert preview["requestId"] == fee_request_id
                assert preview["wallet"] == address and preview["destination"] == recipient
                assert preview["amountAtoms"] == str(amount)
                assert type(preview["feeAtoms"]) is str and 0 < int(preview["feeAtoms"]) <= 2**64 - 1
                assert preview["genesis"] == GENESIS and preview["executable"] is False
                assert len(preview) == 14
                after_estimator = wallet_rpc("get_balance")
                assert after_estimator["balance"] == prior_balance["balance"]
                assert after_estimator["unlocked_balance"] == prior_balance["unlocked_balance"]
                assert rpc(node_port, "get_info")["tx_pool_size"] == 0
                print("PASS: Rust fee estimator rejects wrong chain/wallet and constructs a bound non-relayed estimate")
                if fee_preview_helper is not None:
                    session_file = root / "fee-session.json"
                    username, password = login.read_text(encoding="ascii").split(":", 1)
                    with open(session_file, "x", opener=lambda path, flags: os.open(path, flags, 0o600)) as stream:
                        json.dump({"schema": "monzero-native-fee-session-v1", "network": "mainnet",
                            "genesis": GENESIS, "daemonPort": node_port, "walletPort": wallet_port,
                            "username": username, "password": password}, stream)
                    del username, password
                    wire = {"schema": "monzero-native-fee-request-v1", "requestId": str(uuid.uuid4()),
                        "wallet": address, "destination": recipient, "amountAtoms": str(amount),
                        "account": "0", "priority": "1", "genesis": GENESIS}
                    result = subprocess.run([str(fee_preview_helper), "--fee-preview-stdio-v1",
                        "--session-file", str(session_file)], input=json.dumps(wire).encode(),
                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=35, env={"LANG": "C"})
                    if result.returncode != 0 or len(result.stdout) > 2048:
                        raise RuntimeError("Native preview executable failed")
                    response = json.loads(result.stdout)
                    assert response["schema"] == "monzero-native-fee-preview-v1"
                    assert response["requestId"] == wire["requestId"] and response["executable"] is False
                    for field in ("wallet", "destination", "amountAtoms", "account", "priority", "genesis"):
                        assert response[field] == wire[field]
                    assert len(response) == 14 and int(response["feeAtoms"]) > 0
                    assert wallet_rpc("get_balance")["balance"] == prior_balance["balance"]
                    assert wallet_rpc("get_balance")["unlocked_balance"] == prior_balance["unlocked_balance"]
                    assert rpc(node_port, "get_info")["tx_pool_size"] == 0
                    if qt_fee_driver is not None:
                        checked = subprocess.run([str(qt_fee_driver), "--native-fee-job-fixture",
                            str(fee_preview_helper), str(session_file)], input=json.dumps(wire).encode(),
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=35, env={"LANG": "C"})
                        if checked.returncode != 0 or len(checked.stdout) > 2048:
                            raise RuntimeError("Qt/native fee round trip failed")
                        displayed = json.loads(checked.stdout)
                        assert displayed["valid"] is True and displayed["executable"] is False
                        assert displayed["transportAuthenticated"] is False
                        assert displayed["amountAtoms"] == str(amount) and int(displayed["feeAtoms"]) > 0
                        assert str(uuid.UUID(displayed["requestId"])) == displayed["requestId"]
                        assert wallet_rpc("get_balance")["balance"] == prior_balance["balance"]
                        assert wallet_rpc("get_balance")["unlocked_balance"] == prior_balance["unlocked_balance"]
                        assert rpc(node_port, "get_info")["tx_pool_size"] == 0
                        print("PASS: Qt job launches native helper and accepts its bound display-only preview without broadcast")
                    session_file.unlink()
                    print("PASS: native executable returns a bound fee preview over OS pipes without broadcast or balance changes")
            sent = wallet_rpc("transfer", {"destinations": [{"address": recipient,
                "amount": amount}], "priority": 1, "get_tx_key": engine_request is not None})
            txid = sent["tx_hash"]
            assert len(txid) == 64
            wallet_rpc("close_wallet")
            rpc(node_port, "generateblocks", {"wallet_address": address, "amount_of_blocks": 10})
            height = rpc(node_port, "get_info")["height"]
            if engine_request is not None:
                assert not exported.exists(), "Recovery key exposed before release"
                wallet_rpc("open_wallet", {"filename": "restored", "password": ""})
                wallet_rpc("refresh")
                confirmed = wallet_rpc("get_transfer_by_txid", {"txid": txid})["transfer"]
                assert confirmed["type"] == "out" and confirmed["height"] > 0
                assert confirmed["amount"] == amount and confirmed["confirmations"] >= 10
                wallet_rpc("close_wallet")
                wallet_rpc("generate_from_keys", {"filename": "lock-observer", "password": "",
                    "address": recipient, "viewkey": engine_request[2], "restore_height": 0})
                wallet_rpc("refresh", {"start_height": 0})
                observed = wallet_rpc("get_transfer_by_txid", {"txid": txid})["transfer"]
                assert observed["type"] == "in" and observed["height"] > 0
                assert observed["amount"] == amount and observed["confirmations"] >= 10
                assert wallet_rpc("get_balance")["balance"] == amount
                checked = wallet_rpc("check_tx_key", {"txid": txid,
                    "tx_key": sent["tx_key"], "address": recipient})
                assert checked["received"] == amount and not checked["in_pool"]
                assert checked["confirmations"] >= 10
                wrong_key = "02" + "00" * 31
                assert wrong_key != sent["tx_key"]
                rejected_key = wallet_rpc("check_tx_key", {"txid": txid,
                    "tx_key": wrong_key, "address": recipient})
                assert rejected_key["received"] == 0
                rejected_address = wallet_rpc("check_tx_key", {"txid": txid,
                    "tx_key": sent["tx_key"], "address": destination})
                assert rejected_address["received"] == 0
                assert not exported.exists(), "Spend key exposed before receiver verification"
                # Publish complete bounded evidence atomically; never log the transaction key.
                evidence = {"genesis": GENESIS, "address": recipient, "atoms": str(amount),
                    "txid": txid, "tx_key": sent["tx_key"], "height": observed["height"],
                    "confirmations": observed["confirmations"]}
                pending = root / "engine-release.pending"
                with pending.open("x") as output:
                    os.chmod(pending, 0o600)
                    json.dump(evidence, output)
                    output.flush()
                    os.fsync(output.fileno())
                assert not release_file.exists(), "Unexpected existing release file"
                os.rename(pending, release_file)
                if engine.wait(timeout=60) != 0:
                    raise RuntimeError("Engine recovery fixture failed")
                wallet_rpc("close_wallet")
                if exported.stat().st_size > 512:
                    raise RuntimeError("Oversized engine fixture")
                engine_keys = exported.read_text(encoding="ascii").splitlines()
                if len(engine_keys) != 4 or any(len(k) != 64 or any(c not in "0123456789abcdef" for c in k) for k in engine_keys[1:3]):
                    raise RuntimeError("Invalid disposable engine fixture")
                assert engine_keys[0] == recipient and engine_keys[3] == str(amount)
                recipient_spend, recipient_view = engine_keys[1:3]
            wallet_rpc("generate_from_keys", {"filename": "scanned", "password": "",
                "address": recipient, "spendkey": recipient_spend, "viewkey": recipient_view,
                "restore_height": height})
            assert wallet_rpc("get_balance")["balance"] == 0
            wallet_rpc("scan_tx", {"txids": [txid]})
            wallet_rpc("refresh")
            scanned = wallet_rpc("get_balance")
            assert scanned["balance"] == scanned["unlocked_balance"] == amount
            # Scanning an already imported transaction must not double the balance.
            wallet_rpc("scan_tx", {"txids": [txid]})
            assert wallet_rpc("get_balance")["balance"] == amount
            # Prove the imported spend state is durable, not just in-process.
            wallet_rpc("close_wallet")
            server.terminate()
            server.wait(timeout=15)
            assert server.returncode == 0
            server = subprocess.Popen(server.args, cwd=root, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL)
            processes.append(server)
            ready(server, wallet_port, "get_version", login=login)
            wallet_rpc("open_wallet", {"filename": "scanned", "password": ""})
            resumed = wallet_rpc("get_balance")
            assert resumed["balance"] == resumed["unlocked_balance"] == amount
            if native_spendable:
                # Use actual protocol-recovered keys, not locally minted stand-ins.
                # Leave explicit fee headroom; this is a fixed-principal spend,
                # not a sweep or a claim that residual dust has been recovered.
                wallet_rpc("close_wallet")
                delivered = amount * 9 // 10
                result_file = root / "protocol-spend-result"
                spend_environment = dict(os.environ,
                    MONZERO_TEST_DAEMON_RPC_PORT=str(node_port),
                    MONZERO_TEST_WALLET_RPC_PORT=str(wallet_port),
                    MONZERO_TEST_WALLET_RPC_LOGIN=str(login),
                    MONZERO_TEST_SPEND_WALLET=recipient,
                    MONZERO_TEST_SPEND_DESTINATION=destination,
                    MONZERO_TEST_SPEND_KEY=recipient_spend,
                    MONZERO_TEST_SPEND_VIEW_KEY=recipient_view,
                    MONZERO_TEST_SPEND_RESTORE_HEIGHT=str(height),
                    MONZERO_TEST_SPEND_LOCK_TXID=txid,
                    MONZERO_TEST_SPEND_ATOMS=str(delivered),
                    MONZERO_TEST_SPEND_LOCKED_ATOMS=str(amount),
                    MONZERO_TEST_SPEND_RESULT=str(result_file))
                if environment.get("MONZERO_TEST_QT_QUOTE_DRIVER"):
                    spend_environment.update(
                        MONZERO_TEST_SPEND_QUOTE_CONTEXT=str(exported.with_suffix(".quote-context")),
                        MONZERO_TEST_SPEND_QUOTE_FINGERPRINT=json.dumps(checked_quote["associationFingerprint"]),
                        MONZERO_TEST_SPEND_QUOTE_ROLE=destination_name)
                checked = subprocess.run([str(engine_test_binary), "--exact",
                    "native_swap_wallet_spendable", "--ignored"], env=spend_environment,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
                if checked.returncode != 0:
                    raise RuntimeError("Protocol-recovered native spending failed")
                print("PASS: non-relaying full-lock preview accounts for exact net proceeds plus fee; "
                      "wrong principal and excessive fees rejected (not a full payout)")
                if environment.get("MONZERO_TEST_QT_QUOTE_DRIVER"):
                    print("PASS: native quote-aware spending enforces signed destination and fee limits before RPC")
                if not result_file.is_file() or result_file.stat().st_size != 65:
                    raise RuntimeError("Missing native spending completion marker")
                spend_txid = result_file.read_text(encoding="ascii").strip()
                if len(spend_txid) != 64 or any(c not in "0123456789abcdef" for c in spend_txid):
                    raise RuntimeError("Invalid native spending completion marker")
                rpc(node_port, "generateblocks", {"wallet_address": address, "amount_of_blocks": 10})
                wallet_rpc("open_wallet", {"filename": "scanned", "password": ""})
                wallet_rpc("refresh", {"start_height": 0})
                outgoing = wallet_rpc("get_transfer_by_txid", {"txid": spend_txid})["transfer"]
                assert outgoing["type"] == "out" and outgoing["amount"] == delivered
                assert outgoing["fee"] > 0 and outgoing["confirmations"] >= 10
                residual = wallet_rpc("get_balance")["balance"]
                assert residual > 0 and residual + delivered + outgoing["fee"] == amount
                print("PASS: partial recovery accounting verified; residual XMZ remains (not full settlement)")
                intent = verify_spend_journal(root, spend_txid, delivered, outgoing["fee"], recipient, destination, txid)
                # Simulate a missing success receipt, without deleting evidence.
                # This is not a power-loss fault injection at the broadcast boundary.
                next(root.glob("*.receipt")).rename(root / "withheld-success-receipt")
                journal_before = {p.name: p.read_bytes() for pattern in ("*.intent", "*.prepared") for p in root.glob(pattern)}
                wallet_rpc("close_wallet")
                server.terminate()
                server.wait(timeout=15)
                assert server.returncode == 0
                server = subprocess.Popen(server.args, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                processes.append(server)
                ready(server, wallet_port, "get_version", login=login)
                wallet_rpc("open_wallet", {"filename": intent["walletFile"], "password": "fixture-password"})
                wallet_rpc("refresh", {"start_height": 0})
                recovery_balance = wallet_rpc("get_balance")
                assert rpc(node_port, "get_info")["tx_pool_size"] == 0
                recovery_marker = root / "recovery-checked"
                recovery_environment = dict(os.environ,
                    MONZERO_TEST_DAEMON_RPC_PORT=str(node_port), MONZERO_TEST_WALLET_RPC_PORT=str(wallet_port),
                    MONZERO_TEST_WALLET_RPC_LOGIN=str(login), MONZERO_TEST_RECOVERY_DIRECTORY=str(root),
                    MONZERO_TEST_RECOVERY_WALLET=recipient, MONZERO_TEST_RECOVERY_LOCK=txid,
                    MONZERO_TEST_RECOVERY_TXID=spend_txid, MONZERO_TEST_RECOVERY_CHECKED=str(recovery_marker))
                recovered = subprocess.run([str(engine_test_binary), "--exact", "native_recover_confirmed_spend", "--ignored"],
                    env=recovery_environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
                if recovered.returncode != 0 or not recovery_marker.is_file() \
                        or recovery_marker.read_bytes() != b"native-recovery-confirmed\n":
                    raise RuntimeError("Fresh-process native spend reconciliation failed")
                if fee_preview_helper is not None:
                    session_file = root / "recovery-session.json"
                    username, password = login.read_text(encoding="ascii").split(":", 1)
                    with open(session_file, "x", opener=lambda path, flags: os.open(path, flags, 0o600)) as stream:
                        json.dump({"schema": "monzero-native-fee-session-v1", "network": "mainnet",
                            "genesis": GENESIS, "daemonPort": node_port, "walletPort": wallet_port,
                            "username": username, "password": password}, stream)
                    del username, password
                    wire = {"schema": "monzero-native-recovery-request-v1", "requestId": str(uuid.uuid4()),
                        "genesis": GENESIS, "wallet": recipient, "lockTxid": txid,
                        "txid": spend_txid, "minimumConfirmations": "10"}
                    command = [str(fee_preview_helper), "--recovery-stdio-v1", "--session-file",
                        str(session_file), "--journal-directory", str(root)]
                    for field, value in (("genesis", "0" * 64), ("txid", "0" * 64),
                            ("minimumConfirmations", "18446744073709551615"), ("journalDirectory", str(root))):
                        invalid = dict(wire, **{field: value})
                        rejected = subprocess.run(command, input=json.dumps(invalid).encode(),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=35, env={"LANG": "C"})
                        assert rejected.returncode != 0 and rejected.stdout == b""
                        assert rejected.stderr == b"Native recovery check failed\n"
                    checked = subprocess.run(command, input=json.dumps(wire).encode(),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=35, env={"LANG": "C"})
                    assert checked.returncode == 0 and checked.stderr == b"" and len(checked.stdout) <= 2048
                    response = json.loads(checked.stdout)
                    assert checked.stdout == json.dumps(response, sort_keys=True, separators=(",", ":")).encode("ascii")
                    assert len(response) == 11 and response["schema"] == "monzero-native-recovery-v1"
                    for field in ("requestId", "genesis", "wallet", "lockTxid", "txid"):
                        assert response[field] == wire[field]
                    assert response["retryAllowed"] is False and response["executable"] is False
                    assert response["source"] == "local-wallet-reconciliation" and response["status"] == "confirmed"
                    assert isinstance(response["confirmations"], str) and int(response["confirmations"]) >= 10
                    recovery_driver = os.environ.get("MONZERO_TEST_QT_RECOVERY_DRIVER")
                    if recovery_driver:
                        driver = pathlib.Path(recovery_driver).resolve(strict=True)
                        decoded = subprocess.run([str(driver), "--native-helper", str(fee_preview_helper),
                            str(session_file), str(root), GENESIS, recipient, txid, spend_txid],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=40)
                        if decoded.returncode != 0:
                            raise RuntimeError("Qt recovery receiver rejected native helper evidence")
                        print("PASS: Qt recovery receiver accepts the real native helper response")
                    session_file.unlink()
                    print("PASS: native recovery pipe validates request identity, rejects unsafe inputs and returns bounded confirmation evidence")
                assert wallet_rpc("get_balance") == recovery_balance
                assert rpc(node_port, "get_info")["tx_pool_size"] == 0
                assert not list(root.glob("*.receipt"))
                assert {p.name: p.read_bytes() for pattern in ("*.intent", "*.prepared") for p in root.glob(pattern)} == journal_before
                print("PASS: read-only reconciliation confirms the prepared payment after restart despite a missing receipt")
            else:
                swept = wallet_rpc("sweep_all", {"address": destination, "priority": 1})
                assert len(swept["tx_hash_list"]) == 1
                assert sum(swept["amount_list"]) + sum(swept["fee_list"]) == amount
                delivered = sum(swept["amount_list"])
                assert 0 < delivered < amount
            wallet_rpc("close_wallet")
            rpc(node_port, "generateblocks", {"wallet_address": address, "amount_of_blocks": 10})
            wallet_rpc("open_wallet", {"filename": destination_name, "password": ""})
            wallet_rpc("refresh", {"start_height": 0})
            assert wallet_rpc("get_balance")["balance"] == delivered
            if native_spendable:
                incoming = wallet_rpc("get_transfer_by_txid", {"txid": spend_txid})["transfer"]
                assert incoming["type"] == "in" and incoming["amount"] == delivered
                assert incoming["confirmations"] >= 10
            wallet_rpc("close_wallet")
            print("PASS: normal transfer imported idempotently, persisted across RPC restart, and spent")
            other_name = "refund" if destination_name == "payout" else "payout"
            wallet_rpc("open_wallet", {"filename": other_name, "password": ""})
            wallet_rpc("refresh", {"start_height": 0})
            assert wallet_rpc("get_balance")["balance"] == 0
            wallet_rpc("close_wallet")
            print("PASS: requested payment reaches only the selected payout/refund wallet")
            if engine_keys is not None:
                print("PASS: native fakechain funds spent using the engine-recovered joint spend key")
            if native_spendable:
                print("PASS: native spending primitive uses protocol-recovered keys with exact on-chain fee accounting")
                # Separate construction fixture, not an actual buyer-funded
                # protocol top-up. Both payments use the disposable mint wallet.
                wallet_rpc("create_wallet", {"filename": "two-source-preview", "password": "",
                                              "language": "English"})
                preview_address = wallet_rpc("get_address")["address"]
                wallet_rpc("close_wallet")
                wallet_rpc("open_wallet", {"filename": "restored", "password": ""})
                source_ids = []
                for source_amount in (100_000_000_000, 10_000_000_000):
                    wallet_rpc("refresh")
                    payment = wallet_rpc("transfer", {"destinations": [
                        {"address": preview_address, "amount": source_amount}], "priority": 1})
                    source_ids.append(payment["tx_hash"])
                    rpc(node_port, "generateblocks", {"wallet_address": address, "amount_of_blocks": 10})
                assert source_ids[0] != source_ids[1]
                wallet_rpc("close_wallet")
                wallet_rpc("open_wallet", {"filename": "two-source-preview", "password": ""})
                wallet_rpc("scan_tx", {"txids": source_ids})
                wallet_rpc("refresh")
                preview_balance = wallet_rpc("get_balance")
                assert preview_balance["balance"] == preview_balance["unlocked_balance"] == 110_000_000_000
                preview_chain = rpc(node_port, "get_info")
                assert preview_chain["tx_pool_size"] == 0
                # Public numeric diagnostics only; never request keys or metadata.
                probe = wallet_rpc("transfer", {"destinations": [
                    {"address": destination, "amount": 100_000_000_000},
                    {"address": address, "amount": 10_000_000_000}],
                    "priority": 1, "subtract_fee_from_outputs": [1], "do_not_relay": True,
                    "get_tx_key": False, "get_tx_hex": False, "get_tx_metadata": False})
                print(f"Two-source construction accounting: amount={probe['amount']} fee={probe['fee']} atoms")
                assert probe["amount"] + probe["fee"] == 110_000_000_000
                assert probe["amounts_by_dest"]["amounts"][0] == 100_000_000_000
                del probe
                top_up_journal = root / "two-source-journal"
                top_up_journal.mkdir(mode=0o700)
                preview_environment = dict(spend_environment,
                    MONZERO_TEST_TOP_UP_WALLET=preview_address,
                    MONZERO_TEST_TOP_UP_DESTINATION=destination,
                    MONZERO_TEST_TOP_UP_RETURN=address,
                    MONZERO_TEST_TOP_UP_LOCK=source_ids[0], MONZERO_TEST_TOP_UP_TXID=source_ids[1],
                    MONZERO_TEST_TOP_UP_JOURNAL=str(top_up_journal))
                checked = subprocess.run([str(engine_test_binary), "--exact",
                    "native_exact_net_top_up_preview", "--ignored"], env=preview_environment,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
                if checked.returncode != 0 or b"1 passed" not in checked.stdout:
                    # This test contains only public addresses/amounts, never keys.
                    detail = (checked.stdout + checked.stderr).decode("utf-8", errors="replace")[-2000:]
                    raise RuntimeError("Two-source exact-net construction fixture failed: " + detail)
                assert wallet_rpc("get_balance") == preview_balance
                assert wallet_rpc("get_address")["address"] == preview_address
                after_preview = rpc(node_port, "get_info")
                assert after_preview["height"] == preview_chain["height"]
                assert after_preview["tx_pool_size"] == 0
                journal_records = {}
                for suffix in ("intent", "prepared"):
                    paths = list(top_up_journal.glob(f"*.{suffix}"))
                    assert len(paths) == 1 and paths[0].stat().st_mode & 0o077 == 0
                    journal_records[suffix] = json.loads(paths[0].read_text(encoding="ascii"))
                assert journal_records["intent"]["schema"] == "monzero-exact-net-spend-intent-v1"
                prepared_record = journal_records["prepared"]
                assert prepared_record["schema"] == "monzero-exact-net-prepared-accounting-v1"
                for record in journal_records.values():
                    assert record["lockTxid"] == source_ids[0] and record["topUpTxid"] == source_ids[1]
                    assert record["principalAtoms"] == "100000000000"
                    assert record["destination"] == destination and record["reserveReturnDestination"] == address
                    assert not any(key in record for key in ("tx_metadata", "tx_key", "spendkey", "viewkey"))
                assert int(prepared_record["reserveReturnAtoms"]) + int(prepared_record["feeAtoms"]) == 10_000_000_000
                assert not list((top_up_journal / "mismatch").glob("*.prepared"))
                retained = {p.name: p.read_bytes() for p in top_up_journal.glob("*.*")}
                recovered = subprocess.run([str(engine_test_binary), "--exact",
                    "native_exact_net_top_up_preview", "--ignored"],
                    env=dict(preview_environment, MONZERO_TEST_TOP_UP_RECOVER_ONLY="1",
                        MONZERO_TEST_TOP_UP_EXPECTED_TXID=prepared_record["txid"]),
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
                if recovered.returncode != 0 or b"1 passed" not in recovered.stdout:
                    raise RuntimeError("Fresh-process exact-net journal reload failed")
                assert {p.name: p.read_bytes() for p in top_up_journal.glob("*.*")} == retained
                assert wallet_rpc("get_balance") == preview_balance
                assert rpc(node_port, "get_info")["tx_pool_size"] == 0
                print("PASS: fresh process reloads exact-net preparation without RPC or file changes; "
                      "changed context rejected (not proof of broadcast)")
                unresolved = subprocess.run([str(engine_test_binary), "--exact",
                    "native_exact_net_top_up_preview", "--ignored"],
                    env=dict(preview_environment, MONZERO_TEST_TOP_UP_RECOVER_ONLY="1",
                        MONZERO_TEST_TOP_UP_CHECK_UNCONFIRMED="1",
                        MONZERO_TEST_TOP_UP_EXPECTED_TXID=prepared_record["txid"]),
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
                if unresolved.returncode != 0 or b"1 passed" not in unresolved.stdout:
                    raise RuntimeError("Unbroadcast exact-net reconciliation fixture failed")
                assert {p.name: p.read_bytes() for p in top_up_journal.glob("*.*")} == retained
                assert wallet_rpc("get_balance") == preview_balance
                assert rpc(node_port, "get_info")["tx_pool_size"] == 0
                print("PASS: read-only exact-net reconciliation does not confirm an unbroadcast preparation")
                wallet_rpc("close_wallet")
                print("PASS: two-source exact-net construction preserves principal and returns reserve accounting; "
                      "invalid context rejected, no broadcast or balance change (not protocol top-up settlement)")
                verify_exact_net_relay(wallet_rpc, node_port, address, root, spend_environment, engine_test_binary)
                verify_exact_net_relay(wallet_rpc, node_port, address, root, spend_environment,
                                       engine_test_binary, lose_reply=True)
                verify_raw_restart_candidate(wallet_rpc, node_port, address, root)
                verify_raw_restart_candidate(wallet_rpc, node_port, address, root,
                                             sealed=True, environment=spend_environment)
                verify_topup_dispatch_and_claim(wallet_rpc, node_port, address, root,
                                                spend_environment, engine_test_binary)
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--daemon", required=True, type=pathlib.Path)
    parser.add_argument("--wallet-rpc", required=True, type=pathlib.Path)
    parser.add_argument("--temp-parent", required=True, type=pathlib.Path)
    parser.add_argument("--address-vectors", type=pathlib.Path)
    parser.add_argument("--engine-test-binary", type=pathlib.Path)
    parser.add_argument("--fee-preview-helper", type=pathlib.Path)
    parser.add_argument("--qt-fee-driver", type=pathlib.Path)
    parser.add_argument("--native-spendable", action="store_true",
        help="Spend actual protocol-recovered funds through the Rust native primitive instead of sweep_all")
    parser.add_argument("--engine-test-name", default="tests::calculate_full_refund_transaction_weights",
        choices=["tests::calculate_full_refund_transaction_weights",
                 "engine_redeem_is_accepted_on_regtest",
                 "engine_cancel_and_full_refund_are_accepted_on_regtest"])
    args = parser.parse_args()
    if args.fee_preview_helper and not args.engine_test_binary:
        parser.error("--fee-preview-helper requires --engine-test-binary")
    if args.qt_fee_driver and not args.fee_preview_helper:
        parser.error("--qt-fee-driver requires --fee-preview-helper")
    run(args.daemon.resolve(strict=True), args.wallet_rpc.resolve(strict=True),
        args.temp_parent.resolve(strict=True),
        args.address_vectors.resolve(strict=True) if args.address_vectors else None,
        args.engine_test_binary.resolve(strict=True) if args.engine_test_binary else None,
        args.engine_test_name,
        args.fee_preview_helper.resolve(strict=True) if args.fee_preview_helper else None,
        args.qt_fee_driver.resolve(strict=True) if args.qt_fee_driver else None,
        args.native_spendable)
