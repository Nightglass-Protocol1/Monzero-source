#!/usr/bin/env python3
"""Funded offline fee construction through Qt's RPC owner; no real funds."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import time
import uuid
from test_wallet_recovery import GENESIS, port, ready, rpc


def wait_file(path, process, limit=30):
    deadline = time.monotonic() + limit
    while not path.exists():
        if process.poll() is not None or time.monotonic() >= deadline:
            raise RuntimeError("Owned fee fixture did not complete")
        time.sleep(0.05)
    if not path.is_file() or path.is_symlink() or path.stat().st_size > 2048:
        raise RuntimeError("Unsafe fixture result")
    return json.loads(path.read_bytes())


def run(args):
    root = pathlib.Path(tempfile.mkdtemp(prefix="xmz-owned-fee-", dir=args.temp_parent))
    node = owner = None
    try:
        node_port, p2p_port, wallet_port = port(), port(), port()
        if len({node_port, p2p_port, wallet_port}) != 3:
            raise RuntimeError("Fixture port collision")
        node = subprocess.Popen([str(args.daemon), "--regtest", "--keep-fakechain", "--fixed-difficulty", "1",
            "--offline", "--no-igd", "--hide-my-port", "--no-zmq", "--non-interactive",
            "--data-dir", str(root / "chain"), "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(node_port),
            "--p2p-bind-ip", "127.0.0.1", "--p2p-bind-port", str(p2p_port), "--log-file", str(root / "node.log")],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        info = ready(node, node_port, "get_info")
        if info.get("nettype") != "fakechain" or info.get("offline") is not True:
            raise RuntimeError("Wrong fixture daemon")
        manifest = root / "fakechain-wallet-launch.json"
        with open(manifest, "x", opener=lambda path, flags: os.open(path, flags, 0o600)) as stream:
            json.dump({"schema": "monzero-fakechain-launch-v1", "walletRpc": str(args.wallet_rpc),
                       "daemonPort": node_port}, stream)
        launcher = pathlib.Path(__file__).with_name("fakechain_wallet_launcher.py").resolve(strict=True)
        mode = "--coordinated-fee-job-fixture" if args.coordinator else "--owned-fee-job-fixture"
        helper = args.helper
        digest = None
        if args.pin_helper:
            # Fixture identity check only, not independent release provenance.
            hashed = hashlib.sha256()
            with helper.open("rb") as source:
                for block in iter(lambda: source.read(65536), b""):
                    hashed.update(block)
            digest = hashed.hexdigest()
            helper = root / "pinned-native-helper"
            shutil.copyfile(args.helper, helper)
            helper.chmod(0o500)
        command = [str(args.qt_driver), mode, str(helper),
            str(launcher), str(root), str(node_port), str(wallet_port)]
        if digest is not None:
            command.append(digest)
        owner = subprocess.Popen(command,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        metadata = wait_file(root / "service-info.json", owner)
        service, login, session = (pathlib.Path(metadata[key]) for key in ("directory", "login", "helperSession"))
        if service.parent != root or login.parent != service or session.parent != service:
            raise RuntimeError("Owner escaped fixture directory")
        ready(owner, wallet_port, "get_version", login)
        def wallet(method, params=None):
            return rpc(wallet_port, method, params, login)
        # Setup RPCs finish before the request is published; no concurrent client
        # mutates the wallet while Qt owns the pending construction operation.
        wallet("create_wallet", {"filename": "destination", "password": "", "language": "English"})
        destination = wallet("get_address")["address"]
        wallet("close_wallet")
        if args.coordinator:
            (root / "wallet-create").touch()
            created = wait_file(root / "wallet-ready.json", owner)
            if set(created) != {"address", "readyForTrading"} or created["readyForTrading"] is not False:
                raise RuntimeError("Invalid coordinator wallet result")
            address = created["address"]
            if wallet("get_address")["address"] != address:
                raise RuntimeError("Coordinator wallet identity mismatch")
        else:
            wallet("create_wallet", {"filename": "funded", "password": "", "language": "English"})
            address = wallet("get_address")["address"]
        rpc(node_port, "generateblocks", {"wallet_address": address, "amount_of_blocks": 131})
        wallet("refresh", {"start_height": 0})
        before = wallet("get_balance")
        amount = 200000000000
        if before["unlocked_balance"] <= amount or rpc(node_port, "get_info")["tx_pool_size"] != 0:
            raise RuntimeError("Invalid funded fixture state")
        wire = {"schema": "monzero-native-fee-request-v1", "requestId": str(uuid.uuid4()),
            "wallet": address, "destination": destination, "amountAtoms": str(amount),
            "account": "0", "priority": "1", "genesis": GENESIS}
        pending = root / "request.pending"
        pending.write_text(json.dumps(wire))
        pending.rename(root / "owned-fee-request.json")
        result = wait_file(root / "owned-fee-result.json", owner, 40)
        if result.get("valid") is not True or result.get("executable") is not False \
                or result.get("amountAtoms") != str(amount) or not isinstance(result.get("feeAtoms"), str) \
                or not result["feeAtoms"].isascii() or not result["feeAtoms"].isdecimal() \
                or not 0 < int(result["feeAtoms"]) < 2**64:
            raise RuntimeError("Invalid owned fee preview")
        after = wallet("get_balance")
        if any(before[key] != after[key] for key in ("balance", "unlocked_balance")) \
                or rpc(node_port, "get_info")["tx_pool_size"] != 0:
            raise RuntimeError("Fee construction changed funds or broadcast a transaction")
        if (service / "service.state").read_bytes() != b"monzero-swap-rpc-v1\nactive\n":
            raise RuntimeError("Owner operation was not completed")
        if args.coordinator:
            (root / "wallet-close").touch()
            closed = wait_file(root / "wallet-closed.json", owner)
            if closed != {"closed": True, "readyForTrading": False}:
                raise RuntimeError("Coordinator did not close wallet")
            (root / "wallet-reopen").touch()
            reopened = wait_file(root / "wallet-reopened.json", owner)
            if reopened != {"address": address, "readyForTrading": False}:
                raise RuntimeError("Coordinator did not reopen the same wallet")
            restored = wallet("get_balance")
            if wallet("get_address")["address"] != address \
                    or any(restored[key] != before[key] for key in ("balance", "unlocked_balance")):
                raise RuntimeError("Coordinator reopen changed wallet identity or funds")
            (root / "wallet-close-again").touch()
            if wait_file(root / "wallet-closed-again.json", owner) != {"closed": True, "readyForTrading": False}:
                raise RuntimeError("Coordinator second close failed")
        else:
            wallet("close_wallet")
        (root / "service-stop").touch()
        owner.wait(timeout=20)
        if owner.returncode != 0 or login.exists() or session.exists() \
                or (service / "service.state").read_bytes() != b"monzero-swap-rpc-v1\nstopped\n":
            raise RuntimeError("Owned fixture did not stop cleanly")
        print("PASS: funded Qt owner/native helper fee preview leaves balances and pool unchanged")
        print("PASS: owner completes operation, stops its RPC and removes credential files")
        if args.coordinator:
            print("PASS: coordinator creates, previews, closes and reopens encrypted wallet with unchanged identity and funds")
        if args.pin_helper:
            print("PASS: real helper is digest-pinned for the funded coordinator sequence")
    finally:
        if owner is not None and owner.poll() is None:
            (root / "service-stop").touch()
            owner.wait(timeout=20)  # On timeout preserve data; do not kill an owner and orphan its RPC.
        if node is not None and node.poll() is None:
            node.terminate()
            node.wait(timeout=15)
        if all(process is None or process.poll() is not None for process in (owner, node)):
            shutil.rmtree(root)  # Only this invocation's disposable, stopped fixture.


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("daemon", "wallet-rpc", "qt-driver", "helper", "temp-parent"):
        parser.add_argument("--" + name, type=pathlib.Path, required=True)
    parser.add_argument("--coordinator", action="store_true")
    parser.add_argument("--pin-helper", action="store_true")
    options = parser.parse_args()
    if options.pin_helper and not options.coordinator:
        parser.error("--pin-helper requires --coordinator")
    for name, value in vars(options).items():
        if isinstance(value, pathlib.Path):
            setattr(options, name, value.resolve(strict=True))
    run(options)
