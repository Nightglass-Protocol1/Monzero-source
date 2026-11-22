# Monzero

Monzero (XMZ) is an independent, privacy-preserving digital currency mined with RandomX on ordinary CPUs. It has a built-in decentralised exchange (DEX): XMZ trades directly against other coins by atomic swap, wallet to wallet, with no exchange account and no one else holding your funds.

Website: <https://monzero.org> · Downloads: <https://monzero.org/download/> · DEX: <https://monzero.org/dex/> · Roadmap: <https://monzero.org/roadmap/>

## Current releases

| App | Version | Where |
| --- | --- | --- |
| Linux wallet (graphical wallet with the Exchange page, node and command-line programs) | **v1.3.6** | [download](https://monzero.org/download/) · tag [`v1.3.6`](https://github.com/Nightglass-Protocol1/Monzero-source/tree/v1.3.6) |
| Android wallet (trades through the DEX page) | **1.1.13** | [download](https://monzero.org/download/) |
| Android miner | 1.0.0 | [download](https://monzero.org/download/) |
| Web wallet | live | <https://wallet.monzero.org> |

Every release is signed with the Monzero release key, fingerprint `4C37 C618 789F 86D8 1FEF 28DF 390A EFB7 35F2 70D6`. The signed release file lists the SHA-256 of every download; check it before you run anything.

The network has used LWMA-1 difficulty since hard fork 17 at block 21,000 (31 October 2026). Nodes and wallets older than v1.0.0 stop syncing at that block.

## The DEX

Each trade is an atomic swap: both payments complete, or both sides get their coins back. There are no wrapped tokens, bridges or custodians. Offers are signed by the traders and carried by open relays (`relay1.monzero.org`, `relay2.monzero.org`, and anyone who runs one); a relay can drop an offer but cannot change or invent one.

| Pair | Status |
| --- | --- |
| XMZ / BNB | **live**: EVM swap contract on BNB Smart Chain |
| XMZ / ETH | **live**: same contract on Ethereum, with a finality wait |
| XMZ / SOL | **live**: dedicated Solana swap program |
| XMZ / USDT | **live**: Tether on Solana and on BNB Smart Chain |
| ETH / BNB, ETH / SOL, SOL / BNB | in testing: coin-for-coin swaps without XMZ in between, passed on public testnets |
| XMR pairs | in testing: Monero stagenet swap passed |

Live trading has small per-trade and daily limits, set in a document signed by the project, which can also pause new trades. **The swap contracts and engine have not had an independent security audit: trade only amounts you can afford to lose.** Limits are raised step by step after a community security review and weeks of clean trading.

## Where the code is

`main` is the project's home page and starting point. Released code is in the release tags (`v1.3.6` is the newest); work in progress is in branches, for example:

| Branch | What |
| --- | --- |
| `dex/direct-pairs` | direct coin-for-coin pairs (ETH/BNB/SOL/USDT) and XMR pairs |
| `appliance/images` | Monzero Box: ready-made Docker, virtual machine and Raspberry Pi images (in development) |
| `assets/native-v1` | native tokens and NFTs on the XMZ chain (built, not scheduled) |

| Folder | What it is | Based on |
| --- | --- | --- |
| [`core/`](core/) | `monzerod` node, `monzero-wallet-cli`, `monzero-wallet-rpc`, libraries and tests | Monero |
| [`gui/`](gui/) | desktop wallet with the Exchange page | Monero GUI |
| [`gui/dex-contracts/`](gui/dex-contracts/) | DEX: EVM swap contract, Solana swap program, swap engine (Rust), key derivation; in release tags also the relay (`relay/`) | new |
| [`android/wallet/`](android/wallet/) | Monzero Wallet for Android | Monerujo |
| [`android/miner/`](android/miner/) | Monzero Miner for Android (RandomX CPU miner) | XMRig |
| [`website/`](website/) | the monzero.org website | new |

Whitepaper: [`core/WHITEPAPER.md`](core/WHITEPAPER.md) · Chain parameters: [`core/MONZERO_CHAIN_SPEC.md`](core/MONZERO_CHAIN_SPEC.md) · Security: [`core/SECURITY.md`](core/SECURITY.md)

## Getting the code

```
git clone --recurse-submodules https://github.com/Nightglass-Protocol1/Monzero-source.git
cd Monzero-source
git checkout v1.3.6                      # the current release
git submodule update --init --recursive
```

Submodules: RandomX, rapidjson, supercop and trezor-common (core), quirc (GUI).

## Building

**Core** (node and command-line wallets). See [`core/README.md`](core/README.md) for dependencies and other platforms:
```
cd core && make -j"$(nproc)" release
```

**Desktop wallet.** `gui/monero` is a link to `../core`, so the wallet always builds against this repository's core. See [`gui/README.md`](gui/README.md):
```
cd gui && make release -j"$(nproc)"
```

**Android wallet.** The native libraries need core plus the patches in `android/wallet/core-patches/`:
```
android/wallet/external-libs/prepare-monero.sh     # copies core/ to external-libs/monero and applies the patches
cd android/wallet/external-libs && make           # Docker or Podman build of the native libraries
cd .. && ./gradlew assembleProdMainnetRelease
```

**Android miner.** See [`android/miner/README.md`](android/miner/README.md).

**DEX.** Swap contract: `cd gui/dex-contracts && npm ci && npx hardhat test`. Swap engine and keys: `cargo test --locked` in `gui/dex-contracts/engine` and `gui/dex-contracts/evm-keys`.

## Continuous integration

`.github/workflows/ci.yml` builds core and the desktop wallet on Linux and runs the DEX contract, engine and Solana tests on every push and pull request.

## Licences

Each component keeps its upstream licence: `core/LICENSE` and `gui/LICENSE` (BSD-3-Clause, The Monero Project and the Monzero contributors), `android/wallet` (Apache-2.0, Monerujo) and `android/miner/LICENSE` (GPL-3.0, with XMRig). The DEX contracts, program and engine in `gui/dex-contracts/` are BSD-3-Clause; the patches in `gui/tools/dex-engine-port/` apply to eigenwallet and follow its GPL-3.0 licence. Please keep the notices when you redistribute.

## Issues and benchmarks

Bugs: open an issue with the **Bug report** form. RandomX CPU results for the website's benchmark catalogue: use the **RandomX CPU benchmark** form (results are reviewed before they are added).

## Security

Report vulnerabilities privately as described in the [security policy](.github/SECURITY.md) (also [`core/SECURITY.md`](core/SECURITY.md)) or to security@monzero.org, not in public issues. Never paste wallet seeds, keys or passwords into issues or pull requests.
