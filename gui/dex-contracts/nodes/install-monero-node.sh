#!/bin/bash
# Our own Monero node(s) for the DEX: the XMR trade wallets, the witness check and the apps use
# these instead of public nodes. Installs the official Monero release (signature and checksum
# verified), runs monerod under systemd as its own user, and opens only the node's ports.
#
# Run as root on the node server (Debian/Ubuntu), from the owner's PC for example:
#   scp install-monero-node.sh SERVER: && ssh SERVER sudo bash install-monero-node.sh mainnet
#   ssh SERVER sudo bash install-monero-node.sh stagenet     # the test network (small, syncs in hours)
#   ssh SERVER sudo bash install-monero-node.sh status
#   ssh SERVER sudo bash install-monero-node.sh prune mainnet   # turn an existing full chain into a pruned one
# Safe to run again (an upgrade: set MONERO_VERSION to the new release).
#
# Ports (mainnet / stagenet):
#   18080 / 38080  peer-to-peer, open to all (helps the network)
#   18089 / 38089  restricted RPC, open to all: wallets and the DEX use it; it cannot control the node
#   18081 / 38081  full RPC, this machine only
# Disk: nodes are PRUNED by default (mainnet about 100 GB instead of 300 GB and growing); a pruned
# node serves wallets and the DEX just as well. PRUNE=0 for a full node on a new install.
# `prune <net>` converts an existing full chain: it stops the node, writes a pruned copy with the
# official monero-blockchain-prune, swaps it in, starts the node, and deletes the full copy only
# after the node answers as pruned. Needs about 110 GB free (mainnet); the node is down meanwhile.
set -euo pipefail
VERSION=${MONERO_VERSION:-v0.18.5.4}
PRUNE=${PRUNE:-1}
NET=${1:-}
# binaryFate signs the Monero release hashes (the key in the monero repository, utils/gpg_keys)
SIGNER_FPR=81AC591FE9C4B65C5806AFC3F0AF4D462A0BDF92
HASHES_URL=https://www.getmonero.org/downloads/hashes.txt
KEY_URL=https://raw.githubusercontent.com/monero-project/monero/master/utils/gpg_keys/binaryfate.asc
TARBALL=monero-linux-x64-$VERSION.tar.bz2

