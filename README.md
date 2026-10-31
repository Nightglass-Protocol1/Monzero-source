# Monzero

Monzero (XMZ) is an independent, privacy-preserving digital currency mined with RandomX on ordinary CPUs. This repository holds the source code of every Monzero application:

| Folder | What it is | Based on |
| --- | --- | --- |
| [`core/`](core/) | `monzerod` node, `monzero-wallet-cli`, `monzero-wallet-rpc`, libraries and tests | Monero |
| [`gui/`](gui/) | Monzero desktop wallet (GUI) for Windows and Linux | Monero GUI |
| [`android/wallet/`](android/wallet/) | Monzero Wallet for Android | Monerujo |
| [`android/miner/`](android/miner/) | Monzero Miner for Android (RandomX CPU miner app) | XMRig |
| [`gui/dex-contracts/`](gui/dex-contracts/) | DEX: XMZ ↔ BNB/ETH swap contract, swap engine (Rust) and key derivation | new |
| [`gui/tools/dex-engine-port/`](gui/tools/dex-engine-port/) | DEX: XMZ ↔ LTC/BTC/BCH swap engine, as an ordered patch series on eigenwallet | eigenwallet |

Website: <https://monzero.org> · Whitepaper: [`core/WHITEPAPER.md`](core/WHITEPAPER.md) · Chain parameters: [`core/MONZERO_CHAIN_SPEC.md`](core/MONZERO_CHAIN_SPEC.md) · Security: [`core/SECURITY.md`](core/SECURITY.md)

## Version and branches

`main` holds the **v1.0.0** release line (hard fork 17, LWMA-1 difficulty, at block 21,000; every node and wallet must run v1.0.0 or later) together with the work built on it since:

- **Android Wallet 1.0.1**: no crash or silent close when the node is unavailable.
- **Desktop GUI**: Windows usability fixes (mining reconnect, node start, branding), opt-in public wallet profiles, and the Microsoft Store wallet edition (`-D WITH_MINING=OFF`; core `-D MONZERO_DISABLE_MINING=ON`).
- **Core**: whitepaper 1.5, CI fixes, Windows code signing for release packages, the public wallet directory service (`core/utils/public-wallets/`).
- **Decentralised exchange (DEX), in development**: see below.

## Decentralised exchange (in development)

The DEX swaps XMZ with other coins by atomic swap: each side keeps its own keys and either both payments complete or both are refunded. No server or other user ever holds anyone's funds, and there are no wrapped tokens or bridges.

| Pair | Status |
| --- | --- |
| XMZ ↔ LTC, BTC, BCH | Engine and GUI prototype; testnet trades, refunds and power-loss recovery tested |
| XMZ ↔ BNB | Swap contract on BNB testnet; engine with crash-safe journal; testnet swap, refund, kill/resume, two-machine and GUI trades completed |
| XMZ ↔ XMR | Routed through BTC (XMZ → BTC → XMR); signet/stagenet tests |

**None of this is enabled in release builds.** The DEX is compiled only with `-D WITH_DEX_PROTOTYPE=ON` and also needs a local-prototype environment flag to run. The swap contract is **not audited** and is deployed only on BNB testnet. Real-money trading stays off until independent review. Design and review documents are the `gui/DEX_*.md` files.

## Getting the code

```
git clone --recurse-submodules https://github.com/Nightglass-Protocol1/Monzero-source.git
cd Monzero-source
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

**DEX components.** Swap contract: `cd gui/dex-contracts && npm ci && npx hardhat test`. Swap engine and keys: `cargo test --locked` in `gui/dex-contracts/engine` and `gui/dex-contracts/evm-keys`. Engine port: see `gui/tools/dex-engine-port/README.md` (`verify-upstream-patches.sh` rebuilds it from upstream eigenwallet).

## Continuous integration

`.github/workflows/ci.yml` builds core and the desktop GUI on Linux and runs the DEX contract and engine tests on every push and pull request.

## Licences

Each component keeps its upstream licence: `core/LICENSE` and `gui/LICENSE` (BSD-3-Clause, The Monero Project and the Monzero contributors), `android/wallet` (Apache-2.0, Monerujo) and `android/miner/LICENSE` (GPL-3.0, with XMRig). The DEX swap contract and engine in `gui/dex-contracts/` are BSD-3-Clause; the patches in `gui/tools/dex-engine-port/` apply to eigenwallet and follow its GPL-3.0 licence. Please keep the notices when you redistribute.

## Release verification

Signed release files, checksums and verification steps are at <https://monzero.org/download/>. The release signing key fingerprint is `4C37 C618 789F 86D8 1FEF 28DF 390A EFB7 35F2 70D6`.

## Issues and benchmarks

Bugs: open an issue with the **Bug report** form. RandomX CPU results for the website's benchmark catalog: use the **RandomX CPU benchmark** form (results are reviewed before they are added).

## Security

Report vulnerabilities privately as described in the [security policy](.github/SECURITY.md) (also [`core/SECURITY.md`](core/SECURITY.md)), not in public issues. Never paste wallet seeds, keys or passwords into issues or pull requests.

## Not in this repository

The monzero.org website and the project's social-media tooling live separately.
