#!/usr/bin/env python3
"""Create one recoverable, worthless BTC regtest payment fixture.

The signed transaction and independently chosen expectation are atomically persisted
before broadcast. Rerunning resumes the same fixture rather than creating a payment.
"""
import json
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path("/srv/monzero-dex")
MANIFEST = ROOT / "recovery/bitcoin-settlement-fixture.json"
BTC = [str(ROOT / "bin/bitcoin-cli"),
       f"-conf={ROOT / 'config/bitcoin-regtest.conf'}",
       f"-datadir={ROOT / 'data/bitcoin-regtest'}"]
AMOUNT_BTC = "0.00001000"
AMOUNT_ATOMIC = "1000"
REQUIRED_CONFIRMATIONS = 3


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
    loaded = bitcoin_json("listwallets")
    if name in loaded:
        return
    existing = {entry["name"] for entry in bitcoin_json("listwalletdir")["wallets"]}
    if name in existing:
        bitcoin("loadwallet", name)
    else:
        bitcoin("-named", "createwallet", f"wallet_name={name}", "load_on_startup=true")


def prepare():
    ensure_wallet("dex-test-miner")
    ensure_wallet("dex-test-receiver")
    destination = bitcoin("getnewaddress", "settlement-fixture", "bech32",
                          wallet="dex-test-receiver")
    unsigned = bitcoin("createrawtransaction", "[]", json.dumps([{destination: float(AMOUNT_BTC)}]))
    funded = bitcoin_json("fundrawtransaction", unsigned, wallet="dex-test-miner")
    signed = bitcoin_json("signrawtransactionwithwallet", funded["hex"], wallet="dex-test-miner")
    if signed.get("complete") is not True:
        raise RuntimeError("Bitcoin fixture transaction signing was incomplete")
    decoded = bitcoin_json("decoderawtransaction", signed["hex"])
    manifest = {"version": 1, "status": "prepared", "network": "regtest",
                "transactionId": decoded["txid"], "destination": destination,
                "amountAtomic": AMOUNT_ATOMIC,
                "requiredConfirmations": REQUIRED_CONFIRMATIONS,
                "signedTransaction": signed["hex"]}
    atomic_write(manifest)
    return manifest


def resume(manifest):
    if (manifest.get("version"), manifest.get("network"), manifest.get("amountAtomic"),
            manifest.get("requiredConfirmations")) != (1, "regtest", AMOUNT_ATOMIC,
                                                        REQUIRED_CONFIRMATIONS):
        raise RuntimeError("Unexpected existing Bitcoin fixture manifest")
    txid = manifest["transactionId"]
    if manifest["status"] == "prepared":
        try:
            relayed = bitcoin("sendrawtransaction", manifest["signedTransaction"])
            if relayed != txid:
                raise RuntimeError("Broadcast returned a different Bitcoin txid")
        except subprocess.CalledProcessError:
            # An interrupted prior run may already have relayed it. This query must
            # succeed before advancing the durable state.
            bitcoin("getrawtransaction", txid)
        manifest["status"] = "broadcast"
        atomic_write(manifest)
    if manifest["status"] == "broadcast":
        miner_address = bitcoin("getnewaddress", "", "bech32", wallet="dex-test-miner")
        generated = bitcoin_json("generatetoaddress", str(REQUIRED_CONFIRMATIONS), miner_address,
                                 wallet="dex-test-miner")
        transaction = bitcoin_json("getrawtransaction", txid, "1")
        block_hash = transaction["blockhash"]
        if block_hash not in generated:
            raise RuntimeError("Bitcoin fixture was not included in the generated blocks")
        header = bitcoin_json("getblockheader", block_hash)
        manifest.update({"status": "confirmed", "blockHash": block_hash,
                         "blockHeight": str(header["height"])})
        manifest.pop("signedTransaction", None)
        atomic_write(manifest)
    if manifest["status"] != "confirmed":
        raise RuntimeError("Unknown Bitcoin fixture state")
    transaction = bitcoin_json("getrawtransaction", txid, "1")
    if transaction.get("blockhash") != manifest["blockHash"]:
        raise RuntimeError("Bitcoin fixture block changed")
    print(json.dumps({key: manifest[key] for key in ("status", "transactionId", "destination",
                                                     "amountAtomic", "blockHash", "blockHeight")},
                     sort_keys=True))


def main():
    manifest = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else prepare()
    resume(manifest)


if __name__ == "__main__":
    main()
