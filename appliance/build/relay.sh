#!/bin/bash
# Built in a plain Debian Rust container: a portable x86-64 binary (a build on this PC's
# distribution may require newer CPUs), from the vendored crates, offline
build_relay() {
  local repo=$1 root=$2
  podman run --rm -v "$repo/gui/dex-contracts:/src" -v "$root/core/build/dex-cargo-home:/cargo" -e CARGO_HOME=/cargo \
    -e CARGO_TARGET_DIR=/src/relay/target-portable -w /src/relay docker.io/library/rust:1.95-bookworm \
    cargo build --offline --release 2>&1 | tail -1
  echo "$repo/gui/dex-contracts/relay/target-portable/release/monzero-dex-relay"
}

# The same for 64-bit ARM (Raspberry Pi): cross-compiled, no emulation needed
build_relay_arm64() {
  local repo=$1 root=$2
  podman run --rm -v "$repo/gui/dex-contracts:/src" -v "$root/core/build/dex-cargo-home:/cargo" -e CARGO_HOME=/cargo \
    -e CARGO_TARGET_DIR=/src/relay/target-portable -w /src/relay docker.io/library/rust:1.95-bookworm bash -c '
      apt-get update -qq && apt-get install -y -qq gcc-aarch64-linux-gnu >/dev/null 2>&1
      rustup target add aarch64-unknown-linux-gnu >/dev/null 2>&1
      CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc cargo build --offline --release --target aarch64-unknown-linux-gnu 2>&1 | tail -1' >&2
  echo "$repo/gui/dex-contracts/relay/target-portable/aarch64-unknown-linux-gnu/release/monzero-dex-relay"
}
