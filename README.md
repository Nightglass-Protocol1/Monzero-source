# Monzero

Monzero (XMZ) is an independent, privacy-preserving digital currency mined with RandomX on ordinary CPUs. This repository holds the source code of every Monzero application:

| Folder | What it is | Based on |
| --- | --- | --- |
| [`core/`](core/) | `monzerod` node, `monzero-wallet-cli`, `monzero-wallet-rpc`, libraries and tests | Monero |
| [`gui/`](gui/) | Monzero desktop wallet (GUI) for Windows and Linux | Monero GUI |
| [`android/wallet/`](android/wallet/) | Monzero Wallet for Android | Monerujo |
| [`android/miner/`](android/miner/) | Monzero Miner for Android (RandomX CPU miner app) | XMRig |

Website: <https://monzero.org> · Whitepaper: [`core/WHITEPAPER.md`](core/WHITEPAPER.md) · Chain parameters: [`core/MONZERO_CHAIN_SPEC.md`](core/MONZERO_CHAIN_SPEC.md) · Security: [`core/SECURITY.md`](core/SECURITY.md)

## Version

This is the source of the **v1.0.0** release: core `release/v1.0.0` (046a7f0ab), GUI 44eef24d4 (built on core c4165784d) and Android Wallet and Miner 1.0.0. v1.0.0 activates hard fork 17 (LWMA-1 difficulty) at block 21,000, and every node and wallet must run it before then.

## Getting the code

```
git clone --recurse-submodules https://github.com/Nightglass-Protocol1/monzero-app.git
cd monzero-app
# already cloned without submodules?
git submodule update --init --recursive
```

Submodules: RandomX, rapidjson, supercop and trezor-common (core), quirc (GUI).

## Building

**Core** (node and CLI wallets). See [`core/README.md`](core/README.md) for dependencies and other platforms:
```
cd core && make -j"$(nproc)" release
```

**Desktop GUI.** `gui/monero` is a link to `../core`, so the GUI always builds against this repository's core. See [`gui/README.md`](gui/README.md):
```
cd gui && make release -j"$(nproc)"
```
On Windows, clone with `git config --global core.symlinks true` (developer mode on), or replace `gui/monero` with a copy of `core/`.

**Android Wallet.** The native libraries need core plus the four Monerujo patches in `android/wallet/core-patches/`:
```
android/wallet/external-libs/prepare-monero.sh     # copies core/ to external-libs/monero and applies the patches
cd android/wallet/external-libs && make           # Docker or Podman build of the native libraries
cd .. && ./gradlew assembleProdMainnetRelease
```

**Android Miner.** See [`android/miner/README.md`](android/miner/README.md) (`xmrig/build-android.sh`, then `./gradlew assembleRelease`).

## Licences

Each component keeps its upstream licence: `core/LICENSE` and `gui/LICENSE` (BSD-3-Clause, The Monero Project and the Monzero contributors), `android/wallet` (Apache-2.0, Monerujo) and `android/miner/LICENSE` (GPL-3.0, with XMRig). Please keep the notices when you redistribute.

## Release verification

Signed release files, checksums and verification steps are at <https://monzero.org/download/>. The release signing key fingerprint is `4C37 C618 789F 86D8 1FEF 28DF 390A EFB7 35F2 70D6`.
