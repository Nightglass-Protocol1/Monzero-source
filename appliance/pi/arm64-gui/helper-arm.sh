set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null && apt-get install -y -qq gcc-aarch64-linux-gnu libc6-dev-arm64-cross >/dev/null
rustup target add aarch64-unknown-linux-gnu 2>&1 | tail -1
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc CC_aarch64_unknown_linux_gnu=aarch64-linux-gnu-gcc
cargo build --release --locked --offline --target aarch64-unknown-linux-gnu --bin monzero-dex-evm-helper 2>&1 | tail -1
aarch64-linux-gnu-strip /t/aarch64-unknown-linux-gnu/release/monzero-dex-evm-helper
