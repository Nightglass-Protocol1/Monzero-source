#!/usr/bin/env python3
"""Disposable Dogecoin CLTV/P2SH primitive evidence, not XMZ swap settlement."""
import argparse
import base64
import hashlib
import json
import os
import secrets
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time
import uuid
import urllib.request
import urllib.error
from decimal import Decimal
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

GENESIS = "3d2160a3b5dc4a9d62e7e66a295f70313ac808440ef7400d6c0772171ce973a5"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(args):
    require(not args.report.exists(), "Report already exists")
    root = pathlib.Path(tempfile.mkdtemp(prefix="doge-cltv-", dir=args.temp_parent)).resolve()
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    process = None
    try:
        daemon_args = [str(args.daemon), "-regtest", "-server=1", "-connect=0", "-listen=0",
            "-dnsseed=0", "-discover=0", "-upnp=0", "-txindex=1", "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1",
            "-rpcport="+str(port), "-datadir="+str(root)]
        process = subprocess.Popen(daemon_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        transport = urllib.request.build_opener(urllib.request.ProxyHandler({}))

        def rpc(method, *params):
            auth = base64.b64encode((root / "regtest/.cookie").read_bytes().strip()).decode()
            request = urllib.request.Request(f"http://127.0.0.1:{port}", data=json.dumps(
                {"id":"fixture", "method":method, "params":params}).encode(),
                headers={"Authorization":"Basic "+auth,"Content-Type":"application/json"})
            try:
                response = transport.open(request, timeout=60)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                data = json.loads(response.read(2_000_000), parse_float=Decimal)
            if data.get("error"):
                raise RuntimeError(str(data["error"]))
            return data["result"]

        def wait_ready():
            for attempt in range(100):
                require(process.poll() is None, "Owned Dogecoin daemon exited")
                try:
                    info = rpc("getblockchaininfo")
                    break
                except (OSError, RuntimeError):
                    time.sleep(.1)
            else:
                raise RuntimeError("Owned Dogecoin RPC did not become ready")
            require(info["chain"] == "regtest" and rpc("getblockhash", 0) == GENESIS,
                    "Wrong Dogecoin chain")
            require(rpc("getconnectioncount") == 0, "Fixture has network peers")
            return info

        info = wait_ready()
        def engine(height, funding=None, vout=None, recipient=None):
            request_id = secrets.token_hex(16)
            request_path = root / (request_id+".request.json")
            response_path = root / (request_id+".response.json")
            value = {"schema":"monzero-doge-primitive-request-v1","requestId":request_id,"height":height,
                "funding":funding,"vout":vout,"recipient":recipient}
            with os.fdopen(os.open(request_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w") as out:
                json.dump(value, out)
            result = subprocess.run([str(args.engine_test_binary),"--exact","dogecoin::tests::actual_dogecoin_refund_fixture","--ignored"],
                env=dict(os.environ, MONZERO_DOGE_PRIMITIVE_REQUEST=str(request_path), MONZERO_DOGE_PRIMITIVE_RESPONSE=str(response_path)),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
            require(result.returncode == 0 and response_path.is_file(), "Dogecoin engine fixture failed: "+result.stdout.decode(errors="replace")[-2048:])
            require(response_path.stat().st_size<=16384, "Oversized Dogecoin engine response")
            response = json.loads(response_path.read_text())
            require(response["schema"] == "monzero-doge-primitive-response-v1" and response["requestId"] == request_id
                and response["fullCrossChainSettlement"] is False and response["executable"] is False, "Wrong Dogecoin fixture response")
            return response

        def observe(txid, block, expected_hex, expected_confirmed, minimum=2, unspent_vout=None, expected_unspent=None):
            if not args.observer_test_binary:
                return
            request_id = str(uuid.uuid4())
            request_path = root / (request_id+".observation-request.json")
            response_path = root / (request_id+".observation-response.json")
            value = {"schema":"monzero-doge-observation-request-v1", "requestId":request_id,
                "port":port, "cookie":(root / "regtest/.cookie").read_text().strip(),
                "txid":txid, "block":block, "minimum":minimum, "expectedHex":expected_hex, "unspentVout":unspent_vout}
            with os.fdopen(os.open(request_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w") as out:
                json.dump(value, out)
            result = subprocess.run([str(args.observer_test_binary), "--exact",
                "dogecoin_rpc::tests::actual_dogecoin_observation_fixture", "--ignored"],
                env=dict(os.environ, MONZERO_DOGE_OBSERVATION_REQUEST=str(request_path),
                    MONZERO_DOGE_OBSERVATION_RESPONSE=str(response_path)),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
            require(result.returncode == 0 and b"1 passed" in result.stdout and response_path.is_file(),
                    "Dogecoin observer fixture failed: "+result.stdout.decode(errors="replace")[-2048:])
            require(response_path.stat().st_size <= 16384, "Oversized observation response")
            response = json.loads(response_path.read_text())
            require(response["schema"] == "monzero-doge-observation-response-v1"
                and response["requestId"] == request_id and response["executable"] is False,
                "Wrong observation fixture response")
            require(response["observation"]["confirmed"] is expected_confirmed,
                    "Dogecoin engine confirmation differs from current chain")
            if unspent_vout is not None:
                require(response["unspent"] is expected_unspent,
                        "Dogecoin engine unspent status differs from current chain/mempool")
            if expected_confirmed:
                require(response["observation"]["block"] == block
                    and response["observation"]["confirmations"] >= minimum,
                    "Dogecoin engine returned inconsistent confirmation")

        rpc("generate", 1400)
        info = rpc("getblockchaininfo")
        address = rpc("getnewaddress")
        pubkey = bytes.fromhex(rpc("validateaddress", address)["pubkey"])
        require(len(pubkey) == 33, "Expected compressed public key")
        height = rpc("getblockcount") + 6
        value = height.to_bytes((height.bit_length()+7)//8, "little")
        if value[-1] & 128:
            value += b"\0"
        script = bytes([len(value)]) + value + bytes.fromhex("b17521") + pubkey + bytes.fromhex("ac")
        if args.engine_test_binary:
            description = engine(height)
            script = bytes.fromhex(description["redeemScript"])
        lock_address = rpc("decodescript", script.hex())["p2sh"]
        if args.engine_test_binary:
            require(description["address"] == lock_address, "Engine Dogecoin address differs from node")
        rpc("importaddress", script.hex(), "fixture-cltv", False, True)
        funding = rpc("sendtoaddress", lock_address, "10.00")
        rpc("generate", 1)
        decoded = rpc("getrawtransaction", funding, True)
        outputs = [v for v in decoded["vout"] if lock_address in v["scriptPubKey"].get("addresses", [])]
        require(len(outputs) == 1 and outputs[0]["value"] == Decimal("10"), "Wrong Dogecoin lock output")
        output = outputs[0]
        observe(funding, decoded["blockhash"], decoded["hex"], True, minimum=1,
                unspent_vout=output["n"], expected_unspent=True)
        destination = rpc("getnewaddress")
        inputs = [{"txid":funding,"vout":output["n"],"sequence":4294967294}]
        raw = rpc("createrawtransaction", inputs, {destination:"9.99"}, height)
        if args.engine_test_binary:
            prepared = engine(height, rpc("getrawtransaction", funding), output["n"], destination)
            require(prepared["address"] == lock_address and prepared["redeemScript"] == script.hex()
                and prepared["feeAtoms"] == "1000000", "Engine changed refund lock or fee")
            signed = {"hex":prepared["raw"]}
        else:
            # The wallet's generic signer does not solve arbitrary CLTV scripts.
            # This fixture signs one legacy SIGHASH_ALL input using a disposable key.
            alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
            wif = rpc("dumpprivkey", address)
            number = 0
            for char in wif:
                number = number*58 + alphabet.index(char)
            decoded_key = number.to_bytes((number.bit_length()+7)//8, "big")
            sha256d = lambda value:hashlib.sha256(hashlib.sha256(value).digest()).digest()
            require(len(decoded_key) == 38 and decoded_key[-4:] == sha256d(decoded_key[:-4])[:4]
                    and decoded_key[-5] == 1, "Invalid compressed fixture key")
            key = ec.derive_private_key(int.from_bytes(decoded_key[1:33], "big"), ec.SECP256K1())
            order = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141

            def sign(transaction):
                unsigned = bytes.fromhex(transaction)
                require(unsigned[4] == 1 and unsigned[41] == 0 and len(script) < 76,
                        "Fixture expects one unsigned legacy input")
                preimage = unsigned[:41] + bytes([len(script)]) + script + unsigned[42:] + bytes.fromhex("01000000")
                signature = key.sign(sha256d(preimage), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
                r, scalar = utils.decode_dss_signature(signature)
                signature = utils.encode_dss_signature(r, min(scalar, order-scalar)) + b"\x01"
                require(len(signature) < 76, "Unexpected signature length")
                unlocking = bytes([len(signature)]) + signature + bytes([len(script)]) + script
                require(len(unlocking) < 253, "Unexpected scriptSig length")
                return (unsigned[:41] + bytes([len(unlocking)]) + unlocking + unsigned[42:]).hex()

            signed = {"hex":sign(raw)}
        try:
            rpc("sendrawtransaction", signed["hex"])
        except RuntimeError as error:
            require("non-final" in str(error), "Unexpected premature refund rejection")
        else:
            raise RuntimeError("Premature refund accepted")
        require(not rpc("getrawmempool"), "Premature refund entered mempool")
        rpc("generate", height-rpc("getblockcount"))
        # A mature chain must still reject a spend whose transaction locktime
        # violates the redeem script. This distinguishes CLTV from nLockTime alone.
        invalid = rpc("createrawtransaction", inputs, {destination:"9.99"}, 0)
        try:
            rpc("sendrawtransaction", prepared["invalidLocktimeRaw"] if args.engine_test_binary else sign(invalid))
        except RuntimeError as error:
            require("locktime" in str(error).lower(), "Unexpected CLTV mismatch rejection: "+str(error))
        else:
            raise RuntimeError("CLTV accepted mismatching transaction locktime")
        txid = rpc("sendrawtransaction", signed["hex"])
        # Confirmed funding remains historical evidence after a pending spend;
        # the engine must reject its output before the refund is mined.
        observe(funding, decoded["blockhash"], decoded["hex"], True, minimum=1,
                unspent_vout=output["n"], expected_unspent=False)
        if args.engine_test_binary:
            require(txid == prepared["txid"], "Engine transaction identity differs from node")
        rpc("generate", 2)
        refunded = rpc("getrawtransaction", txid, True)
        require(refunded["confirmations"] >= 2 and len(refunded["vout"]) == 1, "Refund not confirmed")
        require(refunded["vout"][0]["value"] == Decimal("9.99") and
                destination in refunded["vout"][0]["scriptPubKey"]["addresses"], "Refund recipient or amount differs")
        require(rpc("gettxout", funding, output["n"]) is None, "Lock remains unspent")
        observe(funding, decoded["blockhash"], decoded["hex"], True,
                unspent_vout=output["n"], expected_unspent=False)
        # Restart the owned node and re-observe exact retained bytes, not just a
        # cached txid. Confirmation is current chain evidence, not a receipt.
        confirmed_block = refunded["blockhash"]
        observe(txid, confirmed_block, signed["hex"], True, unspent_vout=0, expected_unspent=True)
        observe(txid, confirmed_block, signed["hex"], False, minimum=3)
        observe(txid, confirmed_block, signed["hex"], False, minimum=0)
        observe(txid, GENESIS, signed["hex"], False)
        rpc("stop")
        process.wait(timeout=30)
        require(process.returncode == 0, "Owned Dogecoin daemon failed to stop")
        process = subprocess.Popen(daemon_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_ready()

        def check_refund():
            observed = rpc("getrawtransaction", txid, True)
            require(observed["hex"] == signed["hex"] and len(observed["vout"]) == 1,
                    "Retained refund bytes changed")
            require(observed["vout"][0]["value"] == Decimal("9.99") and
                    destination in observed["vout"][0]["scriptPubKey"]["addresses"],
                    "Retained refund recipient or amount changed")
            return observed

        restarted = check_refund()
        require(restarted["confirmations"] >= 2 and restarted["blockhash"] == confirmed_block,
                "Refund confirmation did not survive restart")
        observe(txid, confirmed_block, signed["hex"], True, unspent_vout=0, expected_unspent=True)
        rpc("invalidateblock", confirmed_block)
        reorganized = check_refund()
        observe(txid, confirmed_block, signed["hex"], False, unspent_vout=0, expected_unspent=False)
        require(reorganized.get("confirmations", 0) <= 0, "Stale refund confirmation after reorg")
        require(rpc("gettxout", funding, output["n"], False) is not None,
                "Confirmed lock spend did not roll back")
        # Resubmit the same saved bytes if the node did not restore the mempool.
        if txid not in rpc("getrawmempool"):
            require(rpc("sendrawtransaction", signed["hex"]) == txid, "Retry changed refund identity")
        rpc("generate", 2)
        reconfirmed = check_refund()
        require(reconfirmed["confirmations"] >= 2 and reconfirmed["blockhash"] != confirmed_block,
                "Exact refund was not confirmed on the replacement chain")
        observe(txid, reconfirmed["blockhash"], signed["hex"], True, unspent_vout=0, expected_unspent=True)
        observe(txid, confirmed_block, signed["hex"], False, unspent_vout=0, expected_unspent=False)
        require(rpc("gettxout", funding, output["n"], False) is None,
                "Reconfirmed refund did not consume lock")
        result = {"schema":"monzero-doge-primitives-results-v1","genesis":GENESIS,"chain":"regtest",
            "p2shCltvVerified":True,"engineRefundSignerVerified":args.engine_test_binary is not None,"prematureRefundRejected":True,"mismatchedLocktimeRejected":True,
            "engineObservationVerified":args.observer_test_binary is not None,
            "engineUnspentObservationVerified":args.observer_test_binary is not None,
            "restartVerified":True,"reorgConfirmationRevoked":True,"exactRefundReconfirmed":True,
            "refundConfirmed":True,"refundAtoms":"999000000","feeAtoms":"1000000",
            "fullCrossChainSettlement":False,"executable":False,
            "softforks":info.get("softforks"),"bip9Softforks":info.get("bip9_softforks")}
        with os.fdopen(os.open(args.report, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w") as handle:
            json.dump(result, handle)
        print("PASS: isolated Dogecoin P2SH/CLTV refund primitives; no XMZ settlement")
    finally:
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        shutil.rmtree(root)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("daemon", "temp-parent", "report"):
        parser.add_argument("--"+flag, required=True, type=lambda value:pathlib.Path(value).resolve())
    parser.add_argument("--engine-test-binary", type=lambda value:pathlib.Path(value).resolve())
    parser.add_argument("--observer-test-binary", type=lambda value:pathlib.Path(value).resolve())
    run(parser.parse_args())
