#!/bin/bash
# Monzero wallet (GUI + exchange) for 64-bit ARM Linux, built natively in an arm64 Debian 13
# container (emulated), dynamically linked against Debian's Qt 5.15. Runs on Debian 13 / Raspberry Pi OS trixie.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential cmake pkg-config git ca-certificates python3 \
  libboost-all-dev libunbound-dev libunwind-dev libssl-dev libzmq3-dev libsodium-dev libhidapi-dev libusb-1.0-0-dev \
  libgcrypt20-dev libpgm-dev libnorm-dev libprotobuf-dev protobuf-compiler libudev-dev libreadline-dev libexpat1-dev libzstd-dev \
  qtbase5-dev qtdeclarative5-dev qttools5-dev-tools libqt5svg5-dev qtbase5-private-dev libx11-xcb-dev libfontconfig1-dev \
  qml-module-qtquick-controls qml-module-qtquick-controls2 qml-module-qtquick-dialogs qml-module-qtquick-xmllistmodel \
  qml-module-qt-labs-settings qml-module-qt-labs-platform qml-module-qt-labs-folderlistmodel qml-module-qtquick-templates2 > /w/apt.log 2>&1
git config --global --add safe.directory '*'
mkdir -p /w/build && cd /w/build
H=/w/helpers/monzero-dex-evm-helper
cmake -D STATIC=OFF -D DEV_MODE=OFF -D MANUAL_SUBMODULES=1 -D ARCH=armv8-a -D BUILD_64=ON -D CMAKE_BUILD_TYPE=Release \
  -D WITH_DEX_PROTOTYPE=ON -D DEX_PUBLIC_EDITION=ON -D DEX_BUILD_TESTS=OFF \
  -D MONZERO_DEX_CONTROL_PUBKEY=9eba2b1784ddfc23f028c03c78f862fecc81af81616722b56e671e6662e0a6fb \
  -D MONZERO_EVM_HELPER=$H -D MONZERO_EVM_HELPER_SHA256=$(sha256sum $H | cut -d' ' -f1) \
  /w/src/gui > /w/cmake.log 2>&1
make -j"$(nproc)" > /w/make.log 2>&1
echo BUILD-DONE
