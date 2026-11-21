#!/bin/bash
# Builds the Monzero appliance VM image (x86_64, UEFI): raw disk + qcow2 (QEMU/Proxmox/KVM) +
# VMDK/OVA-ready disk (VirtualBox/VMware). Rootless: podman + mkfs.ext4 -d + mtools.
#
#   MONZERO_VERSION=1.3.6 bash appliance/vm/build-vm.sh [out dir]
#
# Contents: Debian 13, the signed Monzero release (installed through monzero-update), the DEX
# relay built from this checkout, the setup page, firewall, automatic updates. Nothing secret
# or unique is in the image: keys and the setup code are made on the first boot.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)              # appliance/
REPO=$(cd "$HERE/.." && pwd)
OUT=${1:-$HERE/out}
VERSION=${MONZERO_VERSION:?set MONZERO_VERSION (the signed release to put in the image)}
SIZE_MB=${SIZE_MB:-6144}                            # grows to the whole disk on first boot
ESP_MB=256
TAG=monzero-appliance-rootfs:vm-$VERSION
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
mkdir -p "$OUT"
W=$(mktemp -d); trap 'podman unshare rm -rf "$W"' EXIT

echo "== 1. DEX relay from this checkout ($(git -C "$REPO" rev-parse --short HEAD))"
T=$ROOT/core/build/dex-rust-toolchain-1.95.0/sysroot/usr
( cd "$REPO/gui/dex-contracts/relay" && PATH=$T/bin:$PATH LD_LIBRARY_PATH=$T/lib/x86_64-linux-gnu CARGO_HOME=$ROOT/core/build/dex-cargo-home \
    cargo build --offline --release 2>&1 | tail -1 )
mkdir -p "$W/ctx"
cp "$REPO/gui/dex-contracts/relay/target/release/monzero-dex-relay" "$W/ctx/"
cp -a "$HERE/rootfs" "$W/ctx/overlay"
cp "$HERE/build/setup-rootfs.sh" "$HERE/vm/grub.cfg" "$W/ctx/"

echo "== 2. system (Debian 13 + Monzero $VERSION)"
cat > "$W/ctx/Containerfile" <<'EOF'
FROM docker.io/library/debian:trixie
ARG MONZERO_VERSION
COPY . /build
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends initramfs-tools grub-efi-amd64-bin grub-common >/dev/null \
 && MONZERO_VERSION=$MONZERO_VERSION bash /build/setup-rootfs.sh /build/overlay /build/monzero-dex-relay vm \
 && echo "monzero-appliance $MONZERO_VERSION" > /etc/monzero-root-marker \
 && install -d /boot/grub && cp /build/grub.cfg /boot/grub/grub.cfg \
 && printf 'PARTLABEL=monzero-root / ext4 errors=remount-ro,noatime 0 1\nPARTLABEL=monzero-esp /boot/efi vfat umask=0077 0 2\n' > /etc/fstab \
 && install -d /boot/efi \
 && rm -rf /build
EOF
podman build -q --build-arg MONZERO_VERSION="$VERSION" -t "$TAG" "$W/ctx" >/dev/null
CID=$(podman create "$TAG" true)
podman export "$CID" > "$W/rootfs.tar"; podman rm "$CID" >/dev/null

echo "== 3. disk image"
# Unpack and make the ext4 inside the user namespace, so files keep their real owners
podman unshare bash -c "
  set -e; mkdir '$W/root'; tar -xpf '$W/rootfs.tar' -C '$W/root' --numeric-owner
  rm -f '$W/root/.containerenv' '$W/root/run/.containerenv'
  KVER=\$(ls '$W/root/lib/modules' | sort -V | tail -1)
  cp '$W/root/boot/vmlinuz-'\$KVER '$W/vmlinuz'; cp '$W/root/boot/initrd.img-'\$KVER '$W/initrd.img'
  ROOT_MB=\$(( $SIZE_MB - $ESP_MB - 2 ))
  mkfs.ext4 -q -L monzero-root -d '$W/root' '$W/root.ext4' \${ROOT_MB}M
"
# EFI system partition: GRUB (standalone, finds the root by its marker file)
cat > "$W/early.cfg" <<'EOF'
search --no-floppy --file --set=root /etc/monzero-root-marker
set prefix=($root)/boot/grub
configfile ($root)/boot/grub/grub.cfg
EOF
podman run --rm -v "$W:/w" "$TAG" grub-mkimage -O x86_64-efi -o /w/BOOTX64.EFI -p /boot/grub -c /w/early.cfg \
  part_gpt part_msdos fat ext2 search search_fs_file normal configfile linux echo test all_video gfxterm efi_gop efi_uga gzio
mkfs.vfat -n MONZERO-ESP -C "$W/esp.img" $(( ESP_MB * 1024 )) >/dev/null
mmd -i "$W/esp.img" ::/EFI ::/EFI/BOOT
mcopy -i "$W/esp.img" "$W/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
# Whole disk: GPT with the two partitions
IMG=$OUT/monzero-appliance-$VERSION-x86_64.raw
truncate -s ${SIZE_MB}M "$IMG"
sfdisk -q "$IMG" <<EOF
label: gpt
start=2048, size=$(( ESP_MB * 2048 )), type=uefi, name=monzero-esp
start=$(( 2048 + ESP_MB * 2048 )), type=linux, name=monzero-root
EOF
dd if="$W/esp.img" of="$IMG" bs=1M seek=1 conv=notrunc,sparse status=none
dd if="$W/root.ext4" of="$IMG" bs=1M seek=$(( 1 + ESP_MB )) conv=notrunc,sparse status=none

echo "== 4. formats"
if command -v qemu-img >/dev/null; then QI="qemu-img"; else QI="podman run --rm -v $OUT:$OUT $TAG qemu-img"; fi
if ! command -v qemu-img >/dev/null && ! podman run --rm "$TAG" sh -c 'command -v qemu-img' >/dev/null 2>&1; then
  podman build -q -t monzero-qemu-img - <<'EOF' >/dev/null
FROM docker.io/library/debian:trixie
RUN apt-get update -qq && apt-get install -y -qq qemu-utils >/dev/null
EOF
  QI="podman run --rm -v $OUT:$OUT monzero-qemu-img qemu-img"
fi
$QI convert -O qcow2 -c "$IMG" "${IMG%.raw}.qcow2"
$QI convert -O vmdk -o subformat=streamOptimized "$IMG" "${IMG%.raw}.vmdk"
( cd "$OUT" && sha256sum "$(basename "${IMG%.raw}").qcow2" "$(basename "${IMG%.raw}").vmdk" "$(basename "$IMG")" > "monzero-appliance-$VERSION-x86_64.SHA256SUMS" )
ls -la "$OUT"
echo "Test it:  qemu-system-x86_64 -enable-kvm -m 4096 -smp 2 -bios /usr/share/edk2/x64/OVMF.4m.fd -drive file=${IMG%.raw}.qcow2,if=virtio -nic user,hostfwd=tcp::8443-:8443"
