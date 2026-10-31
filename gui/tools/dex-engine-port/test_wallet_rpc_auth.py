#!/usr/bin/env python3
"""Probe default wallet-RPC Digest authentication using a disposable empty service.

No existing wallets or daemon are used. Generated credentials never go to argv/stdout.
"""
import argparse
import json
import os
import pathlib
import socket
import stat
import subprocess
import tempfile
import time
import requests
import secrets
import uuid
from requests.auth import HTTPDigestAuth


class AuthDenied(Exception):
    pass


def opener(url, credentials=None):
    client = requests.Session()
    client.trust_env = False
    if credentials is not None:
        client.auth = HTTPDigestAuth(*credentials)
    return client


def request(client, url, method="get_version", params=None):
    body = json.dumps({"jsonrpc": "2.0", "id": "auth-probe", "method": method,
        "params": params or {}}).encode()
    with client, client.post(url, data=body, headers={"Content-Type": "application/json"},
            allow_redirects=False, timeout=15, stream=True) as response:
        if response.status_code == 401 and response.headers.get("WWW-Authenticate", "").lower().startswith("digest "):
            raise AuthDenied()
        if response.status_code != 200:
            raise RuntimeError("Unexpected authentication probe status")
        data = bytearray()
        for chunk in response.iter_content(4096):
            data.extend(chunk)
            if len(data) > 65536:
                raise RuntimeError("Oversized authentication probe response")
    return json.loads(data)


def denied(client, url):
    try:
        request(client, url)
    except AuthDenied:
        return
    raise RuntimeError("Unauthorised request was accepted")


