#!/usr/bin/env python3
"""Restore -> import -> estimate -> relay through the Rust orchestration
primitive (`native_funding_rpc::spendable::swap_wallet_spendable`), against a
real disposable fakechain wallet-rpc process and a real relayed transaction.

Still not an atomic swap: this drives the orchestration primitive directly
with locally generated keys standing in for a swap's negotiated shared
address, not a real two-party swap-machine negotiation, redeem signature or
Bitcoin-side lock. It proves the wallet-bridge primitive actually restores a
real wallet, imports a real transaction, and broadcasts a real transfer to a
real destination on this owned fakechain -- not just that its Rust unit tests
pass in isolation.
"""
import argparse
import os
import pathlib
import subprocess
import tempfile
from test_wallet_recovery import GENESIS, port, ready, rpc, verify_spend_journal

LOCKED_ATOMS = 500_000_000_000  # 5 XMZ locked to the shared address.
SPEND_ATOMS = 400_000_000_000  # 4 XMZ relayed onward, leaving native-fee headroom.


def run(daemon, wallet, engine_test_binary, temp_parent):
    with tempfile.TemporaryDirectory(prefix="xmz-spendable-", dir=temp_parent) as directory:
        root = pathlib.Path(directory)
        processes = []
        try:
            node_port, wallet_port, p2p_port = port(), port(), port()
            if len({node_port, wallet_port, p2p_port}) != 3:
                raise RuntimeError("Ephemeral port collision; rerun fixture")
            login = root / f"monzero-wallet-rpc.{wallet_port}.login"

            def wallet_rpc(method, params=None):
                return rpc(wallet_port, method, params, login=login)

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

            # A funded miner wallet plays the role of "wherever the locked
            # funds originally came from" -- irrelevant to the primitive
            # under test, just a source of real spendable atoms.
            wallet_rpc("create_wallet", {"filename": "miner", "password": "", "language": "English"})
            miner = wallet_rpc("get_address")["address"]
            rpc(node_port, "generateblocks", {"wallet_address": miner, "amount_of_blocks": 1})
            for _ in range(13):
                rpc(node_port, "generateblocks", {"wallet_address": miner, "amount_of_blocks": 10})
            wallet_rpc("refresh", {"start_height": 0})
            assert wallet_rpc("get_balance")["unlocked_balance"] > LOCKED_ATOMS
            wallet_rpc("close_wallet")

            # Stand in for the swap's negotiated shared address (what
            # `bob::State5::xmr_keys()` would give: the combined s_a + s_b
            # spend key and view key). A real swap never has these keys in one
            # place until a counterparty's secret is revealed; here we mint
            # them directly since there is no real swap negotiation in this
            # fixture, only the wallet-bridge primitive that runs after one.
            wallet_rpc("create_wallet", {"filename": "shared", "password": "", "language": "English"})
            shared_address = wallet_rpc("get_address")["address"]
            shared_spend = wallet_rpc("query_key", {"key_type": "spend_key"})["key"]
            shared_view = wallet_rpc("query_key", {"key_type": "view_key"})["key"]
            wallet_rpc("close_wallet")

            wallet_rpc("create_wallet", {"filename": "sweep-destination", "password": "", "language": "English"})
            destination = wallet_rpc("get_address")["address"]
            wallet_rpc("close_wallet")

            restore_height = rpc(node_port, "get_info")["height"]

            # Alice's initial lock, in miniature: an ordinary transfer to the
            # shared address. From the sending wallet's point of view this is
            # exactly a normal payment to a computed destination, matching
            # WALLET_BRIDGE_CLASSIFICATION.md's point that no protocol-specific
            # wallet code is needed for that side.
            wallet_rpc("open_wallet", {"filename": "miner", "password": ""})
            lock = wallet_rpc("transfer", {"destinations": [{"address": shared_address,
                "amount": LOCKED_ATOMS}], "priority": 1, "get_tx_key": False})
            lock_txid = lock["tx_hash"]
            assert len(lock_txid) == 64
            wallet_rpc("close_wallet")
            rpc(node_port, "generateblocks", {"wallet_address": miner, "amount_of_blocks": 10})
            print("PASS: ordinary transfer locks funds to the simulated shared address")

            login_text = login.read_text(encoding="ascii")
            username, password = login_text.split(":", 1)
            del login_text
            environment = dict(os.environ, MONZERO_TEST_DAEMON_RPC_PORT=str(node_port),
                MONZERO_TEST_WALLET_RPC_PORT=str(wallet_port), MONZERO_TEST_WALLET_RPC_LOGIN=str(login),
                MONZERO_TEST_SPEND_WALLET=shared_address, MONZERO_TEST_SPEND_DESTINATION=destination,
                MONZERO_TEST_SPEND_KEY=shared_spend, MONZERO_TEST_SPEND_VIEW_KEY=shared_view,
                MONZERO_TEST_SPEND_RESTORE_HEIGHT=str(restore_height), MONZERO_TEST_SPEND_LOCK_TXID=lock_txid,
                MONZERO_TEST_SPEND_ATOMS=str(SPEND_ATOMS),
                MONZERO_TEST_SPEND_LOCKED_ATOMS=str(LOCKED_ATOMS))
            del username, password
            result_file = root / "spend-result"
            environment["MONZERO_TEST_SPEND_RESULT"] = str(result_file)

            checked = subprocess.run([str(engine_test_binary), "--exact",
                "native_swap_wallet_spendable", "--ignored"], env=environment,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
            if checked.returncode != 0:
                raise RuntimeError("Rust swap_wallet_spendable fixture failed")
            if not result_file.is_file() or result_file.stat().st_size != 65:
                raise RuntimeError("Rust swap_wallet_spendable did not produce its completion marker")
            relayed_txid = result_file.read_text(encoding="ascii").strip()
            if len(relayed_txid) != 64 or any(c not in "0123456789abcdef" for c in relayed_txid):
                raise RuntimeError("Invalid relayed txid in completion marker")
            print("PASS: swap_wallet_spendable rejects a wrong chain policy before relaying, "
                  "then restores, imports and relays a real fakechain transfer")

            # The Rust fixture's first (rejected) attempt and second
            # (successful) attempt each restore, then close, the same
            # underlying wallet under different swap-wallet ids. If either
            # attempt had left the wallet-rpc service wedged with a wallet
            # open, this open would fail.
            wallet_rpc("open_wallet", {"filename": "miner", "password": ""})
            wallet_rpc("close_wallet")
            print("PASS: swap_wallet_spendable leaves the wallet-rpc service closed for the next caller")

            rpc(node_port, "generateblocks", {"wallet_address": miner, "amount_of_blocks": 10})
            wallet_rpc("open_wallet", {"filename": "sweep-destination", "password": ""})
            wallet_rpc("refresh", {"start_height": 0})
            received = wallet_rpc("get_balance")
            assert received["balance"] == received["unlocked_balance"] == SPEND_ATOMS
            confirmed = wallet_rpc("get_transfer_by_txid", {"txid": relayed_txid})["transfer"]
            assert confirmed["type"] == "in" and confirmed["amount"] == SPEND_ATOMS
            assert confirmed["confirmations"] >= 10
            wallet_rpc("close_wallet")
            print("PASS: relayed transfer confirms on-chain for the exact requested destination amount")

            # The shared wallet's own remaining balance accounts for exactly
            # what was locked minus what was relayed and its native fee.
            wallet_rpc("open_wallet", {"filename": "shared", "password": ""})
            wallet_rpc("refresh", {"start_height": 0})
            remaining = wallet_rpc("get_balance")["balance"]
            outgoing = wallet_rpc("get_transfer_by_txid", {"txid": relayed_txid})["transfer"]
            assert outgoing["type"] == "out" and outgoing["amount"] == SPEND_ATOMS
            assert outgoing["confirmations"] >= 10 and outgoing["fee"] > 0
            assert remaining + SPEND_ATOMS + outgoing["fee"] == LOCKED_ATOMS
            verify_spend_journal(root, relayed_txid, SPEND_ATOMS, outgoing["fee"],
                shared_address, destination, lock_txid)
            wallet_rpc("close_wallet")
            print("PASS: shared wallet's remaining balance accounts for the relayed amount and its native fee")
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
    parser.add_argument("--engine-test-binary", required=True, type=pathlib.Path)
    parser.add_argument("--temp-parent", required=True, type=pathlib.Path)
    args = parser.parse_args()
    run(args.daemon.resolve(strict=True), args.wallet_rpc.resolve(strict=True),
        args.engine_test_binary.resolve(strict=True), args.temp_parent.resolve(strict=True))
