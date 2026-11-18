#!/usr/bin/env bash
set -euo pipefail

RPC_PORT="${MONZERO_RPC_PORT:-6175}"
MINER_ADDRESS="${1:-${MONZERO_MINER_ADDRESS:-}}"
THREADS="${2:-${MONZERO_MINER_THREADS:-1}}"
RPC_URL="http://127.0.0.1:$RPC_PORT"

if ! [[ "$THREADS" =~ ^[1-9][0-9]*$ ]]; then
  printf 'Error: thread count must be a positive integer.\n' >&2
  printf 'Usage: %s <Monzero address> [threads]\n' "$0" >&2
  exit 1
fi

if [[ ! $MINER_ADDRESS =~ ^[1-9A-HJ-NP-Za-km-z]{95}$ ]]; then
  printf 'Error: provide a valid standard or subaddress for mining rewards.\n' >&2
  printf 'Usage: %s <Monzero address> [threads]\n' "$0" >&2
  printf 'Alternatively set MONZERO_MINER_ADDRESS.\n' >&2
  exit 1
fi

if ! info="$(curl --silent --fail --max-time 2 \
  --header 'Content-Type: application/json' \
  --data '{}' "$RPC_URL/get_info")"; then
  printf 'Error: the Monzero daemon is not running at 127.0.0.1:%s.\n' "$RPC_PORT" >&2
  printf 'Start it separately with ./start-monzerod.sh\n' >&2
  exit 1
fi

python3 - "$info" <<'PY'
import json
import sys

try:
    data = json.loads(sys.argv[1])
except (json.JSONDecodeError, TypeError) as error:
    raise SystemExit(f"Error: daemon returned invalid status JSON: {error}")
if data.get("status") != "OK" or data.get("nettype") != "mainnet":
    raise SystemExit("Error: mining launcher requires a healthy Monzero mainnet daemon.")
if data.get("synchronized") is not True:
    raise SystemExit("Error: refusing to mine while the Monzero daemon is not synchronized.")
PY

request="$(python3 - "$MINER_ADDRESS" "$THREADS" <<'PY'
import json
import sys

print(json.dumps({
    "miner_address": sys.argv[1],
    "threads_count": int(sys.argv[2]),
    "do_background_mining": False,
    "ignore_battery": True,
}, separators=(",", ":")))
PY
)"

response="$(curl --silent --show-error --fail --max-time 10 \
  --header 'Content-Type: application/json' \
  --data "$request" \
  "$RPC_URL/start_mining")"

python3 - "$response" "$THREADS" "$MINER_ADDRESS" <<'PY'
import json
import sys

data = json.loads(sys.argv[1])
status = data.get("status", "")
if status != "OK":
    raise SystemExit(f"Mining failed: {data.get('error_details') or status or data}")
print(f"Mining started with {sys.argv[2]} thread(s).")
print(f"Rewards address: {sys.argv[3]}")
PY
