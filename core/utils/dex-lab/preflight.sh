#!/usr/bin/env bash
# Read-only checks: installs nothing and never starts a service.
set -euo pipefail
printf 'DEX lab preflight (read-only)\n'
uname -srmo
printf '\nMemory\n'
free -h
printf '\nAvailable storage at current directory\n'
df -h .
printf '\nAvailable tools\n'
for tool in python3 cmake g++ git bitcoind bitcoin-cli monzerod monzero-wallet-rpc; do
    if command -v "$tool" >/dev/null 2>&1; then
        command -v "$tool"
    else
        printf '%s: not on PATH\n' "$tool"
    fi
done
printf '\nListening TCP sockets (review for unintended exposure)\n'
ss -lnt
