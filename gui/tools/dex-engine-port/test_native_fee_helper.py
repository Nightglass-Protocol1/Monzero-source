#!/usr/bin/env python3
"""Negative Linux process tests; no wallet, daemon or real credentials needed."""
import argparse
import fcntl
import json
import os
import pathlib
import subprocess
import tempfile


def run(helper, parent):
    with tempfile.TemporaryDirectory(prefix="fee-helper-", dir=parent) as temporary:
        root = pathlib.Path(temporary)
        session = root / "session.json"
        value = {"schema": "monzero-native-fee-session-v1", "network": "mainnet",
                 "genesis": "1" * 64, "daemonPort": 1, "walletPort": 2,
                 "username": "fixture", "password": "fixture-only"}
        session.touch(mode=0o600)
        session.write_text(json.dumps(value))

        for private_input in [b'{"private-password-marker":"private-password-marker"}', b" " * 4097]:
            result = subprocess.run([str(helper), "--wallet-control-stdio-v1", "--session-file", str(session)],
                input=private_input, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5, env={"LANG": "C"})
            if result.returncode != 1 or result.stdout or result.stderr != b"Native wallet control failed\n":
                raise RuntimeError("Private control input was not rejected opaquely")

        def reject(path=session, body=b"{}", extra=()):
            result = subprocess.run([str(helper), "--fee-preview-stdio-v1", "--session-file", str(path), *extra],
                input=body, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5,
                env={"LANG": "C"})
            if result.returncode != 1 or result.stdout or result.stderr != b"Native fee preview failed\n":
                raise RuntimeError("Helper did not reject safely")

        def reject_session(path=session, extra=()):
            # No EOF or request yet: accepting an unsafe session would wait for
            # input instead of exiting within this short deadline.
            with subprocess.Popen([str(helper), "--fee-preview-stdio-v1", "--session-file", str(path), *extra],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    env={"LANG": "C"}) as process:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise RuntimeError("Unsafe session reached input processing") from None
                output, error = process.communicate()
                if process.returncode != 1 or output or error != b"Native fee preview failed\n":
                    raise RuntimeError("Session rejection was not safe")

        reject()  # Invalid request: must stop before contacting either port.
        reject(body=b" " * 2049)
        reject_session(extra=("unexpected",))
        reject_session(path=root / "missing")
        session.chmod(0o644)
        reject_session()
        session.chmod(0o600)
        link = root / "symlink"
        link.symlink_to(session)
        reject_session(path=link)
        hardlink = root / "hardlink"
        os.link(session, hardlink)
        reject_session()
        hardlink.unlink()
        fifo = root / "fifo"
        os.mkfifo(fifo, 0o600)
        reject_session(path=fifo)
        with session.open("rb") as held:
            fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
            reject_session()
        session.write_text(json.dumps(value)[:-1] + ',"password":"duplicate"}')
        reject_session()
        session.write_bytes(b" " * 2049)
        reject_session()
        session.write_text(json.dumps(value))
        # Keep stdin open: the native timeout must also let runtime shutdown finish.
        with subprocess.Popen([str(helper), "--fee-preview-stdio-v1", "--session-file", str(session)],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                env={"LANG": "C"}) as process:
            try:
                process.wait(timeout=35)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise RuntimeError("Helper hung with unfinished input") from None
            output, error = process.communicate()
            if process.returncode != 1 or output or error != b"Native fee preview failed\n":
                raise RuntimeError("Helper timeout did not fail safely")
    print("PASS: helper rejects unsafe sessions/input and competing locks; unfinished input times out without output")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=pathlib.Path, required=True)
    parser.add_argument("--temp-parent", type=pathlib.Path, required=True)
    args = parser.parse_args()
    run(args.helper.resolve(strict=True), args.temp_parent.resolve(strict=True))
