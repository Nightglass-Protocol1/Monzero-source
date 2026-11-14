#!/bin/bash
# Install the 15-minute airdrop tick on monzero-eu and give the website its shared secret.
# Replaces the old cron jobs (airdrop-mining-check.php, airdrop-topup.sh): the webspace has no cron.
# Safe to re-run; keeps the secret. Run from the owner's PC:
#   bash utils/website/deploy-airdrop-tick.sh
# Afterwards:  ssh monzero-eu sudo systemctl start xmzairdrop-tick.service && ssh monzero-eu sudo journalctl -u xmzairdrop-tick -n 5
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
EU=${EU_HOST:-monzero-eu}
WEB=${WEB_HOST:-monzero-webspace}

echo "== installing the tick on $EU"
scp -q "$HERE/airdrop_tick.py" "$EU:/tmp/airdrop_tick.py"
ssh -o BatchMode=yes "$EU" sudo bash -s <<'REMOTE'
set -euo pipefail
id xmzpool >/dev/null 2>&1 || { echo "!! user xmzpool missing: is the pool installed here?"; exit 1; }
install -d -o root -g xmzpool -m 750 /etc/xmzairdrop
install -d -o root -g root -m 755 /opt/xmzairdrop
install -o root -g root -m 644 /tmp/airdrop_tick.py /opt/xmzairdrop/airdrop_tick.py && rm -f /tmp/airdrop_tick.py
umask 027; [ -s /etc/xmzairdrop/secret ] || openssl rand -hex 32 > /etc/xmzairdrop/secret
chown root:xmzpool /etc/xmzairdrop/secret; chmod 640 /etc/xmzairdrop/secret
cat > /etc/systemd/system/xmzairdrop-tick.service <<EOF2
[Unit]
Description=Monzero airdrop tick (mining days + budget) to monzero.org
After=network-online.target xmzpool.service
Wants=network-online.target
[Service]
Type=oneshot
# Runs as the pool user so it can open the pool database read-only (WAL needs the -shm file).
User=xmzpool
Group=xmzpool
ExecStart=/usr/bin/python3 -I /opt/xmzairdrop/airdrop_tick.py --secret-file /etc/xmzairdrop/secret --db /var/lib/xmzpool/pool.sqlite
TimeoutStartSec=120
NoNewPrivileges=yes
ProtectSystem=strict
ReadWritePaths=/var/lib/xmzpool
ProtectHome=yes
PrivateTmp=yes
EOF2
cat > /etc/systemd/system/xmzairdrop-tick.timer <<'EOF2'
[Unit]
Description=Monzero airdrop tick every 15 minutes
[Timer]
OnBootSec=5min
OnUnitActiveSec=15min
RandomizedDelaySec=60
[Install]
WantedBy=timers.target
EOF2
systemctl daemon-reload
systemctl enable --now xmzairdrop-tick.timer
REMOTE

echo "== shared secret -> website (~/monzero-private/airdrop-tick-secret)"
ssh -o BatchMode=yes "$EU" sudo cat /etc/xmzairdrop/secret | ssh -o BatchMode=yes "$WEB" \
  'umask 077; read -r s; case "$s" in *[!0-9a-f]*|"") echo "!! bad secret"; exit 1;; esac; [ ${#s} -eq 64 ] || { echo "!! bad secret"; exit 1; }
   printf "%s\n" "$s" > ~/monzero-private/airdrop-tick-secret.tmp && mv ~/monzero-private/airdrop-tick-secret.tmp ~/monzero-private/airdrop-tick-secret && echo "   website secret written"'
echo "done. First run: ssh $EU sudo systemctl start xmzairdrop-tick.service; ssh $EU sudo journalctl -u xmzairdrop-tick -n 5 --no-pager"
