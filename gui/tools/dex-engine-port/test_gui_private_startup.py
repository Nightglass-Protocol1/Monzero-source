#!/usr/bin/env python3
"""Full GUI startup in a network/home-isolated namespace, with mock or native RPC.

This tests provisioning, not a funded wallet or live settlement. Requires bwrap.
No host home, credentials, display socket or network is exposed to the GUI.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import sys
import time
import uuid


def probe_real_rpc():
    import requests
    from requests.auth import HTTPDigestAuth
    process = subprocess.Popen(["/app/gui", "--disable-check-updates", "--startup-smoke-test",
        "--startup-smoke-delay-ms", "10000", "--dex-private-backend", "/fixture/backend.json"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 9
        login = None
        while time.monotonic() < deadline and process.poll() is None:
            files = list(pathlib.Path("/fixture/backend").glob("*/monzero-wallet-rpc.*.login"))
            if len(files) == 1:
                login = files[0]
                break
            time.sleep(0.05)
        if login is None:
            raise RuntimeError("Owned native RPC did not provision credentials")
        username, password = login.read_text(encoding="ascii").split(":", 1)
        with requests.Session() as client:
            client.trust_env = False
            url = "http://127.0.0.1:28082/json_rpc"
            request = {"jsonrpc":"2.0", "id":"startup-probe", "method":"get_version", "params":{}}
            response = None
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    response = client.post(url, json=request, timeout=1, allow_redirects=False)
                    break
                except requests.ConnectionError:
                    time.sleep(0.05)
            if response is None:
                raise RuntimeError("Native RPC did not become reachable before the startup deadline")
            assert response.status_code == 401
            client.auth = HTTPDigestAuth(username, password)
            response = client.post(url, json=request, timeout=3, allow_redirects=False)
            assert response.status_code == 200 and response.json()["result"]["version"] > 0
            request["method"] = "get_address"
            response = client.post(url, json=request, timeout=3, allow_redirects=False)
            assert response.status_code == 200 and response.json()["error"]["code"] == -13
        del username, password
        assert process.wait(timeout=25) == 0
        pathlib.Path("/fixture/rpc-probed").write_text("authenticated-empty-service\n")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def run(gui, fixture, parent, native_helper=None):
    with tempfile.TemporaryDirectory(prefix="dex-gui-startup-", dir=parent) as temporary:
        root = pathlib.Path(temporary)
        helper = root / "mock-rpc"
        shutil.copyfile(fixture, helper)
        helper.chmod(0o500)
        digest = hashlib.sha256(helper.read_bytes()).hexdigest()
        helper_path = "/fixture/mock-rpc"
        helper_digest = digest
        if native_helper:
            native = root / "native-helper"
            shutil.copyfile(native_helper, native)
            native.chmod(0o500)
            helper_path = "/fixture/native-helper"
            helper_digest = hashlib.sha256(native.read_bytes()).hexdigest()
        data = root / "backend"
        data.mkdir(mode=0o700)
        config = {"schema": "monzero-private-backend-v1", "directory": "/fixture/backend",
            "helper": helper_path, "rpc": "/fixture/mock-rpc",
            "helperDigest": helper_digest, "rpcDigest": digest, "genesis": "1" * 64,
            "daemonPort": "1", "walletPort": "28082" if native_helper else "6"}
        config_path = root / "backend.json"
        config_path.write_text(json.dumps(config, sort_keys=True, separators=(",", ":")))
        config_path.chmod(0o600)
        base = ["bwrap", "--unshare-all", "--die-with-parent", "--new-session",
            "--ro-bind", "/usr", "/usr", "--ro-bind", "/lib", "/lib",
            "--ro-bind", "/lib64", "/lib64", "--proc", "/proc", "--dev", "/dev",
            "--tmpfs", "/tmp", "--dir", "/home/pinhead", "--dir", "/etc",
            "--ro-bind", "/etc/passwd", "/etc/passwd", "--ro-bind", "/etc/fonts", "/etc/fonts",
            "--ro-bind", str(gui), "/app/gui", "--bind", str(root), "/fixture",
            "--ro-bind", str(pathlib.Path(__file__).resolve()), "/app/startup-test.py",
            "--clearenv", "--setenv", "PATH", "/usr/bin", "--setenv", "QT_QPA_PLATFORM", "offscreen",
            "--setenv", "QT_QUICK_BACKEND", "software", "--setenv", "XDG_CONFIG_HOME", "/tmp/config",
            "--setenv", "XDG_DATA_HOME", "/tmp/data", "--setenv", "XDG_CACHE_HOME", "/tmp/cache"]
        def invoke(arguments, enabled=False, probe=False):
            command = list(base)
            if enabled:
                command += ["--setenv", "MONZERO_ENABLE_PRIVATE_DEX", "I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE"]
            command += (["/usr/bin/python3", "/app/startup-test.py", "__probe"] if probe
                else ["/app/gui", "--disable-check-updates"] + arguments)
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
            # Logs contain only isolated fixture data; retain locally for diagnosis.
            (root / "last-run.log").write_bytes(result.stdout)
            return result
        baseline = invoke(["--dex-self-test"])
        if baseline.returncode != 0:
            raise RuntimeError("Isolated GUI self-test failed: " + baseline.stdout.decode(errors="replace")[-2000:])
        assert not list(data.iterdir())
        denied = invoke(["--startup-smoke-test", "--dex-private-backend", "/fixture/backend.json"])
        if denied.returncode == 0 or b"configuration rejected" not in denied.stdout:
            raise RuntimeError("Missing-opt-in check failed (exit %s): %s" %
                (denied.returncode, denied.stdout.decode(errors="replace")[-2500:]))
        assert not list(data.iterdir())
        invalid = invoke(["--startup-smoke-test", "--dex-private-backend", "/fixture/missing.json"], True)
        assert invalid.returncode != 0 and b"configuration rejected" in invalid.stdout
        assert not list(data.iterdir())
        accepted = invoke(["--startup-smoke-test", "--dex-private-backend", "/fixture/backend.json"], True,
            probe=bool(native_helper))
        if accepted.returncode != 0:
            raise RuntimeError("Isolated GUI provisioning failed: " + accepted.stdout.decode(errors="replace")[-3000:])
        services = list(data.glob("monzero-swap-rpc-*"))
        assert len(services) == 1 and (services[0] / "wallets").is_dir()
        assert (services[0] / "service.state").is_file()
        assert not list(services[0].glob("*.login")) and not list(services[0].glob("helper-session-*"))
        assert not list((services[0] / "wallets").glob("*.keys"))
        if native_helper:
            assert (root / "rpc-probed").read_text() == "authenticated-empty-service\n"
            assert (services[0] / "service.state").read_bytes() == b"monzero-swap-rpc-v1\nstopped\n"
            print("PASS: isolated full GUI provisions real authenticated wallet RPC, opens no wallet and cleans credentials on exit")
            from nacl.signing import SigningKey
            btc_key, xmz_key = SigningKey.generate(), SigningKey.generate()
            quote_directory = root / "quotes"
            quote_directory.mkdir(mode=0o700)
            policy = dict(btcGenesis="a" * 64, xmzGenesis="b" * 64,
                btcNetwork="btc-mainnet", xmzNetwork="xmz-fakechain",
                btcConfirmations="3", xmzConfirmations="10", cancelBlocks="72", punishBlocks="72",
                maximumBtcPrincipal="500000", maximumXmzPrincipal="200000000000",
                maximumBtcNativeFee="10000", maximumXmzNativeFee="1000000000")
            terms = dict(schema="monzero-native-btc-xmz-quote-v1", quoteId=str(uuid.uuid4()),
                swapId=str(uuid.uuid4()), btcFunder=btc_key.verify_key.encode().hex(),
                xmzFunder=xmz_key.verify_key.encode().hex(), btcGenesis=policy["btcGenesis"],
                xmzGenesis=policy["xmzGenesis"], btcNetwork=policy["btcNetwork"], xmzNetwork=policy["xmzNetwork"],
                btcSatoshis="500000", xmzAtoms="200000000000", appFeeXmzAtoms="0",
                btcNativeFeeMaxSatoshis="10000", xmzNativeFeeMaxAtoms="1000000000",
                btcPayout="1BoatSLRHtKNngkdXEeobR76b53LETtpyT", btcRefund="1BoatSLRHtKNngkdXEeobR76b53LETtpyT",
                xmzPayout="FSf9UZS3uZwJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmJ7suhUXwdrDJ8QgRfFWTzmJ8QgRfFWTzmJ8QgRfFWTzmCYGLw3H",
                btcConfirmations="3", xmzConfirmations="10", cancelBlocks="72", punishBlocks="72",
                expiresAt=str(int(time.time()) + 120))
            terms["xmzRefund"] = terms["xmzPayout"]

            def canonical(value):
                return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()

            def write_quote():
                payload = canonical(terms)
                def sign(key, role):
                    domain = ("monzero-native-btc-xmz-quote-v1-" + role).encode()
                    message = (b"MONZERO-DEX-OFFER-V1\n" + str(len(domain)).encode() + b":" + domain
                               + str(len(payload)).encode() + b":" + payload)
                    return key.sign(message).signature.hex()
                quote = dict(schema="monzero-private-native-quote-v1", helper=helper_path,
                    helperDigest=helper_digest, directory="/fixture/quotes", policy=policy, terms=terms,
                    btcPeer=terms["btcFunder"], xmzPeer=terms["xmzFunder"],
                    btcSignature=sign(btc_key, "btc-funder"), xmzSignature=sign(xmz_key, "xmz-funder"))
                path = root / "quote.json"
                path.write_bytes(canonical(quote))
                path.chmod(0o600)

            write_quote()
            arguments = ["--startup-smoke-test", "--startup-smoke-delay-ms", "3000",
                         "--dex-private-quote", "/fixture/quote.json"]
            denied_quote = invoke(arguments)
            assert denied_quote.returncode != 0 and b"quote configuration rejected" in denied_quote.stdout
            reviewed = invoke(arguments, True)
            assert reviewed.returncode == 0 and b"Private native quote review verified" in reviewed.stdout
            assert not list(quote_directory.iterdir())
            terms["btcPayout"] = "invalid-address"
            write_quote()  # Valid signatures over an invalid destination must still fail.
            invalid_quote = invoke(arguments, True)
            assert invalid_quote.returncode != 0 and b"quote startup verification failed" in invalid_quote.stdout
            assert not list(quote_directory.iterdir())
            assert len(list(data.glob("monzero-swap-rpc-*"))) == 1
            print("PASS: isolated full GUI verifies signed quote review, rejects missing opt-in/bad addresses and writes no reservations")
        else:
            print("PASS: isolated full GUI rejects missing opt-in/config and provisions a separate mock backend with exit credential cleanup")


if __name__ == "__main__":
    if sys.argv[1:] == ["__probe"]:
        probe_real_rpc()
        sys.exit(0)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gui", type=pathlib.Path, required=True)
    parser.add_argument("--rpc-fixture", type=pathlib.Path, required=True)
    parser.add_argument("--native-helper", type=pathlib.Path,
        help="Enable real-RPC probes; --rpc-fixture must then be the actual wallet-RPC binary")
    parser.add_argument("--temp-parent", type=pathlib.Path, required=True)
    args = parser.parse_args()
    run(args.gui.resolve(strict=True), args.rpc_fixture.resolve(strict=True), args.temp_parent.resolve(strict=True),
        args.native_helper.resolve(strict=True) if args.native_helper else None)
