#!/usr/bin/env python3
"""Actual isolated Litecoin transaction observation and reorg rejection."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile
from test_litecoin_settlement import Node, require, write_private


def run(args):
    require(not args.report.exists(), "Report already exists")
    with tempfile.TemporaryDirectory(prefix="ltc-observation-", dir=args.temp_parent) as temp:
        root = pathlib.Path(temp)
        chain = root / "node"
        chain.mkdir(mode=0o700)
        node = Node(args.daemon, chain)
        sequence = 0
        try:
            node.start()
            node.rpc("createwallet", "fixture")
            mining = node.rpc("getnewaddress", "", "bech32", wallet="fixture")
            node.mine(440, mining)
            recipient = node.rpc("getnewaddress", "", "bech32", wallet="fixture")
            txid = node.rpc("sendtoaddress", recipient, 1, wallet="fixture")

            def observe(block, minimum=3, reject=False, expected=txid):
                nonlocal sequence
                sequence += 1
                request, response = root / f"request-{sequence}.json", root / f"response-{sequence}.json"
                write_private(request, {"port":node.port,"wallet":"fixture","anchor":mining,"contract":recipient,
                    "amountAtoms":100000000,"feeRateAtomsPerKb":1000,"maximumFeeAtoms":100000,
                    "action":"confirmedTransaction","expectedTxid":expected,"block":block,"minimumConfirmations":minimum})
                env = dict(os.environ, MONZERO_LTC_WALLET_REQUEST=str(request), MONZERO_LTC_WALLET_RESPONSE=str(response),
                           MONZERO_LTC_WALLET_COOKIE=str(chain / "regtest/.cookie"))
                result = subprocess.run([str(args.engine_test_binary), "--exact", "export_litecoin_wallet_rpc", "--ignored"],
                                        env=env, capture_output=True, timeout=30)
                if reject:
                    require(result.returncode != 0 and not response.exists(), "Invalid observation was accepted")
                    return
                require(result.returncode == 0, "Rust transaction observation failed")
                value = json.loads(response.read_text())
                require(value["schema"] == "monzero-ltc-wallet-fixture-v1" and value["executable"] is False,
                        "Unexpected observation schema")
                require(value["txid"] == txid and value["block"] == block and value["confirmations"] >= minimum,
                        "Observation identity or depth mismatch")
                require(value["hex"] == node.rpc("getrawtransaction", txid, False, block), "Transaction bytes differ")
                require(value["tip"] == node.rpc("getbestblockhash"), "Observed tip differs")

            observe(node.rpc("getbestblockhash"), reject=True)
            blocks = node.mine(3, mining)
            observe(blocks[0])
            observe(blocks[0], minimum=4, reject=True)
            observe(blocks[0], minimum=0, reject=True)
            observe(blocks[0], expected="01"*32, reject=True)
            node.stop()
            node.start()
            observe(blocks[0])
            node.rpc("invalidateblock", blocks[0])
            observe(blocks[0], reject=True)
            node.rpc("reconsiderblock", blocks[0])
            observe(blocks[0])
            write_private(args.report, {"schema":"monzero-ltc-confirmed-observation-results-v1", "chain":"regtest",
                "exactTransactionVerified":True,"confirmationPolicyVerified":True,"restartVerified":True,
                "reorgRejectionVerified":True,"reconsiderationVerified":True,"queries":sequence,
                "fullCrossChainSettlement":False,"executable":False})
        finally:
            node.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("daemon", "engine-test-binary", "temp-parent", "report"):
        parser.add_argument("--"+flag, type=lambda value:pathlib.Path(value).resolve(), required=True)
    run(parser.parse_args())