def run(binary, parent, engine=None, qt_owner=None, control_helper=None):
    with tempfile.TemporaryDirectory(prefix="xmz-rpc-auth-", dir=parent) as directory:
        root = pathlib.Path(directory)
        # Reserve a non-listening loopback daemon endpoint: no real daemon is contacted.
        with socket.socket() as daemon_endpoint, socket.socket() as candidate:
            daemon_endpoint.bind(("127.0.0.1", 0))
            candidate.bind(("127.0.0.1", 0))
            port = candidate.getsockname()[1]
            candidate.close()
            url = f"http://127.0.0.1:{port}/json_rpc"
            command = [str(binary), "--wallet-dir", str(root / "wallets"),
                "--daemon-address", f"127.0.0.1:{daemon_endpoint.getsockname()[1]}",
                "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(port),
                "--non-interactive", "--max-concurrency", "2", "--log-file", str(root / "rpc.log")]
            if qt_owner is not None:
                command = [str(qt_owner), "--swap-wallet-process-fixture", str(binary), str(root),
                    str(daemon_endpoint.getsockname()[1]), str(port)]
                if control_helper is not None:
                    command.append(str(control_helper))
            process = subprocess.Popen(command,
                cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                login = root / f"monzero-wallet-rpc.{port}.login"
                service_root = root
                if qt_owner is not None:
                    info = root / "service-info.json"
                    deadline = time.monotonic() + 20
                    while not info.exists():
                        if process.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError("Qt process owner startup failed")
                        time.sleep(0.05)
                    if info.stat().st_size > 4096:
                        raise RuntimeError("Oversized service metadata")
                    metadata = json.loads(info.read_bytes())
                    service_root = pathlib.Path(metadata["directory"])
                    login = pathlib.Path(metadata["login"])
                    owned_session = pathlib.Path(metadata["helperSession"])
                    if service_root.parent != root or login.parent != service_root or owned_session.parent != service_root:
                        raise RuntimeError("Service escaped disposable parent")
                deadline = time.monotonic() + 20
                while True:
                    if process.poll() is not None:
                        raise RuntimeError("Authentication fixture exited before readiness")
                    try:
                        denied(opener(url), url)
                        if login.exists():
                            break
                    except (OSError, requests.RequestException):
                        pass
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Authentication fixture readiness timeout")
                    time.sleep(0.1)
                mode = login.lstat().st_mode
                if not stat.S_ISREG(mode) or stat.S_IMODE(mode) & 0o077 or login.stat().st_size > 1024:
                    raise RuntimeError("Generated credentials are not a bounded private file")
                username, password = login.read_text(encoding="ascii").split(":", 1)
                if not username or not password:
                    raise RuntimeError("Empty generated credentials")
                denied(opener(url, (username, password + "-wrong")), url)
                result = request(opener(url, (username, password)), url)
                if result.get("id") != "auth-probe" or "error" in result or not isinstance(result.get("result", {}).get("version"), int):
                    raise RuntimeError("Authenticated version request failed")
                print("PASS: default Digest auth rejects absent/wrong credentials and accepts private generated credentials")
                def wallet_call(credentials, method, params=None):
                    value = request(opener(url, credentials), url, method, params)
                    if value.get("id") != "auth-probe" or "error" in value or not isinstance(value.get("result"), dict):
                        raise RuntimeError("Disposable wallet operation failed")
                    return value["result"]
                if control_helper is not None:
                    session_file = owned_session if qt_owner is not None else root / "control-session.json"
                    if qt_owner is None:
                        with open(session_file, "x", opener=lambda path, flags: os.open(path, flags, 0o600)) as stream:
                            json.dump({"schema": "monzero-native-fee-session-v1", "network": "mainnet",
                                "genesis": "1" * 64, "daemonPort": daemon_endpoint.getsockname()[1],
                                "walletPort": port, "username": username, "password": password}, stream)
                    wallet_id, private_password = str(uuid.uuid4()), secrets.token_urlsafe(32)
                    def control(operation, expected_address=""):
                        request_id = str(uuid.uuid4())
                        wire = {"schema": "monzero-wallet-control-v1", "requestId": request_id,
                            "operation": operation, "walletId": "" if operation == "close" else wallet_id,
                            "password": "" if operation == "close" else private_password,
                            "expectedAddress": expected_address}
                        result = subprocess.run([str(control_helper), "--wallet-control-stdio-v1",
                            "--session-file", str(session_file)], input=json.dumps(wire).encode(),
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=35, env={"LANG": "C"})
                        if result.returncode != 0 or len(result.stdout) > 2048 or private_password.encode() in result.stdout:
                            raise RuntimeError("Private wallet-control executable failed")
                        reply = json.loads(result.stdout)
                        if set(reply) != {"schema", "requestId", "operation", "address", "readyForTrading"}:
                            raise RuntimeError("Unexpected wallet-control reply fields")
                        if reply["schema"] != "monzero-wallet-control-result-v1" or reply["requestId"] != request_id \
                                or reply["operation"] != operation or reply["readyForTrading"] is not False:
                            raise RuntimeError("Unbound wallet-control reply")
                        if expected_address and reply["address"] != expected_address:
                            raise RuntimeError("Wallet-control identity mismatch")
                        return reply["address"]
                    address = control("create")
                    if wallet_call((username, password), "get_address")["address"] != address:
                        raise RuntimeError("Created wallet-control address mismatch")
                    control("close", address)
                    control("open", address)
                    control("close", address)
                    if qt_owner is None:
                        session_file.unlink()
                    print("PASS: private OS-pipe commands create, close and reopen the same disposable wallet without secret replies")
                    if qt_owner is not None:
                        (root / "service-control-start").touch()
                        completed = root / "service-control-result.json"
                        deadline = time.monotonic() + 45
                        while not completed.exists():
                            if process.poll() is not None or time.monotonic() >= deadline:
                                raise RuntimeError("Qt dispatcher lifecycle did not complete")
                            time.sleep(0.05)
                        if completed.stat().st_size > 1024:
                            raise RuntimeError("Oversized Qt dispatcher result")
                        reply = json.loads(completed.read_bytes())
                        if set(reply) != {"schema", "completed", "address", "readyForTrading"} \
                                or reply["schema"] != "qt-wallet-control-fixture-v1" or reply["completed"] != 4 \
                                or reply["readyForTrading"] is not False \
                                or not isinstance(reply["address"], str) or not 90 <= len(reply["address"]) <= 110:
                            raise RuntimeError("Invalid Qt dispatcher completion result")
                        unopened = request(opener(url, (username, password)), url, "get_address")
                        if unopened.get("id") != "auth-probe" or unopened.get("error", {}).get("code") != -13:
                            raise RuntimeError("Qt dispatcher did not leave its wallet closed")
                        if (service_root / "service.state").read_bytes() != b"monzero-swap-rpc-v1\nactive\n":
                            raise RuntimeError("Qt dispatcher did not complete owner operation accounting")
                        print("PASS: Qt dispatcher drives real helper create/close/open/close and completes owner accounting")
                if qt_owner is not None:
                    wallet_password = secrets.token_urlsafe(32)
                    wallet_call((username, password), "create_wallet", {
                        "filename": "restart-fixture", "password": wallet_password, "language": "English"})
                    original_address = wallet_call((username, password), "get_address")["address"]
                    if not isinstance(original_address, str) or not 90 <= len(original_address) <= 110:
                        raise RuntimeError("Invalid disposable wallet address")
                    balance = wallet_call((username, password), "get_balance")
                    if balance["balance"] != 0 or balance["unlocked_balance"] != 0:
                        raise RuntimeError("Disposable wallet unexpectedly funded")
                    wallet_call((username, password), "store")
                if engine is not None:
                    completion = root / "native-lifecycle-result"
                    environment = dict(os.environ, MONZERO_TEST_AUTH_LOGIN=str(login),
                        MONZERO_TEST_AUTH_RESULT=str(completion),
                        MONZERO_TEST_WALLET_EXPECT_OPEN="1" if qt_owner is not None else "0",
                        MONZERO_TEST_AUTH_DAEMON_PORT=str(daemon_endpoint.getsockname()[1]),
                        MONZERO_TEST_AUTH_WALLET_PORT=str(port))
                    checked = subprocess.run([str(engine), "--exact", "native_rpc_digest_auth", "--ignored"],
                        env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=45)
                    if checked.returncode != 0:
                        raise RuntimeError("Rust Digest authentication probe failed")
                    if not completion.exists():
                        raise RuntimeError("Native lifecycle test did not complete")
                    completed = completion.lstat()
                    if not stat.S_ISREG(completed.st_mode) or completed.st_mode & 0o077 or completed.st_size > 64:
                        raise RuntimeError("Unsafe lifecycle completion marker")
                    expected = b"wallet-lifecycle-v1:open\n" if qt_owner is not None else b"wallet-lifecycle-v1:empty\n"
                    if completion.read_bytes() != expected:
                        raise RuntimeError("Native lifecycle marker does not match fixture mode")
                    print("PASS: Rust authentication and wallet lifecycle checks completed with the expected private marker")
            finally:
                if process.poll() is None:
                    if qt_owner is not None:
                        (root / "service-stop").touch()
                    else:
                        process.terminate()
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            if qt_owner is not None:
                if process.returncode != 0 or login.exists() or owned_session.exists() or not (service_root / "wallets").is_dir():
                    raise RuntimeError("Qt owner shutdown or data retention failed")
                print("PASS: Qt owner starts authenticated separate RPC, stops its child, removes credentials and retains wallet directory")
                marker = service_root / "wallets" / "retention-marker"
                marker.write_bytes(b"disposable-retention-check")
                (root / "service-stop").unlink()
                (root / "service-info.json").unlink()
                process = subprocess.Popen([str(qt_owner), "--swap-wallet-resume-fixture", str(binary), str(root),
                    str(daemon_endpoint.getsockname()[1]), str(port), str(service_root)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                try:
                    deadline = time.monotonic() + 20
                    while True:
                        if process.poll() is not None:
                            raise RuntimeError("Resumed service exited early")
                        try:
                            denied(opener(url), url)
                            if login.exists() and (root / "service-info.json").exists():
                                break
                        except (OSError, requests.RequestException):
                            pass
                        if time.monotonic() >= deadline:
                            raise RuntimeError("Resumed service readiness timeout")
                        time.sleep(0.05)
                    metadata = login.lstat()
                    if not stat.S_ISREG(metadata.st_mode) or metadata.st_mode & 0o077 or metadata.st_size > 1024:
                        raise RuntimeError("Unsafe resumed credentials")
                    new_username, new_password = login.read_text(encoding="ascii").split(":", 1)
                    resumed_info = root / "service-info.json"
                    if resumed_info.stat().st_size > 4096:
                        raise RuntimeError("Oversized resumed service metadata")
                    resumed_session = pathlib.Path(json.loads(resumed_info.read_bytes())["helperSession"])
                    if resumed_session.parent != service_root or resumed_session == owned_session:
                        raise RuntimeError("Resumed helper session was not freshly provisioned")
                    session_stat = resumed_session.lstat()
                    if not stat.S_ISREG(session_stat.st_mode) or stat.S_IMODE(session_stat.st_mode) != 0o600 \
                            or session_stat.st_size > 2048 or session_stat.st_nlink != 1:
                        raise RuntimeError("Unsafe resumed helper session")
                    session_data = json.loads(resumed_session.read_bytes())
                    if session_data["username"] != new_username or session_data["password"] != new_password \
                            or session_data["walletPort"] != port \
                            or session_data["daemonPort"] != daemon_endpoint.getsockname()[1]:
                        raise RuntimeError("Resumed helper session does not match its RPC owner")
                    del session_data
                    denied(opener(url, (username, password)), url)
                    result = request(opener(url, (new_username, new_password)), url)
                    if "error" in result or not isinstance(result.get("result", {}).get("version"), int):
                        raise RuntimeError("Resumed authentication failed")
                    if marker.read_bytes() != b"disposable-retention-check":
                        raise RuntimeError("Service restart changed retained data")
                    rejected = request(opener(url, (new_username, new_password)), url, "open_wallet",
                        {"filename": "restart-fixture", "password": wallet_password + "-wrong"})
                    if rejected.get("id") != "auth-probe" or "error" not in rejected:
                        raise RuntimeError("Wrong wallet password was not rejected")
                    unopened = request(opener(url, (new_username, new_password)), url, "get_address")
                    if unopened.get("id") != "auth-probe" or "error" not in unopened:
                        raise RuntimeError("Failed wallet unlock left a wallet accessible")
                    wallet_call((new_username, new_password), "open_wallet", {
                        "filename": "restart-fixture", "password": wallet_password})
                    restored_address = wallet_call((new_username, new_password), "get_address")["address"]
                    balance = wallet_call((new_username, new_password), "get_balance")
                    if restored_address != original_address or balance["balance"] != 0 or balance["unlocked_balance"] != 0:
                        raise RuntimeError("Wallet identity or balance changed across restart")
                    wallet_call((new_username, new_password), "close_wallet")
                finally:
                    if process.poll() is None:
                        (root / "service-stop").touch()
                        try:
                            process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
                if process.returncode != 0 or login.exists() or resumed_session.exists():
                    raise RuntimeError("Resumed owner shutdown failed")
                print("PASS: clean RPC restart retains data, rotates credentials and rejects the previous login")
                print("PASS: encrypted disposable wallet reopens with the same address and zero balance; wrong password rejected")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wallet-rpc", type=pathlib.Path, required=True)
    parser.add_argument("--temp-parent", type=pathlib.Path, required=True)
    parser.add_argument("--engine-test-binary", type=pathlib.Path)
    parser.add_argument("--qt-process-owner", type=pathlib.Path)
    parser.add_argument("--wallet-control-helper", type=pathlib.Path)
    args = parser.parse_args()
    run(args.wallet_rpc.resolve(strict=True), args.temp_parent.resolve(strict=True),
        args.engine_test_binary.resolve(strict=True) if args.engine_test_binary else None,
        args.qt_process_owner.resolve(strict=True) if args.qt_process_owner else None,
        args.wallet_control_helper.resolve(strict=True) if args.wallet_control_helper else None)