# get_info has no pruning field; ask the node itself (full RPC, this machine)
pruned_state() { curl -s -m 10 http://127.0.0.1:$1/json_rpc -d '{"jsonrpc":"2.0","id":0,"method":"prune_blockchain","params":{"check":true}}' | python3 -c 'import sys,json; print("pruned" if json.load(sys.stdin)["result"]["pruned"] else "full")' 2>/dev/null || echo unknown; }

case "$NET" in
  mainnet)  P2P=18080; RPC=18081; RRPC=18089; FLAG="" ;;
  stagenet) P2P=38080; RPC=38081; RRPC=38089; FLAG="stagenet=1" ;;
  status)
    for n in mainnet stagenet; do
      systemctl is-active --quiet "monerod@$n" 2>/dev/null || continue
      port=$([ $n = mainnet ] && echo 18081 || echo 38081)
      echo "== $n: $(systemctl is-active monerod@$n)"
      curl -s -m 10 http://127.0.0.1:$port/get_info | python3 -c 'import sys,json; i=json.load(sys.stdin); print("height %s of %s, synced %s, peers in %s out %s" % (i["height"], i["target_height"] or i["height"], i["synchronized"], i["incoming_connections_count"], i["outgoing_connections_count"]))' || echo "   (not answering yet)"
      echo "   chain: $(pruned_state $port)"
    done
    df -h /var/lib/monero | tail -1
    exit 0;;
  prune)
    PNET=${2:-}; case "$PNET" in mainnet) PP=18081; PF="";; stagenet) PP=38081; PF="--stagenet";; *) echo "usage: $0 prune mainnet|stagenet"; exit 2;; esac
    [ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
    [ -x /opt/monero/monero-blockchain-prune ] || { echo "monero-blockchain-prune missing: run  $0 $PNET  first (installs it)"; exit 1; }
    CONF=/etc/monero/$PNET.conf; D=/var/lib/monero/$PNET
    # monerod keeps the stagenet chain one folder deeper (<data-dir>/stagenet/lmdb); the prune tool does the same
    DB=$D; [ "$PNET" = stagenet ] && DB=$D/stagenet
    [ -f "$DB/lmdb/data.mdb" ] || { echo "!! no chain at $DB/lmdb"; exit 1; }
    [ ! -e "$DB/lmdb-old" ] || { echo "!! $DB/lmdb-old exists (an earlier run?): check and remove it first"; exit 1; }
    set_flag() { grep -q '^prune-blockchain=1' "$CONF" || sed -i 's/^db-sync-mode=/prune-blockchain=1\ndb-sync-mode=/' "$CONF"; grep -q '^prune-blockchain=1' "$CONF" || echo 'prune-blockchain=1' >> "$CONF"; }
    if [ "$(pruned_state $PP)" = pruned ]; then echo "$PNET is already pruned"; set_flag; exit 0; fi
    FULL_GB=$(du -s --block-size=1G "$DB/lmdb" | cut -f1); [ -n "$FULL_GB" ] || exit 1
    NEED_GB=$(( FULL_GB * 38 / 100 + 10 ))
    FREE_GB=$(df -BG --output=avail "$D" | tail -1 | tr -dc 0-9)
    [ "$FREE_GB" -ge "$NEED_GB" ] || { echo "!! need about $NEED_GB GB free, have $FREE_GB"; exit 1; }
    echo "== stopping monerod@$PNET ($(date -u +%H:%M) UTC)"
    systemctl stop "monerod@$PNET"
    echo "== writing the pruned copy (mainnet: a few hours)"
    # On success the tool itself swaps the pruned copy in as lmdb and keeps the full one as lmdb-old.
    if ! runuser -u monero -- /opt/monero/monero-blockchain-prune $PF --data-dir "$D" --log-level 0 || [ ! -d "$DB/lmdb-old" ]; then
      echo "!! pruning failed; starting the node on the chain it has"; systemctl start "monerod@$PNET"; exit 1
    fi
    set_flag
    systemctl start "monerod@$PNET"
    # After the swap the node walks the whole database once more and answers no RPC meanwhile
    # (mainnet: about an hour), so wait for the pruned answer itself, up to four hours.
    echo "== waiting for the node to answer as pruned (mainnet: up to an hour)"
    STATE=unknown
    for i in $(seq 1 240); do STATE=$(pruned_state $PP); [ "$STATE" = unknown ] || break; sleep 60; done
    if [ "$STATE" = pruned ]; then
      rm -rf "$DB/lmdb-old"
      echo "done: $PNET pruned, full copy deleted"; du -sh "$DB/lmdb"; df -h "$D" | tail -1
    else
      echo "!! the node does not answer as pruned; the full copy is kept in $DB/lmdb-old"
      echo "   to go back: systemctl stop monerod@$PNET; mv $DB/lmdb $DB/lmdb-pruned; mv $DB/lmdb-old $DB/lmdb; remove prune-blockchain from $CONF; systemctl start monerod@$PNET"
      exit 1
    fi
    exit 0;;
  *) sed -n '2,25p' "$0"; exit 2;;
esac
case "$NET" in mainnet|stagenet) ;; *) echo "!! unknown network $NET"; exit 2;; esac
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
[ "$(uname -m)" = x86_64 ] || { echo "this installs the x86_64 release"; exit 1; }

echo "== 1. packages"
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq curl gnupg bzip2 ca-certificates ufw >/dev/null

echo "== 2. Monero $VERSION, verified"
if [ "$(/opt/monero/monerod --version 2>/dev/null | grep -o 'v[0-9.]*' | head -1)" = "$VERSION" ] && [ -x /opt/monero/monero-blockchain-prune ]; then
  echo "already installed"
