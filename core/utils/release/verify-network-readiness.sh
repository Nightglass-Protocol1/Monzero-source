#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || {
  echo "Usage: $0 <operator-local-daemon-rpc-url>" >&2
  echo "Example: $0 http://127.0.0.1:6175" >&2
  exit 2
}

rpc_url=${1%/}
case "$rpc_url" in
  http://127.0.0.1:*|http://localhost:*|https://127.0.0.1:*|https://localhost:*) ;;
  *)
    [[ ${ALLOW_REMOTE_RPC:-0} == 1 ]] || {
      echo "Refusing a non-local RPC URL. Run this on the node host, or set ALLOW_REMOTE_RPC=1 for an explicit diagnostic." >&2
      exit 2
    }
    ;;
esac

min_peers=${MONZERO_MIN_RELEASE_PEERS:-2}
max_tip_age=${MONZERO_MAX_TIP_AGE_SECONDS:-900}
max_future_skew=${MONZERO_MAX_FUTURE_SKEW_SECONDS:-300}
[[ $min_peers =~ ^[0-9]+$ && $max_tip_age =~ ^[0-9]+$ && $max_future_skew =~ ^[0-9]+$ ]] || {
  echo "Network readiness thresholds must be non-negative integers" >&2
  exit 2
}

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-network-readiness.XXXXXX")
info_file="$work_dir/info.json"
header_file="$work_dir/header.json"
trap 'rm -f -- "$info_file" "$header_file"; rmdir -- "$work_dir"' EXIT

curl --fail --silent --show-error --max-time 10 \
  -H 'Content-Type: application/json' --data '{}' \
  "$rpc_url/get_info" > "$info_file"
curl --fail --silent --show-error --max-time 10 \
  -H 'Content-Type: application/json' \
  --data '{"jsonrpc":"2.0","id":"release-readiness","method":"get_last_block_header"}' \
  "$rpc_url/json_rpc" > "$header_file"

python3 - "$info_file" "$header_file" "$min_peers" "$max_tip_age" "$max_future_skew" <<'PY'
import json, pathlib, sys, time

info_path, header_path = map(pathlib.Path, sys.argv[1:3])
min_peers, max_tip_age, max_future_skew = map(int, sys.argv[3:6])
try:
    info = json.loads(info_path.read_text(encoding="utf-8"))
    header_response = json.loads(header_path.read_text(encoding="utf-8"))
except (OSError, UnicodeError, json.JSONDecodeError) as exc:
    raise SystemExit(f"Invalid daemon RPC response: {exc}")

failures = []
if info.get("status") != "OK":
    failures.append(f"daemon status is {info.get('status')!r}, not 'OK'")
if info.get("mainnet") is not True or info.get("nettype") != "mainnet":
    failures.append("daemon is not on Monzero mainnet")
if info.get("offline") is True:
    failures.append("daemon reports offline=true")
if info.get("busy_syncing") is True:
    failures.append("daemon reports busy_syncing=true")
if info.get("synchronized") is not True:
    failures.append("daemon reports synchronized=false")

height = info.get("height")
if not isinstance(height, int) or isinstance(height, bool) or height <= 0:
    failures.append("daemon height is missing or invalid")
incoming = info.get("incoming_connections_count")
outgoing = info.get("outgoing_connections_count")
if not isinstance(incoming, int) or not isinstance(outgoing, int):
    failures.append("peer counts are hidden; use the operator-local unrestricted RPC")
    peers = 0
else:
    peers = incoming + outgoing
    if peers < min_peers:
        failures.append(f"peer count {peers} is below required minimum {min_peers}")

header_result = header_response.get("result", {})
header_status = header_result.get("status")
if header_status != "OK":
    failures.append(f"last-block-header RPC status is {header_status!r}, not 'OK'")
header = header_result.get("block_header", {})
timestamp = header.get("timestamp")
header_height = header.get("height")
if header_status != "OK":
    tip_age = None
elif not isinstance(timestamp, int) or isinstance(timestamp, bool) or timestamp <= 0:
    failures.append("latest block timestamp is missing or invalid")
    tip_age = None
else:
    tip_age = int(time.time()) - timestamp
    if tip_age > max_tip_age:
        failures.append(f"latest block is stale by {tip_age} seconds (maximum {max_tip_age})")
    if tip_age < -max_future_skew:
        failures.append(f"latest block is {-tip_age} seconds in the future (maximum skew {max_future_skew})")
if header_status == "OK" and isinstance(height, int) and isinstance(header_height, int) and height != header_height + 1:
    failures.append(f"get_info height {height} disagrees with top block height {header_height}")

if failures:
    print("Network readiness failed:", file=sys.stderr)
    for failure in failures:
        print(f"- {failure}", file=sys.stderr)
    raise SystemExit(1)

print(f"Network readiness passed: height={height}, peers={peers}, tip_age={tip_age}s")
PY
