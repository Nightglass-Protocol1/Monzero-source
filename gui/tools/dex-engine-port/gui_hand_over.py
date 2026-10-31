"""Hand a prepared test-network swap to two real app windows (Alice and Bob).

The live swap driver has already created the wallets, funded Bob's light wallet,
signed the quote and written each side's app folder with the trade kit. Here each
trader gets the real monzero-wallet-gui with its own settings and data folders,
plus an empty XMZ testnet wallet so the app opens straight to its main screen.
A person then runs the whole trade by hand from the DEX page; the driver only
keeps the test services running until both windows are closed.
"""
import json
import os
import pathlib
import socket
import subprocess
import time
import urllib.request

from test_litecoin_settlement import write_private, require


def _free_port():
    probe = socket.socket(); probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]; probe.close()
    return port


def _shell_wallets(wallet_rpc, directory, names):
    """Empty, passwordless XMZ testnet wallets that hold nothing; they only let the
    app reach its main screen. Created offline by a short-lived wallet RPC."""
    directory.mkdir(mode=0o700)
    port = _free_port()
    process = subprocess.Popen([str(wallet_rpc), "--testnet", "--offline", "--wallet-dir", str(directory),
        "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(port), "--disable-rpc-login", "--non-interactive"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    def call(method, params):
        body = json.dumps({"jsonrpc": "2.0", "id": "0", "method": method, "params": params}).encode()
        request = urllib.request.Request(f"http://127.0.0.1:{port}/json_rpc", body, {"Content-Type": "application/json"})
        reply = json.loads(urllib.request.urlopen(request, timeout=60).read())
        require("error" not in reply, f"Shell wallet {method} failed: {reply.get('error')}")
        return reply["result"]
    try:
        deadline = time.monotonic() + 60
        while True:
            try:
                call("get_version", {}); break
            except OSError:
                require(time.monotonic() < deadline and process.poll() is None, "Shell wallet service did not start")
                time.sleep(0.5)
        for name in names:
            call("create_wallet", {"filename": name, "password": "", "language": "English"})
            call("close_wallet", {})
    finally:
        process.terminate()
        process.wait(timeout=30)
    return {name: directory / name for name in names}


def _settings(config_home, wallet_path, daemon_port):
    """The app's own settings file: XMZ testnet, the local test daemon as its node,
    and the shell wallet remembered so startup asks only for its (empty) password."""
    folder = config_home / "monzero-project"
    folder.mkdir(mode=0o700, parents=True)
    nodes = json.dumps({"selected": 0, "nodes": [{"address": f"127.0.0.1:{daemon_port}",
        "username": "", "password": "", "trusted": True}]})
    lines = ["[General]", "askDesktopShortcut=false", "askStopLocalNode=false", "checkForUpdates=false",
        "lockOnUserInActivity=false", "nettype=1", "useRemoteNode=true", "walletMode=2",
        "remoteNodesSerialized=" + json.dumps(nodes), "wallet_path=" + str(wallet_path)]
    (folder / "monzero-core.conf").write_text("\n".join(lines) + "\n")


def hand_over(args, root, fixtures, light_setup, daemon_port, note, timeout):
    gui = args.hand_over_gui
    shells = _shell_wallets(args.wallet_rpc, root / "gui-shell-wallets", ["alice-window", "bob-window"])
    windows, guide = {}, {}
    for role in ("alice", "bob"):
        session = json.loads(fixtures[role].read_text())
        home = root / ("gui-" + role); home.mkdir(mode=0o700)
        for name in ("config", "data", "cache", "trades"):
            (home / name).mkdir(mode=0o700)
        shell = shells[role + "-window"]
        _settings(home / "config", shell.with_suffix(".keys") if shell.with_suffix(".keys").exists() else shell, daemon_port)
        environment = dict(os.environ, MONZERO_ENABLE_PRIVATE_DEX="I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE",
            XDG_CONFIG_HOME=str(home / "config"), XDG_DATA_HOME=str(home / "data"), XDG_CACHE_HOME=str(home / "cache"))
        command = [str(gui), "--disable-check-updates", "--dex-ltc-network", session["ltcNetwork"],
            "--dex-ltc-app", session["appConfigFile"], "--dex-trade-root", str(home / "trades"),
            "--dex-control-file", session["controlFile"]]
        if role == "bob" and light_setup:
            # Electrum test server certificate, and the verified helper that opens Bob's light wallet.
            environment["SSL_CERT_FILE"] = str(args.light_wallet_tls / "root.pem")
            command += ["--dex-light-setup-helper", str(args.light_helper),
                "--dex-light-setup-digest", light_setup["helperSha256"]]
            guide["bobLightWalletFolder"] = str(pathlib.Path(light_setup["launcher"]).parent)
            guide["bobLightWalletPasswordFile"] = str(pathlib.Path(light_setup["launcher"]).parent / "light-password")
            guide["bobLightWalletAddress"] = light_setup["address"]
        log = (home / "app.log").open("xb")
        windows[role] = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT,
            start_new_session=True)
        windows[role].monzero_owned_group = True
        guide[role + "Log"] = str(home / "app.log")
    write_private(root / "hand-over.json", guide)
    note("hand-over", windows=sorted(windows), guide=str(root / "hand-over.json"))
    started = time.monotonic()
    while any(window.poll() is None for window in windows.values()):
        require(time.monotonic() - started < timeout, "Hand-over windows still open after the trade timeout")
        time.sleep(5)
    note("windows-closed", exitCodes={role: window.returncode for role, window in windows.items()})
    return dict(handOverGui=True, handOverSeconds=round(time.monotonic() - started),
        windowExitCodes={role: window.returncode for role, window in windows.items()})
