# DEX trading on the phone

The Android wallet trades on the DEX the same way the desktop wallet does: the DEX web page
(`monzero.org/dex`, opened in the phone's browser) is the screen, the wallet is the engine. The page
finds the wallet on the phone's own loopback address, and everything the page needs from the user
arrives as a **notification** that opens a prompt in the wallet: accept the connection (compare a
six-digit code), enter the trading password, approve a trade, move XMZ back.

No cloud push service is involved: the page and the wallet are on the same phone.

## What runs where

| Part | Where | What it is |
| --- | --- | --- |
| `libmonzero_dex_helper.so` | `app/src/main/jniLibs/<abi>/` | The trade helper, `gui/dex-contracts/engine` (the program the desktop wallet runs), built for Android. One JSON request in, one answer out. |
| `libmonzero_wallet_rpc.so` | `app/src/main/jniLibs/<abi>/` | `monzero-wallet-rpc` from the wallet's own core source, built for Android. Serves the trading wallet and each trade's swap wallet on a loopback port. |
| `dex/DexEngine` and friends | `app/src/main/java/.../dex/` | The phone's version of the desktop `DexEngine` parts the web page drives: trading wallet service, keys, one market per chain, the one running trade, the owner's limits. |
| `dex/DexBridge` | same | The loopback link to the page: the desktop `DexWebBridge` protocol, port 38471-38475, same origin and host checks, same pairing. |
| `dex/DexService` | same | Foreground service that keeps the link and a running trade alive while the user is in the browser, and raises the notifications. |
| `dex/DexPromptActivity`, `dex/DexActivity` | same | The prompts, and the wallet's DEX screen (wallet menu, "Trade on the DEX"). |

Both programs are ordinary executables. Android only lets an app run programs from its native
library folder, which is why they are named `lib*.so` and why `build.gradle` keeps them unpacked
(`useLegacyPackaging`). They are part of the signed APK and cannot be changed after install.

## What is different from the desktop wallet

- **Trading keys.** The desktop wallet derives them from its Bitcoin light wallet. The phone makes
  twelve BIP-39 recovery words of its own (`DexKeys`), kept encrypted under the trading password
  (Argon2id, then AES-256-GCM) in the app's private storage. The trade account is the standard
  Ethereum account `m/44'/60'/0'/0/0` of those words, so they restore it in MetaMask too. The words
  are shown once when the keys are made and again from the DEX screen ("Show recovery words").
- **XMZ node.** There is no node on the phone: the wallet services use the wallet's remote node
  (untrusted mode). The balance can lag a little after a lock until its block is seen.
- **Coins.** ETH, BNB and SOL (what the page trades). No BTC/LTC light wallets on the phone.
- **Limits.** Identical: every new trade is checked against the owner-signed control document for
  its network (`DexRisk`, same key, same rules, same ledger). The mainnet build trades on main
  networks only and answers only `https://monzero.org`; the `devnet` flavor trades on test networks
  and also answers a page served from `localhost` (tests).
- **Certificates.** The helper checks TLS servers against built-in Mozilla roots on Android
  (`chain.rs`, `http_client`): a stand-alone program cannot reach Android's own certificate checker.

## Building the two programs

```sh
# once: the images the wallet's native libraries are built in
cd ../external-libs && ./prepare-monero.sh && make arm64-v8a x86_64     # see the wallet README

cd ../dex-native
./build-wallet-rpc.sh            # jniLibs/{x86_64,arm64-v8a}/libmonzero_wallet_rpc.so
./build-helper.sh                # jniLibs/{x86_64,arm64-v8a}/libmonzero_dex_helper.so
```

`build-helper.sh` needs podman, the `rust:1.95-bookworm` image and NDK 27 (`ANDROID_NDK`). Both
scripts print the SHA-256 of what they made. The binaries are not kept in git.

Then build the wallet as usual (`./gradlew assembleProdMainnetRelease`). On a current Linux the
NDK 17 compiler the wallet's JNI library needs may ask for `libtinfo.so.5`; a copy from the build
image on `LD_LIBRARY_PATH` is enough.

## Trying it

`devnet` flavor on an emulator, with a local copy of the page (`php -S localhost:8000`), a test
relay and `adb reverse` for ports 8000 and the relay's: put
`{"relays":["http://127.0.0.1:18795"]}` in `files/dex/testnet/overrides.json` (read by test builds
only), open a wallet, menu "Trade on the DEX", switch the link on, then open
`http://localhost:8000/dex/?testnet=1#pro-ETH` in the emulator's Chrome.

Unit tests: `./gradlew testProdDevnetDebugUnitTest --tests 'com.m2049r.xmrwallet.dex.*'`.
