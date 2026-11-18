#!/usr/bin/env bash
set -euo pipefail

RPC_URL="${1:-http://127.0.0.1:6177}"
SERVICE_NAME="${2:-monzerod.service}"
DATA_DIR="${3:-/var/lib/monzero}"
LOG_FILE="${4:-/var/log/monzero/monzerod.log}"
MAX_DISK_PERCENT="${MONZERO_MAX_DISK_PERCENT:-90}"
MAX_RSS_MB="${MONZERO_MAX_RSS_MB:-4096}"

failures=()

if ! systemctl is-active --quiet "$SERVICE_NAME"; then
  failures+=("$SERVICE_NAME is not active")
fi

main_pid="$(systemctl show --property MainPID --value "$SERVICE_NAME" 2>/dev/null || true)"
if [[ ! "$main_pid" =~ ^[1-9][0-9]*$ ]] || [[ ! -r "/proc/$main_pid/status" ]]; then
  failures+=("$SERVICE_NAME has no readable main process")
  rss_mb=0
else
  rss_kb="$(awk '/^VmRSS:/ {print $2}' "/proc/$main_pid/status")"
  rss_mb="$(( (rss_kb + 1023) / 1024 ))"
  if (( rss_mb > MAX_RSS_MB )); then
    failures+=("daemon RSS ${rss_mb} MiB exceeds ${MAX_RSS_MB} MiB")
  fi
fi

if [[ ! -d "$DATA_DIR" ]]; then
  failures+=("data directory is missing: $DATA_DIR")
  disk_percent=100
else
  disk_percent="$(df -P "$DATA_DIR" | awk 'NR == 2 {gsub(/%/, "", $5); print $5}')"
  if [[ ! "$disk_percent" =~ ^[0-9]+$ ]]; then
    failures+=("unable to determine filesystem usage for $DATA_DIR")
    disk_percent=100
  elif (( disk_percent > MAX_DISK_PERCENT )); then
    failures+=("filesystem usage ${disk_percent}% exceeds ${MAX_DISK_PERCENT}%")
  fi
fi

rpc_response="$(curl --fail --silent --show-error --max-time 10 \
  --header 'Content-Type: application/json' --data '{}' "$RPC_URL/get_info" 2>&1)" || {
  failures+=("operator RPC request failed: $rpc_response")
  rpc_response='{}'
}

readarray -t rpc_values < <(python3 -c '
import json, sys
try:
    value = json.load(sys.stdin)
except Exception:
    value = {}
print(value.get("status", ""))
print(str(value.get("mainnet", False)).lower())
print(str(value.get("synchronized", False)).lower())
print(value.get("height", 0))
print(value.get("top_block_hash", ""))
' <<<"$rpc_response")
rpc_status="${rpc_values[0]:-}"
rpc_mainnet="${rpc_values[1]:-false}"
rpc_synchronized="${rpc_values[2]:-false}"
height="${rpc_values[3]:-0}"
top_hash="${rpc_values[4]:-}"

[[ "$rpc_status" == "OK" ]] || failures+=("operator RPC status is not OK")
[[ "$rpc_mainnet" == "true" ]] || failures+=("operator RPC is not mainnet")
[[ "$rpc_synchronized" == "true" ]] || failures+=("daemon is not synchronized")
[[ "$height" =~ ^[1-9][0-9]*$ ]] || failures+=("daemon height is invalid")
[[ "$top_hash" =~ ^[0-9a-f]{64}$ ]] || failures+=("top block hash is invalid")

recent_error_lines=0
if [[ -r "$LOG_FILE" ]]; then
  recent_error_lines="$(tail -n 200 "$LOG_FILE" | grep -Eic '(^|[[:space:]])(ERROR|FATAL)([[:space:]]|$)' || true)"
fi

if (( ${#failures[@]} > 0 )); then
  printf 'Node operational health failed:\n' >&2
  printf -- '- %s\n' "${failures[@]}" >&2
  exit 1
fi

printf 'Node operational health passed: service=%s pid=%s height=%s rss=%sMiB disk=%s%% recent_error_lines=%s top_hash=%s\n' \
  "$SERVICE_NAME" "$main_pid" "$height" "$rss_mb" "$disk_percent" "$recent_error_lines" "$top_hash"
