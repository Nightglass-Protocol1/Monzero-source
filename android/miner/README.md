# Monzero Miner for Android

A small Android app that mines Monzero (XMZ) with the phone's CPU. It runs
[XMRig](https://github.com/xmrig/xmrig) 6.26.0 (GPLv3) against the Monzero pool,
`pool.monzero.org:3333`, or any pool you enter.

- Mines only while the phone is charging (default), and pauses when the battery
  reaches 40 °C. It resumes once the battery has cooled by 3 °C.
- Uses half the CPU cores by default. Choose more at your own risk: phones are not
  built for sustained full load.
- XMRig's standard 1% developer donation is kept.

## Build

Requirements: Android SDK 35, NDK 27.2.12479018, CMake 3.22+, JDK 17.

```sh
git clone --branch v6.26.0 https://github.com/xmrig/xmrig ../xmrig
ANDROID_NDK_ROOT=$ANDROID_HOME/ndk/27.2.12479018 xmrig/build-android.sh ../xmrig
./gradlew assembleRelease
```

`xmrig/build-android.sh` downloads libuv 1.51.0 (checksum pinned) and builds an XMRig
binary with only the RandomX CPU miner (no TLS, GPU, hwloc or other algorithms) for
arm64-v8a and x86_64. It writes the binary to `app/src/main/jniLibs/<abi>/libxmrig.so`.
Android only installs `lib*.so` files from an APK, so the binary is packaged under that name and
started from the app's native library directory.

## Licence

GPLv3, the same as XMRig. See LICENSE.
