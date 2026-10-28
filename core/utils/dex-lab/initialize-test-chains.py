#!/usr/bin/env python3
"""Create disposable lab wallets and mature initial regtest balances."""
import json
import pathlib
import secrets
import subprocess
import time
import urllib.request

ROOT = pathlib.Path("/srv/monzero-dex")
BTC = [str(ROOT / "bin/bitcoin-cli"),
       f"-conf={ROOT / 'config/bitcoin-regtest.conf'}",
       f"-datadir={ROOT / 'data/bitcoin-regtest'}"]


def bitcoin(*args):
    return subprocess.check_output(BTC + list(args), text=True).strip()


def rpc(port, method, params=None):
    body = json.dumps({"jsonrpc": "2.0", "id": "dex-lab", "method": method,
                       "params": params or {}}).encode()
    request = urllib.request.Request(f"http://127.0.0.1:{port}/json_rpc", body,
                                     {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=10) as response:
        result = json.load(response)
    if result.get("error"):
        raise RuntimeError(f"{method}: {result['error']}")
    return result["result"]


def daemon_endpoint(path, params=None):
    body = json.dumps(params or {}).encode()
    request = urllib.request.Request(f"http://127.0.0.1:6175/{path}", body,
                                     {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=10) as response:
        result = json.load(response)
    if result.get("status") != "OK":
        raise RuntimeError(f"{path}: daemon endpoint failed")
    return result


def wait_rpc(port, method):
    for _ in range(30):
        try:
            return rpc(port, method)
        except Exception:
            time.sleep(1)
    raise RuntimeError(f"RPC {port} did not become ready")


def initialize_bitcoin():
    info = json.loads(bitcoin("getblockchaininfo"))
    try:
        bitcoin("-rpcwallet=dex-test-miner", "getwalletinfo")
    except subprocess.CalledProcessError:
        bitcoin("-named", "createwallet", "wallet_name=dex-test-miner",
                "load_on_startup=true")
    if info["blocks"] < 101:
        address = bitcoin("-rpcwallet=dex-test-miner", "getnewaddress", "", "bech32")
        bitcoin("-rpcwallet=dex-test-miner", "generatetoaddress",
                str(101 - info["blocks"]), address)


def initialize_monzero():
    wallet_dir = ROOT / "recovery/wallets"
    wallet_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    password_file = ROOT / "recovery/wallet-rpc.pass"
    if not password_file.exists():
        password_file.write_text(secrets.token_hex(32) + "\n")
        password_file.chmod(0o600)
    password = password_file.read_text().strip()
    wait_rpc(6176, "get_version")
    try:
        rpc(6176, "open_wallet", {"filename": "dex-test-miner", "password": password})
    except RuntimeError:
        rpc(6176, "create_wallet", {"filename": "dex-test-miner",
                                    "password": password, "language": "English"})
    address = rpc(6176, "get_address")["address"]
    info = rpc(6175, "get_info")
    if info["height"] < 121:
        rpc(6175, "generateblocks", {"wallet_address": address,
                                     "amount_of_blocks": 121 - info["height"]})
        daemon_endpoint("save_bc")
    rpc(6176, "refresh")
    if rpc(6176, "get_balance")["unlocked_balance"] == 0:
        rpc(6175, "generateblocks", {"wallet_address": address, "amount_of_blocks": 61})
        daemon_endpoint("save_bc")
        rpc(6176, "refresh")
        if rpc(6176, "get_balance")["unlocked_balance"] == 0:
            raise RuntimeError("Disposable Monzero miner balance did not unlock")
    rpc(6176, "store")


if __name__ == "__main__":
    initialize_bitcoin()
    initialize_monzero()
    print("Disposable regtest chains initialized")
