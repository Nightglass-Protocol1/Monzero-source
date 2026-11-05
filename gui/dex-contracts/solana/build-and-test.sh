#!/usr/bin/env bash
# Reproducible build + test for the Monzero XMZ<->Solana swap program.
#
# Requirements:
#   - Solana/Anza toolchain (provides `cargo-build-sbf`):
#       sh -c "$(curl -sSfL https://release.anza.xyz/stable/install)"
#   - A Rust toolchain for the host-side logic + LiteSVM tests (<= 1.95 works here).
#   - Normally `cargo-build-sbf` drives its bundled toolchain via `rustup`. If `rustup`
#     is not installed, this script creates minimal cargo/rustup shims pointing at the
#     platform-tools rust (see SHIM below) so the SBF build still works.
#
# Usage: ./build-and-test.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
sol_bin="$HOME/.local/share/solana/install/active_release/bin"
export PATH="$sol_bin:$PATH"

# Patience for a slow crates.io link (no low-speed abort).
export CARGO_HTTP_LOW_SPEED_LIMIT=0 CARGO_HTTP_TIMEOUT=600 CARGO_NET_RETRY=20

# --- rustup shim (only if rustup is missing) --------------------------------------------
if ! command -v rustup >/dev/null 2>&1; then
  pt="$(ls -d "$HOME"/.cache/solana/*/platform-tools/rust 2>/dev/null | sort | tail -1)"
  [ -n "$pt" ] || { echo "platform-tools rust not found; run cargo-build-sbf once to install it"; exit 1; }
  shim="$(mktemp -d)"
  name="1.95.0-sbpf-solana-v1.57"
  cat > "$shim/cargo" <<EOF
#!/bin/sh
case "\$1" in
  +*) shift; exec env PATH="$pt/bin:\$PATH" RUSTC="$pt/bin/rustc" "$pt/bin/cargo" "\$@";;
  *) exec "$(command -v cargo)" "\$@";;
esac
EOF
  cat > "$shim/rustup" <<EOF
#!/bin/sh
case "\$1" in
  toolchain) [ "\$2" = list ] && { [ "\$3" = "-v" ] && printf '%s\t%s\n' "$name" "$pt" || echo "$name"; }; exit 0;;
  run) shift; shift; PATH="$pt/bin:\$PATH" exec "\$@";;
  which) for a in "\$@"; do l="\$a"; done; echo "$pt/bin/\$l"; exit 0;;
  *) exit 0;;
esac
EOF
  chmod +x "$shim/cargo" "$shim/rustup"
  export PATH="$shim:$PATH"
fi

echo "== host logic tests =="
( cd "$here/swap-logic" && cargo test )

echo "== build program (sbpf v1, to match the LiteSVM test harness) =="
( cd "$here/program" && cargo-build-sbf --arch v1 )

echo "== on-chain integration tests (LiteSVM) =="
( cd "$here/program-tests" && cargo test -- --test-threads=1 )

echo "OK"
