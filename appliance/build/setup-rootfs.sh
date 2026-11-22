#!/bin/bash
# Turns a fresh Debian 13 (trixie) system into a Monzero appliance. Runs as root inside the
# image being built (container build for VM/Raspberry Pi images), never on a live box.
#   setup-rootfs.sh <overlay dir> <relay binary or ""> <kind: vm|pi>
# Installs the Monzero release named by MONZERO_VERSION through monzero-update itself, so the
# image gets exactly what the signed release file lists.
set -euo pipefail
OVERLAY=$1; RELAY_BIN=${2:-}; KIND=${3:-vm}
VERSION=${MONZERO_VERSION:?set MONZERO_VERSION}
export DEBIAN_FRONTEND=noninteractive

echo "== packages"
apt-get update -qq
PKGS=(systemd-sysv systemd-timesyncd systemd-resolved dbus sudo openssh-server ca-certificates curl gpgv python3
      nftables unattended-upgrades apt-listchanges cloud-guest-utils e2fsprogs openssl
      apparmor apparmor-utils apparmor-profiles
      wireguard-tools tor miniupnpc
      xserver-xorg-core xserver-xorg-input-libinput xinit openbox wmctrl x11-utils tint2 pcmanfm python3-tk python3-pil.imagetk x11-xserver-utils x11-xkb-utils locales
      libgl1 libegl1 libgl1-mesa-dri libglib2.0-0t64 libx11-xcb1 libxcb1 libxcb-glx0 libxcb-icccm4
      libxcb-image0 libxcb-shm0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-render0
      libxcb-shape0 libxcb-sync1 libxcb-xfixes0 libxcb-xkb1 libxkbcommon-x11-0 libxkbcommon0 libudev1
      libpcre2-8-0 fonts-dejavu-core fonts-wqy-microhei
      x11vnc novnc python3-websockify python3-pil hsetroot kbd)
case "$KIND" in
  vm) PKGS+=(linux-image-amd64 systemd-boot systemd-boot-efi qemu-guest-agent xserver-xorg-video-qxl xserver-xorg-video-vmware) ;;
  pi) PKGS+=(wpasupplicant) ;;   # Raspberry Pi OS brings its own kernel, firmware and network setup
esac
apt-get install -y -qq --no-install-recommends "${PKGS[@]}" >/dev/null

echo "== users"
id monzero >/dev/null 2>&1 || useradd --system --home-dir /var/lib/monzero --shell /usr/sbin/nologin monzero
id monzero-relay >/dev/null 2>&1 || useradd --system --home-dir /nonexistent --shell /usr/sbin/nologin monzero-relay
id monzero-web >/dev/null 2>&1 || useradd --system --home-dir /var/lib/monzero-web --shell /usr/sbin/nologin monzero-web
# The wallet user: logs in only on the box's own screen, no password, no SSH
id monzero-wallet >/dev/null 2>&1 || useradd --create-home --shell /bin/bash --groups video,render,input monzero-wallet
passwd -l monzero-wallet >/dev/null
# The admin user: SSH with a key only (set in the setup page), sudo with no password needed
# because it has no password at all; root stays locked
id monzero-admin >/dev/null 2>&1 || useradd --create-home --shell /bin/bash --groups sudo monzero-admin
passwd -l monzero-admin >/dev/null
echo 'monzero-admin ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/10-monzero-admin; chmod 440 /etc/sudoers.d/10-monzero-admin
passwd -l root >/dev/null

echo "== files"
cp -a "$OVERLAY"/. /
chown -R root:root /etc /usr/local /usr/share/monzero /usr/lib/python3/dist-packages/monzero_i18n.py
chmod 644 /usr/lib/python3/dist-packages/monzero_i18n.py
# the box's languages (monzero_i18n.LANGS), English until the owner picks one in setup
for loc in $(python3 -I -c 'import monzero_i18n as m; print(" ".join(v[1] for v in m.LANGS.values()))'); do
  sed -i "s/^# *\($loc UTF-8\)/\1/" /etc/locale.gen
