#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GUI_BIN="${MONZERO_GUI_BIN:-$PROJECT_DIR/monzero-gui/build-monzero/bin/monzero-wallet-gui}"
RPC_PORT="${MONZERO_RPC_PORT:-6175}"

if [[ ! -x "$GUI_BIN" ]]; then
  printf 'Error: GUI executable not found: %s\n' "$GUI_BIN" >&2
  printf 'Build the Monzero GUI first, or set MONZERO_GUI_BIN.\n' >&2
  exit 1
fi

if ! curl --silent --fail --max-time 2 \
  --header 'Content-Type: application/json' \
  --data '{}' \
  "http://127.0.0.1:$RPC_PORT/get_info" >/dev/null 2>&1; then
  printf 'Error: the Monzero daemon is not running at 127.0.0.1:%s.\n' "$RPC_PORT" >&2
  printf 'Start it separately with: %s/start-monzerod.sh\n' "$PROJECT_DIR" >&2
  exit 1
fi

printf '\nLaunching Monzero GUI.\n'
printf 'If the GUI asks for a node, choose a remote/custom node and enter:\n'
printf '  Address: 127.0.0.1\n  Port: %s\n  Trusted: yes\n\n' "$RPC_PORT"

exec "$GUI_BIN" --disable-check-updates "$@"
