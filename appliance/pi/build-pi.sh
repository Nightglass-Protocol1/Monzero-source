#!/bin/bash
# Builds the Monzero appliance image for Raspberry Pi 4/5 (and 3B+), 64-bit:
# Raspberry Pi OS Lite (Debian 13 based; the Pi's own kernel and firmware, kept up to date by
# its own updates) + the same Monzero system as the VM image (setup page, firewall, signed
# Monzero updates, wallet on the HDMI screen or in the browser).
#
#   MONZERO_VERSION=1.3.7 bash appliance/pi/build-pi.sh <raspios-lite-arm64.img.xz> [out dir]
#
# Needs on the build PC: podman, and arm64 emulation for containers (Arch: qemu-user-static +
# qemu-user-static-binfmt; Debian/Ubuntu: qemu-user-static binfmt-support). The Monzero release
# must carry a linux-aarch64-gui artifact (monzero-update installs it, signature checked).
# Flash the result with Raspberry Pi Imager or: xzcat <img.xz> | sudo dd of=/dev/sdX bs=4M
# A 64 GB card or better a USB SSD: the XMZ chain is small, but the wallet and logs grow.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); REPO=$(cd "$HERE/.." && pwd)
SRC=${1:?usage: build-pi.sh <raspios-lite-arm64.img.xz> [out dir]}
OUT=${2:-$HERE/out}
VERSION=${MONZERO_VERSION:?set MONZERO_VERSION}
TAG=monzero-appliance-rootfs:pi-$VERSION
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
mkdir -p "$OUT"
W=$(mktemp -d); trap 'podman unshare rm -rf "$W"' EXIT

podman run --rm --platform linux/arm64 docker.io/library/debian:trixie true 2>/dev/null \
  || { echo "!! no arm64 emulation for containers on this PC (install qemu-user-static + binfmt)"; exit 1; }
[ -n "${RELAY_BIN_ARM64:-}" ] || echo "note: RELAY_BIN_ARM64 not set: the image gets no relay until a signed relay release exists"

echo "== 1. Raspberry Pi OS image"
# Check it against the published SHA-256 if one is next to it (<img>.sha256), as downloaded from raspberrypi.com
if [ -f "$SRC.sha256" ]; then (cd "$(dirname "$SRC")" && sha256sum -c "$(basename "$SRC").sha256"); else echo "note: no $SRC.sha256 to check against"; fi
xz -dc "$SRC" > "$W/base.img"
read -r BOOT_START BOOT_SIZE < <(sfdisk -d "$W/base.img" | awk -F'[=,]' '/start=/{print $2, $4; exit}')
read -r ROOT_START < <(sfdisk -d "$W/base.img" | awk -F'[=,]' '/start=/{n++; if(n==2){print $2; exit}}')
dd if="$W/base.img" of="$W/boot.vfat" bs=512 skip="$BOOT_START" count="$BOOT_SIZE" status=none
dd if="$W/base.img" of="$W/root.ext4" bs=512 skip="$ROOT_START" status=none

echo "== 2. its root as a container image, then the Monzero system on top (arm64, emulated)"
podman unshare bash -c "
  set -e; mkdir '$W/root'
  debugfs -R 'rdump / $W/root' '$W/root.ext4' >/dev/null 2>&1
  tar -C '$W/root' --numeric-owner -cf '$W/root.tar' .
"
podman import --platform linux/arm64 "$W/root.tar" monzero-raspios-base:$VERSION >/dev/null
mkdir -p "$W/ctx"; cp -a "$HERE/rootfs" "$W/ctx/overlay"; cp "$HERE/build/setup-rootfs.sh" "$W/ctx/"
[ -n "${RELAY_BIN_ARM64:-}" ] && cp "$RELAY_BIN_ARM64" "$W/ctx/monzero-dex-relay" || : > "$W/ctx/monzero-dex-relay"
cat > "$W/ctx/Containerfile" <<EOF
FROM monzero-raspios-base:$VERSION
ARG MONZERO_VERSION
COPY . /build
RUN RELAY=/build/monzero-dex-relay; [ -s \$RELAY ] || RELAY=""; \
    MONZERO_VERSION=\$MONZERO_VERSION bash /build/setup-rootfs.sh /build/overlay "\$RELAY" pi \
 && systemctl disable userconfig.service sshswitch.service 2>/dev/null || true \
 && rm -rf /build
EOF
podman build -q --platform linux/arm64 --build-arg MONZERO_VERSION="$VERSION" -t "$TAG" "$W/ctx" >/dev/null
CID=$(podman create --platform linux/arm64 "$TAG" true); podman export "$CID" > "$W/new-root.tar"; podman rm "$CID" >/dev/null

echo "== 3. image"
SIZE_MB=${SIZE_MB:-8192}
podman unshare bash -c "
  set -e; rm -rf '$W/root'; mkdir '$W/root'; tar -xpf '$W/new-root.tar' -C '$W/root' --numeric-owner
  rm -f '$W/root/.containerenv'
  echo monzero-box > '$W/root/etc/hostname'
  printf '127.0.0.1\tlocalhost\n127.0.1.1\tmonzero-box\n::1\tlocalhost ip6-localhost ip6-loopback\n' > '$W/root/etc/hosts'
  rm -f '$W/root/etc/resolv.conf'   # NetworkManager writes it
  mkfs.ext4 -q -L rootfs -d '$W/root' '$W/root-new.ext4' $(( SIZE_MB - BOOT_SIZE / 2048 - 8 ))M
"
# The boot partition: Raspberry Pi OS's own, with the first-boot user wizard turned off (the
# Monzero setup page replaces it) and the console on HDMI
mcopy -o -i "$W/boot.vfat" ::/cmdline.txt "$W/cmdline.txt"
sed -i 's| init=/usr/lib/raspberrypi-sys-mods/firstboot||; s| quiet||' "$W/cmdline.txt"
mcopy -o -i "$W/boot.vfat" "$W/cmdline.txt" ::/cmdline.txt
IMG=$OUT/monzero-appliance-$VERSION-raspberrypi-arm64.img
truncate -s ${SIZE_MB}M "$IMG"
sfdisk -q "$IMG" <<EOF
label: dos
start=$BOOT_START, size=$BOOT_SIZE, type=c
start=$(( BOOT_START + BOOT_SIZE )), type=83
EOF
dd if="$W/boot.vfat" of="$IMG" bs=512 seek="$BOOT_START" conv=notrunc status=none
dd if="$W/root-new.ext4" of="$IMG" bs=512 seek=$(( BOOT_START + BOOT_SIZE )) conv=notrunc,sparse status=none
# Raspberry Pi OS finds its root by PARTUUID (disk id + partition number): keep the disk id
DISKID=$(sfdisk --disk-id "$W/base.img"); sfdisk -q --disk-id "$IMG" "$DISKID"
xz -T0 -6 -f "$IMG"
( cd "$OUT" && sha256sum "$(basename "$IMG").xz" > "$(basename "$IMG").xz.sha256" )
ls -la "$OUT" | grep raspberrypi
