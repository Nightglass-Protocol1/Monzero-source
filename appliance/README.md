# Monzero Box

Ready-made Monzero systems: download, start, open a page in your browser, done. The same box
comes in three forms:

| Form | File | For |
|---|---|---|
| Docker | `monzero/node`, `monzero/relay`, `monzero/tor`, `monzero/wallet` images + `docker/compose.yml` | a server or PC that already runs Docker or Podman |
| Virtual machine | `monzero-appliance-<version>-x86_64.qcow2` (Proxmox, KVM, QEMU) and `.vmdk` (VirtualBox, VMware) | a home server or a PC |
| Raspberry Pi | `monzero-appliance-<version>-raspberrypi-arm64.img.xz` | Raspberry Pi 4 or 5 (64 GB card or USB SSD) |

What a box can run (you choose on the setup page):

- **XMZ node**: a full Monzero node. It helps the network and your wallets use it.
- **DEX relay**: carries the DEX's signed offers. It holds no funds and no keys. By default it
  is **shared with the Monzero network through Tor**: other relays pick up the offers it
  carries, with no router setup and without showing your home IP. You can share it through a
  router port instead, or keep it private.
- **Wallet + DEX app**: the desktop wallet with the Exchange page, on the box's own screen
  (HDMI) or in your browser (remote screen, off unless you turn it on). Not on Raspberry Pi
  until a 64-bit ARM wallet app is released.

## Virtual machine

1. Import the `.qcow2` (or `.vmdk`) as the VM's disk, **UEFI** firmware, 2 CPUs, 4 GB memory,
   a disk of 64 GB or more (the image grows to fill it on first start), network: bridged.
2. Start it. The console shows the box's address, a **one-time setup code** and the setup
   page's certificate fingerprint.
3. On a PC in the same network open `https://<address>:8443`. Your browser warns about the
   certificate: compare its SHA-256 fingerprint with the console's, then continue.
4. Enter the setup code, choose a password (12 characters or more) and what the box should run.

## Raspberry Pi

Flash the image with Raspberry Pi Imager ("Use custom") or
`xzcat monzero-appliance-*.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync`. Connect a network
cable and, for the setup code, a screen. Then as steps 3 and 4 above.

## Docker

    docker compose up -d                       # node + relay + Tor (relay shared over Tor)
    docker compose up -d node relay            # without sharing the relay
    docker compose --profile wallet up -d      # also the wallet, at https://127.0.0.1:6080/vnc.html
    docker compose logs wallet                 # the wallet screen's password (first start)

Containers run as an unprivileged user with read-only file systems, no Linux capabilities and
no new privileges. The node's wallet RPC is reachable only by the wallet container.

## Updates

- **Monzero**: once a day the box asks monzero.org for the newest release and installs it only
  if its release file carries a valid signature by the Monzero release key built into the
  box (fingerprint `4C37 C618 789F 86D8 1FEF 28DF 390A EFB7 35F2 70D6`) and every file matches
  its size and SHA-256 there. It never installs an older version. If the node does not come
  back within two minutes, the previous version is put back. A running wallet is never
  restarted; it uses the new version the next time it starts. The status page has
  "Check now".
- **Operating system**: Debian / Raspberry Pi OS security updates install by themselves; when
  a kernel update needs it, the box restarts at 04:30.
- **Docker**: `docker/monzero-docker-update.sh` (daily timer) rebuilds the images from the
  newest signed release and restarts the node and relay.

## Ports

| Port | What | Open to |
|---|---|---|
| 6174 | XMZ peer-to-peer | everyone (rate-limited): that is how a node helps the network |
| 8443 | setup and status page (HTTPS) | your home network only |
| 6177 | node RPC for wallets (restricted) | your home network only |
| 8790 | DEX relay | your home network; the internet only if you share it by router port |
| 6080 | wallet remote screen (HTTPS) | your home network, only while turned on |
| 22 | SSH (keys only) | your home network, only after you add a key on the page |

## Building the images

    MONZERO_VERSION=1.3.6 bash appliance/vm/build-vm.sh [out]          # VM (rootless podman)
    bash appliance/docker/build-images.sh 1.3.6                         # Docker images
    MONZERO_VERSION=1.3.7 bash appliance/pi/build-pi.sh <raspios-lite-arm64.img.xz> [out]

See SECURITY.md for what the box protects and how.
