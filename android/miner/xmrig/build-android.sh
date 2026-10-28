#!/bin/sh -e
# Cross-compile XMRig for the Monzero Miner app and place it in app/src/main/jniLibs.
#
#   ANDROID_NDK_ROOT=~/Android/Sdk/ndk/27.2.12479018 xmrig/build-android.sh /path/to/xmrig-v6.26.0
#
# Only the RandomX CPU miner is built: no GPU backends, TLS, hwloc or other algorithm
# families. XMRig's default 1% developer donation is kept.

XMRIG_SRC=$(cd "$1" && pwd)
NDK=${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT}
API=24
UV_VERSION=1.51.0
UV_SHA256=5f0557b90b1106de71951a3c3931de5e0430d78da1d9a10287ebc7a3f78ef8eb
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$HERE/../app/src/main/jniLibs
WORK=$HERE/build

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d libuv-v$UV_VERSION ]; then
    curl -fL --retry 10 --retry-all-errors -o libuv.tar.gz https://dist.libuv.org/dist/v$UV_VERSION/libuv-v$UV_VERSION.tar.gz
    echo "$UV_SHA256  libuv.tar.gz" | sha256sum -c
    tar -xzf libuv.tar.gz
fi

rm -rf xmrig-src && cp -r "$XMRIG_SRC" xmrig-src
# Without TLS the target is named xmrig-notls, but the strip step still names "xmrig".
sed -i 's/add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD/add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD/' xmrig-src/CMakeLists.txt
# The NDK has pthread and rt inside libc; there are no separate libraries to link.
sed -i 's/set(EXTRA_LIBS pthread rt dl log)/set(EXTRA_LIBS dl log)/' xmrig-src/CMakeLists.txt
grep -q 'set(EXTRA_LIBS dl log)' xmrig-src/CMakeLists.txt

for ABI in arm64-v8a x86_64; do
    TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API"
    PREFIX=$WORK/deps-$ABI
    cmake -S libuv-v$UV_VERSION -B uv-$ABI $TOOLCHAIN -DCMAKE_BUILD_TYPE=Release \
        -DLIBUV_BUILD_SHARED=OFF -DLIBUV_BUILD_TESTS=OFF -DLIBUV_BUILD_BENCH=OFF \
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
    cmake --build uv-$ABI -j"$(nproc)" --target install

    ARCH_FLAGS=""
    [ "$ABI" = arm64-v8a ] && ARCH_FLAGS="-DARM_V8=ON"
    cmake -S xmrig-src -B xmrig-$ABI $TOOLCHAIN -DCMAKE_BUILD_TYPE=Release $ARCH_FLAGS \
        -DWITH_HWLOC=OFF -DWITH_TLS=OFF -DWITH_OPENCL=OFF -DWITH_CUDA=OFF -DWITH_NVML=OFF -DWITH_ADL=OFF \
        -DWITH_CN_LITE=OFF -DWITH_CN_HEAVY=OFF -DWITH_CN_PICO=OFF -DWITH_CN_FEMTO=OFF \
        -DWITH_ARGON2=OFF -DWITH_KAWPOW=OFF -DWITH_GHOSTRIDER=OFF -DWITH_MSR=OFF -DWITH_DMI=OFF \
        -DWITH_BENCHMARK=OFF -DWITH_EMBEDDED_CONFIG=OFF \
        -DUV_INCLUDE_DIR="$PREFIX/include" -DUV_LIBRARY="$PREFIX/lib/libuv.a"
    cmake --build xmrig-$ABI -j"$(nproc)"

    # Android installs only lib*.so files from the APK; the executable rides along under that name.
    mkdir -p "$OUT/$ABI"
    "$NDK"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip -o "$OUT/$ABI/libxmrig.so" xmrig-$ABI/xmrig-notls
    sha256sum "$OUT/$ABI/libxmrig.so"
done
