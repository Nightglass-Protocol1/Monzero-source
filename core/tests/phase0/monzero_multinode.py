#!/usr/bin/env python3

"""Isolated Monzero three-node propagation and restart smoke test.

This script uses temporary regtest data and wallet directories. It never reads
or writes the user's normal Monzero data directory or wallet files.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


HOST = "127.0.0.1"
P2P_PORTS = (26174, 26184, 26194)
RPC_PORTS = (26175, 26185, 26195)
ZMQ_PORTS = (26176, 26186, 26196)
WALLET_RPC_PORT = 26205


class TestFailure(RuntimeError):
    pass


def request(port: int, path: str, body: dict, timeout: float = 5.0) -> dict:
    payload = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        f"http://{HOST}:{port}{path}",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            result = json.loads(response.read().decode("utf-8"))
    except TimeoutError as error:
        # Identify the operation without logging parameters, which may contain
        # temporary recovery seeds or wallet passwords.
        operation = body.get("method", path)
        raise TestFailure(f"RPC {operation} on port {port} timed out after {timeout}s") from error
    if "error" in result:
        raise TestFailure(f"RPC error from port {port}: {result['error']}")
    return result


def json_rpc(port: int, method: str, params: dict | None = None) -> dict:
    # Wallet creation/opening performs password derivation; refresh and signing
    # can also exceed the short node-health timeout on a busy release builder.
    # Keep these operations bounded, and never retry a state-changing request.
    timeout = 60.0 if port == WALLET_RPC_PORT else 5.0
    response = request(
        port,
        "/json_rpc",
        {"jsonrpc": "2.0", "id": "0", "method": method, "params": params or {}},
        timeout=timeout,
    )
    return response.get("result", {})


def get_info(port: int) -> dict:
    return request(port, "/get_info", {})


def transaction_pool_hashes(port: int) -> set[str]:
    response = request(port, "/get_transaction_pool", {})
    return {
        transaction.get("id_hash", "")
        for transaction in response.get("transactions", [])
        if transaction.get("id_hash")
    }


def wait_until(description: str, predicate, timeout: float = 45.0) -> None:
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (OSError, TestFailure, urllib.error.URLError, ValueError) as error:
            last_error = error
        time.sleep(0.25)
    suffix = f"; last error: {last_error}" if last_error else ""
    raise TestFailure(f"Timed out waiting for {description}{suffix}")


def stop_process(process: subprocess.Popen | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def daemon_command(binary: pathlib.Path, root: pathlib.Path, index: int) -> list[str]:
    peers = []
    if index > 0:
        peers.extend(["--add-exclusive-node", f"{HOST}:{P2P_PORTS[index - 1]}"])
    if index + 1 < len(P2P_PORTS):
        peers.extend(["--add-exclusive-node", f"{HOST}:{P2P_PORTS[index + 1]}"])
    return [
        str(binary),
        "--regtest",
        "--fixed-difficulty", "1",
        "--data-dir", str(root / f"node-{index}"),
        "--p2p-bind-ip", HOST,
        "--p2p-bind-port", str(P2P_PORTS[index]),
        "--rpc-bind-ip", HOST,
        "--rpc-bind-port", str(RPC_PORTS[index]),
        "--zmq-rpc-bind-ip", HOST,
        "--zmq-rpc-bind-port", str(ZMQ_PORTS[index]),
        "--rpc-ssl", "disabled",
        "--disable-dns-checkpoints",
        "--check-updates", "disabled",
        "--no-igd",
        "--non-interactive",
        "--log-level", "0",
        *peers,
    ]


def start_daemon(binary: pathlib.Path, root: pathlib.Path, index: int, logs) -> subprocess.Popen:
    log = open(root / f"node-{index}.log", "ab", buffering=0)
    logs.append(log)
    return subprocess.Popen(
        daemon_command(binary, root, index),
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build", help="CMake build directory")
    parser.add_argument("--keep-data", action="store_true", help="keep temporary logs and databases")
    args = parser.parse_args()

    build_dir = pathlib.Path(args.build_dir).resolve()
    daemon = build_dir / "bin" / "monzerod"
    wallet_rpc = build_dir / "bin" / "monzero-wallet-rpc"
    for binary in (daemon, wallet_rpc):
        if not binary.is_file():
            raise TestFailure(f"Required binary not found: {binary}")

    temporary_parent = build_dir / "test-tmp"
    temporary_parent.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix="monzero-phase0-", dir=temporary_parent))
    processes: list[subprocess.Popen | None] = [None, None, None]
    wallet_process = None
    logs = []
    passed = False

    try:
        print(f"Temporary test network: {root}")
        for index in range(3):
            processes[index] = start_daemon(daemon, root, index, logs)

        for index, port in enumerate(RPC_PORTS):
            wait_until(f"node {index} RPC", lambda port=port: get_info(port).get("status") == "OK")

        wait_until(
            "the three-node P2P topology",
            lambda: all(
                get_info(port).get("incoming_connections_count", 0)
                + get_info(port).get("outgoing_connections_count", 0) >= 1
                for port in RPC_PORTS
            ),
            timeout=60,
        )

        wallet_log = open(root / "wallet-rpc.log", "ab", buffering=0)
        logs.append(wallet_log)
        (root / "wallets").mkdir()
        wallet_process = subprocess.Popen(
            [
                str(wallet_rpc),
                "--wallet-dir", str(root / "wallets"),
                "--rpc-bind-ip", HOST,
                "--rpc-bind-port", str(WALLET_RPC_PORT),
                "--disable-rpc-login",
                "--rpc-ssl", "disabled",
                "--daemon-address", f"{HOST}:{RPC_PORTS[0]}",
                "--daemon-ssl", "disabled",
                # Regtest intentionally activates the latest hard fork at
                # height 1, which cannot match the public-network schedule.
                "--allow-mismatched-daemon-version",
                "--log-level", "0",
            ],
            stdout=wallet_log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        wait_until("wallet RPC", lambda: json_rpc(WALLET_RPC_PORT, "get_version").get("version", 0) > 0)
        json_rpc(WALLET_RPC_PORT, "create_wallet", {"filename": "miner", "password": "", "language": "English"})
        address = json_rpc(WALLET_RPC_PORT, "get_address").get("address")
        if not address:
            raise TestFailure("Temporary wallet did not return an address")

        initial_height = get_info(RPC_PORTS[0])["height"]
        json_rpc(RPC_PORTS[0], "generateblocks", {"wallet_address": address, "amount_of_blocks": 12})
        target_height = initial_height + 12
        wait_until(
            f"all nodes to reach height {target_height}",
            lambda: all(get_info(port)["height"] >= target_height for port in RPC_PORTS),
            timeout=90,
        )
        hashes = [get_info(port)["top_block_hash"] for port in RPC_PORTS]
        if len(set(hashes)) != 1:
            raise TestFailure(f"Nodes disagree on the propagated tip: {hashes}")

        print("Stopping node 2 and extending the chain by five blocks...")
        stop_process(processes[2])
        processes[2] = None
        json_rpc(RPC_PORTS[0], "generateblocks", {"wallet_address": address, "amount_of_blocks": 5})
        restart_target = target_height + 5
        wait_until("connected nodes to accept the extension", lambda: get_info(RPC_PORTS[1])["height"] >= restart_target)

        processes[2] = start_daemon(daemon, root, 2, logs)
        wait_until("restarted node 2 RPC", lambda: get_info(RPC_PORTS[2]).get("status") == "OK")
        wait_until(
            f"restarted node 2 to catch up to height {restart_target}",
            lambda: get_info(RPC_PORTS[2])["height"] >= restart_target,
            timeout=90,
        )
        final_info = [get_info(port) for port in RPC_PORTS]
        final_hashes = [info["top_block_hash"] for info in final_info]
        if len(set(final_hashes)) != 1:
            raise TestFailure(f"Nodes disagree after restart: {final_hashes}")

        print("Testing competing forks and longest-chain convergence...")
        stop_process(processes[1])
        processes[1] = None
        wait_until(
            "edge nodes to become isolated",
            lambda: all(
                get_info(port).get("incoming_connections_count", 0)
                + get_info(port).get("outgoing_connections_count", 0) == 0
                for port in (RPC_PORTS[0], RPC_PORTS[2])
            ),
            timeout=30,
        )
        json_rpc(RPC_PORTS[0], "generateblocks", {"wallet_address": address, "amount_of_blocks": 3})
        json_rpc(RPC_PORTS[2], "generateblocks", {"wallet_address": address, "amount_of_blocks": 5})
        longer_fork_height = restart_target + 5
        longer_fork_tip = get_info(RPC_PORTS[2])["top_block_hash"]

        processes[1] = start_daemon(daemon, root, 1, logs)
        wait_until("restarted node 1 RPC", lambda: get_info(RPC_PORTS[1]).get("status") == "OK")
        wait_until(
            "all nodes to adopt the longer competing fork",
            lambda: all(
                get_info(port)["height"] >= longer_fork_height
                and get_info(port)["top_block_hash"] == longer_fork_tip
                for port in RPC_PORTS
            ),
            timeout=120,
        )

        print("Testing wallet-to-wallet transaction relay and confirmation...")
        json_rpc(RPC_PORTS[0], "generateblocks", {"wallet_address": address, "amount_of_blocks": 60})
        spendable_height = longer_fork_height + 60
        wait_until(
            f"all nodes to reach spendable height {spendable_height}",
            lambda: all(get_info(port)["height"] >= spendable_height for port in RPC_PORTS),
            timeout=120,
        )
        json_rpc(WALLET_RPC_PORT, "refresh")

        json_rpc(WALLET_RPC_PORT, "close_wallet")
        json_rpc(WALLET_RPC_PORT, "create_wallet", {"filename": "receiver", "password": "", "language": "English"})
        receiver_address = json_rpc(WALLET_RPC_PORT, "get_address").get("address")
        if not receiver_address:
            raise TestFailure("Receiver wallet did not return an address")
        json_rpc(WALLET_RPC_PORT, "close_wallet")
        json_rpc(WALLET_RPC_PORT, "open_wallet", {"filename": "miner", "password": ""})
        json_rpc(WALLET_RPC_PORT, "refresh")

        transfer = json_rpc(
            WALLET_RPC_PORT,
            "transfer",
            {
                "destinations": [{"amount": 100_000_000_000, "address": receiver_address}],
                "get_tx_key": True,
            },
        )
        transaction_hash = transfer.get("tx_hash")
        if not transaction_hash:
            raise TestFailure(f"Transfer did not return a transaction hash: {transfer}")
        wait_until(
            "transaction relay to every node",
            lambda: all(transaction_hash in transaction_pool_hashes(port) for port in RPC_PORTS),
            timeout=60,
        )

        json_rpc(RPC_PORTS[1], "generateblocks", {"wallet_address": address, "amount_of_blocks": 1})
        confirmed_height = spendable_height + 1
        wait_until(
            f"transaction confirmation at height {confirmed_height}",
            lambda: all(
                get_info(port)["height"] >= confirmed_height
                and transaction_hash not in transaction_pool_hashes(port)
                for port in RPC_PORTS
            ),
            timeout=90,
        )
        json_rpc(WALLET_RPC_PORT, "close_wallet")
        json_rpc(WALLET_RPC_PORT, "open_wallet", {"filename": "receiver", "password": ""})
        json_rpc(WALLET_RPC_PORT, "refresh")
        receiver_balance = json_rpc(WALLET_RPC_PORT, "get_balance").get("balance", 0)
        if receiver_balance != 100_000_000_000:
            raise TestFailure(f"Receiver balance is {receiver_balance}, expected 100000000000")

        receiver_seed = json_rpc(WALLET_RPC_PORT, "query_key", {"key_type": "mnemonic"}).get("key")
        if not receiver_seed:
            raise TestFailure("Receiver wallet did not return its temporary recovery seed")
        json_rpc(WALLET_RPC_PORT, "close_wallet")
        restored = json_rpc(
            WALLET_RPC_PORT,
            "restore_deterministic_wallet",
            {
                "filename": "receiver-restored",
                "password": "",
                "seed": receiver_seed,
                "restore_height": 0,
            },
        )
        if restored.get("address") != receiver_address:
            raise TestFailure(
                f"Restored address {restored.get('address')} does not match {receiver_address}"
            )
        json_rpc(WALLET_RPC_PORT, "refresh")
        restored_balance = json_rpc(WALLET_RPC_PORT, "get_balance").get("balance", 0)
        if restored_balance != receiver_balance:
            raise TestFailure(
                f"Restored balance is {restored_balance}, expected {receiver_balance}"
            )

        final_info = [get_info(port) for port in RPC_PORTS]
        final_hashes = [info["top_block_hash"] for info in final_info]
        if len(set(final_hashes)) != 1:
            raise TestFailure(f"Nodes disagree after transaction confirmation: {final_hashes}")

        print("PASS: connectivity, propagation, catch-up, reorg, transfer, and wallet restoration")
        print("Final height:", final_info[0]["height"])
        print("Final tip:", final_hashes[0])
        print("Confirmed transaction:", transaction_hash)
        passed = True
        return 0
    finally:
        stop_process(wallet_process)
        for process in reversed(processes):
            stop_process(process)
        for log in logs:
            log.close()
        if args.keep_data or not passed:
            print(f"Kept test data at {root}")
        else:
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (TestFailure, OSError, urllib.error.URLError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
