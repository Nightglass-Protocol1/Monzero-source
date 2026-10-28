# Monzero GUI

Monzero GUI is the desktop wallet for the independent Monzero (XMZ) network.
It is derived from Monero GUI but connects to Monzero's separate genesis,
network identity, ports, address prefixes, and data directory. It is not
affiliated with or endorsed by the Monero Project.

This software is experimental and unaudited. Never reuse a Monero wallet file,
private key, or recovery seed with Monzero.

## Release status

The current GUI is Genesis prerelease software for testing. Automatic updates
are disabled and no Monzero donation destination is configured. Do not enable
the inherited updater until Monzero-controlled signed manifests, release keys,
and download infrastructure have been independently reviewed and deployed.

The GUI does not activate Monzero Assets. Hard-fork version 17 is unscheduled,
and no currently configured network accepts asset transactions.

## Source layout

- `monero/` is the pinned Monzero core submodule used by the wallet and daemon.
- `src/` contains the Qt/C++ application layer.
- `components/`, `pages/`, and `wizard/` contain the QML interface.
- `installers/` contains platform packaging material.

The core submodule must be supplied with the source archive. Release builds
must use the exact submodule commit recorded by this repository; do not build
with `DEV_MODE`.

## Build

Initialize the submodule after unpacking a verified source archive:

```bash
cd monzero-gui
git submodule update --init --recursive
```

For a normal pinned build:

```bash
cmake -S . -B build/release \
  -D CMAKE_BUILD_TYPE=Release \
  -D WITH_UPDATER=OFF
cmake --build build/release -j"$(nproc)"
```

Static and cross-platform builds require the inherited depends environment and
platform toolchains. Those upstream-derived recipes are technical reference,
not Monzero release authorization. A release candidate must be built from a
named source commit in a pinned environment and independently reproduced.

## Safety invariants

- The application name and executable are Monzero-specific.
- Default daemon ports and `.monzero` data paths come from the pinned core.
- Linux URI handlers use `monzero:` and `monzeroseed:`, never Monero schemes.
- The bundled daemon is launched with update checks disabled.
- Cross-fork wallet or seed reuse is unsupported and unsafe.

Any change to these invariants requires clean wallet creation, seed restoration,
transfer, daemon synchronization, restart, and package tests.

## Contributing and security

There is currently no canonical public source browser. A dedicated private
security-reporting contact must be published before production launch;
do not disclose wallet seeds, private keys, or sensitive transaction details in
a public report.

## Upstream attribution and license

Monzero GUI retains the copyright and license notices of the Monero GUI,
Monero, CryptoNote, and other upstream contributors. Copyright belongs to the
respective contributors. The source is distributed under the BSD 3-Clause
license; see `LICENSE`.

Upstream links retained in source comments or cryptographic references are for
attribution and technical context. Upstream download, donation, update,
translation, support, and release services are not Monzero services.
