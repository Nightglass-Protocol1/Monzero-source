#!/usr/bin/env bash
# Deploy the Monzero XMZ<->Solana swap program to a cluster.
#
# GATED: a mainnet deploy spends real SOL and MUST wait for the security audit. It refuses to
# run against mainnet-beta unless MONZERO_SOL_MAINNET=I_ACCEPT_REAL_FUNDS_RISK is set. Devnet
# is open for testing.
#
#   ./deploy-solana.sh devnet
#   MONZERO_SOL_MAINNET=I_ACCEPT_REAL_FUNDS_RISK ./deploy-solana.sh mainnet-beta
#
# Needs: the Anza toolchain (cargo-build-sbf, solana) and a funded deployer (solana config).
set -euo pipefail
CLUSTER="${1:-devnet}"

if [ "$CLUSTER" = "mainnet-beta" ] && [ "${MONZERO_SOL_MAINNET:-}" != "I_ACCEPT_REAL_FUNDS_RISK" ]; then
  echo "Refusing mainnet deploy: set MONZERO_SOL_MAINNET=I_ACCEPT_REAL_FUNDS_RISK (and finish the audit first)." >&2
  exit 1
fi

HERE="$(cd "$(dirname "$0")/.." && pwd)"          # .../solana
PROG="$HERE/program"
KEYPAIR="${PROGRAM_KEYPAIR:-$HOME/.config/monzero-dex/solana-program.json}"
if [ ! -f "$KEYPAIR" ]; then
  echo "Program keypair not found at $KEYPAIR." >&2
  echo "Generate one OUTSIDE the repo and back it up:  solana-keygen new -o \"$KEYPAIR\"" >&2
  exit 1
fi

echo "building (sbpf v3, the mainnet loader)…"
( cd "$PROG" && cargo-build-sbf --arch v3 )
SO="$PROG/target/deploy/monzero_sol_swap.so"

echo "deploying to $CLUSTER…"
solana program deploy --url "$CLUSTER" --program-id "$KEYPAIR" "$SO"
PID="$(solana address -k "$KEYPAIR")"

mkdir -p "$PROG/deployments"
SAFE="$(printf '%s' "$CLUSTER" | tr -c 'A-Za-z0-9.-' '_')"   # cluster may be a URL; keep the filename valid
printf '{"cluster":"%s","program":"%s","so_sha256":"%s"}\n' \
  "$CLUSTER" "$PID" "$(sha256sum "$SO" | cut -d' ' -f1)" > "$PROG/deployments/solana-$SAFE.json"
echo "deployed program id: $PID"
echo "recorded deployments/solana-$CLUSTER.json — PIN this program id in the app build (never fetch it from the website)."
