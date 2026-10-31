#!/usr/bin/env python3
"""Rebuild and run isolated native settlement acceptance, never live activation.

Uses the existing disposable regtest/fakechain fixture. Requires local Docker,
the project-local Rust toolchain/cache, built core RPC binaries and Qt drivers.
Does not publish packages, open real wallets or enable any production gate.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def fixture_environment(inherited, core):
    """Pin local build/fixture transport; do not inherit wrappers or prior modes."""
    prefixes = ("MONZERO_TEST_", "CARGO_", "RUST", "DOCKER_", "DYLD_")
    excluded = {"LD_PRELOAD", "LD_AUDIT", "BASH_ENV", "ENV", "PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP"}
    env = {key: value for key, value in inherited.items()
           if not key.startswith(prefixes) and key not in excluded}
    toolchain = core / "build/dex-rust-toolchain-1.95.0/sysroot"
    env.update(LD_LIBRARY_PATH=str(toolchain / "usr/lib/x86_64-linux-gnu"),
               RUSTC=str(toolchain / "usr/bin/rustc"),
               RUSTDOC=str(toolchain / "usr/bin/rustdoc"),
               CARGO_HOME=str(core / "build/dex-cargo-home"),
               CARGO_INCREMENTAL="0", CARGO_PROFILE_DEV_DEBUG="0",
               DOCKER_HOST="unix:///var/run/docker.sock",
               MONZERO_TEST_QT_QUOTE_DRIVER=str(core / "monzero-gui/build/release/src/dex_native_quote_tests"),
               MONZERO_TEST_QT_RECOVERY_DRIVER=str(core / "build/dex-recovery-receiver-test"))
    return env


def built_executable(output, engine, name, kind, source_path, is_test):
    candidates = set()
    for line in output.splitlines():
        event = json.loads(line)
        if (event.get("reason") == "compiler-artifact"
                and event.get("target", {}).get("name") == name
                and kind in event["target"].get("kind", [])
                and event.get("profile", {}).get("test") is is_test
                and event.get("executable")):
            source = Path(event["target"]["src_path"]).resolve(strict=True)
            if source != engine / source_path:
                raise RuntimeError("unexpected build artifact source")
            candidates.add(Path(event["executable"]).resolve(strict=True))
    if len(candidates) != 1:
        raise RuntimeError(f"Cargo did not identify exactly one executable for {name}")
    return candidates.pop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    core = args.core.resolve(strict=True)
    engine = core / "build/eigenwallet-engine-port"
    toolchain = core / "build/dex-rust-toolchain-1.95.0/sysroot"
    cargo = toolchain / "usr/bin/cargo"
    qt_quote = core / "monzero-gui/build/release/src/dex_native_quote_tests"
    qt_recovery = core / "build/dex-recovery-receiver-test"
    daemon = core / "build/release/bin/monzerod"
    wallet = core / "build/release/bin/monzero-wallet-rpc"
    for program in (cargo, toolchain / "usr/bin/rustc", toolchain / "usr/bin/rustdoc", qt_quote, qt_recovery, daemon, wallet):
        if not program.is_file() or not os.access(program, os.X_OK):
            raise RuntimeError(f"required local executable unavailable: {program}")
    if not Path("/var/run/docker.sock").is_socket():
        raise RuntimeError("local Docker socket unavailable; remote Docker is not used by this runner")
    env = fixture_environment(os.environ, core)
    subprocess.run(["bash", str(core / "monzero-gui/tools/dex-engine-port/verify-upstream-patches.sh")],
                   cwd=core, env=env, check=True)
    base = [str(cargo), "test", "--offline", "--locked", "-p", "swap-machine", "-j2"]
    subprocess.run(base + ["--lib", "--", "--include-ignored"], cwd=engine, env=env, check=True)
    subprocess.run(base + ["--doc"], cwd=engine, env=env, check=True)
    built = subprocess.run(base + ["--test", "bitcoin_settlement", "--no-run", "--message-format=json"],
                           cwd=engine, env=env, check=True, stdout=subprocess.PIPE, text=True)
    engine = engine.resolve(strict=True)
    executable = built_executable(built.stdout, engine, "bitcoin_settlement", "test",
                                  "swap-machine/tests/bitcoin_settlement.rs", True)
    helper = built_executable(built.stdout, engine, "native-fee-preview", "bin",
                             "swap-machine/src/bin/native-fee-preview.rs", False)
    if not helper.is_file() or not os.access(helper, os.X_OK):
        raise RuntimeError("native helper was not built alongside the settlement test")
    for name in ("engine_redeem_is_accepted_on_regtest", "engine_cancel_and_full_refund_are_accepted_on_regtest"):
        print(f"Running isolated funded acceptance: {name}", flush=True)
        subprocess.run([sys.executable, "-u", str(core / "monzero-gui/tools/dex-engine-port/test_wallet_recovery.py"),
                        "--daemon", str(daemon), "--wallet-rpc", str(wallet), "--temp-parent", str(core / "build"),
                        "--engine-test-binary", str(executable), "--fee-preview-helper", str(helper),
                        "--native-spendable", "--engine-test-name", name], cwd=core, env=env, check=True)
    print("PASS: isolated native settlement acceptance; partial XMZ proceeds only, NOT live readiness", flush=True)


if __name__ == "__main__":
    main()
