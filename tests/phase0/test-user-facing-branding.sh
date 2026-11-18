#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$project_root"

fail=false

check_absent() {
  local description=$1
  local pattern=$2
  shift 2
  if grep -RInE -- "$pattern" "$@"; then
    echo "User-facing branding gate failed: $description" >&2
    fail=true
  fi
}

for completion in monzerod monzero-wallet-cli monzero-wallet-rpc; do
  file="utils/fish/${completion}.fish"
  [[ -f $file ]] || {
    echo "User-facing branding gate failed: missing $file" >&2
    fail=true
    continue
  }
  grep -Fq "complete -c ${completion}" "$file" || {
    echo "User-facing branding gate failed: $file does not register ${completion}" >&2
    fail=true
  }
done

for obsolete in utils/fish/monerod.fish utils/fish/monero-wallet-cli.fish utils/fish/monero-wallet-rpc.fish; do
  [[ ! -e $obsolete ]] || {
    echo "User-facing branding gate failed: obsolete completion remains: $obsolete" >&2
    fail=true
  }
done

check_absent \
  "Fish completions contain inherited product names, services, or ports" \
  '(^|[^[:alnum:]_])(monerod|monero-wallet-(cli|rpc)|Monero)([^[:alnum:]_]|$)|getmonero\.org|(^|[^0-9])(1808[0-2]|2808[0-2]|3808[0-2])([^0-9]|$)' \
  utils/fish

check_absent \
  "active runtime defaults contain inherited executable or file names" \
  'monero-wallet-(cli|rpc)\.(log|login)|bitmonero\.daemon|Please run monerod|monerod is now disconnected|with Monero address|WINDOWS_SERVICE_NAME = "Monero Daemon"' \
  src/simplewallet/simplewallet.cpp \
  src/wallet/wallet_rpc_server.cpp \
  src/wallet/message_store.cpp \
  src/blockchain_db/lmdb/db_lmdb.cpp \
  src/cryptonote_protocol/cryptonote_protocol_handler.inl \
  src/daemon/command_line_args.h \
  src/daemonizer/posix_fork.cpp

check_absent \
  "active wallet help contains the upstream ticker" \
  '(^|[^[:alnum:]_])XMR([^[:alnum:]_]|$)' \
  src/simplewallet/simplewallet.cpp

for launcher in start-monzero-wallet-cli.sh start-monzero-miner.sh stop-monzero-miner.sh; do
  grep -Fq 'MONZERO_RPC_PORT:-6175' "$launcher" || {
    echo "User-facing branding gate failed: $launcher does not default to mainnet RPC port 6175" >&2
    fail=true
  }
done

grep -Fq 'MONZERO_P2P_PORT:-6174' start-monzerod.sh || {
  echo "User-facing branding gate failed: node launcher does not default to mainnet P2P port 6174" >&2
  fail=true
}
grep -Fq 'MONZERO_ZMQ_PORT:-6176' start-monzerod.sh || {
  echo "User-facing branding gate failed: node launcher does not default to mainnet ZMQ port 6176" >&2
  fail=true
}
for bind_option in '--rpc-bind-ip 127.0.0.1' '--zmq-rpc-bind-ip 127.0.0.1'; do
  grep -Fq -- "$bind_option" start-monzerod.sh || {
    echo "User-facing branding gate failed: Linux launcher omits loopback binding: $bind_option" >&2
    fail=true
  }
  grep -Fq -- "$bind_option" utils/release/windows/start-node.bat || {
    echo "User-facing branding gate failed: Windows launcher omits loopback binding: $bind_option" >&2
    fail=true
  }
done
if grep -Eq '^MINER_ADDRESS="\$\{MONZERO_MINER_ADDRESS:-[^}]+' start-monzero-miner.sh; then
  echo "User-facing branding gate failed: mining launcher contains a default reward address" >&2
  fail=true
fi
if grep -Eq 'Documents/Wallet|Wallet1' start-monzero-wallet-cli.sh; then
  echo "User-facing branding gate failed: wallet launcher contains a developer-specific wallet path" >&2
  fail=true
fi

if $fail; then
  exit 1
fi

echo "User-facing branding gate passed"
