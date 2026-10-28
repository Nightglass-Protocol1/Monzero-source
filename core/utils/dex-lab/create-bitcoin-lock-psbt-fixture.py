#!/usr/bin/env python3
"""Persist one unsigned COMIT P2WSH lock PSBT for read-only C++ validation."""
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path("/srv/monzero-dex")
MANIFEST = ROOT / "recovery/bitcoin-lock-psbt-fixture.json"
BTC = [str(ROOT / "bin/bitcoin-cli"),
       f"-conf={ROOT / 'config/bitcoin-regtest.conf'}",
       f"-datadir={ROOT / 'data/bitcoin-regtest'}"]
KEY_A = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
KEY_B = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5"
AMOUNT_ATOMIC = "10000"
MAXIMUM_FEE_ATOMIC = "10000"
MINIMUM_CONFIRMATIONS = 6


def bitcoin(*args, wallet=None):
    command = BTC + ([f"-rpcwallet={wallet}"] if wallet else []) + list(args)
    return subprocess.check_output(command, text=True).strip()


def bitcoin_json(*args, wallet=None):
    return json.loads(bitcoin(*args, wallet=wallet))


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


def ensure_wallet(name):
    if name in bitcoin_json("listwallets"):
        return
    existing = {entry["name"] for entry in bitcoin_json("listwalletdir")["wallets"]}
    if name in existing:
        bitcoin("loadwallet", name)
    else:
        bitcoin("-named", "createwallet", f"wallet_name={name}", "load_on_startup=true")


def encoded(value):
    raw = str(value).encode("ascii")
    return str(len(raw)).encode("ascii") + b":" + raw


def template_commitment(transaction):
    if transaction.get("version") != 2 or transaction.get("locktime") != 0:
        raise RuntimeError("Unexpected Bitcoin lock transaction version or locktime")
    inputs, outputs = transaction.get("vin"), transaction.get("vout")
    if not inputs or not outputs or len(outputs) > 2:
        raise RuntimeError("Unexpected Bitcoin lock transaction shape")
    payload = b"MONZERO-DEX-BTC-UNSIGNED-TEMPLATE-V1\n"
    payload += encoded(2) + encoded(0) + encoded(len(inputs))
    for item in inputs:
        script = item.get("scriptSig", {})
        if script.get("hex") != "" or item.get("txinwitness"):
            raise RuntimeError("Bitcoin lock PSBT unexpectedly contains signing material")
        payload += encoded(item["txid"])
        payload += encoded(item["vout"])
        payload += encoded(item["sequence"])
    payload += encoded(len(outputs))
    for expected_index, item in enumerate(outputs):
        if item["n"] != expected_index:
            raise RuntimeError("Bitcoin lock outputs are not canonically ordered")
        amount = round(item["value"] * 100_000_000)
        if abs(item["value"] * 100_000_000 - amount) > 0.000001:
            raise RuntimeError("Bitcoin lock output is not an exact satoshi amount")
        payload += encoded(item["n"])
        payload += encoded(amount)
        payload += encoded(item["scriptPubKey"]["hex"])
    return hashlib.sha256(payload).hexdigest()


def main():
    if MANIFEST.exists():
        value = json.loads(MANIFEST.read_text())
        if value.get("version") != 1 or value.get("network") != "regtest":
            raise RuntimeError("Unexpected existing Bitcoin lock PSBT fixture")
        print(json.dumps({"status": "existing", "path": str(MANIFEST),
                          "templateCommitment": value["templateCommitment"]}, sort_keys=True))
        return

    ensure_wallet("dex-test-miner")
    witness_script = "21" + KEY_A + "ad21" + KEY_B + "ac"
    script_pub_key = "0020" + hashlib.sha256(bytes.fromhex(witness_script)).hexdigest()
    descriptor = f"wsh(raw({witness_script}))"
    descriptor_checked = bitcoin_json("getdescriptorinfo", descriptor)["descriptor"]
    addresses = bitcoin_json("deriveaddresses", descriptor_checked)
    if len(addresses) != 1:
        raise RuntimeError("Bitcoin Core did not derive one P2WSH address")
    address = addresses[0]
    outputs = json.dumps([{address: 0.00010000}], separators=(",", ":"))
    options = json.dumps({"add_inputs": True, "includeWatching": False,
                          "lockUnspents": True, "replaceable": False,
                          "conf_target": 6, "estimate_mode": "conservative"},
                         separators=(",", ":"))
    funded = bitcoin_json("walletcreatefundedpsbt", "[]", outputs, "0", options, "true",
                          wallet="dex-test-miner")
    decoded = bitcoin_json("decodepsbt", funded["psbt"])
    transaction = decoded["tx"]
    matches = [item for item in transaction["vout"]
               if item["scriptPubKey"].get("hex") == script_pub_key]
    if len(matches) != 1 or round(matches[0]["value"] * 100_000_000) != int(AMOUNT_ATOMIC):
        raise RuntimeError("Funded PSBT does not contain the exact shared output")
    manifest = {
        "version": 1, "status": "unsigned", "network": "regtest",
        "psbt": funded["psbt"], "address": address,
        "scriptPubKeyHex": script_pub_key, "witnessScriptHex": witness_script,
        "firstContractKeyHex": KEY_A, "secondContractKeyHex": KEY_B,
        "amountAtomic": AMOUNT_ATOMIC, "maximumFeeAtomic": MAXIMUM_FEE_ATOMIC,
        "minimumInputConfirmations": MINIMUM_CONFIRMATIONS,
        "templateCommitment": template_commitment(transaction),
    }
    atomic_write(manifest)
    print(json.dumps({"status": "created", "path": str(MANIFEST),
                      "templateCommitment": manifest["templateCommitment"]}, sort_keys=True))


if __name__ == "__main__":
    main()
