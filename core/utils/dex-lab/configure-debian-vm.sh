#!/usr/bin/env bash
# Initial configuration for the disposable Debian DEX integration VM.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root" >&2
    exit 1
fi

hostnamectl set-hostname monzero-dex-lab

export DEBIAN_FRONTEND=noninteractive
apt-get update -o APT::Update::Error-Mode=any
apt-get -y --no-remove -o Dpkg::Options::=--force-confold dist-upgrade
apt-get install -y --no-install-recommends \
    bash-completion build-essential ca-certificates chrony cmake curl \
    dnsutils fail2ban git jq libsodium-dev libssl-dev nftables ninja-build \
    pkg-config podman python3 python3-venv qemu-guest-agent rsync \
    slirp4netns uidmap unattended-upgrades

install -d -m 0750 -o monzero -g monzero \
    /srv/monzero-dex \
    /srv/monzero-dex/bin \
    /srv/monzero-dex/config \
    /srv/monzero-dex/data \
    /srv/monzero-dex/data/bitcoin-regtest \
    /srv/monzero-dex/data/monzero-fakechain \
    /srv/monzero-dex/logs \
    /srv/monzero-dex/source
install -d -m 0700 -o monzero -g monzero /srv/monzero-dex/recovery

install -d -m 0755 /etc/ssh/sshd_config.d
install -m 0644 /dev/stdin /etc/ssh/sshd_config.d/60-monzero-dex-lab.conf <<'EOF'
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin no
PubkeyAuthentication yes
AllowUsers monzero
X11Forwarding no
AllowAgentForwarding no
PermitTunnel no
MaxAuthTries 3
LoginGraceTime 30
EOF
sshd -t

install -m 0644 /dev/stdin /etc/sysctl.d/60-monzero-dex-lab.conf <<'EOF'
kernel.dmesg_restrict = 1
kernel.kptr_restrict = 2
kernel.unprivileged_bpf_disabled = 1
fs.protected_fifos = 2
fs.protected_regular = 2
net.ipv4.conf.all.accept_redirects = 0
net.ipv4.conf.default.accept_redirects = 0
net.ipv4.conf.all.send_redirects = 0
net.ipv4.conf.default.send_redirects = 0
EOF
sysctl --system >/dev/null

systemctl enable --now qemu-guest-agent chrony fail2ban fstrim.timer
systemctl reload ssh

# Passwordless administration is intentionally not enabled. Proxmox guest-agent
# access remains the recovery channel until a limited sudo policy is reviewed.
touch /srv/monzero-dex/ISOLATED_TEST_FUNDS_ONLY
chown monzero:monzero /srv/monzero-dex/ISOLATED_TEST_FUNDS_ONLY
chmod 0440 /srv/monzero-dex/ISOLATED_TEST_FUNDS_ONLY

echo "DEX lab base configuration complete"
