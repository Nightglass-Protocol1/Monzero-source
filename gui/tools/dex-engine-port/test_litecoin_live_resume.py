#!/usr/bin/env python3
"""Resume a live-chain LTC/XMZ trade after the apps were interrupted, and recover it.

Covers recovery-runbook stage 2: the Litecoin lock was sent, XMZ was never
locked, and the apps stopped. This reopens the saved trade folders exactly as a
restarted app would (no new keys, no new trade), waits for the cancellation
timelock without mining, then has the Litecoin funder approve cancel and refund
and checks the refund on chain.

    test_litecoin_live_resume.py --workspace DIR --lock-txid TXID \
        --ltc-rpc-port 19332 --ltc-cookie COOKIE --xmz-daemon-port 16175 \
        --wallet-rpc monzero-wallet-rpc --gui-test-binary dex_tests --report OUT.json
"""
import argparse
import json
import pathlib
import subprocess
import time
from test_litecoin_settlement import write_private, require, atoms, RpcError
from test_wallet_recovery import ready
from test_litecoin_live_swap import LtcRpc, KEEPALIVE_SECONDS
from litecoin_app_fixture import LitecoinAppFixture


def run(args):
    root = pathlib.Path(args.workspace).resolve()
    request = json.loads((root/"request.json").read_text())
    terms, policy = request["terms"], request["policy"]
    cancel_blocks = int(policy["cancel_blocks"])
    ltc = LtcRpc(args.ltc_rpc_port, args.ltc_cookie)
    processes, resident, evidence = [], None, {"schema":"monzero-ltc-live-resume-evidence-v1",
        "workspace":str(root), "swapId":terms["swapId"], "ltcLockTxid":args.lock_txid, "cancelBlocks":cancel_blocks}
    started = time.monotonic()

    def note(event, **fields):
        print(json.dumps(dict(event=event, elapsedSeconds=int(time.monotonic()-started), unix=int(time.time()), **fields),
            sort_keys=True), flush=True)

    def depth(txid):
        try:
            return int(ltc("getrawtransaction", txid, True).get("confirmations", 0))
        except RpcError:
            return 0

    def wait_until(label, predicate, timeout):
        deadline, last = time.monotonic()+timeout, time.monotonic()
        note("wait", label=label)
        while not predicate():
            require(time.monotonic() < deadline, "Timed out waiting for "+label)
            if resident is not None and time.monotonic()-last >= KEEPALIVE_SECONDS:
                resident.call("alice", "status"); last = time.monotonic()
            time.sleep(20)
        note("waited", label=label)

    try:
        lock = ltc("getrawtransaction", args.lock_txid, True)
        contract = None
        for output in lock["vout"]:
            if atoms(output["value"]) == int(terms["ltcAtoms"]):
                script = output["scriptPubKey"]  # Litecoin Core 0.21 lists "addresses"
                contract = script.get("address") or (script.get("addresses") or [None])[0]
        require(contract, "Lock transaction has no output of the agreed amount")
        note("resume", contract=contract, lockDepth=depth(args.lock_txid), cancelBlocks=cancel_blocks)

        # The same three wallet services the trade was configured with, on the same ports.
        ports = {"source-wallets":json.loads((root/"app-funding-rpc.json").read_text())["nativeWalletPort"],
                 "claim-wallets":json.loads((root/"app-bob-payment-rpc.json").read_text())["nativeWalletPort"],
                 "observer-wallets":json.loads((root/"app-redeem-rpc.json").read_text())["observerWalletPort"]}
        for name, rpc_port in ports.items():
            login = root/f"monzero-wallet-rpc.{rpc_port}.login"
            if login.exists():
                login.unlink()
            command = [str(args.wallet_rpc), "--testnet", "--wallet-dir", str(root/name),
                "--daemon-address", f"127.0.0.1:{args.xmz_daemon_port}", "--trusted-daemon",
                "--allow-mismatched-daemon-version", "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(rpc_port),
                "--non-interactive", "--max-concurrency", "2", "--log-file", str(root/(name+"-resume.log"))]
            process = subprocess.Popen(command, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            processes.append(process)
            ready(process, rpc_port, "get_version", login)

        # Wait for the timelock before reopening, like a user coming back later.
        wait_until("cancellation timelock", lambda: depth(args.lock_txid) >= cancel_blocks, args.ltc_wait)

        session = json.loads((root/"session.json").read_text())
        generation = 1 + sum(1 for _ in root.glob("resident-app-resumed-*"))
        directory = root/f"resident-app-resumed-{generation}"; directory.mkdir(mode=0o700)
        fixture = root/f"resident-resume-{generation}.json"
        session.update(resumeResident=True, residentDirectory=str(directory))
        write_private(fixture, session)
        resident = LitecoinAppFixture(args.gui_test_binary, fixture, directory, processes,
            generation=generation, function_timeout_ms=args.trade_timeout*1000)
        for role in ("alice", "bob"):
            state = resident.call(role, "journal")
        # As the app's own restart path: let the peer connection come back
        # before any operation, or the busy app refuses it without running.
        resident.call("bob", "wait-negotiation", accepted=None)
        note("reopened", bobReservation=state["reservation"]["state"])

        recovered = {}
        for kind in ("cancel", "refund"):
            # A run reopened after this step confirmed (its app stopped later) skips it.
            state = resident.call("bob", "recovery-check", accepted=None, kind=kind)["reservation"]
            observed = state.get("recoveryObservation") or {}
            if state["recoveryCheckState"] == "confirmed" and observed.get("operation") == kind:
                recovered[kind] = observed["txid"]
                note("ltc-"+kind+"-already-confirmed", txid=observed["txid"])
                continue
            review = resident.call("bob", "recovery-review", kind=kind)["reservation"]["recoveryReview"]
            require(review and review["details"]["action"] in ("send", "retransmit"), "No "+kind+" approval offered")
            resident.call("bob", "recovery-submit", accepted=False, commitment="0"*64)
            state = resident.call("bob", "recovery-submit", commitment=review["commitment"])["reservation"]
            txid = review["details"]["txid"]
            note("ltc-"+kind+"-sent", txid=txid)
            wait_until("ltc "+kind+" confirmations", lambda: depth(txid) >= 1, args.ltc_wait)
            def checked():
                state = resident.call("bob", "recovery-check", kind=kind)["reservation"]
                return state["recoveryCheckState"] == "confirmed"
            wait_until("app "+kind+" check", checked, 3600)
            recovered[kind] = txid

        refund = ltc("getrawtransaction", recovered["refund"], True)
        expected = int(terms["ltcAtoms"])-int(terms["ltcCancelFeeAtoms"])-int(terms["ltcRefundFeeAtoms"])
        refund_script = ltc("validateaddress", terms["ltcRefund"])["scriptPubKey"]
        require(len(refund["vout"]) == 1 and atoms(refund["vout"][0]["value"]) == expected
            and refund["vout"][0]["scriptPubKey"]["hex"] == refund_script, "Wrong Litecoin refund amount or destination")
        evidence.update(ltcCancelTxid=recovered["cancel"], ltcRefundTxid=recovered["refund"],
            ltcRefundAtoms=str(expected), appRestartedAfterLock=True, xmzLocked=False, passed=True)
        note("refunded", atoms=str(expected), txid=recovered["refund"])
        resident.stop(); resident = None
    finally:
        if resident is not None:
            try:
                resident.stop()
            except Exception:  # noqa: BLE001 — best effort during failure cleanup
                pass
        for process in processes:
            if process.poll() is None:
                process.terminate()
        for process in processes:
            try:
                process.wait(30)
            except subprocess.TimeoutExpired:
                process.kill()
        if args.report:
            report = pathlib.Path(args.report)
            if report.exists():
                report.rename(report.with_suffix(f".{int(time.time())}.json"))
            write_private(report, evidence)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--lock-txid", required=True)
    parser.add_argument("--ltc-rpc-port", type=int, required=True)
    parser.add_argument("--ltc-cookie", required=True)
    parser.add_argument("--xmz-daemon-port", type=int, required=True)
    parser.add_argument("--wallet-rpc", type=pathlib.Path, required=True)
    parser.add_argument("--gui-test-binary", type=pathlib.Path, required=True)
    parser.add_argument("--report")
    parser.add_argument("--ltc-wait", type=int, default=12*3600)
    parser.add_argument("--trade-timeout", type=int, default=14*3600)
    run(parser.parse_args())
