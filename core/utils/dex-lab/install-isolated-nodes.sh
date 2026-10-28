#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root" >&2
    exit 1
fi

install -m 0755 -o root -g root /srv/monzero-dex/source/bitcoin-31.1/bin/bitcoind /srv/monzero-dex/bin/bitcoind
install -m 0755 -o root -g root /srv/monzero-dex/source/bitcoin-31.1/bin/bitcoin-cli /srv/monzero-dex/bin/bitcoin-cli
chown root:root /srv/monzero-dex/bin/monzerod /srv/monzero-dex/bin/monzero-wallet-cli /srv/monzero-dex/bin/monzero-wallet-rpc
chmod 0755 /srv/monzero-dex/bin/monzero*

install -m 0640 -o root -g monzero /srv/monzero-dex/source/lab-kit/bitcoin-regtest.conf /srv/monzero-dex/config/bitcoin-regtest.conf
install -m 0644 -o root -g root /srv/monzero-dex/source/lab-kit/bitcoin-regtest.service /etc/systemd/system/bitcoin-regtest.service
install -m 0644 -o root -g root /srv/monzero-dex/source/lab-kit/monzero-regtest.service /etc/systemd/system/monzero-regtest.service
install -m 0644 -o root -g root /srv/monzero-dex/source/lab-kit/monzero-wallet-regtest.service /etc/systemd/system/monzero-wallet-regtest.service
install -d -m 0700 -o monzero -g monzero /srv/monzero-dex/recovery/wallets
install -d -m 0700 -o monzero -g monzero /srv/monzero-dex/recovery/shared-ringdb

systemd-analyze verify /etc/systemd/system/bitcoin-regtest.service /etc/systemd/system/monzero-regtest.service
systemctl daemon-reload
systemctl enable --now bitcoin-regtest.service monzero-regtest.service
systemctl enable --now monzero-wallet-regtest.service
echo "Isolated node services installed"
