#!/usr/bin/env bash
set -euo pipefail

[[ $# -ge 3 && $# -le 4 ]] || {
  echo "Usage: $0 <monzero-linux.tar.gz> <expected-sha256> <daemon-host:port> [evidence.json]" >&2
  exit 2
}

archive=$(realpath "$1")
expected_hash=$2
daemon_address=$3
evidence_path=${4:-"$PWD/monzero-linux-wallet-evidence.json"}
[[ -f $archive ]] || { echo "Archive not found: $archive" >&2; exit 2; }
[[ $expected_hash =~ ^[0-9a-f]{64}$ ]] || { echo "Expected SHA-256 must be lowercase hexadecimal" >&2; exit 2; }
[[ $daemon_address =~ ^[A-Za-z0-9.-]+:[0-9]+$ ]] || { echo "Invalid daemon address" >&2; exit 2; }
command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 2; }
command -v curl >/dev/null || { echo "curl is required" >&2; exit 2; }

actual_hash=$(sha256sum "$archive" | awk '{print $1}')
[[ $actual_hash == "$expected_hash" ]] || {
  echo "Archive SHA-256 mismatch: expected $expected_hash, got $actual_hash" >&2
  exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
python3 "$script_dir/validate-binary-archive.py" tar.gz "$archive"

work_dir=$(mktemp -d -t monzero-linux-wallet.XXXXXXXX)
wallet_rpc_pid=
passed=false
cleanup() {
  if [[ -n $wallet_rpc_pid ]] && kill -0 "$wallet_rpc_pid" 2>/dev/null; then
    kill "$wallet_rpc_pid" 2>/dev/null || true
    wait "$wallet_rpc_pid" 2>/dev/null || true
  fi
  if [[ $passed == true ]]; then
    rm -rf -- "$work_dir"
  else
    echo "Failure diagnostics retained at $work_dir" >&2
  fi
}
trap cleanup EXIT

tar -xzf "$archive" -C "$work_dir"
mapfile -t roots < <(find "$work_dir" -mindepth 1 -maxdepth 1 -type d)
[[ ${#roots[@]} -eq 1 ]] || { echo "Archive must contain exactly one root directory" >&2; exit 1; }
package_dir=${roots[0]}
(cd "$package_dir" && sha256sum -c SHA256SUMS)

version=$("$package_dir/monzero-wallet-rpc" --version 2>&1)
revision=$(sed -n "s/.*-\([0-9a-f]\{9\}\)).*/\1/p" <<<"$version")
[[ $revision =~ ^[0-9a-f]{9}$ ]] || { echo "Unable to parse packaged source revision" >&2; exit 1; }

rpc_port=$(python3 - <<'PY'
import socket
with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    print(sock.getsockname()[1])
PY
)
mkdir -m 700 "$work_dir/wallets"
"$package_dir/monzero-wallet-rpc" \
  --wallet-dir "$work_dir/wallets" \
  --rpc-bind-ip 127.0.0.1 \
  --rpc-bind-port "$rpc_port" \
  --disable-rpc-login \
  --daemon-address "$daemon_address" \
  --log-file "$work_dir/wallet-rpc.log" \
  >"$work_dir/wallet-rpc.stdout.log" 2>"$work_dir/wallet-rpc.stderr.log" &
wallet_rpc_pid=$!

python3 - "$rpc_port" "$expected_hash" "$revision" "$version" "$daemon_address" "$evidence_path" <<'PY'
import datetime
import json
import pathlib
import sys
import time
import urllib.error
import urllib.request

port, artifact_hash, revision, version, daemon_address, evidence_path = sys.argv[1:]
url = f"http://127.0.0.1:{port}/json_rpc"

def rpc(method, params=None, timeout=30):
    body = json.dumps({
        "jsonrpc": "2.0", "id": "linux-wallet-smoke",
        "method": method, "params": params or {},
    }).encode()
    request = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        value = json.load(response)
    if "error" in value:
        raise RuntimeError(f"{method} failed: {value['error'].get('message', value['error'])}")
    return value.get("result", {})

for _ in range(120):
    try:
        rpc("get_version")
        break
    except Exception:
        time.sleep(0.5)
else:
    raise RuntimeError("Wallet RPC did not become ready")

rpc("create_wallet", {
    "filename": "native-created", "password": "temporary-release-test", "language": "English",
})
created_address = rpc("get_address")["address"]
seed = rpc("query_key", {"key_type": "mnemonic"})["key"]
if len(created_address) != 95 or not created_address.startswith("F"):
    raise RuntimeError("Created wallet returned an invalid Monzero address")
if not seed.strip():
    raise RuntimeError("Created wallet returned no recovery seed")
rpc("close_wallet")

rpc("restore_deterministic_wallet", {
    "filename": "native-restored", "password": "temporary-release-test",
    "seed": seed, "restore_height": 0,
})
restored_address = rpc("get_address")["address"]
seed = None
if restored_address != created_address:
    raise RuntimeError("Seed restoration did not reproduce the original address")
refresh = rpc("refresh", timeout=120)
balance = rpc("get_balance")
rpc("store")
rpc("close_wallet")

evidence = {
    "schema_version": 1,
    "project": "Monzero",
    "test": "Exact native Linux release-package wallet lifecycle smoke",
    "tested_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "artifact_sha256": artifact_hash,
    "source_revision": revision,
    "executable_version": version.strip(),
    "daemon_address": daemon_address,
    "wallet": {
        "address": created_address,
        "creation": "pass",
        "seed_export": "pass",
        "deterministic_restore": "pass",
        "refresh": "pass",
        "blocks_fetched": int(refresh.get("blocks_fetched", 0)),
        "balance": int(balance.get("balance", 0)),
        "unlocked_balance": int(balance.get("unlocked_balance", 0)),
        "store_and_close": "pass",
    },
    "result": "pass",
}
pathlib.Path(evidence_path).write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
print(f"Native Linux wallet lifecycle smoke passed: {evidence_path}")
PY

kill -TERM "$wallet_rpc_pid"
for _ in $(seq 1 60); do
  kill -0 "$wallet_rpc_pid" 2>/dev/null || break
  sleep 0.5
done
if kill -0 "$wallet_rpc_pid" 2>/dev/null; then
  echo "Wallet RPC did not stop within 30 seconds" >&2
  exit 1
fi
wait "$wallet_rpc_pid"
wallet_rpc_pid=
passed=true
