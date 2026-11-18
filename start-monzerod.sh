#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DAEMON_BIN="${MONZERO_DAEMON_BIN:-}"
if [[ -z $DAEMON_BIN ]]; then
  if [[ -x "$PROJECT_DIR/monzerod" ]]; then
    DAEMON_BIN="$PROJECT_DIR/monzerod"
  else
    DAEMON_BIN="$PROJECT_DIR/build/bin/monzerod"
  fi
fi
DATA_DIR="${MONZERO_DATA_DIR:-$HOME/.monzero}"
P2P_PORT="${MONZERO_P2P_PORT:-6174}"
RPC_PORT="${MONZERO_RPC_PORT:-6175}"
ZMQ_PORT="${MONZERO_ZMQ_PORT:-6176}"
PRIORITY_NODE="${MONZERO_PRIORITY_NODE:-node.monzero.org:6174}"
SECONDARY_PRIORITY_NODE="${MONZERO_SECONDARY_PRIORITY_NODE:-node2.monzero.org:6174}"

if [[ ! -x "$DAEMON_BIN" ]]; then
  printf 'Error: daemon executable not found: %s\n' "$DAEMON_BIN" >&2
  printf 'Build monzerod first, use a release package, or set MONZERO_DAEMON_BIN.\n' >&2
  exit 1
fi

daemon_ready() {
  curl --silent --fail --max-time 2 \
    --header 'Content-Type: application/json' \
    --data '{}' \
    "http://127.0.0.1:$RPC_PORT/get_info" >/dev/null 2>&1
}

if daemon_ready; then
  printf 'Monzero daemon is already running at 127.0.0.1:%s.\n' "$RPC_PORT"
  exit 0
fi

if command -v ss >/dev/null && ss -ltn | awk '{print $4}' | grep -Eq "(^|:)$RPC_PORT$"; then
  printf 'Error: port %s is occupied, but it is not responding as a Monzero daemon.\n' "$RPC_PORT" >&2
  exit 1
fi

mkdir -p "$DATA_DIR"

printf 'Starting Monzero daemon...\n'
printf '  Data: %s\n  P2P: %s\n  RPC: %s\n  ZMQ: %s\n' "$DATA_DIR" "$P2P_PORT" "$RPC_PORT" "$ZMQ_PORT"

"$DAEMON_BIN" \
  --detach \
  --data-dir "$DATA_DIR" \
  --p2p-bind-port "$P2P_PORT" \
  --rpc-bind-ip 127.0.0.1 \
  --rpc-bind-port "$RPC_PORT" \
  --zmq-rpc-bind-ip 127.0.0.1 \
  --zmq-rpc-bind-port "$ZMQ_PORT" \
  --disable-rpc-ban \
  --add-priority-node "$PRIORITY_NODE" \
  --add-priority-node "$SECONDARY_PRIORITY_NODE" \
  --no-igd

for _ in {1..30}; do
  if daemon_ready; then
    info="$(curl --silent --max-time 2 --header 'Content-Type: application/json' --data '{}' "http://127.0.0.1:$RPC_PORT/get_info")"
    python3 - "$info" <<'PY'
import json, sys
data = json.loads(sys.argv[1])
print(f"Daemon ready: height={data.get('height', '?')} synchronized={str(data.get('synchronized', False)).lower()}")
PY
    printf 'Log: %s/monzero.log\n' "$DATA_DIR"
    exit 0
  fi
  sleep 1
done

printf 'Error: daemon did not become ready within 30 seconds.\n' >&2
printf 'Check: %s/monzero.log\n' "$DATA_DIR" >&2
exit 1
