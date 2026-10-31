#!/usr/bin/env python3
"""Actual native wallet import using keys recovered from a test LTC redemption.

The Litecoin signature is a deterministic fixture, not confirmed-chain authority.
"""
import argparse
import json
import hashlib
import stat
import os
import pathlib
import secrets
import shutil
import subprocess
import tempfile
from test_wallet_recovery import GENESIS, port, ready, rpc
from test_native_lock_checkpoint import private_write, require

TEST = "litecoin_negotiation::execution::native_claim_wallet::tests::actual_native_claim_wallet_restore"


def run(args):
    require(not args.report.exists(), "Report already exists")
    root = pathlib.Path(tempfile.mkdtemp(prefix="xmz-claim-wallet-", dir=args.temp_parent)).resolve()
    processes = []
    try:
        daemon_port, wallet_port, p2p_port = port(), port(), port()
        require(len({daemon_port, wallet_port, p2p_port}) == 3, "Fixture port collision")
        request = {"action":"describe", "daemon_port":daemon_port, "wallet_port":wallet_port,
                   "txid":"ab"*32, "password":secrets.token_hex(16)}

        def invoke(success=True):
            path = root / "request.json"
            if path.exists():
                path.unlink()  # Disposable harness input only.
            private_write(path, request)
            response = root / (request["action"] + "-result.json")
            if response.exists():
                response.unlink()  # Disposable harness output, never execution state.
            result = subprocess.run([str(args.engine_test_binary), "--exact", TEST, "--ignored"],
                env=dict(os.environ, MONZERO_NATIVE_CLAIM_FIXTURE=str(root)),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
            require((result.returncode == 0 and b"1 passed" in result.stdout) == success,
                    "Unexpected claim wallet fixture result for " + request["action"])
            require(response.exists() == success, "Unexpected claim fixture response retention")

        invoke()
        described = json.loads((root / "describe-result.json").read_text())
        expected_address = described["address"]
        daemon = subprocess.Popen([str(args.daemon), "--regtest", "--keep-fakechain", "--fixed-difficulty", "1",
            "--offline", "--no-igd", "--hide-my-port", "--no-zmq", "--non-interactive",
            "--data-dir", str(root / "chain"), "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(daemon_port),
            "--p2p-bind-ip", "127.0.0.1", "--p2p-bind-port", str(p2p_port), "--log-file", str(root / "node.log")],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(daemon)
        info = ready(daemon, daemon_port, "get_info")
        require(info["nettype"] == "fakechain" and info["offline"]
                and info["incoming_connections_count"] == info["outgoing_connections_count"] == 0, "Node is not isolated")
        require(rpc(daemon_port, "get_block", {"height":0})["block_header"]["hash"] == GENESIS, "Wrong fakechain genesis")
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

        wallet_rpc("create_wallet", {"filename":"mining", "password":"", "language":"English"})
        mining = wallet_rpc("get_address")["address"]
        wallet_rpc("close_wallet")
        destinations = {}
        for name in ("payout", "reserve"):
            wallet_rpc("create_wallet", {"filename":name,"password":"","language":"English"})
            destinations[name] = wallet_rpc("get_address")["address"]
            if args.subaddress_recipients:
                destinations[name] = wallet_rpc("create_address", {"account_index":0, "label":"owned-payout-fixture"})["address"]
            wallet_rpc("close_wallet")
        if args.coincident_recipients:
            destinations["reserve"] = destinations["payout"]
        wallet_rpc("create_wallet", {"filename":"source", "password":"", "language":"English"})
        source = wallet_rpc("get_address")["address"]
        rpc(daemon_port, "generateblocks", {"wallet_address":source, "amount_of_blocks":131})
        wallet_rpc("refresh", {"start_height":0})
        amount = 200000000000
        sent = wallet_rpc("transfer", {"destinations":[{"address":expected_address,"amount":amount}],
                                      "account_index":0,"priority":1,"get_tx_key":False})
        request["txid"] = sent["tx_hash"]
        rpc(daemon_port, "generateblocks", {"wallet_address":mining, "amount_of_blocks":10})
        wallet_rpc("close_wallet")
        # A view-only file has the same address but cannot authorize spending.
        # Simulate accidental legacy filename collision only inside this fixture.
        request["action"] = "restore_watch"
        invoke()
        watched = json.loads((root / "restore_watch-result.json").read_text())
        require(watched["address"] == expected_address and watched["wallet"] != described["wallet"],
                "Watch and spend wallet namespaces overlap")
        wallet_rpc("close_wallet")
        for suffix in ("", ".keys"):
            (root / "wallets" / (watched["wallet"]+suffix)).rename(root / "wallets" / (described["wallet"]+suffix))
        request["action"] = "open"
        invoke(False)
        wallet_rpc("close_wallet")
        for suffix in ("", ".keys"):
            (root / "wallets" / (described["wallet"]+suffix)).rename(root / "wallets" / (watched["wallet"]+suffix))
        request["action"] = "restore"
        invoke()
        require(json.loads((root / "restore-result.json").read_text()) == described, "Restored wallet identity differs")

        def verify():
            require(wallet_rpc("get_address")["address"] == expected_address, "Wrong restored address")
            wallet_rpc("refresh", {"start_height":0})
            transfer = wallet_rpc("get_transfer_by_txid", {"txid":request["txid"]})["transfer"]
            require(transfer["type"] == "in" and transfer["amount"] == amount, "Claimed wallet lacks exact payment")
            require(wallet_rpc("get_balance")["balance"] == amount, "Unexpected claim wallet balance")
            require(rpc(daemon_port, "get_info")["tx_pool_size"] == 0, "Wallet import broadcast a transaction")

        verify()
        invoke(False)  # Already-open wallet must not be replaced.
        wallet_rpc("close_wallet")
        wallet.terminate()
        wallet.wait(timeout=20)
        wallet = subprocess.Popen(wallet.args, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(wallet)
        ready(wallet, wallet_port, "get_version", login)
        request["action"] = "open"
        password = request["password"]
        request["password"] = "wrong-fixture-password"
        invoke(False)
        request["password"] = password
        invoke()
        require(json.loads((root / "open-result.json").read_text()) == described, "Reopened wallet identity differs")
        verify()
        request["action"] = "prepare_legacy"
        invoke(False)  # V1 may restore funds but cannot authorize the v2 payout path.
        verify()
        request.update(action="prepare_payout",payout_address=destinations["payout"],reserve_address=destinations["reserve"],
                       locked=amount,principal=100000000000,maximum_fee=10000000000)
        if args.zero_reserve_return:
            # Estimate before signing the fixture quote or constructing its saved
            # payout. This disposable preview never relays or supplies a checkpoint.
            estimate = wallet_rpc("transfer", {"destinations":[{"address":destinations["payout"],"amount":amount}],
                "account_index":0,"priority":1,"unlock_time":0,"subtract_fee_from_outputs":[0],
                "do_not_relay":True,"get_tx_key":False,"get_tx_hex":False,"get_tx_metadata":False})
            request["maximum_fee"] = estimate["fee"]
            request["principal"] = amount - estimate["fee"]
        if args.reserve_at_fee_ceiling:
            request["principal"] = amount - request["maximum_fee"]
        invoke()
        prepared = json.loads((root / "prepare_payout-result.json").read_text())
        payout = prepared["payout"]
        require(payout["schema"] == "monzero-native-single-lock-payout-v1" and payout["lockTxid"] == request["txid"],
                "Wrong single-lock payout context")
        require(int(payout["payoutAtoms"]) == request["principal"] and 0 < int(payout["feeAtoms"]) <= request["maximum_fee"],
                "Payout principal or fee differs")
        require(int(payout["payoutAtoms"]) + int(payout["reserveReturnAtoms"]) + int(payout["feeAtoms"]) == amount,
                "Payout does not account for the full lock")
        require(payout["destination"] == destinations["payout"] and payout["reserveReturn"] == destinations["reserve"],
                "Wrong payout destinations")
        if args.zero_reserve_return:
            require(int(payout["reserveReturnAtoms"]) == 0, "Zero-return boundary was not exercised")
        verify()  # Non-relaying construction must preserve the original balance and empty pool.
        checkpoint = root / "payout-checkpoint.bin"
        saved = checkpoint.read_bytes()
        require(stat.S_IMODE(checkpoint.stat().st_mode) == 0o600, "Checkpoint permissions differ")
        require(list(hashlib.sha256(saved).digest()) == prepared["checkpointDigest"], "Checkpoint digest differs")
        proof_key_count = len(json.loads(saved)["tx_keys"]) // 64
        require(proof_key_count >= 1, "Missing retained transaction proof key")
        if args.subaddress_recipients and not args.coincident_recipients and not args.zero_reserve_return:
            require(proof_key_count > 1, "Additional transaction keys were not exercised")
        wallet.terminate()
        wallet.wait(timeout=20)
        daemon.terminate()
        daemon.wait(timeout=20)
        request.update(action="recover_payout", daemon_port=1, wallet_port=2, expected_digest=prepared["checkpointDigest"])
        invoke()
        require(json.loads((root / "recover_payout-result.json").read_text()) == prepared, "Cold payout recovery changed accounting")
        request["principal"] += 1
        invoke(False)
        request["principal"] -= 1
        damaged = bytearray(saved)
        damaged[len(damaged)//2] ^= 1
        checkpoint.write_bytes(damaged)
        invoke(False)
        checkpoint.write_bytes(saved)  # Undo only the owned fixture's corruption injection.
        if args.relay_payout:
            daemon = subprocess.Popen(daemon.args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            processes.append(daemon)
            ready(daemon, daemon_port, "get_info")
            wallet = subprocess.Popen(wallet.args, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            processes.append(wallet)
            ready(wallet, wallet_port, "get_version", login)
            request.update(action="open", daemon_port=daemon_port, wallet_port=wallet_port)
            invoke()
            verify()
            request["action"] = "relay_payout"
            invoke()
            relayed = json.loads((root / "relay_payout-result.json").read_text())
            require(relayed["relayTxid"] == payout["txid"], "Relayed payout identity changed")
            require(checkpoint.read_bytes() == saved, "Relay changed private checkpoint")
            request["action"] = "confirm_payout"
            invoke(False)  # Mempool inclusion is not confirmation.
            rpc(daemon_port, "generateblocks", {"wallet_address":mining, "amount_of_blocks":10})
            wallet_rpc("refresh", {"start_height":0})
            invoke()
            require(json.loads((root / "confirm_payout-result.json").read_text())["confirmedDepth"] >= 10,
                    "Payout confirmation missing")
            require(wallet_rpc("get_balance")["balance"] == 0, "Payout did not consume the full lock")
            # Destroy only this disposable wallet's cache, preserving its keys.
            # Recovery must prove recipient amounts without outgoing destinations.
            wallet_rpc("close_wallet")
            cache = root / "wallets" / described["wallet"]
            require(cache.is_file() and not cache.is_symlink() and pathlib.Path(str(cache)+".keys").is_file(),
                    "Unexpected disposable claim wallet layout")
            cache.rename(root / "claim-cache-before-rebuild")
            request["action"] = "open"
            invoke()
            wallet_rpc("refresh", {"start_height":0})
            rebuilt = wallet_rpc("get_transfer_by_txid", {"txid":payout["txid"]})["transfer"]
            require(rebuilt["type"] == "out" and not rebuilt.get("destinations"), "Cache loss was not exercised")
            request["action"] = "confirm_payout"
            invoke()
            require(json.loads((root / "confirm_payout-result.json").read_text())["confirmedDepth"] >= 10,
                    "Proof recovery failed after cache rebuild")
            require(checkpoint.read_bytes() == saved, "Cache recovery changed private checkpoint")
            wallet_rpc("close_wallet")
            recipients = (("payout", request["principal"]), ("reserve", int(payout["reserveReturnAtoms"])))
            if args.coincident_recipients:
                recipients = (("payout", request["principal"] + int(payout["reserveReturnAtoms"])),)
            for name, expected in recipients:
                wallet_rpc("open_wallet", {"filename":name, "password":""})
                wallet_rpc("refresh", {"start_height":0})
                require(wallet_rpc("get_balance")["balance"] == expected, "Incorrect recipient balance: " + name)
                if expected == 0:
                    history = wallet_rpc("get_transfers", {"in":True})
                    require(not any(item["txid"] == payout["txid"] for item in history.get("in", [])),
                            "Zero-return recipient unexpectedly received payment")
                    wallet_rpc("close_wallet")
                    continue
                received = wallet_rpc("get_transfer_by_txid", {"txid":payout["txid"]})["transfer"]
                require(received["type"] == "in" and received["amount"] == expected, "Incorrect recipient payment")
                wallet_rpc("close_wallet")
        private_write(args.report, {"schema":"monzero-native-claim-wallet-results-v1", "chain":"xmz-fakechain",
            "adaptorRecoveredKeys":True,"nativeRestoreVerified":True,"watchOnlySubstitutionRejected":True,"exactFundingImportVerified":True,
            "alreadyOpenRejected":True,"wrongPasswordRejected":True,"restartReopenVerified":True,
            "noClaimBroadcast":not args.relay_payout,"nativePayoutRelayVerified":args.relay_payout,"cacheRebuildProofRecoveryVerified":args.relay_payout,"nativePayoutRecipientsVerified":args.relay_payout,"subaddressRecipientsVerified":args.relay_payout and args.subaddress_recipients,"coincidentRecipientsVerified":args.relay_payout and args.coincident_recipients,"reserveAtFeeCeilingVerified":args.reserve_at_fee_ceiling,"zeroReserveReturnVerified":args.relay_payout and args.zero_reserve_return,"retainedProofKeyCount":proof_key_count,"singleLockPayoutPreparationVerified":True,"signedV2PayoutPreparationVerified":True,"legacyPayoutRejected":True,"coldPayoutCheckpointVerified":True,"checkpointBindingAndTamperRejected":True,"receivedAtoms":str(amount),"fullCrossChainSettlement":False,"executable":False})
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=20)
        if all(process.poll() is not None for process in processes):
            shutil.rmtree(root)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("daemon", "wallet-rpc", "engine-test-binary", "temp-parent", "report"):
        parser.add_argument("--"+flag, type=lambda value:pathlib.Path(value).resolve(), required=True)
    parser.add_argument("--relay-payout", action="store_true", help="Relay only the disposable fakechain payout and exercise cache rebuild")
    parser.add_argument("--subaddress-recipients", action="store_true", help="Use separate owned recipient subaddresses to exercise additional proof keys")
    parser.add_argument("--coincident-recipients", action="store_true", help="Pay principal and unused reserve to the same owned recipient")
    parser.add_argument("--reserve-at-fee-ceiling", action="store_true", help="Sign a reserve exactly equal to the maximum permitted fee")
    parser.add_argument("--zero-reserve-return", action="store_true", help="Measure the disposable combined-payment fee before signing an exact zero-return quote")
    run(parser.parse_args())