else
  W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
  export GNUPGHOME=$W/gnupg; mkdir -m 700 "$GNUPGHOME"
  curl -fsSL "$KEY_URL" -o "$W/key.asc"
  gpg -q --import "$W/key.asc"
  gpg --list-keys --with-colons | grep -q "fpr:::::::::$SIGNER_FPR:" || { echo "!! the signing key is not binaryFate's ($SIGNER_FPR)"; exit 1; }
  curl -fsSL "$HASHES_URL" -o "$W/hashes.txt"
  gpg -q --status-fd 1 --verify "$W/hashes.txt" 2>/dev/null | grep -q "VALIDSIG $SIGNER_FPR" || { echo "!! hashes.txt is not signed by binaryFate"; exit 1; }
  WANT=$(grep -E "^[0-9a-f]{64}  $TARBALL\$" "$W/hashes.txt" | cut -d' ' -f1)
  [ -n "$WANT" ] || { echo "!! $TARBALL is not in the signed hashes (is $VERSION the current release?)"; exit 1; }
  curl -fSL --progress-bar "https://downloads.getmonero.org/cli/$TARBALL" -o "$W/$TARBALL"
  echo "$WANT  $W/$TARBALL" | sha256sum -c --quiet || { echo "!! checksum mismatch"; exit 1; }
  tar -xjf "$W/$TARBALL" -C "$W"
  install -d /opt/monero
  install -m 755 "$W"/monero-x86_64-linux-gnu-*/monerod /opt/monero/monerod
  install -m 755 "$W"/monero-x86_64-linux-gnu-*/monero-blockchain-prune /opt/monero/monero-blockchain-prune
  echo "installed $(/opt/monero/monerod --version | head -1)"
fi

echo "== 3. user, folders, config"
id monero >/dev/null 2>&1 || useradd --system --home-dir /var/lib/monero --shell /usr/sbin/nologin monero
install -d -o monero -g monero -m 750 /var/lib/monero/$NET /var/log/monero
install -d -m 755 /etc/monero
CONF=/etc/monero/$NET.conf
if [ ! -f "$CONF" ]; then
  cat > "$CONF" <<EOF
# Monzero's own Monero node ($NET). Written by install-monero-node.sh; edit and restart monerod@$NET.
$FLAG
data-dir=/var/lib/monero/$NET
log-file=/var/log/monero/$NET.log
log-level=0
max-log-file-size=10000000
max-log-files=5
$( [ "$PRUNE" = 1 ] && printf 'prune-blockchain=1\nsync-pruned-blocks=1' )
db-sync-mode=safe:sync
p2p-bind-ip=0.0.0.0
p2p-bind-port=$P2P
no-igd=1
out-peers=32
in-peers=64
enable-dns-blocklist=1
# Full RPC: this machine only
rpc-bind-ip=127.0.0.1
rpc-bind-port=$RPC
# Restricted RPC for wallets, the DEX and anyone (cannot stop or reconfigure the node)
rpc-restricted-bind-ip=0.0.0.0
rpc-restricted-bind-port=$RRPC
confirm-external-bind=1
public-node=1
# Each DEX user runs several wallets (main + trade wallets) from one address; the default (3) refuses the rest
rpc-max-connections-per-public-ip=16
EOF
  echo "wrote $CONF (pruned: $PRUNE)"
else
  echo "keeping $CONF"
fi

cat > /etc/systemd/system/monerod@.service <<'EOF'
[Unit]
Description=Monero node (%i)
After=network-online.target
Wants=network-online.target

[Service]
User=monero
Group=monero
ExecStart=/opt/monero/monerod --config-file=/etc/monero/%i.conf --non-interactive
Restart=on-failure
RestartSec=30
TimeoutStopSec=300
LimitNOFILE=65536
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
ReadWritePaths=/var/lib/monero /var/log/monero

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now "monerod@$NET"
systemctl restart "monerod@$NET"

echo "== 4. firewall"
if ufw status | grep -q "Status: active"; then
  ufw allow "$P2P/tcp" comment "monero $NET p2p" >/dev/null
  ufw allow "$RRPC/tcp" comment "monero $NET restricted rpc" >/dev/null
  echo "ufw: $P2P and $RRPC open"
else
  echo "ufw is not active: make sure SSH is allowed first, then:  ufw allow OpenSSH && ufw allow $P2P/tcp && ufw allow $RRPC/tcp && ufw enable"
fi
echo "Also open TCP $P2P and $RRPC in the IONOS Cloud Panel firewall (Network > Firewall Policies)."

sleep 5
systemctl is-active "monerod@$NET"
echo
echo "Started. Syncing takes $([ $NET = mainnet ] && echo 'one to three days' || echo 'a few hours'). Progress:  sudo bash $0 status"
echo "DEX/wallet address once synced:  http://<this server's IP or name>:$RRPC"
