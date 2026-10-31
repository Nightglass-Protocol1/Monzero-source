#!/usr/bin/env python3
"""Actual offline XMZ preparation, cold recovery and relay; not a two-chain swap."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import stat
import subprocess
import tempfile

from test_wallet_recovery import GENESIS, port, ready, rpc

TEST = "native_funding_rpc::relay::lock_checkpoint::tests::actual_native_wallet_preparation_checkpoint"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def private_write(path, value):
    with open(path, "x", opener=lambda name, flags: os.open(name, flags, 0o600)) as stream:
        json.dump(value, stream)


def run(args):
    require(not args.report.exists(), "Report already exists")
    root = pathlib.Path(tempfile.mkdtemp(prefix="xmz-native-lock-", dir=args.temp_parent)).resolve()
    processes = []
    try:
        daemon_port, wallet_port, p2p_port = port(), port(), port()
        require(len({daemon_port, wallet_port, p2p_port}) == 3, "Fixture port collision")
        daemon = subprocess.Popen([str(args.daemon), "--regtest", "--keep-fakechain", "--fixed-difficulty", "1",
            "--offline", "--no-igd", "--hide-my-port", "--no-zmq", "--non-interactive",
            "--data-dir", str(root / "chain"), "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(daemon_port),
            "--p2p-bind-ip", "127.0.0.1", "--p2p-bind-port", str(p2p_port), "--log-file", str(root / "node.log")],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(daemon)
        info = ready(daemon, daemon_port, "get_info")
        require(info.get("nettype") == "fakechain" and info.get("offline") is True
                and info.get("incoming_connections_count") == info.get("outgoing_connections_count") == 0,
                "Daemon is not an isolated offline fakechain")
        require(rpc(daemon_port, "get_block", {"height": 0})["block_header"]["hash"] == GENESIS,
                "Wrong fixture genesis")
        (root / "wallets").mkdir(mode=0o700)
        wallet = subprocess.Popen([str(args.wallet_rpc), "--wallet-dir", str(root / "wallets"),
            "--daemon-address", f"127.0.0.1:{daemon_port}", "--trusted-daemon", "--allow-mismatched-daemon-version",
            "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(wallet_port), "--non-interactive",
            "--max-concurrency", "2", "--log-file", str(root / "wallet.log")], cwd=root,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(wallet)
        login = root / f"monzero-wallet-rpc.{wallet_port}.login"
        ready(wallet, wallet_port, "get_version", login)

        def wallet_rpc(method, params=None):
            return rpc(wallet_port, method, params, login)

        addresses = {}
        for name in ("recipient", "mining", "source"):
            wallet_rpc("create_wallet", {"filename": name, "password": "", "language": "English"})
            addresses[name] = wallet_rpc("get_address")["address"]
            if name != "source":
                wallet_rpc("close_wallet")
        rpc(daemon_port, "generateblocks", {"wallet_address": addresses["source"], "amount_of_blocks": 131})
        wallet_rpc("refresh", {"start_height": 0})
        before = wallet_rpc("get_balance")
        amount, maximum_fee = 200_000_000_000, 100_000_000_000
        require(before["unlocked_balance"] > amount + maximum_fee, "Fixture lacks unlocked funds")
        require(rpc(daemon_port, "get_info")["tx_pool_size"] == 0, "Fixture pool was not empty")
        request = {"action": "prepare", "daemon_port": daemon_port, "wallet_port": wallet_port,
                   "wallet": addresses["source"], "destination": addresses["recipient"], "amount": amount,
                   "maximum_fee": maximum_fee, "binding": [7] * 32, "expected_digest": None}

        def invoke(expected_success=True):
            path = root / "request.json"
            if path.exists():
                path.unlink()  # Only this disposable harness input, never execution state.
            private_write(path, request)
            environment = os.environ.copy()
            environment["MONZERO_NATIVE_LOCK_FIXTURE"] = str(root)
            result = subprocess.run([str(args.engine_test_binary), "--exact", TEST, "--ignored", "--nocapture"],
                env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            success = result.returncode == 0 and b"1 passed" in result.stdout
            if success != expected_success:
                log = args.report.with_suffix(".failure.log")
                with open(log, "xb", opener=lambda name, flags: os.open(name, flags, 0o600)) as stream:
                    stream.write(result.stdout)
                raise RuntimeError(f"Unexpected native fixture outcome; private diagnostic: {log}")

        invoke()
        prepared = json.loads((root / "prepare-result.json").read_text())
        checkpoint = root / "prepared.bin"
        require(stat.S_ISREG(checkpoint.lstat().st_mode) and stat.S_IMODE(checkpoint.stat().st_mode) == 0o600,
                "Prepared checkpoint is not private")
        digest = hashlib.sha256(checkpoint.read_bytes()).digest()
        require(list(digest) == prepared["checkpointDigest"], "Prepared checkpoint digest differs")
        after = wallet_rpc("get_balance")
        require(all(before[key] == after[key] for key in ("balance", "unlocked_balance")),
                "Preparation changed wallet balance")
        require(rpc(daemon_port, "get_info")["tx_pool_size"] == 0, "Preparation broadcast a transaction")
        wallet_rpc("close_wallet")
        for process in reversed(processes):
            process.terminate()
            process.wait(timeout=20)
        require(all(process.poll() is not None for process in processes), "Fixture services did not stop")
        request.update(action="restore", daemon_port=1, wallet_port=2, expected_digest=list(digest))
        invoke()  # A new Rust process; both RPC services are already stopped.
        restored = json.loads((root / "restore-result.json").read_text())
        require(restored == prepared, "Cold recovery changed prepared transaction")
        request["binding"] = [8] * 32
        invoke(False)
        request["binding"] = [7] * 32
        original = checkpoint.read_bytes()
        damaged = bytearray(original)
        damaged[len(damaged) // 2] ^= 1
        checkpoint.write_bytes(damaged)
        invoke(False)
        checkpoint.write_bytes(original)  # Undo only this harness's corruption injection.
        # Restart the same disposable services, then relay the saved metadata.
        daemon = subprocess.Popen(daemon.args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(daemon)
        ready(daemon, daemon_port, "get_info")
        wallet = subprocess.Popen(wallet.args, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(wallet)
        ready(wallet, wallet_port, "get_version", login)
        wallet_rpc("open_wallet", {"filename": "source", "password": ""})
        request.update(action="relay", daemon_port=daemon_port, wallet_port=wallet_port)
        invoke()
        acknowledged = json.loads((root / "relay-result.json").read_text())
        require(acknowledged == prepared, "Relay changed transaction identity or terms")
        require(rpc(daemon_port, "get_info")["tx_pool_size"] == 1, "Exact saved transaction did not enter the pool")
        request["action"] = "confirm"
        invoke(False)  # A pending payment is not confirmed evidence.
        rpc(daemon_port, "generateblocks", {"wallet_address": addresses["mining"], "amount_of_blocks": 3})
        wallet_rpc("refresh", {"start_height": 0})
        invoke()
        confirmed = json.loads((root / "confirm-result.json").read_text())
        require(confirmed["txid"] == prepared["txid"] and confirmed["confirmations"] >= 2,
                "Prepared payment failed confirmation")
        spent = wallet_rpc("get_balance")
        require(spent["balance"] == before["balance"] - amount - int(prepared["feeAtoms"]),
                "Source debit differs from exact payment and fee")
        wallet_rpc("close_wallet")
        wallet_rpc("open_wallet", {"filename": "recipient", "password": ""})
        wallet_rpc("refresh", {"start_height": 0})
        received = wallet_rpc("get_transfer_by_txid", {"txid": prepared["txid"]})["transfer"]
        require(received["type"] == "in" and received["amount"] == amount,
                "Recipient did not receive the exact prepared amount")
        require(rpc(daemon_port, "get_info")["tx_pool_size"] == 0, "Confirmed fixture pool was not empty")
        private_write(args.report, {"schema": "monzero-native-lock-checkpoint-results-v1", "chain": "xmz-fakechain",
            "genesis": GENESIS, "nativePreparationVerified": True, "coldCheckpointRecoveryVerified": True,
            "bindingAndTamperRejectionVerified": True, "preparationBalanceAndPoolUnchanged": True,
            "nativeRelayVerified": True, "nativeProofVerified": True, "receivedAtoms": str(amount),
            "txid": prepared["txid"], "feeAtoms": prepared["feeAtoms"], "weight": prepared["weight"],
            "fullCrossChainSettlement": False, "executable": False})
        print("PASS: actual native wallet preparation retained exact transaction metadata and proof without relay")
        print("PASS: fresh-process recovery with services stopped; wrong binding and altered checkpoint rejected")
        print("PASS: exact saved transaction relayed, pending proof rejected, confirmed proof and exact recipient amount verified")
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=20)
        if all(process.poll() is not None for process in processes):
            shutil.rmtree(root)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("daemon", "wallet-rpc", "engine-test-binary", "temp-parent", "report"):
        parser.add_argument("--" + name, required=True, type=pathlib.Path)
    args = parser.parse_args()
    for name, value in vars(args).items():
        setattr(args, name, value.resolve(strict=name != "report"))
    run(args)
