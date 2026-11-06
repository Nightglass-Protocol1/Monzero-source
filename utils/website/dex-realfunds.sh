#!/usr/bin/env bash
# Owner-operated REAL-FUNDS switch for the Monzero DEX.
#
# YOU run this (it uses your SSH access to the webspace). It flips the server-side `live`
# flag in monzero-private/dex-status.php, which the website bootstrap (/dex/api.php) and the
# app read to present the DEX as live or "coming soon".
#
#   ./dex-realfunds.sh status
#   ./dex-realfunds.sh disable                       # KILL SWITCH — immediate, always allowed
#   ./dex-realfunds.sh enable I_ENABLE_REAL_FUNDS    # requires the exact token
#
# IMPORTANT — this is the convenience/discovery gate, NOT the whole story:
#   * The ULTIMATE gate for real trading is the app's SIGNED operator control document
#     (DexRiskControl) + the per-trade caps/kill-switch. Keep that as the real authority.
#   * Only enable AFTER: the swap program is deployed to mainnet, the security audit/pentest
#     is done, and you have accepted the real-funds risk. Enabling here does not deploy
#     anything or move funds — it only tells clients the DEX is open.
# Disabling is the opposite: it is a safe, instant kill switch — use it any time.
set -euo pipefail

HOST="${DEPLOY_HOST:-monzero-webspace}"
ACTION="${1:-status}"
TOKEN="${2:-}"
ENABLE_TOKEN="I_ENABLE_REAL_FUNDS"

# One PHP helper, run on the server (which has PHP). It locates nothing itself — it is given
# the private dir — reads the current status, optionally sets `live`, and prints the state.
read -r -d '' PHP <<'PHP_EOF' || true
<?php
// argv: <priv> <action status|set> [<live true|false>]
$priv = $argv[1]; $action = $argv[2]; $f = "$priv/dex-status.php";
$cur = (file_exists($f) && !is_link($f)) ? (include $f) : [];
if (!is_array($cur)) $cur = [];
if ($action === 'set') {
    $live = ($argv[3] ?? 'false') === 'true';
    $cur['live'] = $live;
    if ($live && empty($cur['launch_at'])) $cur['launch_at'] = time();
    $tmp = "$f.tmp";
    if (file_put_contents($tmp, "<?php return " . var_export($cur, true) . ";\n") === false) {
        fwrite(STDERR, "write failed\n"); exit(1);
    }
    rename($tmp, $f);
}
printf("live=%s launch_at=%s rendezvous=%d min_app=%s\n",
    !empty($cur['live']) ? 'true' : 'false',
    $cur['launch_at'] ?? 'null',
    is_array($cur['rendezvous'] ?? null) ? count($cur['rendezvous']) : 0,
    $cur['min_app_version'] ?? 'null');
PHP_EOF
PHP_B64="$(printf '%s' "$PHP" | base64 | tr -d '\n')"

# Run the helper on the server: locate the private dir (sibling of the webroot, next to
# admin/bootstrap.php), decode the helper, run it with the requested action.
run_remote() { # $1 = action (status|set), $2 = live (true|false, for set)
    ssh -o BatchMode=yes "$HOST" bash -s "$PHP_B64" "$1" "${2:-}" <<'SH'
set -euo pipefail
PHP_B64="$1"; ACTION="$2"; LIVE="${3:-}"
f=$(ls ~/*/admin/bootstrap.php 2>/dev/null | head -1)
[ -n "$f" ] && [ "$(echo "$f" | wc -l)" = 1 ] || { echo "webroot not found" >&2; exit 1; }
priv="$(dirname "$(dirname "$(dirname "$f")")")/monzero-private"
mkdir -p "$priv"; chmod 700 "$priv" 2>/dev/null || true
tmp="$priv/.dexrf-$$.php"
printf '%s' "$PHP_B64" | base64 -d > "$tmp"
php "$tmp" "$priv" "$ACTION" "$LIVE"
rm -f "$tmp"
SH
}

case "$ACTION" in
  status)
    echo "DEX real-funds status (from the server):"
    run_remote status
    ;;
  disable)
    echo "Disabling real funds (kill switch)…"
    run_remote set false
    echo "DONE — the DEX now reports 'coming soon'. (The app's signed control document remains the real gate.)"
    ;;
  enable)
    if [ "$TOKEN" != "$ENABLE_TOKEN" ]; then
      echo "Refusing to enable real funds." >&2
      echo "Re-run with the exact token:  $0 enable $ENABLE_TOKEN" >&2
      echo "Only do this AFTER: mainnet program deployed, audit/pentest done, caps + signed control document in place." >&2
      exit 1
    fi
    echo "!!! ENABLING REAL FUNDS on the live DEX."
    echo "    Confirm you have: mainnet deploy done, audit complete, caps + signed control document active."
    run_remote set true
    echo "DONE — the DEX now reports live. Monitor it; './dex-realfunds.sh disable' is your instant kill switch."
    ;;
  *)
    echo "usage: $0 {status|enable $ENABLE_TOKEN|disable}" >&2
    exit 2
    ;;
esac