done
locale-gen >/dev/null
echo "LANG=en_US.UTF-8" > /etc/default/locale
chmod 755 /usr/local/sbin/*
chmod 755 /usr/local/lib/monzero-wallet/{session,desktop,wallet,personalise,about,maximize-new}
gpg --dearmor < /usr/share/monzero/release-public.asc > /usr/share/monzero/release-keyring.gpg 2>/dev/null \
  || python3 -I - <<'PY'
# gpgv reads an armored export too once dearmored; without gpg, strip the armor by hand
import base64, re
t = open("/usr/share/monzero/release-public.asc").read()
body = re.search(r"\n\n(.*?)\n=", t, re.S).group(1)
open("/usr/share/monzero/release-keyring.gpg", "wb").write(base64.b64decode("".join(body.split())))
PY
install -d -o monzero-web -g monzero-web -m 750 /var/lib/monzero-web /var/lib/monzero-web/requests
install -d -m 755 /etc/monzero
[ -f /etc/monzero/appliance.json ] || echo '{"roles": [], "vnc": false, "sshkey": "", "configured": false}' > /etc/monzero/appliance.json
install -d -o monzero -g monzero -m 750 /var/lib/monzero /var/log/monzero

echo "== Monzero $VERSION (signed release, through monzero-update)"
if [ -n "${MONZERO_SKIP_RELEASE:-}" ]; then
  # Test builds only (checks the rest of the image): no Monzero installed, the box's daily
  # signed update installs it once the release carries this machine's artifact. Never ship.
  echo "!! TEST BUILD: Monzero release not installed (MONZERO_SKIP_RELEASE)"
else
  MONZERO_UPDATE_VERSION=$VERSION /usr/bin/python3 -I /usr/local/sbin/monzero-update
  [ "$(cat /opt/monzero/current/VERSION)" = "$VERSION" ]
fi
if [ -n "$RELAY_BIN" ]; then
  # Until releases carry a relay artifact: the relay built from the image's source commit,
  # as version 0.0.0 so the first signed relay release replaces it
  install -d /opt/monzero/relay/releases/0.0.0
  install -m 755 "$RELAY_BIN" /opt/monzero/relay/releases/0.0.0/monzero-dex-relay
  echo 0.0.0 > /opt/monzero/relay/releases/0.0.0/VERSION
  ln -sfn releases/0.0.0 /opt/monzero/relay/current
fi

echo monzero-box > /etc/hostname
printf '127.0.0.1	localhost
127.0.1.1	monzero-box
::1	localhost ip6-localhost ip6-loopback
' > /etc/hosts

echo "== services"
systemctl enable monzero-firstboot monzero-screen monzero-web monzero-apply.path monzero-update.timer monzero-public-relay.timer nftables unattended-upgrades apparmor systemd-timesyncd >/dev/null 2>&1
# VM: systemd-networkd (DHCP on any wired adapter); Raspberry Pi OS keeps its NetworkManager
[ "$KIND" = vm ] && systemctl enable systemd-networkd systemd-resolved >/dev/null 2>&1
[ "$KIND" = vm ] || rm -f /etc/systemd/network/20-wired.network
systemctl disable ssh monzerod monzero-dex-relay monzero-wallet-session monzero-wallet-vnc tor >/dev/null 2>&1 || true
systemctl set-default multi-user.target >/dev/null
# Firewall until setup: only the setup page, from your own network
/usr/local/sbin/monzero-firewall 2>/dev/null || true   # nft is not available inside the build; the file is what matters
[ -s /etc/nftables.conf ] && grep -q 8443 /etc/nftables.conf

echo "== tidy"
rm -f /etc/ssh/ssh_host_*          # made on first boot
: > /etc/machine-id                # made on first boot
rm -f /var/lib/dbus/machine-id
apt-get clean
rm -rf /var/lib/apt/lists/* /var/cache/monzero-update /tmp/* /root/.bash_history
echo "rootfs ready ($KIND, Monzero $VERSION)"
