# Building the Linux release wallet (v1.3.6; earlier releases the same way)

The published `monzero-v1.3.6-linux-gui-x86_64.tar.gz` was produced with these steps. Paths are
examples; `$SRC` is this repository at the release commit with its submodules checked out.

## 1. Build image

`gui/Dockerfile.linux` is upstream's static Qt recipe with four changes: Ubuntu 22.04 as the base
(the DEX code needs glibc 2.34 and C++17), OpenSSL 3.5 LTS, Qt built with TLS, and zstd.

    podman build -t monzero-gui-build-env:linux-static-monzero --build-arg THREADS=14 -f gui/Dockerfile.linux /empty-dir

## 2. DEX helper programs

Three programs the wallet runs for swaps, built in `rust:1.95-bullseye` (glibc 2.31) for generic
x86-64, offline from a local copy of the crates in each `Cargo.lock`:

    export RUSTUP_TOOLCHAIN=1.95.0 RUSTFLAGS="--remap-path-prefix=<checkout parent>=/build -C target-cpu=x86-64"
    # in gui/dex-contracts/engine
    cargo build --release --locked --offline --bin monzero-dex-evm-helper
    # in the adapted swap engine (monzero-v1.1.0-dex-helper-source.tar.gz, engine/); v1.3.6 reuses
    # the v1.1.0 native-ltc-* binaries (that source did not change)
    cargo build --release --locked --offline -p swap-machine --bin native-ltc-light --bin native-ltc-journal
    strip monzero-dex-evm-helper native-ltc-light native-ltc-journal

## 3. Wallet, node and tools

Inside the build image, with no network, in an empty build directory:

    cmake -D STATIC=ON -D DEV_MODE=OFF -D MANUAL_SUBMODULES=1 -D ARCH=x86-64 -D BUILD_64=ON -D CMAKE_BUILD_TYPE=Release \
      -D WITH_DEX_PROTOTYPE=ON -D DEX_PUBLIC_EDITION=ON -D DEX_BUILD_TESTS=OFF \
      -D MONZERO_DEX_CONTROL_PUBKEY=9eba2b1784ddfc23f028c03c78f862fecc81af81616722b56e671e6662e0a6fb \
      -D MONZERO_EVM_HELPER=<path> -D MONZERO_EVM_HELPER_SHA256=<sha256> \
      -D MONZERO_JOURNAL_HELPER=<path> -D MONZERO_JOURNAL_HELPER_SHA256=<sha256> \
      -D MONZERO_LIGHT_HELPER=<path> -D MONZERO_LIGHT_HELPER_SHA256=<sha256> \
      $SRC/gui
    make

`MONZERO_DEX_CONTROL_PUBKEY` is the public key the wallet checks signed control documents against.
The wallet runs a helper only if its SHA-256 is the one given here.

## 4. Package

    cd $SRC/core && bash utils/release/package-linux-gui.sh <build dir>/bin <output dir> v1.3.6

The archive is deterministic for the same binaries. The binaries themselves have not been shown
to rebuild byte for byte (`binary_build_reproducibility=unverified` in `BUILD-MANIFEST.txt`).
