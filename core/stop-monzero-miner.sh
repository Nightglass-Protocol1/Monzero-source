#!/usr/bin/env bash
set -euo pipefail

RPC_PORT="${MONZERO_RPC_PORT:-6175}"
RPC_URL="http://127.0.0.1:$RPC_PORT"

response="$(curl --silent --show-error --fail --max-time 120 \
  --header 'Content-Type: application/json' \
  --data '{}' "$RPC_URL/stop_mining")"

python3 - "$response" <<'PY'
import json
import sys

data = json.loads(sys.argv[1])
status = data.get("status", "")
if status != "OK":
    raise SystemExit(f"Unable to stop mining: {data.get('error_details') or status or data}")
print("Mining stopped. The daemon remains running.")
PY
