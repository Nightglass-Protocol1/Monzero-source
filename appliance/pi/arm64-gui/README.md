# 64-bit ARM wallet (linux-aarch64-gui)

The graphical wallet with its exchange for Raspberry Pi (Raspberry Pi OS trixie / Debian 13, arm64),
built natively in an emulated arm64 Debian 13 container and linked against the system's Qt 5.15 and
Boost 1.83 (their runtime packages: `runtime-packages.txt`, installed by the Pi image). First built
for v1.3.8 on 24 Nov 2026 (work folder `build/arm64-gui-<version>`).

1. `helper-arm.sh`: the DEX helper cross-built for aarch64 (rust:1.95-bullseye, like the relay).
2. `build-in-container.sh` in `podman run --arch arm64 ... debian:trixie` with a copy of the source at
   `/w/src` and the git directory mounted (GIT_DIR/GIT_WORK_TREE), helper in `/w/helpers` (several hours).
3. `pack-and-smoke.sh` in an arm64 Debian 13 container: packages with `package-linux-gui-aarch64.sh`
   (Monero's official linux-armv8 monero-wallet-rpc bundled, checked against its signed hash list),
   then starts the packaged wallet on a virtual screen and checks the DEX page link.

The BTC/LTC helpers are not built for ARM (those pairs are not public). A build of the tagged release
commit names itself `<version>-release`; the packager accepts that only when the tag points at it.
