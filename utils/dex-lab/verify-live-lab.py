#!/usr/bin/env python3
"""Fail-closed health check for the two isolated lab nodes."""
import json
import math
import os
import pathlib
import signal
import subprocess
import sys
import urllib.request
import urllib.error

from health import validate_node

ROOT = pathlib.Path("/srv/monzero-dex")
EXPECTED = {
    "BTC": "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206",
    "XMZ": "84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4",
}

MAX_RPC_BYTES = 1024 * 1024
XMZ_RPC_URL = "http://127.0.0.1:6175/json_rpc"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        raise urllib.error.HTTPError(request.full_url, code, "RPC redirects are forbidden", headers, fp)


# Loopback lab RPC must not be routed through a shell-configured HTTP proxy.
RPC_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate RPC object key")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError("Non-finite RPC number")


def finite_float(value):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError("RPC number exceeds finite range")
    return number


def decode_rpc(encoded):
    # Python's default decoder accepts NaN/Infinity and overflowing exponents.
    # Reject ambiguity in either chain's response, including unused fields.
    return json.loads(encoded, object_pairs_hook=unique_object,
                      parse_constant=reject_constant, parse_float=finite_float)


def xmz(method, params=None):
    body = json.dumps({"jsonrpc": "2.0", "id": "health", "method": method,
                       "params": params or {}}).encode()
    request = urllib.request.Request(XMZ_RPC_URL, body,
                                     {"Content-Type": "application/json"})
    with RPC_OPENER.open(request, timeout=5) as response:
        if response.status != 200:
            raise RuntimeError("Unexpected RPC HTTP status")
        encoded = response.read(MAX_RPC_BYTES + 1)
    if len(encoded) > MAX_RPC_BYTES:
        raise RuntimeError("RPC response exceeds size limit")
    reply = decode_rpc(encoded)
    if not isinstance(reply, dict) or reply.get("jsonrpc") != "2.0" or reply.get("id") != "health":
        raise RuntimeError("RPC response identity mismatch")
    if "error" in reply or "result" not in reply:
        raise RuntimeError("RPC returned an error or omitted its result")
    return reply["result"]


def bitcoin(*args):
    return subprocess.check_output([
        str(ROOT / "bin/bitcoin-cli"),
        f"-conf={ROOT / 'config/bitcoin-regtest.conf'}",
        f"-datadir={ROOT / 'data/bitcoin-regtest'}", *args], text=True, timeout=10).strip()


def main():
    btc_info = decode_rpc(bitcoin("getblockchaininfo"))
    btc_errors = validate_node("BTC", btc_info, bitcoin("getblockhash", "0"), EXPECTED["BTC"])
    xmz_info = xmz("get_info")
    xmz_errors = validate_node("XMZ", xmz_info, xmz("on_get_block_hash", [0]), EXPECTED["XMZ"])
    errors = [f"BTC: {error}" for error in btc_errors] + [f"XMZ: {error}" for error in xmz_errors]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"BTC regtest ready at height {btc_info['blocks']}; XMZ fakechain ready at height {xmz_info['height']}")
    return 0


def run_bounded(command, timeout=20):
    """Run the Linux lab probe in its own process group with a total deadline."""
    if os.name != "posix":
        raise RuntimeError("The isolated lab probe requires POSIX process-group cleanup")
    with subprocess.Popen(command, start_new_session=True) as process:
        try:
            return process.wait(timeout=timeout)
        except BaseException as error:
            # The group belongs to this newly launched probe, including any
            # bitcoin-cli child. Do not leave that child behind on timeout.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            if isinstance(error, subprocess.TimeoutExpired):
                print("Isolated lab health check exceeded its total deadline", file=sys.stderr)
                return 124
            raise


if __name__ == "__main__":
    if sys.argv[1:] == ["--probe-worker"]:
        raise SystemExit(main())
    if sys.argv[1:]:
        raise SystemExit("This lab checker accepts no configuration arguments")
    raise SystemExit(run_bounded([sys.executable, str(pathlib.Path(__file__).resolve()),
                                 "--probe-worker"]))
