# Monzero web wallet: design and security model

Status: design and build preparation (2026-10-31). Nothing is deployed.

## What it is

A **non-custodial** XMZ wallet that runs entirely in the user's browser at
`https://wallet.monzero.org`. Keys, the 25-word recovery phrase and the wallet file never leave the
browser. The server serves static files and relays blockchain data from a Monzero node; it never
receives, stores or can derive anything secret.

Features (first version): create a wallet, restore from 25 words (with a restore height), balance and
history, receive (address, subaddresses, `monzero:` payment QR), send (with fee preview and a clear
confirmation), an encrypted wallet kept in the browser, export of the recovery words, lock after
inactivity. Mainnet and testnet. No DEX in the first version.

Not doing: a custodial wallet (the server holding keys), or a view-key light-wallet server (the server
would see every user's incoming payments). Both conflict with Monzero's no-custody and privacy goals.

## How it is built

The wallet logic is **Monzero's own C++ wallet code** (`core/src/wallet/wallet2.cpp`, the code the
desktop, CLI and Android wallets use), compiled to WebAssembly. No wallet cryptography is written in
JavaScript.

- `monero-ts` (MIT, pinned commit `d2ce5bf`, 2026-10-23) provides the TypeScript API, the web worker and
  the Emscripten build.
- `monero-cpp` (MIT, pinned `65e5356`, 2026-10-22) wraps `wallet2`. Its `monero-project` submodule is
  **replaced by this repository's `core/`** (Monzero, Monero v0.18.5.1 base), so address prefixes,
  network types, fork rules and amounts come from Monzero's code.
- Toolchain: Emscripten **3.1.66** exactly (the version monero-ts is built and tested with).
- Monzero changes to the wrappers are kept as patches in `web-wallet/patches/` and applied by the build
  script, so every change is reviewable.

### Monzero-specific changes (checked by tests before any release)

| Upstream assumption | Monzero | Why it matters |
| --- | --- | --- |
| `XMR_AU_MULTIPLIER = 10^12` (monero-cpp `monero_utils.h`) | `10^11` (`COIN` in `core/src/cryptonote_config.h`) | Otherwise every displayed and entered amount is off by 10x |
| Payment URI scheme `monero:` | `monzero:` (as `wallet2::make_uri`) | QR codes and links must open Monzero wallets |
| Monero address prefixes, network IDs, genesis | from `core/` | The wallet must refuse Monero addresses and networks |
| Hard-fork table / LWMA difficulty (v1.0.0, fork 17 at 21,000) | from `core/` | Fees and transaction versions must match the chain |

Release tests (wasm, in a headless browser): create and restore against known vectors, address
prefixes for mainnet/testnet, amount round trips (1 XMZ = 100,000,000,000 atomic units), send and
receive on testnet, refusal of a Monero address, refusal of an amount with 12 decimals.

## Threat model and defences

| Threat | Defence |
| --- | --- |
| Website or server compromised and the page altered to steal keys | Versioned, reproducible release bundles with published SHA-256 hashes; every script loaded with Subresource Integrity; strict Content-Security-Policy (`script-src 'self' 'wasm-unsafe-eval'`, no inline scripts, no third-party origins); the page is static files only, deployed separately from the website's PHP; a "verify this release" page and the downloadable bundle that can be run from local files. Users are told plainly: large amounts belong in the desktop or Android wallet |
| Malicious or compromised node returning false data | The wallet verifies what it can (transaction proofs, key images it creates); it never sends keys; the node only learns which blocks are fetched and the transactions broadcast. Users can point the wallet at their own node |
| Node gateway abused (DoS, private RPC) | Gateway allows only the read and broadcast calls a wallet needs (list below), restricted RPC mode on the node, per-IP rate limits, request size limits, no admin or mining calls |
| Theft from the browser (other sites, extensions, someone with the device) | Wallet data encrypted with the user's password by wallet2's own encryption (ChaCha20 with a key derived from the password by the CryptoNight slow hash, as in Monzero's other wallets; the web wallet raises `kdf_rounds` above the default 1 to slow down guessing), kept in IndexedDB for `wallet.monzero.org` only; auto-lock after inactivity; never in localStorage, cookies or URLs; recovery words shown only on request after re-entering the password |
| Clipboard / address swapping malware | Show the address with its first and last characters highlighted and a checksum-verified preview before sending; confirmation screen repeats amount, fee and address |
| Phishing copies of the site | Only `wallet.monzero.org`; linked from monzero.org; HSTS; the page shows the release hash; users are told never to enter their 25 words anywhere else |
| Supply chain (npm dependencies) | Lockfile with integrity hashes, minimal runtime dependencies, dependencies audited at each release, no CDN at runtime |
| Analytics / tracking | None. No cookies, no analytics, no external requests except the node gateway |

## Page dependencies

The page itself has one runtime dependency: `qrcode-generator` 2.0.4 (MIT, no dependencies), pinned
with its integrity hash in `app/package-lock.json`, installed with scripts disabled. Reviewed
2026-11-01: no network access, eval, storage or timers; it only builds QR images. A `monzero:`
payment link encoded with it decodes back exactly (zbar).

## Node gateway (on monzero-eu; applied 2026-11-01)

Same origin as the wallet, at the site root (`https://wallet.monzero.org/json_rpc`, `/getblocks.bin`, …;
Monzero's wallet code cannot reach a node behind a path prefix such as `/node/`), in front of the node's restricted RPC
(Caddy, `deploy/Caddyfile.wallet`). Allowed paths only:

- JSON-RPC methods: `get_info`, `get_last_block_header`, `get_block_header_by_height`,
  `get_block_headers_range`, `get_fee_estimate`, `get_version`, `hard_fork_info`
- Binary/other: `/getblocks.bin`, `/gethashes.bin`, `/get_o_indexes.bin`, `/get_outs.bin`, `/get_output_distribution.bin` (decoy selection when sending),
  `/get_transaction_pool_hashes.bin`, `/gettransactions`, `/get_transaction_pool`,
  `/sendrawtransaction`, `/get_height`, `/getinfo`, `/get_info`

Everything else returns 403 (checked: start_mining, stop_daemon, get_peer_list). Same origin, so no CORS
is needed and the page's CSP allows `connect-src 'self'` only. Maximum request body 1 MB. Per-IP limits:
for now the node's own (100 connections per IP); a proper rate limit (Caddy plugin or firewall) must be
added before the mainnet wallet goes live.

## Release process

1. Build in a clean container from pinned sources; record the toolchain and dependency versions.
2. Run the release tests above; testnet send/receive must pass.
3. Publish the bundle, its SHA-256 and a signature with the project's release key; deploy only the
   files whose hashes match.
4. Testnet first at `https://wallet.monzero.org/testnet/`; mainnet only after testnet use and review.
