# Monzero (XMZ)

Monzero is an independent privacy-coin network derived from
Monero. It has its own genesis block, network identity, ports, address
prefixes, monetary policy, and native currency (XMZ). Monzero is not affiliated
with or endorsed by the Monero Project.

Monzero v1.0.0 is the current stable release. It has not been independently
audited, so do not use it for funds you cannot afford to lose, and never reuse
a Monero wallet file, private key, or recovery seed with Monzero.

## Current status

- Mainnet block target: 120 seconds
- Proof of work: RandomX
- Difficulty: LWMA-1 over 60 blocks from block 21,000 (hard fork 17); upgrade nodes before then
- Native currency: XMZ, with 11 decimal places
- Target primary emission: 100,000,000 XMZ
- Permanent tail emission: 3.26 XMZ per block
- Official bootstrap node: `node.monzero.org:6174`
- Restricted public RPC: `node.monzero.org:6175`
- Default data directory: `.monzero`

The exact protected network parameters and genesis vectors are documented in
[`MONZERO_CHAIN_SPEC.md`](MONZERO_CHAIN_SPEC.md) and
[`config/monzero-consensus.json`](config/monzero-consensus.json).
The protocol design, privacy model, monetary-policy equations, security
assumptions, and current limitations are presented in
[`WHITEPAPER.md`](WHITEPAPER.md).

Monzero Assets is an inactive, pre-activation prototype. Hard-fork version 17
is unscheduled, and no currently configured network accepts asset
transactions. Assets require Phase 0 stabilization, independent cryptographic
and implementation review, an isolated development network, and an extended
public testnet before activation can be considered. See
[`docs/MONZERO_ASSETS_V1_SPEC.md`](docs/MONZERO_ASSETS_V1_SPEC.md).

## Build from source

Monzero uses CMake. On Debian or Ubuntu, install the common build dependencies
with:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libboost-all-dev \
  libssl-dev libzmq3-dev libunbound-dev libsodium-dev libunwind8-dev \
  liblzma-dev libreadline-dev libldns-dev libexpat1-dev libpgm-dev \
  libhidapi-dev libusb-1.0-0-dev libprotobuf-dev protobuf-compiler \
  libudev-dev ccache doxygen graphviz
```

Build the release binaries from this source tree. A public clone URL is not
currently published, so source should be obtained as a verified
archive from a maintainer and unpacked first:

```bash
cd monzero-core
git submodule update --init --recursive
cmake -S . -B build/release -D CMAKE_BUILD_TYPE=Release
cmake --build build/release -j"$(nproc)"
```

The binaries are written to `build/release/bin/`. Build and platform details are in
[`docs/COMPILING_DEBUGGING_TESTING.md`](docs/COMPILING_DEBUGGING_TESTING.md).

## Run a node and wallet

Start the daemon and connect it to the official bootstrap node:

```bash
./build/release/bin/monzerod --add-priority-node node.monzero.org:6174
```

In another terminal, create a new Monzero-only wallet:

```bash
./build/release/bin/monzero-wallet-cli \
  --generate-new-wallet "$HOME/MonzeroWallets/my-wallet" \
  --daemon-address 127.0.0.1:6175
```

Record the new recovery seed offline. Do not import a seed used on another
CryptoNote-derived network. For packaged controls and mining
instructions, follow the README included in the release archive.

## Test

After configuring a test-enabled build, run:

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Consensus changes must also pass the protected Monzero vector, synchronization,
partition, reorganisation, transaction, and database tests. The complete
release gate is maintained in [`docs/RELEASE_CHECKLIST.md`](docs/RELEASE_CHECKLIST.md).

## Release and security status

Release archives must be built from a named source commit, independently
reproduced, verified after packaging, and accompanied by signed SHA-256
manifests. The current developer packages do not yet satisfy every strict
release requirement. Do not describe them as production-ready.

Security reports should not include private keys, wallet seeds, or sensitive
transaction details in a public issue. Report vulnerabilities privately to
[`security@monzero.org`](mailto:security@monzero.org) and follow the
coordinated-disclosure process in [`SECURITY.md`](SECURITY.md).

## Contributing

Review the stabilization plan in
[`docs/MONZERO_PHASE0_STABILIZATION.md`](docs/MONZERO_PHASE0_STABILIZATION.md)
before changing consensus, wallet behavior, packaging, or public-network
configuration. General contribution guidance is in
[`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md).

## Upstream attribution and license

Monzero is based on the Monero codebase and retains its upstream copyright and
license notices. Copyright belongs to the respective Monero, CryptoNote, and
Monzero contributors. The source is distributed under the BSD 3-Clause
license; see [`LICENSE`](LICENSE).

Upstream Monero documentation remains in parts of the source tree for technical
reference. Upstream services, release channels, donation addresses, and support
contacts are not Monzero services.
