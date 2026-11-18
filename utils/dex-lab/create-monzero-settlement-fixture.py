#!/usr/bin/env python3
"""Create one recoverable, worthless XMZ fakechain incoming-payment fixture."""
import json
import os
import pathlib
import tempfile
import time
import urllib.request

ROOT = pathlib.Path("/srv/monzero-dex")
MANIFEST = ROOT / "recovery/monzero-settlement-fixture.json"
PASSWORD_FILE = ROOT / "recovery/wallet-rpc.pass"
AMOUNT_ATOMIC = 100000000
REQUIRED_CONFIRMATIONS = 3


def rpc(port, method, params=None):
    body = json.dumps({"jsonrpc": "2.0", "id": "fixture", "method": method,
                       "params": params or {}}, separators=(",", ":")).encode()
    request = urllib.request.Request(f"http://127.0.0.1:{port}/json_rpc", body,
                                     {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=30) as response:
        envelope = json.load(response)
    if envelope.get("error"):
        raise RuntimeError(f"{method}: {envelope['error'].get('message', 'RPC error')}")
    return envelope["result"]


def daemon_endpoint(path, params=None):
    body = json.dumps(params or {}, separators=(",", ":")).encode()
    request = urllib.request.Request(f"http://127.0.0.1:6175/{path}", body,
                                     {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=30) as response:
        result = json.load(response)
    if result.get("status") not in (None, "OK"):
        raise RuntimeError(f"{path}: daemon endpoint failed")
    return result


def atomic_write(value):
    MANIFEST.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=MANIFEST.name + ".", dir=MANIFEST.parent)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w") as output:
            json.dump(value, output, sort_keys=True, separators=(",", ":"))
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(name, MANIFEST)
        directory = os.open(MANIFEST.parent, os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        try:
            os.unlink(name)
        except FileNotFoundError:
            pass


def close_wallet():
    try:
        rpc(6176, "close_wallet", {"autosave_current": True})
    except RuntimeError as error:
        if "No wallet file" not in str(error) and "not open" not in str(error).lower():
            raise


def open_wallet(name, password, create=False):
    close_wallet()
    try:
        rpc(6176, "open_wallet", {"filename": name, "password": password})
    except RuntimeError:
        if not create:
            raise
        rpc(6176, "create_wallet", {"filename": name, "password": password,
                                    "language": "English"})
    rpc(6176, "refresh")


def prepare(password):
    open_wallet("dex-test-receiver", password, create=True)
    destination = rpc(6176, "get_address")["address"]
    open_wallet("dex-test-miner", password)
    created = rpc(6176, "transfer", {
        "destinations": [{"address": destination, "amount": AMOUNT_ATOMIC}],
        "account_index": 0, "subaddr_indices": [], "priority": 1,
        "ring_size": 0, "unlock_time": 0, "payment_id": "",
        "get_tx_key": False, "do_not_relay": True,
        "get_tx_hex": False, "get_tx_metadata": True})
    txid, metadata = created.get("tx_hash"), created.get("tx_metadata")
    if not isinstance(txid, str) or len(txid) != 64 or not metadata:
        raise RuntimeError("Monzero fixture preparation returned incomplete recovery data")
    manifest = {"version": 1, "status": "prepared", "network": "fakechain",
                "transactionId": txid, "destination": destination,
                "amountAtomic": str(AMOUNT_ATOMIC),
                "requiredConfirmations": REQUIRED_CONFIRMATIONS,
                "transactionMetadata": metadata}
    atomic_write(manifest)
    return manifest


def receiver_transfer(manifest, password, require_unlocked=False):
    open_wallet("dex-test-receiver", password)
    for _ in range(30):
        rpc(6176, "refresh")
        try:
            result = rpc(6176, "get_transfer_by_txid", {"txid": manifest["transactionId"]})
            transfers = result.get("transfers", [])
            incoming = [entry for entry in transfers if entry.get("type") == "in"]
            if (incoming and incoming[0].get("confirmations", 0) >= REQUIRED_CONFIRMATIONS
                    and (not require_unlocked or all(not entry.get("locked", True)
                                                     for entry in incoming))):
                return result
        except RuntimeError:
            pass
        time.sleep(1)
    raise RuntimeError("Monzero receiver did not observe the confirmed fixture")


def resume(manifest, password):
    if (manifest.get("version"), manifest.get("network"), manifest.get("amountAtomic"),
            manifest.get("requiredConfirmations")) != (1, "fakechain", str(AMOUNT_ATOMIC),
                                                        REQUIRED_CONFIRMATIONS):
        raise RuntimeError("Unexpected existing Monzero fixture manifest")
    txid = manifest["transactionId"]
    if manifest["status"] == "prepared":
        open_wallet("dex-test-miner", password)
        try:
            relayed = rpc(6176, "relay_tx", {"hex": manifest["transactionMetadata"]})
            if relayed.get("tx_hash") != txid:
                raise RuntimeError("Relay returned a different Monzero txid")
        except RuntimeError:
            pool = daemon_endpoint("get_transaction_pool")
            if txid not in {entry.get("id_hash") for entry in pool.get("transactions", [])}:
                raise
        manifest["status"] = "broadcast"
        atomic_write(manifest)
    if manifest["status"] == "broadcast":
        open_wallet("dex-test-miner", password)
        miner_address = rpc(6176, "get_address")["address"]
        rpc(6175, "generateblocks", {"wallet_address": miner_address,
                                     "amount_of_blocks": REQUIRED_CONFIRMATIONS})
        daemon_endpoint("save_bc")
        result = receiver_transfer(manifest, password)
        incoming = [entry for entry in result["transfers"] if entry.get("type") == "in"]
        height = incoming[0]["height"]
        block = rpc(6175, "get_block", {"height": height})
        hashes = set(block.get("tx_hashes", []))
        if block.get("miner_tx_hash") != txid and txid not in hashes:
            raise RuntimeError("Monzero fixture is absent from its claimed block")
        manifest.update({"status": "confirmed", "blockHash": block["block_header"]["hash"],
                         "blockHeight": str(height)})
        manifest.pop("transactionMetadata", None)
        atomic_write(manifest)
    if manifest["status"] != "confirmed":
        raise RuntimeError("Unknown Monzero fixture state")
    result = receiver_transfer(manifest, password)
    incoming = [entry for entry in result["transfers"] if entry.get("type") == "in"]
    if any(entry.get("locked", True) for entry in incoming):
        open_wallet("dex-test-miner", password)
        miner_address = rpc(6176, "get_address")["address"]
        rpc(6175, "generateblocks", {"wallet_address": miner_address, "amount_of_blocks": 10})
        daemon_endpoint("save_bc")
        result = receiver_transfer(manifest, password, require_unlocked=True)
        incoming = [entry for entry in result["transfers"] if entry.get("type") == "in"]
    if sum(entry["amount"] for entry in incoming) != AMOUNT_ATOMIC:
        raise RuntimeError("Monzero fixture amount changed")
    print(json.dumps({key: manifest[key] for key in ("status", "transactionId", "destination",
                                                     "amountAtomic", "blockHash", "blockHeight")},
                     sort_keys=True))


def main():
    password = PASSWORD_FILE.read_text().strip()
    if not password:
        raise RuntimeError("Empty disposable wallet password")
    manifest = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else prepare(password)
    resume(manifest, password)


if __name__ == "__main__":
    main()
