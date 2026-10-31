#!/usr/bin/python3
"""Test-only exec launcher; never package this as a wallet application binary.

The fixture creates private fakechain-wallet-launch.json in the parent of the
RPC owner's data directory. Only a verified loopback fakechain receives the
compatibility override. exec preserves the child PID owned by Qt.
"""
import argparse
import json
import os
import pathlib
import stat
import urllib.request

GENESIS = "84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4"


def private_manifest(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    with os.fdopen(descriptor, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) != 0o600 \
                or info.st_uid != os.geteuid() or info.st_nlink != 1 or info.st_size > 2048:
            raise ValueError("Unsafe fixture manifest")
        raw = stream.read(2049)
    if len(raw) > 2048:
        raise ValueError("Oversized fixture manifest")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate fixture field")
            result[key] = value
        return result
    value = json.loads(raw, object_pairs_hook=unique)
    if not isinstance(value, dict) or set(value) != {"schema", "walletRpc", "daemonPort"} \
            or value["schema"] != "monzero-fakechain-launch-v1" \
            or type(value["daemonPort"]) is not int or not 0 < value["daemonPort"] < 65536:
        raise ValueError("Invalid fixture manifest")
    return value


def checked_command(argv, probe=None):
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    for flag in ("wallet-dir", "daemon-address", "rpc-bind-ip", "rpc-bind-port", "max-concurrency", "log-file"):
        parser.add_argument("--" + flag, required=True)
    parser.add_argument("--non-interactive", action="store_true", required=True)
    args = parser.parse_args(argv)
    # Reject repeated options as well as parser-unknown flags.
    flags = [item for item in argv if item.startswith("--")]
    if len(argv) != 13 or len(flags) != 7 or len(set(flags)) != 7:
        raise ValueError("Invalid fixture arguments")
    wallets = pathlib.Path(args.wallet_dir)
    if not wallets.is_absolute() or wallets.name != "wallets" or wallets.resolve(strict=True) != wallets:
        raise ValueError("Invalid fixture wallet directory")
    service = wallets.parent
    for directory in (wallets, service, service.parent):
        info = directory.stat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
            raise ValueError("Unsafe fixture directory")
    manifest = private_manifest(service.parent / "fakechain-wallet-launch.json")
    daemon_port = manifest["daemonPort"]
    if args.daemon_address != f"127.0.0.1:{daemon_port}" or args.rpc_bind_ip != "127.0.0.1" \
            or not args.rpc_bind_port.isascii() or not args.rpc_bind_port.isdecimal() \
            or not 0 < int(args.rpc_bind_port) < 65536 or int(args.rpc_bind_port) == daemon_port \
            or args.max_concurrency != "2" or pathlib.Path(args.log_file) != service / "rpc.log":
        raise ValueError("Unexpected fixture endpoints")
    binary = pathlib.Path(manifest["walletRpc"])
    if not binary.is_absolute() or binary.resolve(strict=True) != binary or not binary.is_file() \
            or not os.access(binary, os.X_OK) or binary == pathlib.Path(__file__).resolve():
        raise ValueError("Invalid fixture wallet executable")
    if probe is None:
        def probe(method, params):
            request = urllib.request.Request(f"http://127.0.0.1:{daemon_port}/json_rpc",
                data=json.dumps({"jsonrpc": "2.0", "id": "fixture-launch", "method": method,
                                 "params": params}).encode(), headers={"Content-Type": "application/json"})
            # Do not let a redirect turn a loopback probe into an external request.
            class NoRedirect(urllib.request.HTTPRedirectHandler):
                def redirect_request(self, *unused):
                    return None
            local = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
            with local.open(request, timeout=5) as response:
                raw = response.read(65537)
            if len(raw) > 65536:
                raise ValueError("Oversized fixture RPC response")
            result = json.loads(raw)
            if result.get("id") != "fixture-launch" or "error" in result or not isinstance(result.get("result"), dict):
                raise ValueError("Invalid fixture RPC response")
            return result["result"]
    info = probe("get_info", {})
    if info.get("nettype") != "fakechain" or info.get("offline") is not True \
            or info.get("incoming_connections_count") != 0 or info.get("outgoing_connections_count") != 0:
        raise ValueError("Daemon is not an isolated offline fakechain")
    if probe("get_block", {"height": 0}).get("block_header", {}).get("hash") != GENESIS:
        raise ValueError("Wrong fakechain genesis")
    return [str(binary), *argv, "--allow-mismatched-daemon-version", "--trusted-daemon"]


if __name__ == "__main__":
    import sys
    try:
        command = checked_command(sys.argv[1:])
        os.execv(command[0], command)
    except Exception:
        # Never expose private fixture contents through exception formatting.
        sys.stderr.write("Fakechain wallet launcher rejected configuration\n")
        sys.exit(2)
