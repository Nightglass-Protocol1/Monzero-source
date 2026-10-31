# Handoff: shared light wallet (LTC/BTC/BCH/DOGE), Claude session 2026-10-12

## Active completion criteria — 15 October 2026

Continue the existing active DEX goal through implementation and verification.
Wallet creation, keys, mnemonic recovery material and private trade checkpoints
must remain in user-controlled local application storage; no hosted custody or
server-side wallet backup is part of this goal.

1. Finish BCH local-wallet payout preparation, explicit review, one-time relay
   and read-only confirmation, including lost-reply and reopen tests.
2. Complete local-wallet cancellation/refund and repair of interrupted private
   release publication without granting duplicate spend authority.
3. Route verified engine operations through the helper and desktop controls,
   then validate full paired BCH/XMZ success, payout, refund and restart flows.
4. Finish the remaining supported coin DEX paths and their network verification;
   record evidence separately for LTC, BTC, BCH, DOGE and XMR/XMZ where applicable.
5. Verify local key/mnemonic access, encrypted backups and recovery of active
   trades; clearly distinguish key recovery from private trade-state recovery.
6. Package only verified helper/application artifacts and update progress and
   roadmap claims to match tests. Public pilot/release gates remain explicit;
   development tests alone do not establish public-market readiness.

These criteria clarify the existing goal; it remains active and incomplete.
Mining remains disabled. Existing unrelated working-tree changes are preserved.


Written 2026-10-13 for Codex. State checked today: both trees are unchanged since
the snapshots below, except for the last BCH note in `DEX_MULTICOIN_PROGRESS.md`,
which is uncommitted.

## Codex continuation — 13 October 2026

The snapshot statement above describes the original handoff, not the current
working trees. Desktop multicoin lifecycle/payment wiring and private raw-seed
recovery have since been added. See the desktop wallet checkpoint near the top
of `DEX_MULTICOIN_PROGRESS.md` for evidence and remaining work. BCH/DOGE are
still regtest-only. Mnemonic creation, private viewing and saved-account vault
restoration have since been implemented. A guided settings form now feeds the
existing wallet creation flow for all four coins. Explicit saved-folder reopening
now verifies the trusted helper in a fresh process. Folder paths are remembered
locally per coin and reattached when the trusted setup service starts; explicit
startup selections win. Linux builds now support a copied helper with a digest
compiled into the GUI; CMake and isolated full-GUI bundle checks passed. Signed
release distribution, reviewed helper upgrades and lost-configuration recovery
remain unfinished
and light-wallet swap integration. The current
implementation section of the progress document supersedes older checkpoint
limitations. The goal remains active, with mining disabled.

## Pending mnemonic import engine — 13 October 2026

The engine now distinguishes imported mnemonic setup from generated wallets,
retains only encrypted import material and produces a recovery-pending schema.
The helper now exposes private preview/import endpoints with public account
review bound to exact settings. Four-coin helper and legacy/raw plus mnemonic Qt
regressions pass. GUI import review/approval now works through the real Qt worker
and recognizes pending imports after restart. Address discovery, allocation
recovery and explicit activation remain necessary. The bundle digest was updated to the
tested helper and isolated full-GUI packaging/startup checks passed again. Rebuilding after engine changes requires reviewing/updating
the explicit CMake digest;
a digest mismatch deliberately blocks configuration instead of silently repinning.

## Recovery coverage engine — 13 October 2026

`WalletObservation::review_recovery_coverage` now binds scanned ranges and known
allocation floors to an opaque, approved coverage record. Retention reserves
all scanned indices, keeps existing request identities and rejects changed
account/chain or insufficient coverage. The 192-pass wallet suite includes
four-coin checks. Helper discovery now returns approved bounded coverage and
provisional observations from a pending import. Actual local LTC/BTC/BCH/DOGE
node discovery tests passed; no allocations or activation are published.
Desktop discovery, durable recovery/backup coordination and activation remain
unfinished. See the coverage/discovery patches and progress log.

## Recovery-backup prerequisite — 13 October 2026

`vault::mnemonic::storage::backup::{verify_current,retain_current}` bind a backup
to exact current encrypted vault bytes and allocation bytes. Equal counts with
different reservation IDs fail. Recovery snapshots need their own private
directory: the original immutable backup cannot be replaced. Retrying a retained
snapshot verifies it and reuses its ciphertext; wrong passwords authenticate
before destination publication. These APIs are not wired into helper/GUI yet.
Next: durable approved allocation intent, interruption-safe retention and backup
receipt, then separately approved activation and desktop discovery controls.
The existing packaged helper/GUI remain unchanged by this library-only work.
Acceptance: 193 light-wallet tests passed, seven ignored; all Rust features and
targets compile; ordered patches reproduce engine sources. Evidence is under
`../build/light-wallet-recovery-backup-*-20261013.log`; implementation is retained
in `tools/dex-engine-port/upstream-light-wallet-recovery-backup.patch`.

## Durable allocation retention — 13 October 2026

`session::wallet::recovery::retention` adds allocation-only intent/completion.
Start via opaque `RecoveryCoverage::retain_journaled`; resume via
`resume_retention` with separately retained approval and configuration hashes.
`AddressBook` prepares exact new IDs without writes, then applies only the
saved before/after transition while checking that all old entries survive.
The journal receipt still requires backup refresh and explicitly declines
activation. Next wire password authentication, this transition and exact
backup verification into the helper coordinator, then expose desktop recovery
and separately approved activation. Do not infer current backup status from
the allocation completion receipt. No helper rebuild or bundle repin yet.
Acceptance: 194 wallet tests passed, seven ignored; all features/targets compile
and ordered patches reproduce engine sources. See
`../build/light-wallet-recovery-retention-*-20261013.log` and
`tools/dex-engine-port/upstream-light-wallet-recovery-retention.patch`.

## Authenticated recovery coordinator — 13 October 2026

`session::wallet::recovery::retention::mnemonic` now provides
`RecoveryCoverage::retain_mnemonic_recovery` and `resume_mnemonic_recovery`.
They authenticate before writes, bind the exact encrypted vault and separate
backup directory, apply durable allocations, verify an immutable encrypted
backup and retain a combined completion receipt. Every retry reauthenticates
and revalidates actual backup bytes. A fresh operation refuses an occupied
backup destination before address publication. Receipt activation stays false.
If only backup context exists and allocation intent was never published, resume
fails: obtain matching fresh coverage and call begin again. It must not invent
coverage from a context file. The low-level allocation API remains available;
helper recovery should use this combined coordinator.
Next implement private pending-import recovery endpoints, desktop controls and
separate activation. Existing helper/GUI binaries are unchanged.
Acceptance: 195 light-wallet tests passed, seven ignored; all Rust features/targets
compile; patch reconstruction passes. See
`../build/light-wallet-recovery-coordinator-*-20261013.log` and
`tools/dex-engine-port/upstream-light-wallet-recovery-coordinator.patch`.
The interruption test covers one stage per coin, not every stage on every coin.

## Pending-import recovery helper — 13 October 2026

`--wallet-import-recover-stdio-v1` now uses the combined coordinator with explicit
review/configuration approval, local password and separate private destinations.
Begin recomputes the approved discovery review; resume uses retained coverage
and the selected chain without network contact. Both leave the configuration
pending and activation false. Discovery parsing/observation was factored into
shared helper routines. Four funded isolated-node cases passed, including
network-free retry and unchanged initial backup/vault/configuration. All targets
compile and ordered patches reproduce sources. The tested helper was repinned
and the desktop rebuilt. Packaging and isolated GUI bundle startup checks
also passed. See `light-wallet-recovery-helper-*` evidence in build.
Next: Qt worker/GUI discovery and recovery, restart-state presentation, separate
explicit activation. Do not treat the helper receipt as active-trade recovery
or complete wallet history. Saved launchers still need reviewed helper migration.

## Desktop pending-import discovery — 13 October 2026

`LitecoinLightJob::discoverImportedWallet`, `DexEngine::discoverLightWalletImport`
and `LightWalletDiscoveryPanel.qml` now expose bounded, explicitly approved
server discovery. `LightWalletDiscoveryProtocol.h` checks public response
bindings, allocation floors/gaps, commitments and current allocation-record hash.
`LitecoinJournalProtocol::privateRecord` allows bounded 2 MB journals while
`privateConfig` retains its 64 KiB hard cap. The first real Qt run failed because
that original cap rejected the requested journal limit; corrected all-coin runs
passed. State is `wallet-import-discovered`; status exposes `importDiscovery`,
but receive/sign/trading remain disabled. Secret import fields stay hidden.
Acceptance: 84 QML tests passed; funded LTC/BTC/BCH/DOGE real Qt/helper cases
passed (`light-wallet-discovery-qt-*-fixed-20261013.json`). Final build is
`light-wallet-discovery-desktop-fixed-build-20261013.log`.
Desktop regressions: 56 passed, 21 skipped
(`light-wallet-discovery-desktop-tests-20261013.log`).
Next: desktop recovery approval/resume with retained public review and password
input, then separately approved activation. Discovery review is currently only
in memory; no claim of desktop recovery resumption after restart yet.

## Desktop imported-wallet recovery — 13 October 2026

`LightWalletImportRecovery.h` validates/saves immutable public reviews under
`setupDirectory/import-recovery-<reviewDigest>/review.json`, with separate
`journal` and `backup` children. Records are private, atomically published and
synced; they contain no credentials. Saved-review validation intentionally checks
historical evidence without requiring the original allocation digest to match a
book that may already have completed its approved transition. Live discovery
still checks the current allocation digest.
`LitecoinLightJob::recoverImportedWallet`/`DexEngine::recoverLightWalletImport`
and `LightWalletImportRecoveryPanel.qml` implement fresh consent/password entry,
helper begin/resume selection, and validation against actual local completion,
vault, allocation and encrypted backup bytes. Status has `importRecoveryRequests`
(historical, backupVerified=false) and `importRecovery` (fresh operation only).
The current result clears on another operation and is never restored as verified
just by reading a receipt on startup. Each fresh process can explicitly retry.
All four funded Qt recovery/restart tests passed; existing import Qt regressions
passed; QML 85 passed; desktop tests 56 passed/21 skipped. Evidence is under
`light-wallet-import-recovery-*` in build. An initial QML visibility failure was
fixed before final acceptance. No Rust/helper change or helper repin this turn.
Next: separately approved imported-wallet activation and restart-safe config
publication. The wallet remains pending and cannot receive/sign/trade yet.

## Imported-wallet activation core/helper — 13 October 2026

`vault::setup::configuration::activation::activate` parses the original pending
configuration, opens its actual vault/book/cache, reauthenticates and replays
recovery verification against the explicitly approved recovery-receipt digest.
It publishes immutable intent, separate active config and completion under the
setup directory: `litecoin-wallet-activation.json`, `litecoin-wallet-active.json`,
`litecoin-wallet-activated.json`. All destinations are preflighted; exact retry
reuses records. `--wallet-import-activate-stdio-v1` exposes it via private stdin.
Receipt says walletActivated=true but tradingEnabled=false and history unproven.
Next wire Qt explicit activation consent/password using the fresh importRecovery
receipt, validate the active file plus completion against the pending source,
and support restart. Never automatically open an active file without completion,
and never treat historical activation as a freshly verified backup after later
address allocations. The desktop still opens pending configuration today.
Acceptance: 196 engine wallet tests passed, seven ignored; all features/targets
compile and ordered patch reconstruction passes. Funded helper discovery, recovery,
activation, exact retry and post-recovery receive-address tests pass on all four
local coins (`light-wallet-import-activation-<coin>-20261013.json`).
The tested helper is repinned into the desktop bundle. Packaging and isolated GUI
startup checks passed (`light-wallet-import-activation-bundle-20261013.json`).

## Desktop activation and guarded reopening — 13 October 2026

`LightWalletImportActivation.h` validates historical activation intent/completion,
recovery/context commitments and an active config exactly derived from the pending
source. `LitecoinLightJob::activateImportedWallet` requires the fresh recovery
receipt digest, explicit consent and password; success adopts the validated active
config while retaining source/bootstrap/activation snapshots. Saved setup launcher
reopening uses the same validation without helper execution or current backup
claims. Missing completion stays pending (`wallet-import-activation-incomplete`);
invalid proof/configuration disables controls (`wallet-import-activation-invalid`).
Status exposes `importActivated`, `importActivation`, `importActivationAvailable`
and `importActivationDigest`; fresh `importRecovery` clears on activation/reopen.
`LightWalletImportActivationPanel.qml` exposes consent/password controls and makes
clear that normal wallet use does not enable DEX trading.
Acceptance: four funded Qt/helper activation and reopening reports passed,
including interrupted/mutated records and post-recovery receive allocation;
86 QML tests passed; desktop regressions 56 passed/21 skipped. See
`light-wallet-import-activation-qt-<coin>-20261013.json`,
`light-wallet-import-activation-qml-20261013.log` and
`light-wallet-import-activation-desktop-tests-20261013.log` in build.
Next: recovered-wallet payment/backup lifecycle acceptance, reviewed helper
upgrades, and light-wallet DEX funding/settlement. No Rust/helper change this turn.

## NAMING WARNING: "litecoin" here means all four chains

`litecoin_light`, `native-ltc-light`, `LtcNetwork`-taking functions and the
Litecoin-worded messages are **shared by LTC, BTC, BCH and DOGE**. The name is
historical and was kept on purpose, because renaming would break persisted
Litecoin formats. Do not assume code is Litecoin-only because of its name.
Chain-specific code lives only in `network.rs` and `script.rs`. See the per-chain
labels below and `swap-machine/src/litecoin_light/CHAINS.md` in the engine.

| Chain | Schema tag | `LightNetwork` variant | Spend type (`ScriptKind`) | Addresses | Key path (main) | Header rules enabled | E2E test / node |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **LTC** Litecoin | `ltc` (default) | `Litecoin(LtcNetwork)` | `WitnessV0` | Bech32 `ltc`/`tltc`/`rltc` | 84'/2' | main, test, regtest | `test_litecoin_light_chain.py` / litecoind 0.21.5.8 |
| **BTC** Bitcoin | `btc` | `Bitcoin(BtcNetwork)` | `WitnessV0` | Bech32 `bc`/`tb`/`bcrt` | 84'/0' | main, testnet3, regtest (no testnet4) | `test_light_wallet_chain.py btc` / Bitcoin Core 31.1.0 |
| **BCH** Bitcoin Cash | `bch` | `BitcoinCash(BchNetwork)` | `P2pkhForkId` | CashAddr `bitcoincash`/`bchtest`/`bchreg` | 44'/145' | **regtest only** | `test_light_wallet_chain.py bch` / BCHN 29.0.0 |
| **DOGE** Dogecoin | `doge` | `Dogecoin(DogeNetwork)` | `P2pkhLegacy` | Base58 P2PKH | 44'/3' | **regtest only** | `test_light_wallet_chain.py doge` / Dogecoin 1.14.9 |

## What was done

Your Litecoin light wallet (`swap-machine/src/litecoin_light`, untracked WIP in the
engine over upstream `0e17c7f7c`) is now a shared UTXO light-wallet core for four
chains. Litecoin behaviour, persisted formats, schema strings and error messages
are unchanged. Every public function still takes `LtcNetwork`.

Engine: `~/projects/Monzero-App/core/build/eigenwallet-engine-port`

- `swap-machine/src/litecoin_light/network.rs` (new): `LightNetwork` =
  `Litecoin | Bitcoin | BitcoinCash | Dogecoin`, each with its deployment. It holds
  genesis headers and per-chain header consensus. The Litecoin rules were moved
  verbatim. Bitcoin follows Core, including the testnet3 min-difficulty rule and
  the BIP34/65/66 heights. It also has the SLIP-44 coin types, BIP84/BIP44
  purposes, `SegwitAddress` (byte-identical to `LtcSegwitAddress` for LTC), and
  `LegacyAddress` (CashAddr and Base58Check). `storage_identity` is the genesis
  hash for LTC, BTC and DOGE. For BCH it is a tagged hash, because BCH shares
  Bitcoin's genesis.
- `swap-machine/src/litecoin_light/script.rs` (new) covers three spend types:
  - P2WPKH (BIP84)
  - P2PKH with the legacy sighash (DOGE)
  - P2PKH with BIP143 SIGHASH_FORKID (BCH)

  It also handles signature placement with read-back verification, and the
  maximum-size fee templates. The BIP143 digest matches rust-bitcoin's P2WPKH
  digest.
- Signing, reload verification, fee estimation and the helper now dispatch on
  the script kind. P2PKH signatures change the txid, so the helper checks that
  only spending data changed. It takes the final txid from the retained signed
  bytes.
- `swap-machine/src/bin/native-ltc-light.rs` picks the chain from the request
  schema tag (`ltc` / `btc` / `bch` / `doge`). The config, setup and response
  schemas carry the same tag.
- Header validation for BCH and DOGE mainnet/testnet is refused with an explicit
  error. Only their regtest rules are enabled.

GUI repo: `~/projects/Monzero-App/core/monzero-gui`, branch `fork/main`

- `tools/dex-engine-port/upstream-light-network-core.patch` (31 files)
  reconstructs the engine sources. It shows zero drift against the
  `wip/codex-engine-20261011` snapshot.
- `tools/dex-engine-port/test_light_wallet_chain.py CHAIN HELPER NODE REPORT`
  is the new E2E test. It creates an encrypted wallet, receives a real payment,
  refreshes, prepares, signs, broadcasts, confirms, reconciles and refreshes
  again. It runs on regtest through a local TLS Electrum peer computed from the
  node.
- `DEX_MULTICOIN_PROGRESS.md`: new section "Shared light wallet for Bitcoin,
  Bitcoin Cash and Dogecoin — 12 October 2026" near the end, plus an index link
  near line 85.

## Snapshot branches (nothing pushed, nothing merged)

| Repo | Before my changes (your WIP) | After my changes |
| --- | --- | --- |
| engine | `wip/codex-engine-20261011` (d0b799e0d) | `wip/light-wallets-engine-20261012` (6682d4a4d) |
| monzero-gui | `wip/codex-dex-20261011` (9873eb227) | `wip/light-wallets-20261012` (857d1fe4f) |

The working trees still hold everything as uncommitted/untracked files on their
normal branches. The `wip/` branches are only safety copies. Diff
`wip/codex-*` against `wip/light-wallets-*` to see exactly what I changed.

## Evidence (all regtest, isolated nodes, disposable keys)

In `~/projects/Monzero-App/core/build/`:

- `light-wallet-multichain-20261012.json`: summary.
- `light-wallet-unit-tests-20261012.log`: 176 passed, 0 failed, 7 ignored. These
  are your 153 original tests, all passing under the same names, plus 23 new
  network and script tests. The new tests use real BTC mainnet headers including
  the block 32256 retarget, BIP173/BIP84/CashAddr vectors, and BCHN/Dogecoin node
  address vectors.
- E2E reports:
  - `light-wallet-btc-20261012.json`: Bitcoin Core 31.1.0
  - `light-wallet-bch-20261012.json`: BCHN 29.0.0
  - `light-wallet-doge-20261012.json`: Dogecoin 1.14.9

  Each node accepted the signatures with its own interpreter.
- `light-wallet-ltc-regression-20261012.json`: your existing
  `test_litecoin_light_chain.py --create-wallet --reconcile` passes with the new
  helper.

## How to build and test

- Toolchain: `~/projects/Monzero-App/dex-overnight/cargo.sh <cargo args>` (pinned
  rust 1.95 sysroot, offline cargo home). Never `cargo clean`.
- Unit tests: `cargo.sh test -p swap-machine litecoin_light`. They take about
  10 min.
- All real-node E2E runs: `~/projects/Monzero-App/dex-overnight/verify-all-light.sh`.
  This needs the release helper
  `target/release/native-ltc-light` and the lab node binaries in `core/build/`:
  - `bchn-29.0.0-lab`
  - `dogecoin-1.14.9-lab`
  - `litecoin-0.21.5.8-lab`
  - `/usr/bin/bitcoind`
- Use the release helper for the Python tests. In a debug build, the vault
  scrypt step makes them time out.
- The helper hides internal errors by design. I used a temporary debug hook and
  removed it.

## Not done / next steps (labelled by chain)

| Chain | Remaining |
| --- | --- |
| **LTC** | Swap funding/settlement with the light wallet; app/UI is Codex's existing work |
| **BTC** | Testnet4 header rules; Qt/app UI; swap funding/settlement |
| **BCH** | Mainnet/testnet headers (ASERT + checkpoint, item 1); Qt/app UI; swap funding/settlement |
| **DOGE** | Mainnet/testnet headers (DigiShield + AuxPoW + checkpoints); Qt/app UI; swap funding/settlement |
| **All** | Chain-neutral naming (item 6) |

Details:

1. **BCH mainnet:** start from a pinned post-fork checkpoint and validate with
   ASERT (aserti3-2d). Groundwork is in `~/projects/Monzero-App/dex-overnight/`:
   - `bch-mainnet-headers.json`: 1,056 real headers, each one reconstructed to
     its hash.
   - `bch_asert_reference.py`: matches all 1,053 checked blocks only with
     C++-style truncating division. Floor division fails 863 of them.
   - Blocker: `HeaderSequence` and the header cache assume they start at
     genesis. Changing that touches shared Litecoin code, so I left it for
     review.
2. **DOGE mainnet:** DigiShield, AuxPoW and checkpoints.
3. **BTC testnet4** rules.
4. **Qt/app wiring and UI** for BTC/BCH/DOGE. Only the helper and engine support
   them today.
5. **Light-wallet swap funding and settlement**, on any chain.
6. **Chain-neutral wording** in shared messages and record names, which still
   say "Litecoin"/`ltc`. I left these alone on purpose to keep LTC formats
   stable. A rename needs a migration plan.

Live trading stays disabled. No live market, public server, user wallet or
mining setting was changed.

## Recovered-wallet payments accepted — 13 October 2026

`test_light_wallet_import_discovery.py --qt-payment --qt-activate --qt-recover`
now runs the new `test_imported_wallet_payment.py` acceptance path after guarded
activation/reopening. All LTC/BTC/BCH/DOGE owned regtest fixtures passed fresh-Qt
prepare, wrong-password rejection, sign without broadcast, separate approved
broadcast, node confirmation, reconciliation, change balance and refreshed backup
save/verify ([5,4] allocations). Earlier recovery backups remain unchanged and
activation reopens without advertising fresh recovery verification. Reports:
`../build/light-wallet-import-payment-qt-{ltc,btc,bch,doge}-20261013.json`.
No production changes or helper repin were needed. Mining remains off.
Next: restored refreshed-backup acceptance and reviewed helper upgrades, then
light-wallet DEX funding/settlement and remaining public-chain/release gates.
The complete DEX goal stays active.

## Refreshed-backup restore acceptance — 13 October 2026

The payment fixture now restores into separate empty directories via Qt, with
wrong-password/digest rejection, exact vault/address bytes, idempotent retry,
fresh sync and balance scan, next receive index 5, stale-backup refusal after
allocation advances, and restored-key signing without broadcast. LTC/BTC/BCH
mempool preflight accepts the restored signature; DOGE only decodes the txid.
Original wallet files and backup ciphertext remain unchanged. Final four-coin
reports: `../build/light-wallet-import-restore-isolated-qt-{ltc,btc,bch,doge}-20261013.json`.
Qt public receive-request settings are isolated under the temporary fixture.
A prior assertion reused the old fixed receive request, correctly returning index
4; final tests use wallet-current and check index 5. No production changes.
This fixture constructs replacement settings; a guided backup-to-new-folder
workflow remains unfinished. Next implement reviewed helper upgrades, then guided
replacement settings and light-wallet DEX funding/settlement. Mining is off;
the full goal remains active. No test nodes remain running.

## Reviewed helper migration implemented — 13 October 2026

New `src/dex/LightWalletHelperUpgrade.h` binds review to original launcher/config
hashes and installed trusted helper. Approval publishes an immutable private
launcher-helper identity file (original preserved), fsyncs and atomically renames
without replacement. `DexEngine` exposes review/apply, and explicit/remembered
reopen selects the installed-helper migration if present, failing closed if bad.
`LightWalletSetupPanel.qml` displays identities and requires consent cleared by
folder/coin changes. No secrets or helper execution involved.
Desktop build passed; Qt 57 passed/21 skipped; QML 87 passed. Follow-up four-coin
engine API + restart test passed. Evidence logs use `light-helper-upgrade-*` in
build. Current acceptance uses setup-only wallets and fake never-executed helpers;
next verify a funded activated wallet migrates with unchanged wallet records and
can still transact. Release-version format compatibility is not established.
Then finish guided backup replacement setup and light-wallet DEX integration.
Mining remains off; full DEX goal active.

## Funded helper migration accepted — 13 October 2026

`test_light_wallet_import_discovery.py --qt-upgrade --qt-payment --qt-activate
--qt-recover` now invokes `test_imported_wallet_upgrade.py` before payment.
It migrates the funded activated wallet to a copied trusted helper at a new path,
removes the old binary, rejects bad approval, checks exact retry and unchanged
wallet/launcher records with zero chain queries. Payment/confirmation/change and
refreshed-backup restoration follow through fresh Qt processes. All four final
reports pass: `../build/light-helper-funded-migration-final-{ltc,btc,bch,doge}-20261013.json`.
The probe supports --review-helper/--upgrade-helper and selects migrated launchers
on --attach-local. Production exact-retry handling now fsyncs both existing
launcher and directory (crash-after-rename durability); desktop/probe builds and
migration/remembered-wallet tests pass in light-helper-migration-durability logs.
No engine/helper change or helper repin. Same binary relocation, not cross-release
format compatibility. Owned nodes are stopped; mining off; full goal active.
Next: guided backup-to-new-folder configuration. Existing backup verification
requires a pre-existing public account/wallet ID config. The missing path should
authenticate the encrypted backup, derive its public account/settings, review
network/destination locally, and publish a new config without replacing a wallet.
Do not claim this reconstructs active trade or spend journals. Then proceed to
light-wallet DEX funding/settlement and public-chain/release gates.

## Backup identity recovery implementation — 13 October 2026

New engine `litecoin_light::vault::backup::discovery` authenticates raw-seed and
BIP39 encrypted backups without a surviving wallet configuration. Caller selects
coin/network; header magic chooses only one verifier and header wallet UUID is
untrusted until full authentication. `InspectedBackup` exposes public descriptor,
wallet ID, format, allocation counts and ciphertext hash; retains only encrypted
bytes; cannot be deserialized. `restore_into` reuses existing preflight/no-overwrite
restorers and reauthenticates the password. `load` uses private Journal file checks.
No helper protocol or desktop UI is wired yet; the installed helper is unchanged.
Patch: `upstream-light-wallet-backup-discovery.patch` follows import-activation.
All-features/all-targets check and patch reconstruction pass. Wallet suite:
197 passed, seven ignored (`../build/backup-discovery-wallet-tests-20261013.log`).
No running test/build process remains from this checkpoint.

Next implementation: a backup-specific setup config (selected network, local
source backup, new private destination folders and optional chain endpoint),
private-stdin preview returning authenticated public account/allocations/hash,
then separately approved configuration publication and restore. Bind approval to
both setup bytes and authenticated backup bytes. Never trust header-only metadata,
rewrite an existing wallet, claim history freshness or reconstruct active-trade
journals from this seed/address backup. Follow with Qt guided folder setup and
four-coin actual-node acceptance, then continue DEX funding/settlement integration.

## Backup configuration coordinator and helper — 13 October 2026

New engine module `vault/backup/discovery/configuration.rs`:
`RestoreOptions {setup,vault,addresses,cache,spends,maximum_height,endpoint}`;
`InspectedBackup::configuration_review` returns 14-field public review including
full normal config, backup digest, seed encoding, wallet ID, allocation counts and
scope flags. `restore_configuration` accepts SHA256(compact review), reauthenticates,
requires empty initial destinations, retains intent then restores records, config,
completion. Retries require identical intent and immutable compatible records;
cache/spend folders must stay empty. Stages Intent/Records/Configuration/Complete
have interruption acceptance across four coins and both backup formats.
Files in setup: `litecoin-backup-restore-intent.json`, `litecoin-wallet-restored.json`,
`litecoin-backup-restore-complete.json`. Completion is required before automatic
reopening; historical receipt does not establish current backup freshness.

Helper `backup_import.rs` adds `--wallet-backup-preview-stdio-v1` and
`--wallet-backup-import-stdio-v1`. Private config schema:
`monzero-private-{coin}-backup-setup-v1`; fields network, backupDirectory (source),
setupDirectory, vaultDirectory, addressBookDirectory, cacheDirectory,
spendDirectory, maximumHeight, optional endpoint. No prior wallet ID/account needed.
Private stdin <=8192: schema `monzero-{coin}-light-{mode}-request-v1`, requestId,
approvedConfigDigest, password. Import additionally requires restoreApproved true
and approvedReview. Preview forbids those fields. Preview envelope schema/requestId/
configurationDigest/review/reviewDigest/executable false; import envelope
schema/requestId/restoration/executable false. No network or key material output.

Engine 198 passed/7 ignored, all-features/all-targets compile, ordered patch audit
passes. New patch `upstream-light-wallet-backup-configuration.patch` follows backup
discovery. `test_backup_configuration_helper.py HELPER REPORT` uses bwrap with
network disabled, eight raw/BIP39 four-coin cases, approval/password/ciphertext/
settings negatives, no preview writes, exact retry, next receiving index and
stale-restore refusal. Report `../build/backup-configuration-helper-acceptance-20261013.json`.
Development desktop/probe rebuilt and bundle checks pass; new trusted helper pin
9488fbc5457634ac01aadc17c69abdb048b1a3899ebab29f5f2367dcf3d3d134.
Old pin ae9f05b4574bbc5532a07547cb8ac535d4cfc71d2b9edcb13852bb692c5e0795
is superseded. New helper commands are not wired into Qt yet. Next implement
backup-specific local folder setup, worker preview/review/private-password restore,
guarded reopening and QML form; then funded recovery/DEX integration. No ongoing
test/build processes, mining off, complete DEX goal active.

## Guided backup recovery wired into desktop — 13 October 2026

`LightWalletSetup::prepare` accepts optional final backupSource argument; creates
backup-setup settings (no new key ID/account/seed) and private destination folders,
with source backup separate. `DexEngine` adds prepareLocalBackupRestore,
previewLightBackupImport and importLightBackup. Helper migration accepts this schema.
`LitecoinLightJob` handles m_backupSetup, previewBackupImport/importBackup,
private password packets and strict public review validation using new
`LightWalletBackupRestore.h`; normal config is additionally checked by lightConfig.
`adoptBackupRestore` requires exact intent/completion/config bindings, snapshots
receipt/intent/bootstrap for sourcesMatch, then exposes normal wallet controls.
Status: backupSetupConfigured/backupSetup, backupImportPreviewAvailable,
backupImportAvailable/backupImportReview, backupRestoration. States
backup-setup-ready, backup-import-reviewed, backup-restore-incomplete,
backup-restore-invalid; successful adoption is ready. Historical completion never
requires source backup presence or exact current allocations and disables init.

UI: setup panel backup source field/prepare button, new
LightWalletBackupRestorePanel.qml in Dex.qml/qml.qrc. Separate preview and restore
approval, password clearing on submission/context/hidden/inactive/60s timeout.
Probe: --prepare-backup-local coin network parent helper digest backup host port;
wallet-backup-preview and wallet-backup-import read password on private stdin
(import probe previews first; options approved/approvedReview support negatives).

`test_backup_configuration_helper.py --probe PROBE HELPER REPORT` now also calls
`test_backup_configuration_qt.py`: eight real helper/Qt cases offline in bwrap,
preview no writes, rejected passwords/approvals, successful restore/restart,
missing/modified completion, modified config, disconnected backup, new index,
historical receipt after allocations. Final report:
`../build/backup-configuration-qt-final-20261013.json`, pins both sources and probe.
QML89passed; desktop57passed21skipped; app/probe build and full-GUI bundle pass.
Logs `backup-desktop-*` in build. Helper pin remains
9488fbc5457634ac01aadc17c69abdb048b1a3899ebab29f5f2367dcf3d3d134.
No running tests/builds or owned nodes remain; mining off; full DEX goal active.
Next: extend the funded imported-wallet payment fixture's restore_refreshed path
(currently builds replacement settings manually) to use --prepare-backup-local
and wallet-backup-import with its TLS endpoint, then confirm balance and spending
from that guided recovered wallet on all four regtest chains. Continue DEX funding/
settlement integration after this. Public BCH/DOGE and active-trade recovery gates
remain unfinished. Do not claim offline recovery acceptance is full DEX readiness.

## Funded guided backup restoration accepted — 13 October 2026

`test_imported_wallet_payment.py::restore_refreshed` now calls the real
--prepare-backup-local probe with the owned TLS endpoint, then preview/import
through fresh Qt processes. It no longer constructs replacement config by hand.
It verifies recovered balance, exact vault/address bytes, retry preservation,
next receive index 5, stale backup rejection and guarded reopening. Restored keys
now make a separately approved payment that each actual node mines; reconciliation,
node output value and remaining balance are checked. This includes DOGE consensus
validation of the restored signature, improving its earlier decode-only check.
All four reports: `../build/guided-backup-funded-{ltc,btc,bch,doge}-20261013.json`.
No production changes or builds were needed. All owned nodes stopped, mining off.

Next move to actual DEX integration. Inspected: LightWalletProfile permits LTC/BTC
P2WSH payment shapes; selected light-wallet inputs use Sequence::MAX. Existing
LTC RecoveryPlan::new requires a PSBT with witness UTXOs, stable non-RBF SegWit
inputs, quote fee/contract matching, and later peer cancel/refund recovery before
funding approval. Execution::sign_recovered_lock and lock_approval currently use
LitecoinRpc/Core wallet reservations. Do not bypass those gates by routing a normal
wallet-send directly to a displayed contract address. Add a reviewed adapter from
ObservedClaimedSpend/retained claims to the negotiation PSBT/recovery path, then
bind local signing/relay to that agreement with restart/negative tests. Legacy
BCH/DOGE contracts require their own protocol paths; LTC pre-signed funding rules
cannot simply be generalized to legacy txids. Goal remains full DEX completion.


## Light-wallet funding proposal/recovery bridge — 13 October 2026

`ObservedClaimedSpend::funding_psbt_on` rechecks the same-tip observation and
exports the exact claim transaction and owned P2WPKH witness UTXOs. Its restricted
`ClaimedSpend::recovery_psbt` verifies the operation and disk claims, refuses prior
submission evidence, and requires non-RBF SegWit funding. No signatures or relay.
`Execution::light_recovery_signing_frame` requires the LTC-funder role and matching
network, validates RecoveryPlan, preflights the immutable trade/checkpoint/quote/
claim/PSBT binding, obtains the existing recovery frame, then durably retains and
reads back the binding before returning. Interrupted publication can retry exactly.

New policy test `litecoin_light::funding_tests` runs two controllers through peer
recovery and restart; rejects changed commitments/trades/contracts/roles, spent
observations, replaceable inputs and altered claim records. It checks the cancel
outpoint and retained refund and that the wallet never signs funding. Its injected
inclusion is explicitly policy-only, not consensus evidence. Test lives beneath
litecoin_light to preserve the private inclusion constructor. Original compile
errors were fixture API/scope errors and have been corrected.

Release suites: wallet 199 passed/7 ignored; negotiation 27 passed/2 ignored.
Existing isolated LTC/XMZ Core-backed paired-chain success regression passes,
including unconfirmed/reorged redemption rejection, recovery and final payout:
`../build/light-dex-funding-paired-success-20261013.json` and matching log.
This is regression coverage of the existing path, not light-wallet settlement.
Owned fixture nodes have stopped. Mining stays off.

Delivery: `upstream-light-wallet-dex-funding-proposal.patch` appended to the ordered
patch audit; audit passes. Baselines in
`../build/light-dex-funding-baseline-20261013`. Engine helper/GUI binaries and the
installed helper pin were not changed; this new API is not wired to those yet.
Final compile and validation manifest are recorded in the roadmap checkpoint.

Next implement a separate local funding review/approval that checks this immutable
binding against the exact retained peer recovery and fresh quote policy before
vault or mnemonic unlocking. Preserve the shared input journal and exact signed
bytes across restarts. Then separately approve relay/reconciliation through the
helper/desktop; do not route a normal wallet-send around peer recovery. Existing
Execution::sign_recovered_lock and lock_approval remain Core-RPC backed. BCH/DOGE
legacy txids cannot reuse this presigned SegWit proposal rule. Public BCH/DOGE,
other market protocols, active-trade recovery and release gates remain unfinished.
Goal remains active; this is a development checkpoint, not full DEX completion.


## Local light-wallet DEX funding approval/signing — 13 October 2026

`execution/light_funding/approval.rs` adds `review_light_funding` and
`sign_light_funding`, with `LightFundingVault::{Seed,Mnemonic}` borrowing encrypted
local vaults. Review rechecks the observed claim and exact durable proposal binding,
constructs a fresh RecoveryPlan, authenticates the retained local recovery message
against that plan, and calls recover_recovery to revalidate peer signatures and
receipts. It checks the cancel lock outpoint and refund txid and requires the funder
recovery shape. The domain-separated approval fingerprint includes unsigned bytes,
claim, trade, quote, binding hash, fee, contract/refund destinations, recovery txids,
timelocks and supplied chain tip. Its action is sign; broadcastAuthorized is false.
Signing regenerates the review, checks explicit approval, then uses the existing
vault/mnemonic observed signing API. Retained signatures are independently verified.
No relay and no new helper/UI mode in this checkpoint.

Shared `light_funding_binding` avoids divergent binding serialization between
proposal and approval. Ordered incremental patch:
`upstream-light-wallet-dex-funding-signing.patch`; audit passes. Baselines:
`../build/light-dex-signing-baseline-20261013`. The compile error using a nonexistent
hex crate was fixed with the existing Bitcoin serialize_hex API. Final all-targets/
features check passes. Wallet suite: 200 passed, 7 ignored. Both seed/mnemonic tests
reject missing or altered peer evidence, wrong approval, spent inputs, expired quote
and wrong password before a signature; verify exact bytes after reopening both
execution and claims and no submission records. Injected inclusion remains a policy
fixture, not consensus evidence. Actual isolated Core-backed paired-chain success
regression also passes on the new test binary, including reorg rejection and payout.
Final protocol-suite evidence is recorded in the validation manifest/roadmap.

Next: separately approved light-wallet funding relay and read-only reconciliation,
then helper/GUI integration. After submission may have occurred, do not feed retry
through funding_psbt_on (it deliberately rejects submission evidence) or require a
new wallet signature. Use exact retained signed bytes and endpoint-specific attempt
records, with current chain reconciliation and explicit retry approval. Claims remain
held. Existing ClaimedSpend::observe_payment_on and TLS broadcast records implement
those low-level wallet primitives. Review semantics must distinguish initial send
from recovery; quote expiry cannot become a fresh signing exemption.

Helper boundary inspected: native-ltc-light owns WalletConfig, local vault/password
and light observation; native-ltc-journal owns Execution startup and Core reservation
configuration. Avoid casually duplicating those validators when connecting the two.
Installed helper pin and desktop binaries remain unchanged. Actual light-wallet
cross-chain settlement, public BCH/DOGE, other market protocols, active-trade recovery
and release gates remain incomplete; full goal stays active and mining stays off.


## Light-wallet funding send, observation and retry — 13 October 2026

`execution/light_funding/relay.rs` adds review/send_light_funding, read-only
observe_light_funding_on and review/retry_light_funding. Initial send regenerates
the fresh funding review, verifies retained signatures and binds endpoint plus
signed-byte digest into a separate approval domain. It delegates to the existing
observed broadcast path; intent is durable before connection, and errors remain
uncertain. It never accepts signing approval as broadcast approval.

Cold readback reconstructs the exact retained PSBT using the new read-only
ClaimedSpend::retained_funding_psbt, verifies trade/network/claim and immutable
proposal binding, original admitted policy and both recovery messages, then verifies
signed bytes against the lock. Observation uses only the retained transaction and
rechecks local evidence after reading. Retry requires an attempt at that exact
endpoint plus an observation for the same txid/current selected tip. Pending and
not-reported remain untrusted; included funding is rejected for retry. Approval
binds acknowledgement status, observed position/tip and endpoint. This path has no
password/vault or new transaction selection. Endpoint migration after uncertainty
is currently rejected; a future explicit migration review would need to bind the
original global attempt rather than infer a fresh-send exemption.

ObservedClaimedSpend::retained_claim is crate-only for use under the same journal
lease. TLS endpoint host/port/network now have public read-only getters. The initial
proposal export still rejects prior submission; only the explicitly read-only
retained export allows reconstruction after an attempt. All existing claim checks,
SegWit/non-RBF restrictions and account/network binding remain.

The seed funding fixture now closes an owned TLS peer to exercise durable uncertain
submission, reopens execution and claims, observes pending/absent without send,
rejects changed approvals/endpoint/status/tip, verifies exact retry bytes through
the protocol encoder, and rejects included funding. Mnemonic signing coverage stays.
A synthetic PoW/Merkle inclusion tests SPV policy only, not full-block consensus.
Its first run failed because a genesis-version header violates LTC regtest's active
SegWit rule; fixed to version 0x20000000, with only insufficient-work errors retried.
The production verifier was not relaxed. Failure log preserved as
light-dex-relay-wallet-tests-header-fixture-failure-20261013.log.

All-targets/features compile and ordered patch audit pass. Existing Core-backed
paired-chain settlement regression passed before the test-only header correction;
its exact binary is saved at ../build/light-dex-relay-paired-engine-20261013. The
final wallet run and source/binary hashes are in the validation manifest/roadmap.
Patch: upstream-light-wallet-dex-funding-relay.patch; baseline directory:
../build/light-dex-relay-baseline-20261013. Production helper/app binaries and
installed helper pin are unchanged. No helper/GUI mode yet exposes these new APIs.

Next integrate the local light-wallet funding path through a pinned helper and the
existing desktop negotiation/recovery lifecycle, then prove actual light-wallet
cross-chain settlement and interruptions. Reuse validated configuration and journal
leases; don't bypass quote/peer recovery by invoking an ordinary wallet-send. Other
coin protocols, BCH/DOGE public-chain support, active-trade recovery and release gates
remain outstanding. Goal remains active, mining off, no live-market activation.


## Real light-wallet DEX helper funding accepted — 13 October 2026

native-ltc-light/dex.rs exposes eight --dex-funding-<mode>-stdio-v1 commands:
prepare, review, sign, send-review, send, observe, retry-review, retry. Main dispatch
recognizes these before ordinary wallet modes. Existing config validation now uses
parse_config so both paths share it. Dedicated private wrapper includes wallet file,
trade journals, tradeId, pins and Policy; no duplicate journal-helper Config parser.
Both raw private config byte hashes are bound in a sorted JSON context fingerprint.
Requests have an independent requestId; reservations always use tradeId. Password
is accepted only for sign. Approvals are mode-specific, cold/fresh budgets strict,
input cap 8192 and output cap 262144. LTC regtest only. Protocol/DTO documented in
`tools/dex-engine-port/LIGHT_WALLET_DEX_HELPER.md`.

New Execution::check_light_funding_payment checks fresh quote/role/keys, contract,
principal, fee limit and minimum change before wallet preparation. Helper reuses
wallet refresh/selection/reopen, local vault dispatch, immutable input claims and
all previously tested DEX approval/recovery/relay APIs. Prepare returns fundingPsbt
and recoveryFrame; existing local identity/peer exchange must complete before
review/sign. All commands synchronize and require peer tip on selected chain.
Cold observe/retry reopen exact signed data without wallet password or selection.
Final status fix makes sign/send/retry watchOnly=false and send-review signed=true,
matching the ordinary helper's operation flags. Tests explicitly check them.

Owned acceptance `test_light_dex_helper.py` runs both raw and BIP39 wallets through
fresh helper processes against a real isolated litecoind and local TLS Electrum
peer. Both node-accepted funding transactions confirm. Wrong config/password/
approval and missing peer recovery reject; sign approval cannot authorize send;
retries preserve txid and included funding rejects retry. Rust ignored driver
`litecoin_light::funding_tests::helper_fixture::actual_light_dex_helper_context`
only creates disposable trade context and exchanges peer identity signatures;
wallet funding stays in the actual helper. It uses env MONZERO_LIGHT_DEX_FIXTURE_ROOT
and MONZERO_LIGHT_DEX_FIXTURE_STAGE=init|peer, and root context/payment/prepared JSON.
This proves the LTC funding leg, not full XMZ settlement or desktop integration.

Final evidence: ../build/light-dex-helper-acceptance-20261013.json and
../build/light-dex-helper-validation-20261013.json. Wallet200passed8ignored (one new
external fixture); helper command2passed; all features/targets compile; ordered
patch audit passes. Python compiles. Initial include-doc-comment compile error
was fixed, then the status flags were corrected and the real-node cases rerun.
No owned nodes, builds or tests remain. Mining stays off; full goal stays active.

Ordered patch: upstream-light-wallet-dex-helper.patch. Baselines:
../build/light-dex-helper-baseline-20261013. Tested nlink1 mode0500 helper preserved:
/home/tripo/projects/Monzero-App/core/build/light-dex-helper-tested-20261013
SHA256: ef0d5613afd1f3a7eaa94deb146cd048e2fc1cc8e704b4b0f39ada2b1fc3bc42
Bundled app helper remains unchanged at
9488fbc5457634ac01aadc17c69abdb048b1a3899ebab29f5f2367dcf3d3d134.
Do not mistake the newly rebuilt target/release/native-ltc-light for the old pinned
bundle. The next Qt build must deliberately pin/copy the tested helper and verify
ordinary wallet compatibility plus the new DEX process bridge before claiming UI
readiness. Existing helper-upgrade consent/migration support remains applicable.

Next: wire a desktop light-wallet reservation/funding job to this helper and the
existing recovery/private negotiation lifecycle. Keep the GUI's trusted executable
checks, configuration fingerprint, trade binding, request validation and explicit
signature/send approvals. Prepare's recoveryFrame still needs the offer-identity
signature and peer admission. The existing Core-backed reservation path remains
available. Then prove real light-wallet full settlement/cancel/refund and process
interruptions. Public BCH/DOGE, remaining protocols, endpoint migration after an
uncertain attempt, active-trade backup/recovery and release gates remain unfinished.


## Desktop light-wallet funding boundary — 13 October 2026

Added `LitecoinLightFundingJob`, an asynchronous Qt worker for the eight local
LTC funding commands. It independently pins the helper, fingerprints both private
configuration files before/after execution, keeps passwords on stdin, bounds
responses and checks the returned trade, request, amount, fee, retained claim and
operation flags. It recomputes separate signing/relay approval digests and checks
review payment/endpoint binding. Failure or cancellation remains an unconfirmed
outcome; the worker never automatically signs, submits or retries.

The real Litecoin fixture now optionally runs all DEX commands through fresh Qt
workers. Both raw-seed and BIP39 wallets passed funding preparation, mandatory peer
recovery, rejected bad approvals/passwords, separate signing and sending, node
acceptance, exact retry, inclusion and rejection of retry after confirmation.
Sixteen additional process-boundary fault cases passed. Desktop build and bundle
checks passed; the existing Qt DEX suite reports **57 passed, 21 skipped**. The
bundled helper now matches the accepted helper digest
`ef0d5613afd1f3a7eaa94deb146cd048e2fc1cc8e704b4b0f39ada2b1fc3bc42`.

This is a tested process boundary, not a finished desktop trading flow. Next:
connect the selected active wallet with its activation/restoration guards; retain
funding request/review context for restart; connect prepared PSBTs to the existing
peer recovery session; expose separate signing/send/observe/retry controls; then
accept full XMZ/LTC settlement, cancellation/refund and interruption recovery.
Other coin DEX paths retain their existing individual gates. No public-market
activation, user-wallet modification, website deployment or real mining occurred.

Evidence: `build/light-dex-qt-{acceptance,guards,bundle,validation}-20261013.json`,
`build/light-dex-qt-suite-20261013.log` and
`tools/dex-engine-port/LIGHT_WALLET_DEX_HELPER.md`.

Implementation files are `src/dex/LitecoinLightFundingJob.{h,cpp}` and
`src/dex/tests/litecoin_light_funding_probe.cpp`. CMake adds the probe target;
the desktop source glob also compiles the worker. `DexEngine`/QML do not yet
instantiate it. `run` accepts a public helper request and a separate password
byte array; the caller owns review consent and durable request context. Do not
naively use a setup launcher's original configuration after wallet activation or
backup adoption: preserve `LitecoinLightJob::sourcesMatch` receipt guards when
exposing its actual active configuration. No new engine patch was necessary.

The actual acceptance driver accepts `--qt-worker PROBE`. The normal helper
interface is unchanged. New isolated fault driver: `test_light_dex_qt_guards.py`.
CMake cache points at `build/light-dex-helper-tested-20261013` with digest above.
Full GUI, Qt DEX tests and worker probe built; all owned tests/nodes exited.
The DEX objective remains active; this checkpoint does not satisfy full completion.


## Active desktop wallet binding and funding interlock — 13 October 2026

The funding worker can now bind to an already-open `LitecoinLightJob` rather than
reading only a standalone wallet path. Selection requires the actual active LTC
regtest configuration, signing configuration and the independently trusted helper.
The wallet rechecks its launcher, configuration and retained activation/restoration
sources before and after each funding command. Setup/import-pending wallets do
not provide a funding configuration. Keys and passwords remain local.

An in-process interlock prevents normal wallet operations during a funding command;
wallet availability reflects it through `busy` and `dexFundingBusy`. Completion,
failed commands and cancellation release the interlock. Engine storage leases
remain responsible for cross-process protection and durable input reservations.

The attached-wallet integration fixture passed every DEX operation with raw-seed
and BIP39 wallets, including Litecoin node acceptance, exact retry, confirmation
and rejection after inclusion. Each launched operation checks that personal-wallet
receive/refresh cannot start and that the interlock is released afterward. The Qt
suite reports **58 passed, 21 skipped**; 16 process-boundary faults and packaging
checks also passed. A new unit case checks selected-path/helper equality, competing
owners, availability and rejection of changed launcher/configuration/helper bytes.

Next remains the trade controller and QML connection: durable payment/review context,
selected-wallet funding controls and existing peer recovery exchange. The worker
is compiled into the application but is not yet instantiated by `DexEngine`.
Full light-wallet XMZ/LTC settlement and active-trade recovery remain unaccepted;
other coin market gates remain unchanged. The complete DEX goal stays active.

Evidence: `build/light-dex-wallet-binding-{acceptance,guards,bundle,validation}-20261013.json`
and `build/light-dex-wallet-binding-qt-tests-20261013.log`.

`LitecoinLightFundingJob::configureWallet` uses a QPointer to the selected wallet.
`LitecoinLightJob::fundingConfiguration` exposes only public configuration bytes
and trusted executable metadata after existing `sourcesMatch` checks. The internal
`acquireFunding`/`releaseFunding` owner interlock affects caller-side guards and
availability; the job's own QProcess lifetime still uses `m_busy` separately.
`configureWallet` is the required desktop selection path; direct `configure` is
retained for low-level boundary tests. The real fixture uses `--qt-bound` alongside
`--qt-worker`; the probe checks receive/refresh rejection and release. The initial
fixture used noncanonical launcher JSON and correctly failed; corrected to compact,
sorted JSON, then reran successfully. No production parsing was relaxed.

All owned tests and disposable nodes exited. App/helper packaging remains at helper
ef0d5613afd1f3a7eaa94deb146cd048e2fc1cc8e704b4b0f39ada2b1fc3bc42.
No new engine code/patch, public-market activation, website deployment or user
wallet/mining mutation. Next inspect `DexLitecoinApp.cpp` operation interlocks and
`LitecoinPaymentRequest.h` durable context before adding the trade controller.


## Durable light-wallet funding context — 13 October 2026

Bound desktop funding workers now retain an immutable payment intent before
launching the helper and retain the validated wallet claim/txid before reporting
success. Private per-trade files use exclusive creation, directory locking,
file/directory fsync and readback. Existing records must match exactly. Changed
payments/configuration, altered claims, partial files and orphan reviews fail
closed; errors never silently overwrite or discard an attempted intent.

Fresh workers load the payment and claim from disk, supply an omitted retained
review and require all later commands to use that same context. Saved claim
commitments are not signing or broadcast approvals; fresh explicit approvals are
still required. Once attempted, an invalid initial payment cannot simply be edited;
the controller must obtain/review negotiated payment terms before preparation.

Both raw-seed and BIP39 owned-node scenarios passed preparation, separate signing/
send consent, Litecoin acceptance, exact retry and inclusion after the fixture
stopped supplying the claim review to new Qt processes. Changed payment requests
were rejected. The Qt suite reports **59 passed, 21 skipped**; 16 boundary cases,
desktop build and bundle checks also passed. This verifies retained-context restart,
not exhaustive crash/power-loss or full-swap recovery.

Next: expose read-only funding terms from the completed engine negotiation. The
current desktop negotiation status does not provide the contract address; the UI
must obtain it from the engine before committing the payment intent. Then connect
the trade controller, peer recovery and QML approval controls. Full light-wallet
XMZ/LTC settlement, active-trade recovery and other coin market gates remain open.

Evidence: `build/light-dex-context-{acceptance,guards,bundle,validation}-20261013.json`
and `build/light-dex-context-qt-tests-20261013.log`.

New header `src/dex/LitecoinLightFundingContext.h` stores public intent/review
records under the wrapper's metadataDirectory, named
`ltc-light-dex-ui-<trade>-{payment,review}.json`. `access` reads when proposed fields
are empty; payment creation and review retention are immutable. Review retention
requires a pre-existing payment intent and binds its digest. Context is enabled
only for `configureWallet`, not the unbound boundary-test interface.
`LitecoinLightFundingJob::status` now exposes retainedPayment/retainedReview/
retainedTxid. `run` rechecks these, supplies a missing saved review, acquires the
wallet interlock, retains intent, then launches. `finish` durably saves the claim
before emitting the prepared PSBT. No approval/password/key material is persisted.

The `--qt-bound` fixture now omits caller retainedReview after successful prepare
and also rejects changed payment context. Its deliberately wrong initial contract
case remains on the unbound helper path; in bound mode changed payment is tested
after correct intent creation, since even rejected attempts retain their intent.
One new Qt unit case covers immutable reuse, altered payment/config/claim/txid,
file permissions, partial records and orphan reviews. All owned tests/nodes exited.

Next read `execution/light_funding.rs::check_light_funding_payment`: it already
loads the quote and completed keys and knows contract_address, ltcAtoms,
ltcFundingFeeMaxAtoms and ltcMinimumOutputAtoms. Add a read-only terms interface
there/helper before controller wiring so no arbitrary user-entered contract becomes
a permanent UI intent. `LitecoinNegotiationSession::status` currently does not expose
that address. Do not bypass peer recovery or fresh signing/send approvals.
No engine edits/new patch in this checkpoint; helper digest remains ef0d5613...
No website/public-market/user-wallet/mining changes. Goal remains active.


## Negotiated light-wallet funding terms — 13 October 2026

The engine and local helper now expose a read-only funding-terms query. It derives
the contract address, principal, fee ceiling, minimum change, refund destination,
expiry and required swap-lock confirmations from the admitted quote and completed
key exchange. It rejects the wrong role, incomplete exchange and expired quotes.
The quote commitment uses the journal's hexadecimal format. Reading terms grants
no signing/broadcast authority and creates no desktop payment intent.

The Qt funding worker validates this response against its trade/configuration and
LTC regtest chain identity. The owned-node acceptance driver uses returned terms
to construct the funding payment. Both raw-seed and BIP39 flows passed through the
helper and bound desktop worker, including signing, submission, exact retry and
confirmation. Terms queries left wallet/trade records unchanged and made no chain
requests; unknown secret/payment fields and nil request IDs were rejected.

Validation: **200 engine wallet tests passed, 8 ignored; 3 helper-command tests
passed; 21 Qt boundary cases passed**. All-feature/all-target compilation, desktop
build, bundle checks and ordered patch reconstruction passed. The app now bundles
helper `af5dab3b1963d50f2fe9fe2eb59bf1bfd16a89d11d826eedc2936435e8ffaefe`.
An initial quote-commitment serialization mismatch was caught by integration,
corrected and retested; the unsuccessful artifacts remain separate.

Next: connect these tested operations to the trade controller, existing peer
recovery session and trading-screen controls. The controller must compare the
returned quote commitment with the selected negotiated trade, review the fee before
retaining intent, and preserve separate signing/send approvals. Expired terms must
not disable cold observation/retry of already-retained funding. Full light-wallet
cross-chain settlement, active-trade recovery and remaining coin release gates are
still incomplete; the overall DEX goal remains active.

Evidence: `build/light-dex-terms-{acceptance,guards,bundle,validation}-20261013.json`,
`build/light-dex-terms-wallet-tests-fixed-20261013.log`,
`build/light-dex-terms-command-tests-fixed-20261013.log` and
`tools/dex-engine-port/upstream-light-wallet-dex-terms.patch`.

Implementation: `Execution::light_funding_terms(now)` in `execution/light_funding.rs`
and `--dex-funding-terms-stdio-v1` in the light helper `dex.rs`. Request fields are
only schema/requestId/approvedConfigDigest. Result schema is
`monzero-ltc-light-funding-terms-v1` with 13 fields; network is the LTC genesis
identity, not a network tag. `lockConfirmations` belongs to the swap lock and is
separate from the selected input minimumConfirmations. The Qt worker has a terms
validation branch and a shared launch routine; terms never persist payment intent.
Both the helper and Qt path are exercised in the node driver, with file-content
snapshots and peer request counts proving the scoped read-only behavior.

Pinned helper: `build/light-dex-terms-helper-tested-20261013`, SHA256 above.
The app's CMake helper path/digest and copied bundle now use it. Owned-node fixture
binary is preserved at `build/light-dex-terms-fixture-tested-20261013` (setup/peer
signatures only, from the pre-serialization-fix test build; unchanged fixture code).
Final engine wallet regression tests were rebuilt and passed after the hex fix.
The first acceptance attempt correctly failed on array-vs-hex quoteCommitment;
its log/helper are retained with `before-hex-fix` names. An in-flight obsolete
regression compile was deliberately stopped for that source fix, then replaced
by the completed final run. All current test/build handles are terminal.

New ordered patch: `upstream-light-wallet-dex-terms.patch`, after dex-helper.
Baseline copies are in `build/light-dex-terms-baseline-20261013`. Patch audit passes.
No rustfmt executable was available; existing local code style was preserved.
No public-market activation, user-wallet writes, real mining or website deployment.
Next implement the actual trade controller/QML journey, using configureWallet,
retainedPayment/retainedReview/retainedTxid and the negotiated terms response.
Do not require fresh terms to observe/retry an already-signed expired trade.


## Desktop local-wallet funding controls — 13 October 2026

Added a trade-facing controller and connected it to `DexEngine` and the LTC trading
panel. A user with an open LTC regtest wallet and configured private trade can
select local funding, query negotiated terms, enter an LTC fee, prepare the exact
payment, resume recovery exchange, review/sign locally, separately review/send,
and observe or review a retry. Contract/principal come from the verified terms.
Approvals are tied to the currently displayed action and quote; passwords and
consent clear after use or a changed review.

The selected wallet configuration is retained immutably in the trade metadata
directory. The native negotiation session supplies only a source-checked public
context. Prepared PSBTs connect to the existing peer recovery session. Native
operation interlocks include the controller, and Core funding actions reject a
persisted local-wallet selection. A trade with pre-existing recovery intent cannot
silently select a second funding path. Cold observation/retry do not require fresh
terms; fresh preparation still requires a ready peer and valid terms.

The controller passed owned-node funding/confirmation for raw-seed and BIP39
wallets, with independent expected quote binding, wrong-quote rejection, separate
sign/send approvals, restart context and exact retry. **60 native Qt tests passed
(21 skipped), and 90 QML presentation tests passed.** The new QML test exercises
fee conversion, Core-control suppression, separate signing/sending/retry dispatch,
password/consent clearing, busy controls and confirmed-state presentation. App
build and helper bundle checks passed.

This is not yet end-to-end acceptance of the full app/private-peer journey: the
controller node fixture still uses a disposable engine driver for peer signatures,
and the new QML test uses a mock backend. Next extend the existing owned-node
`LitecoinAppFixture` flow to select a local wallet and drive the actual negotiation
session, including restart and uncertain funding outcomes. Then accept full
light-wallet XMZ/LTC settlement/cancellation/refund and remaining coin gates.
The public-market gates and overall DEX goal remain unchanged.

Evidence: `build/light-dex-controller-acceptance-20261013.json`,
`build/light-dex-desktop-{bundle,validation}-20261013.json`,
`build/light-dex-desktop-qt-tests-20261013.log` and
`build/light-dex-desktop-qml-20261013.log`.

New components: `LitecoinLightFundingController.{h,cpp}`, `LitecoinLightFundingSetup.h`,
`DexLitecoinLightFunding.cpp`, `components/LightDexFundingPanel.qml` and the
`dex_litecoin_light_funding_controller_probe` target. Controller operations consume
an independently supplied trade/quote identity; returned terms and review quote
arrays are checked against it. Action approvals cannot cross sign/send/retry types.
Observation limits default to 20/100/1000/10000; fixture explicitly uses 2/10/50/1000.
The node driver accepts `--qt-controller PROBE` and obtains the expected quote from
its own engine fixture claim, not from the returned terms. It runs each operation
with a fresh controller; sign/send/retry first obtain the corresponding current
review, then apply the separately supplied approval. Wrong quote tests cover terms
and signature review. No new engine edits or patch were needed.

`LitecoinNegotiationSession::lightFundingContext` checks original config/quote/
helper sources and exposes only schema/trade/directories/pins/policy. `useLitecoinLightWallet`
uses the active wallet and installed helper pins, retaining
`<trade>.ltc-light-wallet.json` privately before configuring the controller.
`litecoinLightFundingSelected` also detects that marker across restarts and denies
Core prepare/sign/send paths. New selection is refused over an existing recovery
intent without that marker. The selected wallet cannot be overwritten. Peer-ready
checks gate prepare/reopen; cold observation/retry use the saved context without
requiring a live authenticated transport. All actions still check source bindings
and native operation interlocks. Prepared PSBT forwarding currently needs acceptance
through the real app fixture; it is wired to `startLitecoinRecovery`.

QML tests passed with the existing isOpenGL/ImageMask and backup fixture warning;
no new panel binding errors. Build and packaging passed with helper af5dab3b...
All owned tests/builds/nodes exited. No live mining, user-wallet writes, public
market activation or website deployment. Next prioritize the real private-peer
app fixture rather than adding another standalone interface: extend
`src/dex/tests/LitecoinAppFixture.h` and its Python owned-node driver to exercise
local wallet selection, terms, peer recovery and funding through DexEngine.
Full swap/restart/refund acceptance is still outstanding; do not mark the goal done.


## 13 October 2026 — actual app peer recovery accepted

Extended `LitecoinAppFixture.h` with local wallet configure/open, select, terms,
prepare, reopen and signature-review commands, plus lightFunding status. The
resident Python process uses a private XDG_CONFIG_HOME and optional owned TLS root.
`litecoin_app_light_fixture.py` creates a disposable BIP39 wallet, funds it from the
owned regtest node, supplies an independently hashed helper and canonical launcher,
and opens it through DexEngine. Electrum uses a separate RPC client/counter from
the main driver. No user wallet or mining changes.

`test_litecoin_paired_chain.py --gui-light-recovery-helper HELPER
--light-wallet-tls TLS` requires the resident app funding prerequisites and stops
explicitly after actual authenticated peer recovery, local signature review,
Core funding rejection, and same-process reopen of the identical reservation.
Both chain pools remain unchanged. Full settlement is explicitly false. Cleanup
stops both app/node processes and the local Electrum listener even on failures.

Accepted: build/light-app-peer-recovery-20261013.json and
build/light-app-peer-validation-20261013.json. Reproduction command and fixed
paths: build/run-light-app-peer-recovery-20261013.py (report/log use exclusive create,
choose new evidence names before rerunning). Native Qt: 60 passed, 21 skipped.
First attempts rejected fixture-only uppercase coin code and unsorted launcher
JSON; failure logs retained, then corrected normal open-folder path and canonical
launcher passed. All owned runs are terminal. No production engine/helper changes.

Next: extend the resident fixture through local sign/send and full XMZ settlement,
then cancellation/refund and interrupted-process restart. Existing paired Core
settlement and isolated light-controller funding evidence must not be relabelled
as full light-wallet settlement. Public markets remain disabled; goal active.


## 13 October 2026 — complete local-LTC-funded success path accepted

Actual resident two-peer app test now passes local BIP39 LTC funding through final
XMZ payout and recipient/reserve balances. `--gui-light-settlement` extends the
recovery-only mode, requiring resident settlement and the success path. Added
fixture commands light-sign/send-review/send/observe/retry-review/retry. Disposable
password is in an owned 0600 test file, never request JSON, argv or response; removed
after signing/funding checks. This is fixture-only; no production key-storage change.
`submit_light_funding` verifies separate consent, signing without broadcast,
wrong-action/repeated consent rejection, exact retry and inclusion. Core reservation
verification flag is false for this path. Fee accounting uses decoded inputs/outputs.

First run found fixture depth mismatch: local inclusion mined one block before the
ordinary fixture's two-block step. Corrected the latter to mine one for light funding,
so invalidating the tip again tests the intended one-confirmation rejection. Failed
run retained in light-app-settlement-before-depth-fixture-fix-20261013.log. Final
full run passed; all owned processes terminal. Standard Qt: 60 passed, 21 skipped.
Reports and source hashes: build/light-app-settlement-20261013.json and
build/light-app-settlement-validation-20261013.json. Runner:
build/run-light-app-settlement-20261013.py (exclusive log/report; new names to rerun).
No engine/helper/app production changes this turn; existing desktop wiring passes.

Important scope: LTC light-wallet funding plus existing node-backed settlement
services, owned LTC regtest and XMZ fakechain. This is NOT complete no-node public
settlement. Next implement/accept cancellation/refund to local wallet custody and
active-trade process restart, then replace remaining settlement node dependencies
where needed and progress BTC/BCH/DOGE integrations. Public gates remain closed.


## 13 October 2026 — complete local-wallet refund custody accepted

Refactored litecoin_app_light_fixture.py into create_light_wallet (before quote)
and attach_light_wallet (after app startup). The first receive address becomes the
signed LTC refund destination. attach verifies returned terms match that known
address before funding. --gui-light-settlement now allows the resident refund path.
verify_light_refund explicitly uses monzero-ltc-light sync and refresh schemas;
shared chain.Helper was unsuitable because its module CHAIN defaults to None.
First failure log preserved in light-app-refund-before-schema-fixture-fix-20261013.log.

Final accepted report build/light-app-refund-20261013.json proves local funding,
full refund path, exact 199979000-atom local balance (two outputs), confirmed refund
script destination, native refund restoration/payout, reorg rejection and app-state
reopening. Final runner exited0, all owned processes cleaned up. Evidence hashes
in build/light-app-refund-validation-20261013.json. Runner is
build/run-light-app-refund-20261013.py; select new exclusive log/report names to rerun.
No C++/Rust/helper changes or native-test rerun this checkpoint; unchanged Qt binary
from accepted 60-pass suite was used for the actual refund fixture.

Next production gap: helper terms currently emits execution.light_funding_terms
without verifying refund destination against the selected wallet account. Fixture
comparison is NOT a production guard. PublicAccount::derive(Branch,index) and
AddressBook::open are available; ownership must bind to retained issued/recovered
address metadata with bounded work and maintain cold observation/retry availability.
Do not silently substitute a refund address after quote approval. Add a reviewed
local wallet refund-selection/binding path and negative ownership checks, then
active-trade interruption recovery. Full no-node/public settlement remains open.
Engine implementation target: swap-machine/src/bin/native-ltc-light/dex.rs terms
and execute; account module account/allocation. No engine edits yet this turn.


## 13 October 2026 — production refund ownership guard accepted

AddressBook::contains_allocated_address checks retained allocations (including
retained discovery), account/network, MAX_ENTRIES bound and before/after bytes.
No new allocations or quote substitutions. native-ltc-light/dex.rs terms and all
non-cold execute operations call verify_refund_wallet before network sync/funding.
Cold observe/retry-review/retry intentionally keep prior behavior. Public account
membership is distinct from vault/key authentication at signing.

Engine unit suite: 201passed8ignored; DEX command tests3passed; all-features/all-targets
check passed. QML90passed with added funding-panel guidance. Ordered patch audit
passed; new final patch upstream-light-wallet-refund-ownership.patch reconstructs
allocation, unit tests, command guard and ignored helper fixture changes. Baseline
build/light-refund-ownership-baseline-20261013. Actual helper fixture now requires
refund-address.json before init; test_light_dex_helper.py supplies its local receive
address and adds foreign-address cases for both vault formats. Both reject terms
and prepare without changed wallet/trade file bytes or any Electrum query.

Current accepted helper build/light-refund-ownership-helper-tested-20261013
SHA256 ab6fbb7efa41cf7d07949b20ae9d06c82b5a68887a27452dee22c6c204010631.
Current fixture binary build/light-refund-ownership-fixture-tested-20261013
SHA256 0bddef3d69a3d56a26ef9e6e33959e07e85142e532a898f41bfc044f3c09ec1d.
CMake and built app bundle pinned to that helper; full app build and bundle test
passed. Existing user wallet files were not edited. Sources/outputs pinned in
build/light-refund-ownership-validation-20261013.json. Controller acceptance:
build/light-refund-ownership-acceptance-20261013.json. Complete resident app refund
accepted in build/light-refund-ownership-paired-20261013.json (both local LTC and
XMZ refunds, reorg and app-state reopening). Runner:
build/run-light-refund-ownership-paired-20261013.py. All owned sessions terminal.

Next active-trade interruption/restart: resident fixture currently assumes empty
retained state at startup. Extend it explicitly for resume without new trade/start
approval or replacements; reconstruct the same DexEngine private app config,
reconnect saved negotiation through journal check, restore the remembered wallet,
reselect the same retained light wrapper and observe/retry exact bytes. Keep
separate consents and fail closed on changed context. Success/refund evidence so
far includes final app-state reopening, not arbitrary mid-trade crash recovery.
No public readiness claim; remaining settlement services still node-backed.


## 13 October 2026 — active app restart after local LTC broadcast accepted

Production gap found: peer restart restores negotiation, but recoveryComplete stays
false until its phase is resumed. Funding reopen is unsuitable after inputs have
been spent. Added LitecoinNegotiationSession::resumeRecovery(): source-checked
context, privateRecord <=131072 of <trade>.ltc-recovery-intent, strict expected shape,
then existing startRecovery(saved PSBT). Existing native helper checks exact intent,
checkpoint and quote before identity signing. No new reservation/broadcast/fresh
terms. DexEngine::resumeLitecoinLightRecovery gates it to selected saved local
funding and ready peer; status now includes recoveryComplete. QML has separate
Resume saved recovery exchange and Reopen funding reservation buttons. New QML
test verifies resume works without fresh terms and is gated/hidden appropriately.

Fixture now supports resumeResident with nonempty retained state. crash_resume
kills only owned idle Qt process, starts new transport directory while retaining
private preferences/journals, checks journal reconnect, remembered local wallet,
exact retained trade/quote/config/payment/review/txid, no saved approval, malformed
intent rejection then recovery redelivery. Old send approval rejected; fresh retry
review allows same tx. Complete refund flow passed after restart. Earlier incorrect
fixture wait ordering intentionally interrupted; preserved log
build/light-active-restart-before-resume-fix-20261013.log. Final accepted run exited0
and all owned nodes/processes stopped. Only C++/QML/Python changed; Rust helper
remains ab6fbb7efa41cf7d07949b20ae9d06c82b5a68887a27452dee22c6c204010631.

Evidence build/light-active-restart-20261013.json, validation JSON, bundle JSON,
Qt log60passed21skipped, QML log91passed, full app/test build. Runner
build/run-light-active-restart-20261013.py uses exclusive output names; choose new
names before repeating. No live user wallet/mining state touched.

Next: interruption after signing/before send and lost-ack/in-flight boundaries,
plus success-path settlement after resume. This acceptance is SIGKILL at an idle
post-ack broadcast boundary followed by full refunds, not arbitrary crash/power-loss
or key-only active-trade restoration. No public-network/light-settlement claim.


## 13 October 2026 — signed-before-send restart and full success accepted

Extended Python fixture only. --gui-light-restart-stage choices signed|sent
(default sent), used with --gui-light-restart. submit_light_funding can restart
after local signing before obtaining send review. Stale sign/send consent fails;
no mempool transaction appears on restart; a fresh send review approves the saved
payment. crash_resume additionally snapshots all spendDirectory/addressBookDirectory
and vault directory file hashes before/after process replacement and recovery
redelivery. Records must be byte-identical, proving no re-signing/key/address
replacement as well as same retained transaction identity. No source secret output.

Run build/run-light-signed-restart-success-20261013.py accepted the complete
success path after this interruption: local LTC funding, native funding, peer
settlement, LTC redemption, XMZ claim-wallet recovery, payout/exact recipient and
reserve balances, reorg handling and final app reopening. Final process exited0;
all owned nodes cleaned up. Report build/light-signed-restart-success-20261013.json
and provenance build/light-signed-restart-validation-20261013.json. App/Qt binary
hashes verified unchanged against light-active-restart-validation-20261013.json
(60 native and 91 QML passed there). No redundant native suite rebuild/rerun.

Next: actual lost broadcast acknowledgement / in-flight relay interruption. Current
crash_resume intentionally requires idle app and peer; do not weaken that assertion
and claim in-flight coverage. Need explicit owned helper/transport fault injection
with exact process ownership and durable intent checks, then fresh observation and
exact-byte retry without fresh funding/signing. Existing ElectrumPeer fixture
answers broadcast by node.sendrawtransaction then sends JSON result; a lost-ack
hook can close the owned TLS connection after node acceptance. Public-chain/light
settlement and remaining coin integrations remain open; goal active.


## 13 October 2026 — lost acknowledgement/restart/full refund accepted

Only Python fixture and QML test changes. ElectrumPeer has a default-off one-shot
`drop_next_broadcast_reply` flag: node RPC first accepts raw transaction, full-byte
SHA256 goes into accepted_broadcast_digests, dropped_broadcast_replies increments,
then the server closes TLS without returning JSON. Normal/default transport unchanged.
--gui-light-lost-ack requires restart stage sent. submit_light_funding verifies
outcome-unconfirmed and empty result despite actual mempool acceptance, restarts
through the owned idle-boundary fixture, rejects stale consent, observes pending
with broadcastStatus outcome-unknown, obtains fresh retry review, and verifies
exactly two accepted broadcasts with identical full-byte digest. Full refund flow
then passes including 199979000-atom local balance, native refund and reorg/reopen.

Accepted build/light-lost-ack-restart-20261013.json and
build/light-lost-ack-validation-20261013.json. Runner
build/run-light-lost-ack-restart-20261013.py; final run exited0 and all owned children
cleaned up. QML92passed with new uncertain-state test (uses unchanged production
resource bundle light-active-restart-20261013.rcc). App/helper binaries unchanged.
No native-suite rerun because production/native fixtures unchanged this turn.

Next actual in-flight process interruption: do NOT weaken crash_resume idle guard
and claim it. Consider spawn owned Qt Popen with start_new_session=True so only its
app/helpers have a dedicated process group; verify pgid before any group kill.
Separate async command enqueue/read from call, hold test TLS reply behind a barrier
after node accepts, validate the owned active helper/process group, interrupt that
group, release the server barrier and reconstruct the same app. Check durable
intent/no ack, no automatic resend/new signatures, then exact observation/retry.
Current lost-ack test closes connection and waits for helper failure before restart;
it does not yet interrupt an active QProcess or simulate filesystem power loss.
All broader public/light settlement and additional coin DEX work remains open.


## 13 October 2026 — active app/helper relay interruption accepted

--gui-light-inflight now holds the Electrum reply after node acceptance, verifies
exactly one direct native-ltc-light send helper and its owned process group, and
SIGKILLs that group before a completed app response exists. Qt fixtures launch with
start_new_session=True. Separate enqueue permits a genuinely active operation;
existing crash_resume still requires idle app/peer. The reply barrier is released
on interruption/cleanup. Restored app retains identical wallet records, clears old
consent, resumes saved recovery, observes unknown acknowledgement/pending payment,
and explicitly retries byte-identical signed bytes (two total accepted broadcasts).

Runner build/run-light-inflight-restart-20261013.py exited0. Accepted report
build/light-inflight-restart-20261013.json and hash provenance
build/light-inflight-validation-20261013.json. Full refund, exact local LTC balance,
XMZ payout, reorg and final app reopening passed. No production/native/QML source
changes; actual app, dex_tests and helper hashes match preceding validation.
All owned processes completed/cleaned up. Power-loss and public-network acceptance
remain false. Do not equate SIGKILL with filesystem power loss.

Next: return to other coin wallet/DEX integration, beginning with production BCH
funding/preparation and its currently node-backed boundary. Shared personal wallets
already support LTC/BTC/BCH/DOGE; that does not establish swap funding support for
all four. Keep market gates closed, preserve local key custody and derive claims
from actual code/tests. Full DEX goal remains active.


## 13 October 2026 — BCH local funding boundary implementation

Production funding/light.rs now derives public payment terms from the restored
AdmittedExecution agreement and checks exact covenant script/principal, positive
quote-bounded fee, minimum change floor/money ceiling, role, admission time and
quote expiry. No input reservations, secrets, signing or relay. Tests extend the
existing two-role execution admission fixture. All-features/all-targets check and
ordered-patch reconstruction passed; release fixture execution is pending at this
entry (see following acceptance entry before treating it as passed).

New final patch upstream-bch-light-funding-terms.patch; pre-edit baseline
build/bch-light-terms-baseline-20261013. Build/test logs bch-light-terms-{check,test,
patch-audit}-20261013.log. No deployed helper or Qt pin changed.

Further code inspection established two concrete integration requirements:
1. litecoin_light/network.rs LegacyAddress only accepts 20-byte P2PKH/P2SH
CashAddr. BCH covenant.rs emits OP_HASH256 + 32 bytes + OP_EQUAL. Add properly
validated P2SH32 CashAddr before the helper's address-based payment API can fund
this contract. Keep Dogecoin/version/type rejection strict; verify against BCHN.
2. ClaimedSpend::retained_funding_psbt intentionally rejects legacy inputs because
pre-signing txid is unstable. Do not remove that SegWit guard for BCH. BCH already
constructs recovery from signed funding; add a separate locally signed funding
admission/claim path preserving that ordering, durable exact signed bytes and
separate broadcast approval. Existing BCH funding/signing.rs is bound to BCHN
wallet-lock intent and deployment; do not fabricate a node-wallet lock to bypass it.

The new terms API is only the first boundary; it is not connected to the command
helper or GUI yet, and is not end-to-end BCH light-wallet swap acceptance.

P2SH32 implementation reference for next step: BCHN upgrade specifications
https://upgradespecs.bitcoincashnode.org/cashaddr/ (CashAddr hash-size/version
encoding and vectors), and local swap-core/src/bch/covenant.rs script_pubkey.
Read the primary specification before altering network.rs; destination support
does not imply that wallet keys control arbitrary script-hash outputs.

Acceptance update: bch-light-terms release test exited0, 1 passed / 0 failed
(two-role admitted execution fixture, including new terms/payment assertions).
All-features/all-targets check exited0 and ordered patch audit passed. Provenance
build/bch-light-terms-validation-20261013.json records scope and source hashes.
No live build/test sessions remain. Next step is P2SH32 local wallet destination
support, then actual BCH local signing/funding admission; goal stays active.


## 14 October 2026 — BCH P2SH32 destination implementation in validation

network.rs LegacyAddress now maps exact BCH HASH256/32-byte/EQUAL scripts to
CashAddr version11 and parses them back. Only type/length pairs (0,20), (8,20),
(11,32) accepted; invalid checksum/padding, reserved/token types, wrong network,
noncanonical case and BCH scripts on DOGE/BTC fail. No account ownership changes.
Added published CashAddr test vector and rejection tests in network/tests.rs.
Source: https://upgradespecs.bitcoincashnode.org/cashaddr/.

Baseline build/bch-p2sh32-baseline-20261013; final patch
upstream-bch-light-p2sh32.patch follows bch-light-funding-terms in verifier.
Initial compile failed on Vec-to-array error conversion, fixed using slice conversion.
Corrected all-features/all-targets check and patch audit passed; old failure logs
preserved. Release litecoin_light suite and helper build still running at this entry.

Python test_light_wallet_chain.py adds optional BCH-only --p2sh32. Node encodes
OP_TRUE HASH256 output itself; helper prepares/signs/sends, node judges signatures,
and fixture verifies exact output script/inclusion/change. Existing default flow
unchanged. Preflight node accepted address/script, saved in
build/bch-p2sh32-node-address-20261013.json; this alone is not payment acceptance.
Do not repin packaged helper before full suite and actual-chain fixture pass.

Acceptance update 14 October: corrected release wallet suite exited0 with
202 passed / 8 ignored. Actual BCH P2SH32 payment and ordinary BCH/DOGE regressions
all passed. Reports build/bch-p2sh32-{payment,bch-regression,doge-regression}-20261014.json.
P2SH32 fixture uses OP_TRUE test-script hash, not negotiated covenant spending;
it proves destination encoding/payment/node acceptance, not a swap.

Current packaged helper build/bch-p2sh32-helper-tested-20261014 SHA256
c6ee291abac889a38cb6c31531b0eab5619463c836d3f3c1b3facbe3f53d0036, mode0500/nlink1.
CMake cache repinned and desktop rebuilt; GUI SHA256
5810a11dd0a885da44efc34e21cfa58ba66a22b2455bf7fe560e8b6e5cb53461.
Bundle checks passed (valid/missing/modified/opt-out); native60passed21skipped.
No production QML changes, so no redundant QML suite rerun. Existing QML92 remains
prior evidence. Patch audit passed. All sessions terminal and owned nodes cleaned.
Provenance build/bch-p2sh32-validation-20261014.json. Goal stays active.

Next: implement actual BCH local-wallet funding claim/signing admission using
new light_funding_terms/check_light_funding_payment and P2SH32 LightAddress.
Keep BCH legacy signed txid ordering; do not remove LTC PSBT SegWit guard or
fabricate BCHN wallet-lock records. Existing funding/signing.rs signed restoration
is tied to node lock intent, so local-source binding must be explicit and tested.


## 14 October 2026 — BCH retained signed wallet export and trade verification

New ClaimedSpend::retained_bch_funding returns immutable RetainedBchFunding
(unsigned transaction, exact signed transaction, owned previous outputs). Validates
expected claim, BCH ForkID chain, operation UUID, all durable input claims, saved
signing intent and signatures via existing load_signed. No signing, mutation,
chain freshness or relay assertion. Existing LTC recovery PSBT SegWit guard stays.
Test proves signed/unsigned txids differ, cold reopen returns identical signed
bytes, wrong chain/operation/commitment and changed manifest fail without mutation.

AdmittedExecution::verify_light_signed_funding consumes that retained wallet data,
requires quote network/genesis/trade binding, derives covenant payment and minimum
change from admitted terms, verifies full source transactions against owned
prevouts and checks exact signed funding through FundingPreview::verify_signed.
No preparation publication yet. Test covers real local signatures and negotiated
BCH/XMZ agreement for mainnet/testnet/regtest identity contexts (offline only).
Wrong network/commitment/source and expired/premature requests fail.

Node name main/test differs from quote name bch-mainnet/bch-testnet; now mapped
explicitly. First export-only test passed, final all-features/all-targets check
passed, ordered patch audit passed. Broader bch_ release suite still running at
this entry; see following acceptance record. Final patch
upstream-bch-light-signed-export.patch, baseline build/bch-signed-export-baseline-20261014.
No app/helper repin: these new APIs are not connected to runtime commands yet.

Next is durable local-source admission: retain explicit local source binding and
exact signed funding under the admitted execution lease, reject source replacement
or mixing node lock records, and make recovery restoration use the validated local
record. Recovery/relay methods currently use node wallet deployment/locks; do not
fabricate BCHN wallet-lock intents or remove stale/unknown relay checks. Integrate
helper/GUI only after source binding/restart/recovery tests and node-backed swap
acceptance. Packaged helper remains c6ee291a...0036 (P2SH32 accepted checkpoint).

Current validation handoff: session44792 is LIVE (final bch_ release suite).
Do not rerun/restart on observation timeout. Log
build/bch-signed-export-final-tests-20261014.log. New retained-export and
admitted-trade tests passed; all three offline BCH identities covered. The last
remaining test reported running >60s: wallet_locks::durable_bch_wallet_locks_reconcile_ambiguous_results_and_wallet_restart.
Poll the same session until terminal before accepting suite. Final check session97959
already exited0; patch audit passed. Earlier export-only session73389 exited0.
No other owned app/node/build sessions remain. Next after successful suite: record
provenance/roadmap acceptance, then implement durable local-source binding and
recovery admission. Do not mark full goal complete or repin helper for unused APIs.


## 14 October 2026 — durable local BCH funding / recovery targeted acceptance

New funding/light/retention.rs: immutable local-signed record precedes shared
preparation publication. It contains admitted context, network/claim, exact
Template and signed bytes. Retain revalidates owned Claim and signed trade terms,
rejects existing node/partial preparations, preserves original timestamp on exact
retry. Cold restore revalidates canonical record, admitted policy at historical
time, source/signature proof and matching shared preparation pin. Missing pin is
incomplete publication. Node preparation/locking reject any local record; local
readback rejects node wallet preparation/lock/signing/reconciliation markers.
restore_signed_bch_funding dispatches to local record if present, never falls back
on malformed/partial local state. No node lock intent is fabricated.

Targeted admitted_bch_trade_verifies_exact_local_wallet_signed_funding passed in
15.74sec across all3 offline network identities. Added same-byte retry, replacement
signed transaction rejection, time rejection, cold reopen, missing-pin repair,
source-mixing/corrupt-record rejection, authenticated refund exchange, final signed
funding txid in cancellation, recovered native spend key and cold private-recovery
commitment tests. All-features/all-targets check and ordered patch audit passed.
Evidence build/bch-local-retention-targeted-validation-20261014.json. No runtime
helper, app or CMake repin this turn; packaged helper remains c6ee291a...0036.

TWO LIVE TEST SESSIONS at this entry:
- 44792: PREVIOUS signed-export bch_ suite, log build/bch-signed-export-final-tests-20261014.log.
  Its binary was frozen before new release build as build/bch-signed-export-fixture-20261014
  SHA256 0f7eff62dbec7e188ea11c2ffd67d6e07dd62498cf9a52fb7556e7cef89b1e3f.
  Source versions for changed files are in bch-local-retention-baseline-20261014;
  do NOT hash current changed files and claim prior test covered retention.
  Long durable wallet-lock test remains active; observed native-proof-received
  metadata advances and CPU use, not a missing/terminal handle. Do not restart it.
- 35047: NEW local-retention bch_ suite, log build/bch-local-retention-regression-20261014.log.
  Direct release test binary, started after targeted acceptance. This is a new
  regression required by changed signing restore/source guards, not a retry of44792.
All check/targeted/build sessions terminal. Poll both handles to completion before
reporting full regression acceptance. Preserve unknown timeouts as live waits.

Patch upstream-bch-light-local-retention.patch follows signed-export patch; baseline
build/bch-local-retention-baseline-20261014. Next: finish regression evidence and
connect local funding to helper execution with saved local source/endpoint binding,
then explicit relay/reconciliation and desktop integration. Existing relay,
cancellation/refund release and success paths still consult node wallet deployment
intents; do not bypass those checks or claim full light-wallet swaps yet.


## 14 October 2026 — BCH local first-send implementation under validation

New light/relay.rs review_light_bch_funding_send/send_light_bch_funding validates
same-tip inputs against trade confirmation depth, exact retained source/claim,
private recovery, quote time, funding-message encodability and explicit approval
binding to signed bytes/recovery/tip/endpoint. Delegates publication to existing
wallet TLS broadcast (durable intent before network, no key unlock/re-signing).
ObservedClaimedSpend::check_funding_inputs_on uses existing inclusion revalidation.
retention.rs require_local_funding_claim compares original immutable source marker
and signed transaction against wallet claim. No helper/runtime integration yet.

Important discovered edge: BroadcastStatus is PER ENDPOINT. A global submission
marker can exist while another endpoint reports NoLocalAttempt. First-send input
check now calls ensure_not_submitted before observing inputs, preventing endpoint
switches from bypassing reconciliation. Method visibility broadened only to the
signing::review module, not public. Initial fix compile failed on private method;
corrected check passed. Existing LTC first-send already reaches that global guard
through funding_psbt_on. Do not mistake endpoint-local absence for never sent.

New tests/light_send.rs extends existing admitted trade test with synthetic BCH
regtest proof/header chain via public ReadSession duplex protocol (not actual
chain acceptance). Verifies shallow-depth rejection despite wallet minimum1,
missing recovery/input proofs, wrong claim, observed spend, expiry/time and changed
endpoint, wrong consent without intent, cancellation of owned loopback TLS attempt
leaving OutcomeUnknown, repeated first-send rejection. Added explicit switched-
endpoint rejection after unknown attempt. No production TLS bypass or live funds.
Initial targeted test passed before endpoint guard extension; corrected release
test still building at this entry. Check and ordered patch audit passed.

New patch upstream-bch-light-first-send.patch follows local-retention patch.
Baseline build/bch-light-send-baseline-20261014 includes all5 changed preexisting
files (2 new files have no baseline). Source/claims-broadcast visibility baseline
was added when correcting access. Packaged helper remains c6ee291a...0036.

Previous retention binary frozen before new compile:
build/bch-local-retention-fixture-20261014 SHA256
016c018c09d7c0b3c82f817d2cc067b704f9be031ab00d47c053158e30d83c67.
Session35047 continues using that version. Its changed source versions are in
bch-light-send-baseline-20261014; don't attribute current relay tests to that run.

Next after targeted result: scoped proof metadata and roadmap. Then implement
trade-bound cold observation/explicit retry and original endpoint handling (global
marker written before endpoint intent is a partial-publication boundary); wire
runtime BCH helper with selected wallet, allocated owned refund script check,
preparation/recovery/send approvals and GUI. Current engine unit quote refund
script is fixture-selected, not a demonstrated local-wallet refund balance.
Full node-backed swap acceptance remains required; public gates remain off.

Targeted acceptance update: corrected first-send test exited0, 1 passed in19.96sec.
14 claims regression tests passed using same release binary. All-features/all-
targets check and final patch audit passed. Evidence
build/bch-light-send-targeted-validation-20261014.json. Initial private-access
compile failure logs preserved; corrected-* logs are authoritative. No runtime
helper/app binary rebuilt or repinned. No new broader bch_ suite was started for
first-send: the active prior suites cover prior source versions; new methods are
covered by targeted test and claims regression, not attributed to old runs.

Still LIVE: session44792 (signed-export bch_ suite), session35047 (local-retention
bch_ suite). Both progressed in owned fixture state and CPU; long wallet-lock test
covers many nested stages. Latest observed older run reached redemption-relay ack;
newer progressed through native-proof receipt. Neither is terminal. Poll same
handles, never restart for elapsed time. Corrected build/test session8016 terminal0;
all other first-send compile sessions terminal (15782 failed before visibility fix).

Next prioritize trade-bound cold observation and explicit retry / original-endpoint
partial intent handling, then runtime helper + GUI and actual full swap fixtures.
Do not mark full goal complete or enable public markets. Current packaged helper
still bch-p2sh32-helper-tested-20261014 c6ee291a...0036. Current engine lib test binary
includes first-send methods; older regression binaries are frozen as documented.


## 14 October 2026 — BCH cold observation / explicit retry targeted pass

New light/relay/retry.rs persists LocalSendIntent (admission, approved_at, exact
review details+commitment) BEFORE wallet submission. First-send review rejects
any prior trade intent even if wallet publication is absent. Retry reconstructs
original send review against saved source/recovery and exact original endpoint;
trade-intent-only state covers interruption before wallet/global endpoint markers.
Approval is fresh and bound to original intent hash, signed bytes, endpoint,
observation tip/position and local status. Current quote policy required because
retry can newly publish; cold observation has no expiry gate. Included funding,
stale tips, changed endpoints/claims/approval/time and malformed intents rejected.
Observation checks wallet/trade identity before and after read-only transport.
No re-signing, input replacement or auto-retry. This is not power-loss acceptance.

Expanded tests/light_send.rs uses public ReadSession with synthetic pending and
included responses. Reopens wallet claim, simulates trade-to-wallet intent gap by
holding two wallet markers aside, rejects old review after status changes, retries
via owned-loopback TLS timeout, and verifies regenerated wallet markers, signed tx
and original trade intent bytes unchanged. Adds missing-intent/corrupt-intent,
wrong-endpoint/expiry/time, stale-observation and included-payment rejection.
Targeted test session86620 exited0: 1 passed, 26.30sec. Check session66658 exited0;
patch audit passed. Provenance build/bch-light-retry-targeted-validation-20261014.json.
Patch upstream-bch-light-retry.patch after first-send; baseline
build/bch-light-retry-baseline-20261014. No app/helper rebuild or CMake repin.

Still LIVE when last polled: session44792 signed-export bch_ suite and session35047
local-retention bch_ suite. Old run progressed into native-payout relay ack; newer
into native funding/proof state. Do not restart either on elapsed time. Frozen
binaries/source baselines described above preserve their actual version scope.
They do not validate these newly added relay/retry methods. No other live sessions.

Next concrete gap: existing funding/message.rs calls acknowledged_bch_funding_at,
which still calls node-backed bch_funding_relay_state/funding_relay_materials and
requires node wallet lock intent. Add explicit local relay acknowledgement record
only after matching wallet endpoint acknowledgement; handle wallet-ack/trade-ack
interruption by validating/importing the saved receipt without network mutation.
Then make shared historical state and funding-message acknowledgement choose the
validated local source, retaining unchanged node path and no fabricated node lock.
Observation/inclusion alone must not silently create a server-ack record. Keep
ack distinct from confirmation. After this, runtime BCH local helper, allocated
refund-script ownership, GUI and actual paired-chain acceptance remain required.


## 14 October 2026 — BCH local acknowledgement and peer funding handoff

Added funding/light/relay/acknowledgement.rs. reconcile_light_bch_funding_ack
validates the exact saved local source/claim, original send intent and original
endpoint's ServerAcknowledged wallet status before retaining local-relay-ack.
Successful first-send/retry now import that receipt. Restart reconciliation has
no network/key access or current-expiry gate. saved_local_send reconstructs the
original reviewed context without requiring an open wallet; shared relay state
and acknowledged_bch_funding_at dispatch on the validated local source. No node
wallet-lock records are fabricated. Mixed node relay records and orphan local
relay markers fail closed. Observation/inclusion never becomes server ack.

Targeted test passed (66.75s): source/endpoint binding, unknown/included rejection,
simulated saved-wallet-receipt import, exact replay, corrupt/orphan/mixed records,
authenticated peer funding frame, receiver acceptance and cold sender reopen.
The positive receipt is a persisted fixture, NOT live TLS/server acceptance.
Initial test failed correctly on a fixture receipt created with broad permissions;
fixture now uses create_new mode0600. Production permission checks unchanged.
Retain original failure log bch-light-ack-test-20261014.log; corrected-test and
corrected-final-check logs are authoritative. Allfeatures/alltargets and audit
passed. Evidence build/bch-light-ack-targeted-validation-20261014.json.
Patch upstream-bch-light-acknowledgement.patch follows retry; source baseline is
build/bch-light-ack-baseline-20261014. Packaged helper/app untouched.

Regression handles:
- 44792 TERMINAL exit0: older signed-export suite 52passed/14ignored/0failed,
  2782.64s. Evidence bch-signed-export-broad-validation-20261014.json, frozen binary
  bch-signed-export-fixture-20261014. Does NOT cover later retention/send/retry/ack.
- 35047 still running older local-retention suite. Preserve handle/log and scope.
- 31368 newly running current bch_ regression, log
  build/bch-light-ack-regression-20261014.log; runs frozen binary
  build/bch-light-ack-fixture-20261014 (hash in targeted evidence).
Do not restart these merely for elapsed time. All other new handles are terminal.

Next concrete work: bind BCH quote refundScript to an allocated address from the
selected wallet before preparing new funding, using existing AddressBook ownership
logic (LTC helper dex.rs verify_refund_wallet is the precedent). BCH terms expose
script bytes; LightAddress::from_script can render canonical BCH address. Build
local helper commands around verified BCH quote/admission records, exact approvals,
local configs/leases and existing shared wallet APIs, then GUI and actual paired
BCH/XMZ chain acceptance. Existing personal-payment helper is packaged; new BCH DEX
methods are not. Several settlement paths still require node deployment/locks;
resolve source-aware observation/settlement without inventing wallet-lock records.
Goal remains active. Wallet files/keys/recovery records remain on user's system.


## 14 October 2026 — BCH local refund-wallet binding accepted

retain_light_signed_funding now requires &AddressBook immediately after &ClaimedSpend.
After signed/quote verification and BEFORE any local-source/shared-preparation
writes, it converts recovery_plan.terms().bch_funder_refund to LightAddress and
calls claim.check_allocated_destination(book,address,expected). This validates
all saved claim records and the exact funding account. AddressBook's new
require_allocated_address explicitly checks account.network() as well as the
legacy account identity hash, then checks the retained allocation without writes.
Do not weaken to genesis alone: BTC/BCH share genesis. Existing historical restore
APIs are unchanged; they do not allocate or demand a new live wallet lease.

The local BCH test now constructs its signed quote with the funding wallet's
actual derived refund address on each offline BCH network identity. It rejects
unallocated and other-account books before local marker/preparation writes,
checks allocation bytes unchanged, and proves the constructed recovery refund
transaction pays that selected account. It still does NOT prove chain inclusion.
New allocation test imports the exact xpub under BTC/BCH identities, demonstrates
legacy hash equality, and verifies explicit coin/network rejection.

Final target session3267 exit0: 1passed/67.62sec. Allocation session28282 exit0:
9passed, including cross-coin test. Final allfeatures/alltargets session94433 exit0;
ordered patch audit passed. Earlier interim target24214 exit0 predates the explicit
network check; use final logs. Evidence build/bch-light-refund-wallet-validation-20261014.json.
Frozen binary build/bch-light-refund-wallet-fixture-20261014; hash in evidence.
Patch upstream-bch-light-refund-wallet.patch follows acknowledgement; baseline
build/bch-light-refund-wallet-baseline-20261014. No app/helper rebuild/repin.

Regression status correction:
35047 is TERMINAL exit101: 51passed/1failed/14ignored, 2795.77sec. Failure in
funding/claims.rs different_trades_cannot_claim_the_same_input_or_partially_claim_on_conflict,
error Litecoin journal is busy. Ten isolated reruns on the SAME frozen older
binary passed; do not erase failure or claim root cause proved. Evidence
build/bch-local-retention-broad-validation-20261014.json. The newer acknowledgement
run passed that individual test. 31368 remains LIVE, with frozen ack-version binary
and build/bch-light-ack-regression-20261014.log. It does not cover refund-wallet work.
No other test/build sessions live from this turn. No broad refund-version run yet.

Next: local BCH helper/GUI integration. native-ltc-light.rs dispatches existing
LTC DEX modes to native-ltc-light/dex.rs; BCH admission helper provides the verified
quote/admission config model and private leases. A BCH local config must bind
wallet config, selected account/book, verified quote/identity pins/policy/admission,
trade directories and request approvals. Add refund ownership preflight BEFORE
wallet input/change preparation as well as the now-enforced retention boundary.
Use LightAddress::from_script for quote refundScript; AddressBook::require_allocated_address
is public for helper preflight. Existing payment APIs, BCH terms, retain/sign/send,
observe/retry/reconcile-ack are available. Do not invent node lock records or enable
public markets; node-bound settlement paths and real paired-chain acceptance remain.


## 14 October 2026 — BCH terms exposed through actual local helper

native-ltc-light.rs now dispatches --bch-dex-funding-terms-stdio-v1 to included
native-ltc-light/bch_dex.rs. Config schema monzero-private-bch-light-dex-v1:
walletConfigFile, quoteFile, quoteCommitment, admittedAt (QUOTE admission time),
identityPins[2][32], policy, tradeId, metadataDirectory, snapshotDirectory,
negotiationPins. Requests monzero-bch-light-dex-funding-terms-request-v1 contain
only schema/requestId/approvedConfigDigest. Digest is SHA256 of canonical JSON
{schema:monzero-bch-light-dex-context-v1,dexDigest,walletDigest,quoteDigest}, each
inner digest SHA256 of exact file bytes. Response echoes context/request/trade,
executablefalse, and result light_funding_terms plus canonical BCH contractAddress
and refundAddress. Scope isolated BCH regtest. No network/keys/claims are opened.

Helper holds private config leases, restores signed quote at its admittedAt,
checks real selected BCH genesis, resumes BchFunder execution and validates
saved_admission_review against configured pins. Requires allocated refund in
selected book/account. Existing private_config delegates to private_config_limit
with SAME4096 limit; only BCH quote reader uses16384, retaining uid/mode0600/nlink,
canonicalpath/inode/race checks and bounded reads. Stdin4096 bounded/zeroized;
unknown secret fields fail and generic stderr remains unchanged.

Actual subprocess test light_helper::bch_local_terms_helper_checks_quote_admission_and_refund_wallet
is ignored by default and requires MONZERO_BCH_LIGHT_HELPER plus SHA256. Test now
uses admit_reviewed, since low-level admit lacks the saved consent receipt that
helper correctly requires. Original failing log retained. Final run 1passed/1.15s;
valid/unallocated/refund, changed config digest, bad negotiation pins, wrong wallet
network, unknown password field and unchanged snapshot/metadata/book contents.
No cache directory was created and no endpoint/vault existed in fixture.

Staged helper build/bch-light-terms-helper-20261014 SHA256
ad4c5adf91cb9c8d2a29c44c23706952ca74ef8078434986f515fb263cb61cfa, mode0500.
Frozen test binary build/bch-light-helper-terms-fixture-20261014 (hash in evidence).
Separate real BCH-node P2SH32 personal payment passed using same helper:
build/bch-light-helper-terms-payment-20261014.json; not a DEX covenant swap.
Allfeatures/alltargets final check passed, patch audit passed. Evidence
build/bch-light-helper-terms-validation-20261014.json. Initial E0753 include-file
comments fixed to ordinary comments; original failure logs retained. Patch
upstream-bch-light-helper-terms.patch follows refund-wallet; baseline
build/bch-light-helper-terms-baseline-20261014. Desktop bundle/CMake pins untouched.

Still LIVE:31368 frozen acknowledgement-version bch_ regression. Latest observed
owned fixture records progressed through native funding and success receipt.
Do not restart due elapsed time. No new build/test handles left live this turn.
Older local-retention full-run journal-busy failure remains documented/unresolved.

Next: extend bch_dex.rs with prepare/review/sign and then send-review/send/observe/
retry-review/retry/reconcile-ack, using existing wallet primitives and BCH engine
APIs. Preserve config digest, same-chain inputs, quote confirmation minima, refund
allocation preflight BEFORE address/input reservation, explicit independent signing
and broadcast consent, original endpoint/exact signed-byte retries. BCH engine
retain_light_signed_funding now also requires &AddressBook after claim. For source
transactions use independently verified selected-chain wallet inputs; do not trust
unverified caller-supplied prevouts. Existing native-ltc-light/dex.rs is LTC workflow
reference, not a BCH protocol shortcut. Then add Qt job/UI integration (current
LitecoinLightFundingJob is deliberately LTC-schema-only) and actual BCH/XMZ paired
chain success/refund/restarts. Node-bound later settlement paths remain outstanding.


## 14 October 2026 — BCH helper preparation/review on actual funded chain

bch_dex.rs now dispatches terms/prepare/review through
--bch-dex-funding-{mode}-stdio-v1. Existing strict TermsRequest retained. FundingRequest
requires schema/requestId/approvedConfigDigest/maximumDownloadHeaders/walletScan/
payment; retainedReview is required only for review and forbidden for prepare.
Unknown secret/approval fields fail. Context/quote/reviewed admission validation
is shared with terms. All routes isolated BCH regtest. prepare() checks exact
payment via engine, requested input depth >= dex.policy.bch_confirmations, exact
selected account/book/refund allocation BEFORE cache/network/input selection.
After selected-tip synchronization and wallet refresh, recheck quote validity,
then prepare_payment or reopen_payment under dex.trade_id. No signing or trade
signed-source retention yet. Result uses unsignedTxid instead of txid (P2PKH
signatures change final txid), broadcastAuthorizedfalse, watchOnlytrue.

New monzero-gui/tools/dex-engine-port/test_bch_light_preparation.py is invoked
from ignored Rust test bch_local_preparation_helper_uses_real_confirmed_wallet_inputs.
Refactored light_helper.rs fixture shares all earlier terms assertions; optional
probe requires pinned script env/hash and explicit owned node/report paths.
Actual BCH node/TLS fixture pays the retained wallet refund address. Rejects
weakened requested depth and wrong amount without even querying peer; actual
one-confirmation input is rejected without changing book/claims/trade records.
After second confirmation, prepare succeeds; review and prepare retry produce
same commitment/unsigned txid/records, changed commitment rejected. No sign records,
no broadcasts, mempool empty. All resources owned and stopped/removed by fixture.

Accepted: targeted test71651 exit0, 1passed/6.99s. Actual-chain report
build/bch-light-helper-prepare-chain-20261014.json. Allfeatures/alltargets44995 exit0,
release helper58894 exit0, lib testbuild20301 exit0, patch audit passed. Evidence
build/bch-light-helper-prepare-validation-20261014.json. Helper frozen at
build/bch-light-prepare-helper-20261014 SHA256
ffe594bda38099711687565e4aa28d5327e94c22e98789175d32c91fc39746aa, mode0500.
Frozen test binary build/bch-light-helper-prepare-fixture-20261014. Probe SHA256
ae3b85a9dedd78daf2aa7aedc2cc558f39fd84190b65bbb909020620cdf6cbb9.
Patch upstream-bch-light-helper-prepare.patch follows helper-terms; baseline
build/bch-light-helper-prepare-baseline-20261014. No app bundle/CMake repin.

Only live session remains31368 (acknowledgement-version broad bch_ suite). Re-polled
live at checkpoint; do not restart for elapsed time. This frozen older suite does
not cover refund-wallet/helper changes. Original journal-busy full-run failure on
older local-retention version remains documented. Other current handles terminal.

Next concrete dependency before signing: WalletObservation verifies IncludedTransaction
but that type retains only effects/proof, not raw source bytes. ReadSession's
reported_transaction(txid) is PRIVATE. Do not expose unchecked caller prevouts or
bypass inclusion. Add a bounded source-export method inside session/wallet, selecting
ONLY this observed spend's exact inputs from verified scan evidence, checking
same selected chain/tip and trade confirmation minimum, then fetching raw txs through
private reported_transaction (which validates txid) and returning exact serialized
sources. Existing payment_inputs is private; reopen_payment already shows exact
input matching. Use sources to validate_bch_funding_preview before local key unlock,
then sign via existing raw/mnemonic vault APIs with explicit approval, and
retain_light_signed_funding(claim,&book,network,expected,sources,now). No automatic
broadcast. Add review/sign helper modes and tests, then send/observation/retry/ack
with existing engine methods. Qt LitecoinLightFundingJob remains LTC-only; add BCH
integration rather than silently accepting another protocol in its current checks.
Full paired-chain swaps/settlement/recovery and public-release gates remain open.


## 14 October 2026 — verified BCH funding sources used by helper preview

Added ReadSession::funding_sources_on(chain,&WalletObservation,&ObservedClaimedSpend,
minimum_confirmations) in litecoin_light/session/wallet/payment.rs. It matches
ONLY exact prepared inputs to scan candidates, checks network/input count/depth
bounds, calls check_funding_inputs_on before/after export, deduplicates source
txids, fetches through private reported_transaction (canonical raw/txid check),
limits total bytes2MB and poisons session on source/size error. No claims, keys,
signing or broadcast writes. Raw witness data is not independently authenticated;
API docs retain this limitation. Existing scan evidence anchors prevout effects.

bch_dex prepare/review now keep session until sources are fetched, validate the
BCH FundingPreview against exact sources/change/fee/minimum and admitted quote,
then return fundingVerifiedtrue/sourceTransactions count. Selected change script
is used when present, otherwise the already-validated owned refund script. No
signing mode yet. Sources remain local; response contains count, not raw source data.

Actual-node probe extended: during review's second transaction.get (source export,
after refresh), serve a DIFFERENT VALID chain transaction. Helper rejects, no
claims/book/trade-record changes, normal reconnect/review succeeds. Positive
preparation confirms fundingVerified and one source. Probe passed8.82s, session34393
exit0. Allfeatures/alltargets6372 exit0, release helper87941 exit0, patch audit passed.
Evidence build/bch-light-funding-sources-validation-20261014.json and chain report.
Staged helper build/bch-light-funding-sources-helper-20261014 SHA256
adcf9d6d4db13f9d4b5542571f59df7dc94eaf6f088324fb734355d2917e8437.

IMPORTANT test scope: reused frozen build/bch-light-helper-prepare-fixture-20261014
ONLY as unchanged negotiation-record producer/subprocess driver. Updated Python
probe and NEW pinned helper executed new source code. No new release lib-test
binary built this turn; target/release/deps/swap_machine-* binary still predates
source-export changes. All-target dev check includes current source. Do not
attribute new API tests to an old direct lib-test invocation. Broad31368 remains
LIVE on its own frozen acknowledgement version; re-polled at checkpoint. No other
live handles. No helper/app packaging/repin. Older journal-busy failure unresolved.

Patch upstream-bch-light-funding-sources.patch after helper-prepare; baseline
build/bch-light-funding-sources-baseline-20261014 includes old Python probe copy.

Next concrete step: helper sign mode with explicit exact retained signing approval
and password only in private stdin. Reuse vault_format::sign for raw and mnemonic
vaults (already validates observed inputs and matching vault/account). Validate
quote and funding preview/sources before unlock; compare approved commitment with
prepared.signing_commitment, never automatic sign. Then call mutable execution's
retain_light_signed_funding(prepared.retained_claim(),&book,network,expected,&source_refs,now)
to bind signed bytes/preparation to trade; report FINAL signed txid separately
from unsignedTxid. No broadcast. Use same-trade exact retries, fail safely on
partial publication. Extend actual-node probe with locally created disposable
vault and wrong approval/password/changed review tests, then later send/recovery
and Qt job/UI. Full settlement paths still need source-aware node integration.


## 14 October 2026 — actual BCH helper signing, raw and BIP39 vaults

bch_dex mode sign added. FundingRequest requires retainedReview for review/sign,
approval/password ONLY for sign; unknown fields still denied. After quote/refund,
fresh wallet scan, source export and funding-preview validation, helper compares
approval with prepared.signing_commitment, calls vault_format::sign (raw or BIP39),
then execution.retain_light_signed_funding using the same held wallet claim and
address book. Result signedtrue/watchOnlyfalse, final txid plus unsignedTxid,
tradeFundingRetainedtrue, broadcastfalse and broadcastAuthorizedfalse.

New ObservedClaimedSpend::into_retained_claim(self) consumes observation wrapper
WITHOUT releasing durable claim journal lease. This avoids exposing private
retained_claim getter or a close/reopen lock gap. It grants no new evidence or
consent. Helper's execution is now mutable. Initial compile failures (private
access and name shadowing) corrected; original log retained, corrected-check
is authoritative.

Extended ignored real-helper test bch_local_signing_helper_checks_both_local_vault_formats:
fixture uses SeedVault::create and MnemonicVault::create with a stored BIP39
passphrase, each local/ephemeral. Updated Python probe detects configured vault,
rejects wrong approval/password/changed fee with no writes, signs, verifies exact
raw tx and contract output with actual BCH Node, checks testmempoolaccept allowed,
then repeats signing and asserts all bytes/records unchanged. Mempool empty and
no Electrum broadcast. Rust cold-resumes trade and matches signed txid after helper
exits. Two reports .raw.json/.bip39.json under build/bch-light-helper-sign-chain-20261014.
Target32951 exit0, 1test (both formats) passed28.89s. Allfeatures/alltargets63817 exit0,
release55916 exit0, libtestbuild94408 exit0, patch audit passed. Evidence
build/bch-light-helper-sign-validation-20261014.json. Staged helper
build/bch-light-sign-helper-20261014 SHA256
8cf9cc4da0203750881f512c0e3564155a6b652a59905d161d8fc97f56565d55, mode0500.
Frozen current lib binary build/bch-light-helper-sign-fixture-20261014 (hash in evidence).
Patch upstream-bch-light-helper-sign.patch follows funding-sources; baseline
build/bch-light-helper-sign-baseline-20261014 includes prior Python probe. No app
bundle/CMake repin. Signed funding is not a complete swap and was not broadcast.

Regression status:
-31368 TERMINAL exit0, frozen acknowledgement version 52passed/14ignored/0failed,
 2767.82s. Evidence build/bch-light-ack-broad-validation-20261014.json. Includes claims
 test previously journal-busy; older failure still recorded, root cause unproved.
 Does not cover subsequent refund-wallet/helper changes.
-5075 is LIVE: current frozen signing binary bch_ --test-threads=1, log
 build/bch-light-helper-sign-regression-20261014.log. Serial to avoid simultaneous
 fixture process launches while assessing the earlier transient lock issue.
 Do not restart for elapsed time. All other current handles terminal.

Next: helper send-review/send, observe/retry-review/retry and acknowledgement
reconciliation. Existing engine methods support these but send/retry consent is
separate, original endpoint/exact signed bytes enforced, private BCH recovery must
be retained before first publication. Do not bypass protection to make a test pass.
Source export currently filters and verifies exact observed inputs internally;
engine review_light_bch_funding_send also takes IncludedInput proofs. Consider a
checked WalletObservation::funding_inputs_on method reusing the existing matching
logic rather than duplicating/exposing unchecked metadata. Cold observation/retry
must not depend on unspent-input scans after publication. Existing recovered_payment
can reopen the claim; engine validates local source and saved send intent.

Then Qt job/UI (current LitecoinLightFundingJob remains LTC-only), package only
after actual helper/app validation, and full BCH/XMZ paired-chain settlement and
restart coverage. Several later settlement paths still node-deployment-bound.
All public release gates and overall goal remain open. User mining untouched.

## 14 October 2026 — BCH helper historical observation accepted

Added strict password-free `--bch-dex-funding-observe-stdio-v1`: reopen the
retained signed claim, synchronize the selected chain, validate the admitted
local trade source, report position and endpoint-local send status separately.
Historical observation bypasses current quote expiry without bypassing source
or admission checks. No signing, send intent or acknowledgement is inferred.

Actual-node ignored fixture `bch_local_observation_helper_tracks_publication_and_quote_expiry`
passed in 62.86 seconds: absent, pending, included, then included after expiry;
fresh review/sign rejected after expiry; password rejected; wallet/trade records
unchanged. Publication was by the disposable fixture directly, not the helper.
Evidence: `build/bch-light-helper-observe-validation-20261014.json`.
Staged helper: `build/bch-light-observe-helper-20261014`, SHA256
`4b7571df6361fee33b6a73c623848b3d87f219a509a713adf7ac6a062b93a15c`.
Frozen fixture: `build/bch-light-helper-observe-fixture-20261014`.
All-features/all-targets check, release builds and ordered patch audit passed.
Incremental patch: `upstream-bch-light-helper-observe.patch` after sign patch.

The desktop package remains unchanged. Next: checked scan-derived funding-input
access for helper send review; private recovery before publication; explicit
relay/retry/acknowledgement; Qt integration and full paired-chain recovery.
Signing-version broad regression session 5075 remains running (frozen binary);
its log is `build/bch-light-helper-sign-regression-20261014.log`. Do not restart it.
The earlier intermittent busy-journal failure remains unresolved, with its failed
log and successful isolated reruns preserved. Goal remains active; no user mining.

## 14 October 2026 — verified funding input access ready for helper relay

`WalletObservation::funding_inputs_on` now provides only exact retained payment
inputs from the verified scan, after checking network, selected chain, required
depth, input/confirmation bounds and the claim's global submission guard.
`ReadSession::funding_sources_on` uses this shared method and still rechecks after
network reads. This gives the helper the evidence required by the existing
BCH `review_light_bch_funding_send` / `send_light_bch_funding` methods without
exposing unchecked caller-created metadata.

Ten wallet scan tests passed, including exact-input, stale-chain, shallow-depth
and invalid-bound assertions. Actual BCH Node signing passed for both raw-seed
and BIP39 vaults (29.13 seconds); no transactions were broadcast. All-features /
all-targets compilation, release helper/test builds and ordered patch audit passed.
Evidence: `build/bch-light-input-access-validation-20261014.json`.
Patch: `upstream-light-funding-input-access.patch` after helper-observe.
Helper/fixture frozen as `build/bch-light-input-access-{helper,fixture}-20261014`.

Next implement helper send review/publication using the checked inputs and the
existing private-recovery guard, plus retry/ack recovery. Existing
`native-bch-admission.rs::recovery_retention_operation` already exposes local
review/retain/status for recovery and can be reused by the desktop orchestration.
The helper fixture will need the native peer's authenticated refund adaptor and
retained private recovery before a positive actual TLS publication test; existing
`tests.rs` around the local-source test and `tests/light_send.rs` demonstrate it.
Do not use observation's direct fixture publication as send-path acceptance.

Signing-version broad regression session 5075 / PID 763596 was confirmed live;
its separate frozen run is unchanged. No new broad duplicate was started.
Desktop package unchanged; goal active and no user mining.

## 15 October 2026 — BCH helper approved TLS publication accepted

New helper modes: `recovery-review`, `recovery-retain`, `send-review`, `send`.
Recovery uses its own strict request (schema/id/context/optional approval only)
and the existing reviewed recovery engine. Only identifiers/commitments leave
the helper. Sending reuses verified wallet scan/source checks and newly exported
`funding_inputs_on`, then existing review/send guards for quote, saved private
recovery, exact transaction, depth, global submission marker, tip and endpoint.
Send needs explicit matching approval and forbids a password. Durable intent and
acknowledgement use existing engine ordering. Helper remains regtest-only.

New ignored `bch_local_send_helper_requires_recovery_and_explicit_approval`
passed actual owned BCH Node/TLS integration in 33.76 seconds. Fixture sets up
the native peer's genuine authenticated refund adaptor, signs locally, rejects
send before private recovery, rejects wrong recovery/send approval/password,
retains private recovery, publishes through helper TLS, observes pending/included,
rejects duplicate ordinary send, cold-reopens acknowledgement and prepares the
peer funding message. Failed requests preserved watched wallet/trade records.
Probe optional `--send` mode retains existing prepare/sign/observe modes.

Evidence `build/bch-light-helper-send-validation-20261015.json`; frozen helper
`build/bch-light-send-helper-20261015` SHA256
`3e5e5fa0efdc0aff22fc4383d99088b49c0e1a88ef7831ea93ecad32ddd579e5`;
frozen test `build/bch-light-helper-send-fixture-20261015`.
Allfeatures/alltargets corrected check and release builds pass; initial check
failed only because test's reopened execution needed mutability for message
preparation. Original log retained. Ordered patch audit passes;
`upstream-bch-light-helper-send.patch` follows input-access patch. Baseline under
`build/bch-light-helper-send-baseline-20261014` (work crossed midnight).

Broad signing-version run session5075 has FINISHED: 52pass17ignored0fail in
2822.83sec; evidence `build/bch-light-helper-sign-broad-validation-20261015.json`.
No background regression needs preserving now. Next helper retry-review/retry and
ack reconciliation should reopen exact saved signed claim without requiring
fresh unspent inputs, use saved original endpoint/intent and fresh observation,
and distinguish read-only historical observation/ack from quote-valid new relay.
Then actual dropped-reply/process interruption tests, desktop job and full BCH/XMZ
settlement/refund/restart. No desktop repin, no public activation, no user mining.
Goal remains active. These results do not prove full swap completion.

## 15 October 2026 — BCH helper retry and process-crash recovery accepted

Added `retry-review`, `retry`, `reconcile-ack` modes using the observation request
with optional approval required only for retry. They recover the retained signed
claim without fresh unspent-input scanning. Retry calls existing engine guards
for original intent/endpoint, current quote, fresh selected-chain position and
separate consent. Local ack reconciliation runs before chain/cache/TLS work and
requires a saved original-endpoint wallet acknowledgement. No password accepted.

New ignored fixture `bch_local_retry_helper_recovers_a_dropped_send_reply`
uses probe `--retry`. It was run twice: default dropped reply (49.33s) and explicit
`MONZERO_BCH_PROBE_KILL_SEND=1` (49.47s). The latter holds the owned TLS reply
after actual node acceptance, verifies the helper is live, SIGKILLs only that
helper and waits for it, then releases the owned barrier. Fresh helper processes
observe outcome-unknown/pending, reject ordinary resend, wrong approval/password
and missing intent; approve exact-byte retry, confirm, reject included retry,
reconcile acknowledgement without peer requests and cold-reopen the trade to
prepare the peer funding message. No replacement payment or key unlock on retry.
Both passed; allfeatures/alltargets check, release builds and patch audit pass.
Evidence: `build/bch-light-helper-retry-validation-20261015.json`.

Frozen helper `build/bch-light-retry-helper-20261015`, SHA256
`7e0db192cbc38bf88887a28be1eca5d4a5324e41a92355f323fb75e06ef1f4e5`.
Frozen fixture `build/bch-light-helper-retry-fixture-20261015`.
Patch `upstream-bch-light-helper-retry.patch` follows helper-send.
No background tests remain from this checkpoint. Desktop package unchanged.
Next desktop BCH helper transport/context/UI integration, then actual full
BCH/XMZ localwallet success/refund/restart flows and downstream source-aware
settlement/payout paths. Process-kill acceptance is not power-loss acceptance.
Do not mark goal complete; public activation gates and other coin work remain.

## 15 October 2026 — BCH desktop context and explicit coin lease accepted

Added header `src/dex/BchLightFundingContext.h` using existing hardened local
file read/retain primitives from LitecoinLightFundingContext. BCH files are
separate `bch-light-dex-ui-<trade>-{payment,review,signed}.json`. Payment binds
configuration; review binds payment bytes and unsignedTxid; signed binds review
bytes and signedTxid. All proposed stages validate before writes; exact retries
are idempotent; incomplete/corrupt/orphan stages fail closed. Output clears on
failure. No secrets/approvals retained. Caller must validate the helper response
before retaining context; the new job remains to be implemented.

`LitecoinLightJob::fundingConfiguration` and `acquireFunding` gained optional
`expectedCoin`, default `ltc`; allow only ltc/bch, exact selected coin, regtest,
complete local wallet/vault/spend config and unchanged sources. Existing LTC
callers retain default restrictions. BCH job must pass explicit `bch` on every
configuration/lease access. Parameterized existing lease test covers both coins.

New CMake target `dex_bch_light_funding_context_tests`:54checks passed.
Full dex_tests:61passed0failed21skipped,52037ms. App rebuilt successfully.
Evidence `build/bch-light-desktop-context-validation-20261015.json` includes
app/helper/source hashes. Initial build could not see new target until explicit
CMake regeneration; configuration and subsequent builds passed. Tests live in
build/cachyos-multicoin/src, app in bin. No running tests from this checkpoint.

Next implement BCH-specific helper transport/response validator using the new
context, wallet coin guard, source pins including quote bytes, bounded stdout,
private password stdin and conservative unknown-outcome on cancellation/error.
LTC Job is schema-specific and must not silently reinterpret BCH. BCH review
hash domains/terms/results differ; unsigned preparation has unsignedTxid rather
than final txid, recovery requests have no payment, and reconcile-ack is local.
Then wire controller/QML and actual Qt-to-helper lifecycle tests before repinning
staged helper into app. Full BCH/XMZ success/refund/restart still outstanding.

## 15 October 2026 — BCH Qt funding transport accepted on actual node

Added `BchLightFundingJob.{h,cpp}` and `dex_bch_light_funding_probe` CMake target.
Job follows existing process/lease mechanics but has BCH-specific configuration,
request and response validation. Context digest includes exact dex/wallet/quote
bytes. Bound configuration/lease calls explicitly request bch. Source/hash checks
run before launch and before accepting response. Password only sign/private stdin,
output bounded, stderr discarded, cancellation/error outcome-unconfirmed. Full
BCH operations through terms/prepare/review/sign, recovery-review/retain,
send-review/send, observe, retry-review/retry, reconcile-ack. Review commitments
hash BCH domain+NUL+canonical details; relay endpoint and saved transaction bind.
Unsigned preparation uses unsignedTxid and writes distinct signed desktop record.
Recovery has no payment fields and does not overwrite payment context.

Probe is actual Qt job with selected `LitecoinLightJob("bch")`; checks funding
lease, blocked wallet receive/refresh and lease release. Python owned-node probe
now supports explicit SHA-pinned `MONZERO_BCH_QT_PROBE` and `_SHA256`. It writes
canonical wallet/launcher JSON for desktop, runs each request via Qt, and asserts
that insufficient-depth failure retains ONLY the desktop payment intent. Other
watched engine/wallet records remain unchanged. Initial test failed at configure
because the fixture used noncanonical JSON; gate not weakened, failed log saved.

Actual Qt dropped-reply/retry scenario passed55.96s; raw+BIP39 signing passed32.26s.
Full app rebuilt. Evidence `build/bch-light-desktop-job-validation-20261015.json`.
Frozen Qt probe `build/bch-light-desktop-job-probe-20261015`; helper remains
`build/bch-light-retry-helper-20261015`. Tests used frozen latest Rust fixture as
negotiation producer/test driver, with the updated hash-pinned Python probe and
newly built Qt binary. No Rust engine changes in this checkpoint. No running tests.

Next BCH funding controller plus DexEngine/QML binding. Existing
LitecoinLightFundingController assumes LTC recovery-frame/PSBT; BCH should use
its authenticated peer refund-protection and recovery-retention flow, not fake
LTC frames. Staged helper not yet bundled/pinned into app; new job compiled into
app via dex source glob, but no trading UI activation yet. Qt process-group kill,
full BCH/XMZ success/refund/restart and public-chain/release work remain.

## 15 October 2026 — BCH controller accepted; Claude cooperation active

User explicitly amended the goal to use Claude AI on this machine for assistance.
Local CLI `/home/tripo/.local/bin/claude` is available. Completed a source-only
read-only review via `claude --print --safe-mode --tools '' --strict-mcp-config
--mcp-config '{"mcpServers":{}}' --no-session-persistence --output-format text`.
Input contained only four public development sources; no wallet files/keys.
Review `build/claude-bch-controller-review-20261015.log`, input adjacent. Use
Claude for concrete bounded tasks going forward, independently verify outputs.
Do not interfere with unrelated existing Claude sessions/processes on machine.
Our review session14467 finished exit0; no reviewer task remains running.

Added `BchLightFundingController.{h,cpp}` and bound controller Qt probe/CMake
target. BCH signature review uses helper-validated signingCommitment; recovery,
send/retry use their own domain-bound review commitments. Configure binds exact
trade/quote and expiry. Controller clears displayed approval on operation/cancel,
checks expected request/operation, rejects expired consent, and hides job response
when request launch is rejected. Observations/ack remain usable historically.

Verified Claude findings and implemented: reject unexpected review fields on
non-review operations; require signing approval==retained commitment at request
and response; refresh retained context and require signed ID for recovery;
job m_finishing/m_starting guards block reentrant operations while releasing
wallet/start callbacks; QPointer guards surround start and callback handling.
Helper independently enforces expiry, per-trade/wallet locks and private recovery;
Claude's questions about these were resolved by existing engine evidence.

`dex_bch_light_funding_job_tests`:46checks passed, including injected review and
wrong signing approval (rejected before helper invocation) and changed response
commitment. Controller actual-node flow passed64.00s, including a wallet.changed
listener attempting reviewRetry during every completion: all blocked. Controller
probe freshly obtains review before explicit approve operations and validates
retained input fixture requests. Python mode `MONZERO_BCH_QT_CONTROLLER=1`
accepts controller-generated request UUID (job still verifies exact response ID).
Full app rebuilt. Evidence `build/bch-light-controller-validation-20261015.json`;
frozen probe `build/bch-light-controller-probe-20261015`. Initial controller
integration passed64.41s before review fixes; final evidence uses reviewed run.

Next DexEngine/session/QML wiring. LTC setup/controller expects PSBT recovery and
cannot simply be renamed into BCH peer flow. BCH config must come from admitted
BCH session with quoteFile, quoteCommitment, admittedAt, identityPins, policy,
tradeId, metadataDirectory, snapshotDirectory, negotiationPins and selected
walletConfigFile; restrict BCH funder, avoid mixed node/local source selection.
Staged helper still not packaged. No running tests in this checkpoint. Goal active;
full swap, Qt crash, public validation and other coin work remain outstanding.

## 15 October 2026 — BCH local wallet source selection

The engine now retains the selected local wallet before funding preparation.
Changed wallet identity, mixed node/local funding records and missing source
selection fail closed. Node funding rejects the selected local source before RPC.
The desktop funding job and controller now expose source selection with strict
request/response checks and the existing wallet lease; selection does not sign
or authorize broadcast. Admission exports light-wallet context only after a
validated BCH-funder status response with unchanged sources.

Validation: helper and desktop-controller owned-node dropped-reply recovery
passed; the controller run now performs selection through Qt. All 46 job
approval checks passed and the application rebuilt. Evidence:
`build/bch-light-desktop-source-validation-20261015.json`. The broader BCH
regression remains running in `build/bch-light-source-selection-broad-20261015.log`;
do not count it as passed yet. An initial app build used an invalid target;
the corrected `monzero-wallet-gui` build passed.

Next: connect authenticated admission and selected wallet to DexEngine/QML,
package the reviewed helper, then prove complete BCH/XMZ settlement/refund and
restart recovery. Public-chain validation and remaining coin integrations
remain required. Wallet keys, mnemonics and recovery records stay local.
The overall DEX goal remains active, not complete.

## 15 October 2026 — BCH desktop admission/wallet binding and funding panel

Added DexBchLightFunding.cpp and BchLightFundingSetup.h. DexEngine binds the
validated BCH-funder admission to the selected BCH wallet and pinned helper,
retaining an immutable `<trade>.bch-light-wallet.json` in local metadata. Exact
context keys include admittedAt deliberately. Every operation checks the saved
admission context, trade/quote identity and shared BCH busy state. Configuration
blocks synchronous reentry. The wrapper is desktop intent; the engine-owned
source record remains funding authority. Controller preparation now requires a
successfully acknowledged source selection in this process; on restart the
selection can be rechecked without replacing the wallet. Historical observation
and acknowledgement do not require fresh terms/source selection.

Added BchLightDexFundingPanel.qml to Dex.qml and qml.qrc: configure wallet,
confirm source, terms/fee/preparation, distinct signing/recovery/send/retry
approvals, observation and saved acknowledgement recovery. Uses negotiated
confirmation depth and shows separate cancellation/refund/punishment transaction
IDs. Password/consent clear when review or usable context changes. No automatic
signing or broadcast. Only the private BCH-funder/regtest flow exposes this.

Validation: Qt63passed21skipped0failed; QML93passed0failed; full application
rebuilt. Updated actual BCH-node controller retry flow passed69.07s, including
source recheck before preparation. Evidence:
`build/bch-light-desktop-binding-validation-20261015.json`. Engine admission
binding test uses the metadata-only test helper; this does NOT yet prove the
whole DexEngine/admission-helper/real-node path in one fixture. QML run retains
an existing backup-fixture warning at LightWalletBackupRestorePanel.qml:37;
no new panel warnings. Initial QML attempts without the fonts import produced
no usable results; accepted invocation uses build/dex-qml-runner, resource
bundle, `-import monzero-gui/fonts` and explicit text output.

Claude source review: build/claude-bch-desktop-binding-review-20261015.log.
Accepted exact wrapper keys and full context revalidation. Did not duplicate
engine journal exclusivity with a GUI flock: that would not protect cross-process
engine mutations. Expiry gates remain aligned with the existing helper;
post-expiry refund-protection retention and reconnect after an unsuccessful
admission refresh need explicit future coverage. Admission refresh temporarily
disables funding; successful recheck restores the unchanged binding.

Next: real DexEngine/admission-helper fixture, packaged helper update after
regression acceptance, then full light-funded BCH/XMZ success/refund/restart
flows and source-aware downstream settlement. Public-chain and other-coin
requirements remain open. Broad BCH regression PID908151 was verified live
this checkpoint; log build/bch-light-source-selection-broad-20261015.log.
Do not restart it merely because a session handle is unavailable. No user mining.
Goal remains active.

## 15 October 2026 — real DexEngine funding integration and stale status fix

Added test-only BchLightEngineProbe.h dispatched by dex_tests
`--bch-light-engine-probe DEX HELPER DIGEST OP LAUNCHER`. Python accepts
MONZERO_BCH_QT_ENGINE=1, generates a disposable canonical admission config
with a pinned real native-bch-admission helper, then invokes the public
DexEngine methods. Every invocation opens the existing wallet, checks real
admission, binds the same immutable local wrapper and requests its operation.
The probe reconstructs the fixture envelope from validated UI status; it does
not bypass BchLightFundingJob protocol checks. Password is stdin-only with a
scope wipe; request schema/UUID, payment, retained review, config digest and
trade/quote are checked. Busy probes also try forbidden wallet, admission,
node preparation and second light-funding operations. Settings are isolated.

Actual-node dropped-reply flow passed133.44s initially; reviewed version passed
373.37s. The full reviewed desktop suite passed63 with21environment skips.
Real admission/config binding across process reopening is now tested, beyond
the prior metadata-only unit fixture. Frozen binaries and log hashes are in
`build/bch-light-engine-integration-validation-20261015.json`.

Production fix: BchLightFundingJob status revalidates sources; controller
availability checks this; DexEngine rejects changed wallet/quote/wrapper
contexts and hides stale terms/reviews/results in QML. Existing launch/result
checks still apply. A targeted wallet-file mutation test passed. Excessive
helper hashing became visible in integration runtime, so the latest build uses
a cheap busy() accessor and reuses already-validated context/status snapshots
within one getter. It does not cache source validation across actions.

Pending at this checkpoint: session55959 verifies the optimized status build;
session49219 subsequently completed:63passed21skipped0failed (52.23s). Session59903 completed reviewed dropped
reply and now tests SIGKILL of an owned Qt process session and its helper after
node acceptance while the TLS reply is held. Python starts only that desktop
process in a new session and uses killpg only for this owned group; normal
helper-only kill remains supported. Do not count process-kill recovery passed
until its terminal log is checked. Broad Rust regression PID908151 remains live
at about34minutes. No user mining or unrelated Claude process was touched.

Claude review build/claude-bch-engine-binding-review-20261015.log informed the
stale-view and probe checks. Several hypotheses were rejected against source:
wallet reference is already QPointer and sources are checked before actions;
helper request buffer is separately wiped; admission status preserves the
original admittedAt; result validation already types confirmations and checks
commitments. Expired retry/recovery retention currently follows engine policy;
do not simply remove GUI expiry checks or reset immutable trade bindings.

Next substantive gap: funding/success.rs, recovery/cancel.rs, recovery/refund.rs,
success/native_claim/wallet/payout/preparation/approval.rs and
observe_signed_bch_funding still call funding_lock_intent. These must acquire
a proper light-funded observer binding instead of inventing node wallet lock
records. BchRpc::observe_funding itself calls wallet_ready, so merely replacing
the lock lookup is insufficient for a wallet-independent observer. Preserve
fresh BCH unspent/maturity checks and XMZ proofs before success/secret release.
Then complete paired-chain light-funded settlement/refund/restart, package
reviewed helpers after regression acceptance, and continue remaining coins
and public-network gates. Goal remains active.

### Desktop integration terminal results (15 October)

Session55959 passed: optimized-status real DexEngine/admission-helper/node
dropped-reply lifecycle178.09s. Session59903 passed: owned Qt+helper session
SIGKILL after accepted broadcast, then cold reopen, pending/unknown observation,
rejected acknowledgement without receipt, explicit same-byte retry and saved
acknowledgement recovery373.28s. Both node acceptance and retained transaction
identity were checked. These are process-crash tests, not power-loss recovery.
Evidence updated in build/bch-light-engine-integration-validation-20261015.json.
No mining or public market activation. The older broad Rust regression remains
running; new wallet-independent observer work is separate from that frozen build.

## 15 October 2026 — wallet-independent BCH chain observer

Added bch_rpc::observer::OwnedBchObserver, extracting the existing authenticated
loopback transport and chain checks from BchRpc. Public observer API is only
constructor/network/deployment identity/funding observation; no wallet context,
key, signing, locking or broadcast operations. BchRpc embeds it while preserving
wallet ownership checks before/after observations and input/change ownership
checks. Original wallet deployment commitments remain byte-identical (golden
vector tested); observer identity uses a separate schema and excludes credentials
so they can rotate without changing the configured deployment.

Shared observation validates the exact signed transaction output, including
mempool spends, value/script/token/coinbase/depth checks, pinned genesis/fork
checkpoint/subversion, synchronized chain and stable tip. The observer returns
BoundFundingObservation; callers must supply an independently retained deployment
pin via for_deployment before using confirmation/maturity evidence. No SPV or
public-peer trust claim: this remains an independently authenticated owned local
BCHN observer. It does not yet connect light-funded success/refund settlement.

Tests: allfeatures/alltargets check passed;11RPC tests passed1.59s, including
18loopback observation cases prohibiting wallet or mutating calls, wrong chain,
checkpoint, tip, output, token/coinbase, reply identity, errors and mempool size.
Real-node authenticated handoff passed20.91s with the new observer checking
pending and confirmed funding and rejecting the wallet deployment identity.
Legacy preparation/locking/signing/relay and safety-margin refusal remained
verified in that run. Evidence build/bch-walletless-observer-validation-20261015.json.

Ordered patch upstream-bch-walletless-observer.patch appended to verifier; audit
passed. Initial generated patch omitted new-file mode metadata; corrected audit
is patch-audit-retry, original failure log preserved. Formatter executable was
not available in system or pinned toolchain; existing local source style retained.
Claude source review log build/claude-bch-walletless-observer-review-20261015.log;
added explicit deployment-bound observation and compatibility vectors after
review. SignedFunding already privately enforces valid outpoint/index/signatures.

Frozen fixture build/bch-walletless-observer-fixture-20261015 now runs the broad
BCH regression; build/bch-walletless-observer-broad-20261015.log. The prior
source-selection broad regression completed52passed0failed20ignored2840.52s;
it does not cover this observer extraction.

Next: retain an explicit observer binding for local-funded trades, then adapt
success/refund/cancellation observation without fake node wallet-lock records.
Preserve both-chain fresh checks and all maturity/safety windows. Complete paired
light-wallet BCH/XMZ settlement/refund/restart and package reviewed helper builds;
remaining coins/public consensus gates still open. Goal remains active.


## Amended completion scope — 2026-10-15

The active goal remains completion of the app and multicoin DEX, including local
wallet creation/import, user access to keys and mnemonic recovery, and usable
funding, settlement and recovery flows for supported coins.
Wallet secrets and recovery records remain on the user's system; no server-side
custody or secret storage is part of this design.
A passing isolated checkpoint is not completion of the full goal.

Next steps, in dependency order:

1. Verify the explicit BCH local-funding observer binding and orphan-record guards.
2. Strengthen the durable link between selected local wallet and retained signed
   claim before treating the binding as settlement authority.
3. Connect local-funded BCH success/cancellation/refund paths to independently
   pinned observations, preserving fresh checks on both chains and safety windows.
4. Exercise paired BCH/XMZ success, refund, restart and ambiguous-broadcast paths
   through the desktop; package only the helper version actually validated.
5. Complete the corresponding BTC/DOGE and remaining LTC integration and public
   consensus validation gates, then reconcile the roadmap with measured evidence.

The owned local BCH observer is a development component; it does not establish
public light-client readiness or a completed BCH/XMZ swap.


## 2026-10-15 — BCH local-wallet observer binding verified

Added explicit review and immutable binding of an owned local BCH chain observer
for exact retained local-wallet funding.
The binding pins the admission, selected source, wallet claim, signed transaction
and observer deployment without retaining credentials or wallet secrets.
Review and binding perform no RPC, signing or relay.
Historical observation may be explicitly bound after quote expiry, but never
approves fresh spending or secret release.
Changed observer configuration, changed source, missing signatures, malformed
records and backwards time fail closed.
Credential rotation preserves deployment identity.

New source-selected signed records retain the selected-source digest and verify
it on reload; legacy canonical records stay readable but cannot silently gain
observer authority.
The helper already checks wallet selection before signing.
The lower-level signature-verification API still supports unselected fixtures and
non-regtest networks; these cannot bind this regtest-only observer.
Source-bound trade recovery requires the original selected-source metadata in
addition to signatures and other trade records; key-only recovery is not proven.
Node funding now rejects orphan local relay intent/ack records before RPC.

Validation: all features/all targets compile passed, release lib build passed,
funding/observer/restart regression passed (1 test, 73.86 seconds), BCH RPC tests
passed (11 tests, 1.57 seconds), owned BCHN handoff passed (21.08 seconds).
The binding regression covers changed selected wallet, legacy missing digest,
first binding after expiry, explicit approval, changed observer, credential
rotation, corrupt/missing records, isolated orphan relay records, exact confirmed
output and reopened execution with unchanged binding bytes.
The owned-node test covers existing wallet locking/signing/relay, node restart,
confirmed observation and native funding safety windows; it is not a complete
local-wallet BCH/XMZ settlement test.
Ordered patch audit passed for upstream-bch-light-observer-binding.patch.
Evidence: build/bch-light-observer-binding-validation-20261015.json.
Claude source-only reviews and independent dispositions are retained in build/.
An initial test failed because the fixture tried to recreate an existing relay
record; the corrected test isolates and restores both relay records.
The original failure log is preserved.

The earlier frozen walletless-observer broad regression remains in progress and
does not cover this new binding.
No bundled helper replacement, website deployment or public-market activation
was performed for this checkpoint.
Next: integrate bound observation with light-funded BCH success/cancellation/
refund, preserve fresh two-chain and maturity checks, prove paired settlement and
restart in the desktop, then package verified helpers and continue other coins.
The app/multicoin DEX completion goal remains active.


## 2026-10-15 — local BCH success release implementation; runtime verification pending

Added a private explicit node/local observer choice to shared success release.
The local path requires exact retained source/signatures and the approved observer
binding; the node path still requires node-wallet lock state and rejects local
records.
Both paths share native payment verification, BCH confirmation/cancellation
window checks and the final native snapshot recheck before exposing any adaptor.
Local prepare/signature-retention/signed-release operations require distinct
review commitments in a local-wallet domain.
Private release records remain on the user's machine.

All-features/all-targets compile, admission helper release build and ordered patch
audit passed; 11 BCH RPC tests passed.
The new local regression initially failed on an incorrect test assumption:
offline safety margin 17 is valid for minimum depth 2/cancellation 20, while live
depth 3 would reject it.
The fixture now derives invalid margins/depths from policy.
Corrected release build/runtime verification is pending in
build/bch-light-success-validation-20261015.json.
Do not claim a passing local release flow before inspecting its terminal log.

The current node-backed paired regression runs via
build/run-bch-light-success-node-regression-20261015.py, using frozen engine/helper
binaries and existing Qt proof/identity/private request adapters.
The old test_bch_paired_settlement.py selects a retired test name, ran zero tests,
and correctly failed its driver assertion; it is not settlement evidence.
Preserved failed logs and exact binary hashes are in the validation record.
The earlier frozen walletless-observer broad test is still a separate run.

Claude source-only review identified an existing interrupted-publication gap:
a release pin written without its private record blocks restoration.
This fails closed, but needs explicit exact-record repair and failure-injection
coverage before a power-loss recovery claim.
Other review questions were checked against the actual source and recorded in
build/bch-light-success-review-disposition-20261015.txt.

Next: finish runtime validation, connect local-funded cancellation/refund and
native claim/payout, add reviewed helper/desktop operations and verify a complete
paired local-wallet swap.
No bundled-helper replacement or public-market activation; full goal remains active.

Corrected release lib build passed. Frozen build/bch-light-success-final-fixture-20261015 now runs the local release regression; terminal result remains pending in build/bch-light-success-final-tests-20261015.log. The paired regression uses the prior frozen fixture with identical production code and only the old local-test boundary assertion.

The earlier walletless-observer broad regression completed: 54 passed, 0 failed, 20 ignored in 2876.28 seconds. Evidence: build/bch-walletless-observer-broad-validation-20261015.json. Its frozen binary predates the local observer binding/success-release/claim changes.


## 2026-10-15 — local BCH native-claim path; complete runtime validation pending

Moved exact confirmed-transaction reads into OwnedBchObserver, including explicit
block hints and optional transaction-index lookup.
Both validate active-chain membership, exact depth and bytes, supported shape,
pinned chain context and stable tip.
Existing BchRpc wrappers preserve the historical configured wallet deployment
commitment; root chain reads are not live wallet-ownership attestation.
Deployment schemas remain domain-separated and existing golden encodings remain
unchanged.

Added recover_light_bch_native_claim, using the same private native-key recovery
as the node path after validating the local source/observer binding, prepared
success frame, authenticated native payment, confirmed covenant spend and final
native snapshot.
Recovered keys remain private in NativeClaim; this is not wallet import, payout,
relay permission or a public status response.

All-features/all-targets check passed; first release lib build passed; 11 BCH RPC
tests passed in 1.66 seconds, including both node/local confirmed-read paths.
Ordered upstream-bch-light-native-claim.patch audit passed.
Final release build adds explicit changed/missing observer, cross-source API and
missing release-pin cases before running the full local claim regression.
That test constructs a genuine covenant redemption from fixture shares and must
verify exact recovered spend/view keys, inactive/changed/shallow redemption
rejection, wrong outpoint rejection, both-chain checks and indexed lookup.
Do not count it passed until its terminal result exists.
Evidence: build/bch-light-claim-validation-20261015.json.
Claude review and independently checked dispositions are retained in build/.

The earlier local success-release regression and node-backed paired regression
are still separate frozen-binary runs; neither covers these claim changes.
Full local-wallet payout, cancellation/refund, helper/desktop exposure and
interrupted release-record repair remain incomplete.
No bundled-helper update or public-market enablement; full goal remains active.

Final claim release build passed. Frozen build/bch-light-claim-fixture-20261015 now runs the full local funding/release/claim regression; terminal result pending in build/bch-light-claim-tests-20261015.log.

Node-backed paired regression completed: 1 passed, 0 failed in 1125.73 seconds. The current paired funding/proof fixture also exercises reviewed success preparation/retention/release with the Qt encrypted identity/private adapters. Frozen helper/probe hashes were rechecked. Evidence: build/bch-light-success-node-regression-20261015.json and build/bch-light-success-validation-20261015.json. This verifies the shared success refactor on the node path, not the newer local claim changes or complete local-wallet settlement/payout.

Local success-release regression completed: 1 passed, 0 failed in 1094.42 seconds. This verifies the reviewed local release path, distinct consent, native/BCH rejection cases and exact reopened release. Evidence: build/bch-light-success-final-tests-20261015.log and build/bch-light-success-validation-20261015.json. Its frozen binary predates native-claim and claim-wallet additions, whose runtime verification remains separate.


## 2026-10-15 — local-funded native wallet lifecycle; runtime tests pending

Added restore_light_bch_claim_wallet, open_light_bch_claim_wallet and
check_light_bch_claim_wallet.
Each recovers a fresh claim through the retained local BCH observer, then uses the
same private lifecycle logic as node-funded trades.
Restore intent remains durable before generate_from_keys; an attempted restore
cannot be repeated and must be reconciled through explicit open/check.
Spend key, address and exact native transaction are verified before readiness.
These APIs neither prepare nor relay a payout.
Wallet secrets remain in the owned local session and private user storage.

Node wrappers preserve their original recover-before-intent ordering.
Common helpers borrow the claim, and key encoding avoids temporary per-byte
format strings; the expected-key comparison buffer is zeroizing from allocation.
The new fixture covers native confirmation refusal before wallet RPC, one restore
only, lost reply, explicit session recovery, reopen and malformed readiness.
It also requires duplicate restore checks to reobserve the native chain.

All-features/all-targets compile and ordered patch audit passed.
Initial release lib build passed; final release/runtime checks remain pending in
build/bch-light-claim-wallet-validation-20261015.json.
Claude source-only review and independent dispositions are retained in build/.
The earlier local claim regression is a separate live frozen-binary run.
No payout authority, bundled-helper replacement or public readiness claim.
Next: verify lifecycle, add local payout preparation/relay, then finish recovery
and helper/desktop integration. Full goal remains active.

Final claim-wallet release build passed. Wallet-session regression: 12 passed, 0 failed in 1.16 seconds. Frozen build/bch-light-claim-wallet-fixture-20261015 now runs separate integrated local-flow and node-wallet lifecycle tests, logs build/bch-light-claim-wallet-tests-20261015.log and build/bch-light-claim-wallet-node-tests-20261015.log. Both results remain pending; inspect live processes/terminal logs before restarting anything.

Local release/native-claim regression completed: 1 passed, 0 failed in 1363.83 seconds. It verifies exact recovered spend/view keys from a genuine fixture covenant redemption, explicit/indexed lookup, wrong observer/source/missing metadata, unsafe native and BCH evidence, wrong outpoint and restart. Evidence: build/bch-light-claim-validation-20261015.json. The frozen binary predates claim-wallet/payout additions.


## 2026-10-15 — local BCH native payout; runtime validation pending

Added local-wallet native payout preparation, review, relay state, explicit relay
and read-only confirmation APIs alongside the existing node-funded operations.
Both sources share the existing exact-payment, private checkpoint and durable
attempt logic. Local review uses a separate commitment domain and verifies the
retained local funding/observer binding. Approval applies only to saved bytes;
uncertain construction or relay never authorizes automatic repetition.
Confirmation can reconcile a lost acknowledgement without fabricating one.

All-features/all-targets compilation and ordered patch audit passed.
Claude source-only review prompted extra tests for source/observer mismatch,
backwards time and post-expiry recovery. Release build is running; combined
runtime validation remains pending. See build/bch-light-payout-validation-20261015.json.
Earlier local and node claim-wallet lifecycle runs remain live in their separate
frozen binaries; they do not validate these new payout APIs.

The new APIs are not yet routed through the helper/desktop. Cancellation/refund,
interrupted private-record repair, full paired swap validation and packaging
remain required. No bundled helper replacement or public-readiness claim.

Payout release build passed (3m44s), then 12 wallet-session tests passed with
zero failures (1.17s). The combined local funding/release/claim/wallet/payout run
is live in frozen build/bch-light-payout-fixture-20261015; inspect its terminal
log before claiming a runtime pass. This binary excludes the subsequent
read-only recovery-output extraction.


## 2026-10-15 — local BCH recovery-output evidence verified

OwnedBchObserver now exposes confirmed_unspent_output for exact, freshly
confirmed parent transactions. The existing BchRpc method delegates to a private
shared implementation while preserving its original node-wallet deployment pin.
Callers cannot select or relabel the expected identity. Cross-source parent
records and absent outputs fail before network I/O.

Both paths retain the stable-tip/depth checks, mempool-inclusive gettxout, exact
amount/script checks and coinbase/token refusal. This adds read-only evidence;
it does not grant cancellation/refund broadcast authority or public SPV trust.

All-features/all-targets check passed (17.19s), release lib build passed (3m51s),
ordered patch audit passed, and all 11 BCH RPC tests passed (1.97s).
Source-only Claude review and disposition are saved with the evidence in
build/bch-light-recovery-output-validation-20261015.json.

The frozen payout test remains running (PID1078213, session73754,
build/bch-light-payout-tests-20261015.log). Earlier claim-wallet local/node runs
remain separate (PID1055695/session33594 and PID1055713/session74632).
Recheck their processes and terminal logs before restarting or reporting results.
Next implement local cancellation/refund using retained funding/observer identity,
explicit approval and existing durable attempt semantics, then finish interrupted
private-record repair and helper/desktop integration. Bundled helper unchanged.


Local claim-wallet lifecycle completed: 1 passed, 0 failed in 1626.04 seconds.
The frozen claim-wallet fixture covers restore/open/check, lost restore reply,
reconciliation and reopening; it predates native payout additions.
Node-wallet regression remains live. Evidence updated in
build/bch-light-claim-wallet-validation-20261015.json.

## 2026-10-15 — local BCH cancellation observation; runtime pending

Added review_light_bch_cancel_relay, light_bch_cancel_relay_state,
observe_light_bch_cancel and observe_light_bch_recovery_output.
Review binds the retained local source/observer and exact private recovery,
using a separate local-domain commitment and public identifiers only.
Either peer may publish cancellation; its confirmation does not require a local
attempt, create an acknowledgement or authorize a refund. Same-tip unspent
recovery-output evidence remains read-only. Existing node review fields/domain
and the native-funder branch remain intact.

The new regtest recovery driver uses the real retained funding/recovery setup,
then tests explicit/indexed cancellation, unsafe evidence, source/observer
refusal, orphan acknowledgement, expiry and reopened review. It hashes metadata
and private stores to check observation does not mutate them. Recovery output
fixture responses are explicitly opt-in so success fixtures stay strict.

Final all-features/all-targets check and ordered patch audit passed.
Initial release build passed; final release/runtime remain pending in
build/bch-light-cancel-observation-validation-20261015.json.
The earlier payout and node-wallet runs remain separate frozen binaries.
No local cancellation/refund dispatch or desktop exposure yet. Next add explicit
contract relay capability separate from the read-only observer, with approved
exact bytes and durable attempt before dispatch, then local refund handling.

Final cancellation-observation release build passed (3m44s), and all 11 BCH RPC
tests passed (1.95s). Frozen build/bch-light-cancel-observation-fixture-20261015
now runs the dedicated local recovery regression (PID1099392/session68811) and
current node-wallet regression (PID1099439/session68366). Their logs are
build/bch-light-cancel-observation-tests-20261015.log and
build/bch-light-cancel-observation-node-tests-20261015.log; terminal results are
pending. The new node run covers the current shared cancellation-source changes;
the older node run uses the separate claim-wallet binary and should not be
restarted or mistaken for this version. The payout run remains a separate live
frozen binary (PID1078213/session73754). Full goal remains active.


Cancellation observation regression completed: 1 passed, 0 failed in 73.29 seconds.
Earlier node claim-wallet lifecycle regression completed: 1 passed, 0 failed in
2847.29 seconds. Their evidence files now distinguish terminal passes from the
separate current-node and payout runs that are still live.

## 2026-10-15 — local cancellation/refund relay; runtime pending

Added OwnedBchContractRelay as explicit API selection separate from the public
read-only observer. It is not approval: only admitted execution methods can call
its crate-private dispatch operations. Local cancellation and refund now share
the node path's approval, fresh evidence, durable intent and acknowledgement
state machines through a sealed transport enum. Node wire/review formats stay
unchanged; local refunds use a distinct commitment domain and retained source.

Local refund review validates the original attempted safety margin. Confirmation
requires the exact attempted refund and preserves uncertainty/ack history.
No automatic resend follows a lost reply. A deadline expiring during durable
intent retention still leaves an uncertain attempt and needs separate repair.
Malformed acknowledgement errors are sanitized, and tests explicitly check
that echoed transaction bytes do not appear in displayed errors.

All-features/all-targets check and ordered patch audit passed. Release build and
runtime tests remain pending in build/bch-light-contract-relay-validation-20261015.json.
Tests cover acknowledged/lost cancellation and refund, maturity/spent-source
refusal, approval/source/observer mismatch, restart, backwards time, changed
margin and read-only confirmation. These synthetic fixtures are not evidence
of full paired-chain settlement. Native refund wallet/payout, interrupted-record
repair, helper/desktop wiring and actual packaged validation remain incomplete.

Final contract-relay release build passed (3m45s). All 12 BCH RPC tests passed
(2.00s), including exact dispatch, expired deadline, lost reply, wrong transaction
ID and sanitization of echoed transaction bytes in rejection/malformed replies.
The combined local cancellation/refund regression passed: 1 passed, 0 failed in
99.33 seconds. It exercises both acknowledged/lost histories, source and observer
refusal before RPC, maturity/spent-output rejection before intent, reopened
no-repeat state, original safety margin and read-only exact refund confirmation.
Evidence: build/bch-light-contract-relay-validation-20261015.json.

Current node regression remains live in frozen
build/bch-light-contract-relay-fixture-20261015 (PID1115701/session65016), log
build/bch-light-contract-relay-node-tests-20261015.log. The prior observation
node run (PID1099439/session68366) and native payout run (PID1078213/session73754)
remain separate frozen-binary validations. Recheck process/terminal logs before
any restart or result claim. The completed local relay run was PID1115634/session17998.
No helper bundle replacement or public readiness claim. Next resolve interrupted
private success-record publication, then helper/desktop routing and full paired
local-wallet settlement validation. Native-peer refund and public-chain paths
must be audited against actual integration requirements, not inferred complete
from these BCH-funder synthetic fixtures. Full goal remains active.


Combined local funding/release/claim-wallet/native-payout regression completed:
1 passed, 0 failed in 2902.10 seconds. It covers prepared payout accounting,
lost preparation reply, exact reviewed relay, acknowledged/lost relay histories,
reopen refusal of duplicate sends and read-only confirmation after expiry.
Evidence: build/bch-light-payout-validation-20261015.json. This frozen binary
predates cancellation/refund and candidate-publication additions, and is a
synthetic integration fixture rather than full paired blockchain settlement.

## 2026-10-15 — exact success publication repair; runtime pending

Success publication now retains an exact private candidate before writing the
public pin and final private record. A partial candidate is not regenerated or
exposed automatically. Separate node/local repair review and execution APIs
validate the retained source, admission, proof, frame and original timestamp,
then finish only the exact approved candidate. Repair returns no frame and no
chain approval. Signed repair requires published preparation; prepared repair
checks deterministic agreed bytes and every available signed copy's pairing.

Candidate-only and candidate+pin are supported interrupted states. A published
private record without a trusted pin is rejected as metadata loss/rollback,
not silently reanchored. Conflicting pins/private bytes/candidates fail closed.
Legacy complete records without candidates remain readable and gain candidates
on a fresh checked release. Missing legacy private bytes without a candidate
cannot be synthesized from their public hash.

All-features/all-targets check and patch audit passed. The first check's test
visibility error was fixed and its log retained. Source review also led to
missing-pin rejection and deterministic-frame checks; independent inspection
found and fixed a nested non-reentrant private-journal lease before runtime.
Release/runtime validation remains pending in
build/bch-success-publication-repair-validation-20261015.json.

The test simulates persisted partial states, not actual power loss. Helper/Qt
private-state handling must preserve candidate files before this engine change
can be packaged, and the repair operation still needs desktop routing. No
bundled helper update or broad completion claim. Existing node regressions
remain separate frozen-binary runs; inspect terminal logs before restarting.


## 2026-10-15 — resumed; BCH publication repair helper

Resumed after the user-requested pause. The two old node regression processes
were deliberately stopped during that pause; they are not passing results.
The publication repair release build had passed in 3m47s before the pause.
Its frozen binary now runs the local interruption regression (session78007)
and current node regression (session10185). Check terminal logs before claiming
results or starting replacements:
- build/bch-success-publication-repair-runtime-20261015.log
- build/bch-success-publication-repair-node-20261015.log

Added explicit private helper review/submit modes for exact retained success
publication repair, for both node-wallet and local-wallet sources. Requests bind
prepared/signed stage and separate submit consent. The wrapper pins admission,
RPC descriptor, deployment and funding source; the engine rechecks saved source
and exact candidate. Repair exposes no frame and grants no chain approval.
The new observer descriptor is walletless and regtest-only. No RPC is performed
by repair; the trusted caller still must authenticate the owned process before
creating its descriptor. Owner-only local journals persist candidate records;
this is not encryption at rest or a completed active-trade backup facility.

All-features/all-targets compilation passed (1.38s), ordered patch audit passed,
and all 29 native-bch-admission helper tests passed (0.01s), including the two new
repair boundary tests. The release test build took 1m21s. Claude source-only
review and checked dispositions are recorded in:
- build/claude-bch-publication-helper-review-20261015.log
- build/bch-publication-helper-review-disposition-20261015.txt

Patch: tools/dex-engine-port/upstream-bch-success-publication-helper.patch.
The helper production build, process checks and integration evidence are tracked
in build/bch-success-publication-helper-validation-20261015.json. Desktop repair
controls, full helper-to-engine interrupted repair tests, encrypted trade backup,
full paired local-wallet settlement and packaging remain outstanding. No bundled
helper replacement, public enablement, website deployment or mining restart.

Production repair helper build passed. Frozen helper: build/bch-success-publication-helper-20261015. Five actual executable refusal smoke checks passed with empty stdout and no echoed sensitive sentinel; these do not constitute a successful persisted-repair integration test. The bundled application helper remains unchanged.


## 2026-10-15 — real publication repair helper integration fixture

Added admitted_bch_success_publication_helper_repairs_retained_local_trade,
an explicitly ignored test requiring pinned MONZERO_BCH_ADMISSION_HELPER and
MONZERO_BCH_ADMISSION_HELPER_SHA256. It uses the real helper over complete
negotiation/funding/native-proof records with reviewed admission. Prepared
candidate-only and signed candidate+pin histories are copied independently;
review equality, wrong-consent unchanged stores, exact restoration, fresh-process
retry and no observer RPC are asserted. Historical quote expiry is retained.

All-features/all-targets compilation passed in 16.60s and patch reconstruction
passed. Release build session46771 is pending; runtime has NOT run yet.
Evidence: build/bch-publication-helper-integration-validation-20261015.json.
Patch: tools/dex-engine-port/upstream-bch-publication-helper-integration.patch.
The earlier engine-only publication test/session78007 and node/session10185
remain live in their original frozen binary. Do not restart or claim completion
without terminal evidence. Next run this real-helper fixture, then connect the
verified repair operation to desktop controls. No full DEX completion claim.

Claude test review strengthened this fixture: exact full public/private hash restoration, cross-stage approval refusal and rejection of unexpected non-file journal entries. The final all-features/all-targets check passed in 16.17s and patch audit passed. Initial release passed in 3m49s; the final release build is session28908. Runtime is not yet started. Review and disposition are retained under build/claude-bch-publication-integration-review-20261015.log and build/bch-publication-integration-review-disposition-20261015.txt.

Final helper integration release build passed in 3m48s. Frozen binary build/bch-publication-helper-integration-fixture-20261015 now runs the explicitly selected ignored test with the pinned staged repair helper. Session50272, log build/bch-publication-helper-integration-runtime-20261015.log. Runtime result pending; do not count the ignored default-suite test as a pass.

Engine exact publication repair regression completed: 1 passed, 0 failed, 1396.56s. Evidence: build/bch-success-publication-repair-runtime-20261015.log. This covers synthetic persisted interruptions, not actual power loss or the newer real-helper integration. The node regression/session10185 and real-helper integration/session50272 remain separate pending validations.


## 2026-10-15 — Qt publication repair validation and process worker

Added BchPublicationRepairProtocol.h and BchPublicationRepairJob.cpp/.h.
The validator binds public review to exact quote/trade/source/deployment/stage,
checks canonical consent and chronology, rejects extra private fields and
chain-approval claims, and binds responses to request ID and submit consent.
The worker pins helper/config/quote/RPC descriptor, separates review from submit,
invalidates consent on source changes, bounds output and treats lost replies as
unknown. It exposes no private frame and never retries submit automatically.

Both new CMake targets configured and built. Their tests passed: protocol 19/0
in 7ms, worker 9/0 in 516ms. Initial manual compile used a nonexistent moc path;
/usr/bin/moc resolved it. Initial worker tests shared a fixture call log across
cases (6 pass/3 fail); resetting that log fixed isolation. Original logs remain.
Evidence: build/bch-publication-qt-validation-20261015.json. Claude source review
is session2776, build/claude-bch-publication-qt-review-20261015.log, pending.

These are application worker components, not finished visible controls. Next
attach the worker to DexEngine with operation exclusion/invalidation, add private
configuration routing and explicit prepared/signed repair UI, and validate it
through the actual helper. Current worker tests use a mock helper. Real Rust
helper integration/session50272 and node/session10185 are still independent
pending runs; do not restart them. No bundled helper replacement or deployment.


Qt source review completed. Queued start/finish notifications protect process
callbacks from consumer deletion; available actions now wait for actual process
exit and late termination emits a change. Start returns the actual busy state.
Final CMake worker tests: 11 passed, 0 failed, 668ms; protocol 19/0. Disposition:
build/bch-publication-qt-review-disposition-20261015.txt. Explicit idempotent repair
retry remains allowed after a fresh review; no automatic retry or spend occurs.

The original node regression FAILED (0 passed, 1 failed, 1583.17s):
"BCH success publication is pending; review repair first". Located the old
no-signed-envelope claim fixture: it deleted signed final/pin but left the new
candidate. Corrected native_claim.rs to use an isolated private copy, explicitly
assert pending-candidate refusal, then remove the candidate to represent never
saved signing. The original private store is no longer renamed for this case.
Patch: upstream-bch-publication-node-fixture.patch. Patch audit passed; compile
is session86331, log build/bch-publication-node-fixture-check-20261015.log.
A corrected release build and node runtime rerun are REQUIRED; the failure is
not a pass. Real-helper integration/session50272 is still running unchanged.

Node fixture correction all-features/all-targets check passed (16.27s). Supervisor session73978 now builds release; only on success it freezes build/bch-publication-node-fixture-20261015 and starts the corrected node regression. Logs: build/bch-publication-node-fixture-release-20261015.log and build/bch-publication-node-fixture-runtime-20261015.log. Do not start a duplicate. Evidence: build/bch-publication-node-fixture-validation-20261015.json.


## 2026-10-15 — private desktop BCH record repair controls

Real Rust-helper integration completed: 1 passed, 0 failed in 714.48s. This
exercises copied historical local-wallet records, exact prepared/signed repair,
wrong/cross-stage approval refusal, original public/private hash restoration,
fresh-process retry and no BCH RPC. Evidence is
build/bch-publication-helper-integration-validation-20261015.json.

Connected BchPublicationRepairJob to DexEngine, with shared BCH operation
exclusion through actual process exit and dependency-driven consent clearing.
Added hidden private-prototype --dex-bch-publication-repair configuration.
Dex.qml now offers explicit preparation/signed-record review, trade/stage display,
consent and repair, with unknown/completed messaging and consent reset on market
or status changes. No startup repair or automatic submission.

Qt tests use a shared PublicationRepairFixture. Initial fixture extraction
introduced a shadowed self-reference; corrected to repairDetails, preserving
the initial failed controller/worker logs. The initial raw qmltestrunner launch
lacked FontAwesome import; adding fonts resolved it. The app's own QML harness
then passed all 94 recovery tests with resources. Full app build passed.
Controller suite: 64 passed, 0 failed, 21 skipped; worker after Claude review:
12 passed, 0 failed, 669ms. Review refinement suppresses redundant consent
invalidation but separately refreshes availability. The final controller suite
is session76685 and final app status-text build session3059; recheck logs.

Visual evidence: bch-publication-repair-panel.png (mock reviewed state, inspected).
Controller review/disposition: build/claude-bch-publication-controller-review-20261015.log
and build/bch-publication-controller-review-disposition-20261015.txt.
Evidence: build/bch-publication-desktop-validation-20261015.json.

Next verify final build/tests, then run the actual Qt worker against the real
repair helper and retained trade records. This is distinct from the passing
mock UI/controller tests and passing Rust-helper integration. Corrected node
regression remains live in frozen build/bch-publication-node-fixture-20261015,
PID1233196, supervisor session73978; do not duplicate it. Bundled light helper
remains c6ee291abac889a38cb6c31531b0eab5619463c836d3f3c1b3facbe3f53d0036.
No public pilot/chain activation, website deployment or mining restart.

Final verification completed: controller suite 64 passed/0 failed/21 skipped (52424ms), final app build passed, targeted repair-panel test through the final app harness passed (3 including init/cleanup). Screenshot refreshed. Actual Qt-to-real-helper retained-trade integration remains next; corrected node/session73978 still independent.


## 2026-10-15 — production Qt worker to real helper integration

Added dex_bch_publication_repair_probe around BchPublicationRepairJob. Each fresh
Qt process must configure the pinned helper and independently review before
using the fixture's explicit approval; it emits only validated public status.
The retained-trade test can select the independently hash-pinned probe through
MONZERO_BCH_QT_PUBLICATION_REPAIR_PROBE and its SHA256 companion. Incomplete or
wrong probe configuration fails; absent probe variables preserve the direct Rust
helper path. Rust helper identity in the private admission configuration remains
the actual Rust executable. The expected sanitized failure differs for Qt/direct.

Qt probe build and 3 refusal smoke checks passed. All-features/all-targets check
passed in 16.61s; patch audit passed. Supervisor session57260 builds release,
then freezes build/bch-publication-qt-integration-fixture-20261015 and runs the
explicitly ignored retained-trade test through staged
build/bch-publication-qt-probe-20261015 plus
build/bch-success-publication-helper-20261015. Logs:
- build/bch-publication-qt-integration-release-20261015.log
- build/bch-publication-qt-integration-runtime-20261015.log
Evidence: build/bch-publication-qt-integration-validation-20261015.json.
Claude source review/session31160 remains pending in
build/claude-bch-publication-qt-integration-review-20261015.log.
Do not duplicate this run or call its pending runtime a pass.

The corrected node regression/session73978 remains live separately (PID1233196).
Read-only scope audit confirms native-funder refund recovery and its wallet APIs
still take BchRpc and match the deployment saved with native payment. They are
not automatically covered by BCH-funder local-wallet repair tests. Complete
walletless native-peer observation/payment binding before claiming full paired
local-wallet success/refund coverage. No production change to that path yet.

Qt integration release build passed (3m41s); supervisor session57260 has now
started the actual Qt-worker/Rust-helper retained-trade test in its frozen
binary. Runtime log: build/bch-publication-qt-integration-runtime-20261015.log.
Claude review completed; checked dispositions are in
build/bch-publication-qt-integration-review-disposition-20261015.txt. The probe
is evidence of the explicit review-then-submit UI sequence, not standalone
coverage of bare-submit rejection (that is covered by worker/controller tests).
Actual helper response validation occurs inside the production worker before
status is exposed; the probe only reconstructs its outer fixture envelope.
Runtime remains pending. Continue polling this exact run and corrected node
session73978; do not restart on quiet logs or observation timeout.

### 2026-10-15 — Qt repair integration passed; native payer observer preflight

The real Qt worker to pinned Rust helper retained-trade integration passed:
1 passed, 0 failed, 743.79s.
See build/bch-publication-qt-integration-validation-20261015.json and its runtime log.
This uses synthetic chain observations; it is not proof of public-network settlement.

Native payer now has read-only authenticated BCH funding observation/preflight through OwnedBchObserver.
The shared terms check preserves role, quote expiry, payment destination/amount/fee and receipt chronology checks.
Observation checks the deployment and funding window without BCH wallet access.
A review follow-up uses explicit errors for missing network/genesis terms and documents that the result cannot authorize later payment.
Native payment preparation, relay and refund are not yet routed through this observer.
The observer still requires a trusted owned node; it is not a public SPV implementation.

All-features/all-targets compilation passed (16.30s initially, 17.27s after review).
The ordered patch audit passed including upstream-bch-native-walletless-preflight-review.patch.
Release compilation passed (3m44s) before the review follow-up.
Frozen native preflight regression remains running; do not report it as passed or restart because its log is quiet.
The corrected older node publication regression also remains pending.
Evidence: build/bch-native-walletless-preflight-validation-20261015.json.
Review disposition: build/bch-native-walletless-preflight-review-disposition-20261015.txt.
Mining remains off; no deployment or bundled helper replacement was performed.

### 2026-10-15 — walletless native preparation and relay (runtime pending)

Added prepare_light_bch_native_funding, prepare_reviewed_light_bch_native_funding,
and relay_light_bch_native_funding through a private Node/Walletless observer dispatcher.
Existing node APIs use the same preparation/relay bodies without changing persisted schemas.
The original observer deployment remains immutable in the native intent and review.
Both new operations perform fresh chain checks; retained read-only preflight results are not authorization.
Exact-once preparation/dispatch records and fail-closed ambiguous outcomes are preserved.

New copied-store native wallet scenarios cover wrong observer approval, insufficient funds,
unsafe chain state, successful preparation, lost preparation response, reopening,
cross-backend rejection, expired historical preparation recovery, wrong/expired relay approval,
unsafe relay window, acknowledgement/lost response, and no automatic reconstruction/retransmission.
All-features/all-targets compilation passed (16.96s); ordered patch audit passed.
Claude source review session51772 and release/runtime supervisor session14164 are pending.
Logs use build/bch-native-walletless-payment-* and claude-bch-native-walletless-payment-review-20261015.log.
Do not duplicate the running regression or describe it as passed.
Evidence: build/bch-native-walletless-payment-validation-20261015.json.

Next: route native refund recovery, dedicated refund wallet and payout lifecycle through
this same immutable observer binding, then test actual paired recovery before helper/GUI activation.
Existing OwnedBchObserver confirmed transaction methods can supply read-only refund evidence;
current native_refund and wallet/payout methods still take BchRpc and cannot be claimed complete.

Walletless payment release build passed (3m45s). Supervisor session14164 started
the frozen bch-native-walletless-payment-fixture-20261015 regression. Runtime and
Claude source review remain pending; do not duplicate these runs.

### 2026-10-15 — walletless native refund and verified fixture correction

Added recover_light_bch_native_refund using the same historical restoration and
fresh two-chain evidence as node recovery, bound to the original payment observer.
New cases recover exact spend/view keys after acknowledged and uncertain relay,
reject wrong observer/role, unconfirmed native payment, immature payment and
invalid BCH block/transaction evidence, and exercise explicit/indexed block lookup.
Dedicated refund wallet and payout APIs are still node-only and remain next work.
Initial all-features/all-targets compilation passed (17.54s).

The previous walletless payment runtime FAILED (0 passed, 1 failed, 154.62s).
The second independent relay scenario reused a NativeTransferRelay already dispatched
by the first scenario; the production guard correctly refused before creating intent.
Correction uses a fresh native client for each independent copied-store scenario.
No production retransmission guard was weakened.
The refund release compiler was explicitly stopped after this verified failure;
its exit 101 is an intentional interruption, not a completed build or source error.
Corrected check/build/runtime will use final/reviewed logs and must pass before acceptance.
Claude payment source review remains live under session51772 (PID1270384).
Older node publication and preflight regressions are still running; do not duplicate them.

Corrected all-features/all-targets check passed (16.07s), ordered patch audit passed.
Supervisor55029 is building then running the corrected frozen payment/refund test;
logs: build/bch-native-walletless-refund-final-{release,runtime}-20261015.log.
Evidence: build/bch-native-walletless-refund-validation-20261015.json.
No passing runtime or completed Claude review claimed yet.

### 2026-10-15 — walletless refund-wallet lifecycle; node regression passed

Added restore_light_bch_refund_wallet, open_light_bch_refund_wallet and
check_light_bch_refund_wallet sharing the node journal/wallet implementations.
Fresh recovery evidence uses the originally pinned observer. Restore intent precedes
wallet creation; interrupted restore/scan requires explicit open/check, not regeneration.
Copied-store scenarios cover normal restore, lost restore/scan replies, reopen,
corrupt readiness and missing intent, with exactly one generate_from_keys operation.
Claude payment review completed; dispositions saved in
build/bch-native-walletless-payment-review-disposition-20261015.txt.
Strengthened rejected-observation tests to assert zero additional broadcasts,
reciprocal approval mismatch rejection and the specific lost-result recovery error.
Complete recovery/payout routing remains a release blocker before helper/UI activation.
Observer credentials are excluded from identity; port/subversion migration needs explicit design.

Final all-features/all-targets check passed (16.23s); ordered patches passed.
Release/runtime logs: build/bch-native-walletless-refund-wallet-{release,runtime}-20261015.log.
Evidence: build/bch-native-walletless-refund-wallet-validation-20261015.json.
These new runtime cases remain pending.

Corrected node publication regression PASSED: 1 passed, 0 failed, 2833.30s.
It predates the walletless native changes and does not validate them.
The corrected walletless payment/refund release passed (3m47s), runtime55029 remains live.
Preflight regression PID1263830 also remains live; do not restart either on quiet logs.
Website roadmap source updated with the verified node result and pending wallet lifecycle;
no live website deployment performed.

Wallet lifecycle release/runtime supervisor29278; source-only Claude review31774.
Poll these existing handles. Do not edit engine sources while release compilation is active.

### 2026-10-15 — walletless native refund payout (runtime pending)

Added walletless prepare/review/relay/observe native refund payout APIs using shared
node implementations and the original payment observer binding.
Saved preparation retries now reject a changed observer before returning historical accounting.
No saved schema, approval requirement, dispatch ordering or uncertainty guard was relaxed.
Walletless copies of payout and relay suites cover exact principal/reserve/fee,
lost construction replies, retained checkpoint corruption, approval rejection,
acknowledged/lost dispatch, reopen, no repeated construction/relay and confirmed payout evidence.
The pending regression retains the existing node lifecycle/payout scenarios too.

Final all-features/all-targets check passed (17.60s), ordered patch audit passed.
Release/runtime supervisor99277 uses bch-native-walletless-refund-payout-* logs and frozen binary.
Evidence: build/bch-native-walletless-refund-payout-validation-20261015.json.
The preceding refund-wallet release passed (3m47s), runtime29278 remains live.
Refund-only runtime55029 and original preflight PID1263830 also remain live.
Do not restart these on quiet logs, or count them as passed.

Claude refund-wallet review complete; checked dispositions saved in
build/bch-native-walletless-refund-wallet-review-disposition-20261015.txt.
Session APIs already wrap incoming key/password Strings in Zeroizing; removed per-byte
hex allocations and now explicitly zeroize the recovered spend scalar after copying
its bytes into Zeroizing. Shared native RPC JSON/body copies still need secret-aware
handling; this remains follow-up before release, not a complete memory-erasure claim.
Next: transport secret handling, native punishment routing, full paired runtime acceptance,
then helper/GUI routing. Latest payout changes are not separately source-reviewed yet.
Website roadmap source marks payout implemented/validation pending; not deployed.

### 2026-10-15 — native RPC owned-buffer hardening (runtime pending)

Native RPC parameters are owned by a recursive string/key wiping guard before awaiting
the auth lock; request bytes use a zeroizing owner shared through Bytes for Digest retry.
Serialization counts first and allocates once, avoiding abandoned realloc buffers.
Bounded response accumulation is zeroizing and preallocated. Failed response envelopes
are guarded; rpc removes/moves result out rather than cloning its whole JSON subtree.
Added the already locked bytes1.11.1 dependency and updated the swap-machine lock entry.
Tests cover escaped/nested request preservation, shared retry storage and nested clearing.

All-features/all-targets check passed (18.03s); ordered patches passed.
Supervisor81626 builds a frozen binary then runs native_funding_rpc:: (previously74 tests,
plus2 new tests). Source-only Claude review83005 remains pending.
Evidence: build/native-rpc-sensitive-buffers-validation-20261015.json.
Do not edit engine source while this release compile is active.
Limits: result Values returned to callers, third-party HTTP/OS buffers, Digest internals
and parser-failure intermediate allocations remain outside this erasure claim.
No complete end-to-end memory-erasure claim or public activation is justified.

Refund payout release passed (3m54s); runtime99277 remains live.
Earlier preflight1263830, refund55029 and refund-wallet29278 are also live.
Next DEX gap: native punishment observer/review/relay routing is still node-only;
OwnedBchContractRelay currently supports cancellation/refund but needs explicit
punishment relay support with the same immutable native-payment observer identity.


Claude native RPC review completed; accepted the unpolled-future cancellation gap.
rpc and rpc_response now wrap params synchronously before returning an async block.
Added unpolled-future wiping-path assertions, same-connection Digest auth-int retry
with identical request bytes, rejected second authentication, malformed/wrong-ID/
oversized response cases, and guarded opened_wallet response handling.
Final all-features/all-targets compilation passed (17.74s) and final patch audit passed.
Initial release compiler was intentionally stopped (exit101) after the verified defect;
it is not a passing build. Corrected release/runtime supervisor21570 uses
build/native-rpc-sensitive-buffers-final-{release,runtime}-20261015.log.
The pending suite is native_funding_rpc:: (74 existing plus5 new tests).
Review disposition: build/native-rpc-sensitive-buffers-review-disposition-20261015.txt.
Do not edit engine sources during this corrected release compile.
No new completion claim added to the public roadmap until this validation passes.

### 2026-10-15 — native walletless cancellation and punishment (runtime pending)

Added light punishment review, maturity window, explicit contract relay and confirmation
through shared existing node implementations. OwnedBchObserver remains read-only;
OwnedBchContractRelay now supports exact punishment dispatch after engine consent,
confirmed native payment, mature unspent recovery output and durable attempt.
Saved schemas and no-retransmission semantics remain unchanged.

Found and fixed a separate role-routing gap: light cancellation materials were
BchFunder-only. They now route NativeFunder through its authenticated receipt and
retained payment observer/time binding, without the BCH funder's private bundle.
Copied-store cancellation and punishment tests cover wrong approval/observer,
immaturity/spent output, lost/mismatched acknowledgement, restart, no retransmission,
exact confirmation, damaged journals, backwards time and independent private stores.
Both acknowledged and uncertain native funding branches invoke these scenarios.

Reviewed all-features/all-targets check passed (17.08s); ordered patch audit passed.
Release/runtime supervisor18075 uses build/bch-native-walletless-recovery-* logs.
Evidence: build/bch-native-walletless-recovery-validation-20261015.json.
Punishment Claude review completed; dispositions verify quote-policy delay equality,
metadata directory flock and conservative deadline/uncertainty semantics.
The later native cancellation addition has not received separate Claude review yet.
New runtime is pending. Do not edit source while this release compile is active.

Completed evidence: native RPC hardened suite PASSED 78 tests, 0 failed, 1 ignored,
1.31s, release4m00s. The ignored actual_native_wallet_preparation_checkpoint requires
an explicitly launched funded offline fakechain; it is not counted as passed.
Original frozen walletless preflight PASSED 1 test, 0 failed, 2867.84s; this predates
its get/context follow-up and later native payment/refund code.
Website roadmap source updated for these verified results, with limits; not deployed.
Refund55029, refund-wallet29278 and refund-payout99277 regressions remain live.
Next: finish current runtime/review acceptance and full paired-chain recovery,
then wire the native walletless paths into private helper/GUI routing. Keep public
activation and packaged helper replacement gated on that broader evidence.

### 2026-10-15 — private native BCH observer helper component checks passed

The private helper now explicitly selects an owned BCH observer for native payment
preparation and separately approved relay using its v2 configuration. Existing v1
configuration retains the node-wallet backend. Incompatible descriptors and missing,
null or unsupported observer choices are rejected rather than silently falling back.

All-features/all-targets check passed (18.09s), release test build passed (1m24s),
and the frozen helper test binary passed 31 tests, 0 failed, 0 ignored (0.02s).
Ordered patch reproduction passed, with an existing whitespace warning.
Evidence: build/bch-native-observer-helper-validation-20261015.json.

These are component results, not full paired-chain or app acceptance. Payment,
refund, refund-wallet, payout and recovery runtime logs still have no final result.
The later shared cancellation shape/accounting check needs runtime acceptance too.
No Qt v2 configuration integration, packaged helper replacement or public activation
is claimed. Website roadmap source now records the helper result and keeps the
remaining recovery work pending; no live website deployment was verified.

Source-only Claude helper review completed. Verified distinct schema-tagged backend
identities, canonical XMZ address parsing and strict unknown-field rejection; no
blocking helper finding remains from that review. Disposition:
build/bch-native-observer-helper-review-disposition-20261015.txt.
A separate real CLI release build is running in supervisor36969, with log
build/bch-native-observer-helper-cli-release-20261015.log. Do not edit engine source
during compilation. The paired fixture still needs explicit owned-observer routing;
its existing node-wallet preparation result does not cover the v2 helper route.
Four frozen walletless regression processes were confirmed live (1286937,1295282,
1302904,1326039); no final runtime result yet. No test was restarted.

### 2026-10-15 — paired owned-observer helper fixture added (acceptance pending)

Actual helper CLI release passed; frozen copy is
build/bch-native-observer-helper-cli-20261015 (SHA256
62824083d70ca55fa720557f3a43f514c7195d4c62ffba91042f52d158fb72f2).
Python paired runner now explicitly selects --owned-observer and verifies a separate
nativeOwnedObserverVerified result. It clears inherited mode via an explicit 0/1
setting; v1 Qt preparation/relay probes are rejected for v2 mode. Rust fixture writes
the v2 descriptor/configuration, resumes native preparation/relay against the same
observer identity, and keeps BCH-funder release routing on its original backend.
Existing wrong-approval, repeat-submit, offline status and store-invariance checks
remain in the actual-helper path. This is not full settlement/recovery acceptance.

All-features/all-targets compilation PASSED (17.46s), Python syntax PASSED and ordered
patch reproduction PASSED. Patch: upstream-bch-owned-observer-paired.patch.
Release fixture build supervisor66607 is running; it will freeze
build/bch-owned-observer-paired-fixture-20261015 but DOES NOT launch the paired test.
Claude source-only review supervisor68904 is running; log
build/claude-bch-owned-observer-paired-review-20261015.log.
Next: inspect review, finish release, launch test_bch_native_relay_paired.py with
--owned-observer and pinned isolated BCHN/XMZ binaries, then record the result.
No engine edits during the release compile. No new runtime completion on roadmap.

Corrected frozen walletless payment/refund-key recovery regression PASSED: 1 test,
0 failed, 0 ignored, 2964.68s. Evidence updated in
build/bch-native-walletless-refund-validation-20261015.json; runtime log
build/bch-native-walletless-refund-final-runtime-20261015.log.
This predates refund-wallet lifecycle/payout, cancellation/punishment, RPC-buffer
follow-ups and helper routing. Three later frozen regressions remain live.
Website roadmap source updated with this scoped pass; no live deployment verified.

### 2026-10-15 — paired fixture review strengthened; actual chain run queued

Claude source review completed and checked against source. Added exact retained
payment identity and unchanged-journal checks after restart, explicit observer pin
assertions, rejection of wallet-backend replay, helper environment isolation and
correct Qt reporting gates. Owned mode creates a second BCH wallet to reject
implicit root wallet RPC access. Existing singleton native mempool check also
rejects duplicate deposits. Shared snapshot fixture and no native-daemon restart
remain limits. Disposition: build/bch-owned-observer-paired-review-disposition-20261015.txt.
Reviewed all-features/all-targets check PASSED (16.80s). Initial release fixture
finished successfully; it predates the review improvements and was not run.
Reviewed runner build/run-bch-owned-observer-paired-20261015.py now builds a separate
frozen fixture then automatically runs --owned-observer on isolated BCHN regtest
and offline XMZ fakechain with pinned actual helper. Status file:
build/bch-owned-observer-paired-run-status-20261015.json. No engine edits during
its release compile. Do not launch a duplicate runner. No paired pass claimed yet.

### 2026-10-15 — desktop native observer configuration component tests passed

BchNativePreparationJob and BchNativeRelayJob now share explicit configuration-version
validation: v1 requires absent observer kind; v2 requires owned-node and exact field
count. Existing path, approval, deployment, source-change and helper digest checks
remain. Added schema/null/missing/wrong-kind/unknown-field rejection tests plus
consent and restart tests for both versions. Focused build passed; worker tests
11 preparation +13 relay passed, protocol tests16 +20 passed, no failures/skips.
Evidence: build/bch-native-observer-desktop-validation-20261015.json.
Full app/probe build running (bch-native-observer-desktop-app-build-20261015.log).
Claude source review70497 running (claude-bch-native-observer-desktop-review-20261015.log).
This is controlled-process coverage, not real Qt paired-chain acceptance. The
current paired runner23444 still deliberately excludes Qt probes in owned mode;
it is building a reviewed Rust fixture before running actual helper isolated chains.
Do not edit engine sources during its compile or restart its live supervisor.
No bundled Rust helper replacement or public activation performed.

### 2026-10-15 — refund-wallet engine lifecycle and reviewed desktop checks passed

Frozen walletless refund-wallet regression PASSED 1 test, 0 failed, 0 ignored,
3028.97s. Evidence updated in bch-native-walletless-refund-wallet-validation-20261015.json.
This predates payout and later recovery/helper changes. Payout1302904 and
cancellation/punishment1326039 still confirmed live; do not restart for quiet logs.

Claude desktop review completed; disposition in
build/bch-native-observer-desktop-review-disposition-20261015.txt.
Qt config validation now checks exact field names; tests cover replaced required
fields, wrong types and versions, and changing observer mode in both directions
after approval. Reviewed worker13+15 and protocol16+20 checks PASSED (64 total).
Initial full app/probe build passed; final reviewed app/probe rebuild is live in
supervisor26185, log bch-native-observer-desktop-reviewed-app-build-20261015.log.
Actual helper paired runner23444 finished its release and is now running isolated
chains (bch-owned-observer-paired-run-status-20261015.json); runtime log still pending.
No real Qt paired-chain claim yet. Website source updated for scoped wallet lifecycle
pass; no live deployment, bundled helper replacement or public market activation.

### 2026-10-15 — real desktop-worker paired acceptance queued

Final reviewed app and preparation/relay probes built successfully. Desktop evidence
JSON now hashes the reviewed runtime logs and full app build.
New engine test opt-in MONZERO_BCH_NATIVE_OWNED_OBSERVER_QT_PAIRED requires owned
observer mode plus both probe paths/digests. Unpinned or implicit Qt mode remains
rejected. Patch upstream-bch-owned-observer-qt-paired.patch reproduces the change.
All-features/all-targets compilation PASSED (17.47s); ordered patch audit PASSED.

Supervisor35216 runs build/run-bch-owned-observer-qt-paired-20261015.py: reviewed
release build, independent frozen machine, then actual Qt workers + pinned actual
helper on isolated chains. Current stage is building; inspect
build/bch-owned-observer-qt-paired-run-status-20261015.json and corresponding
reviewed-release/driver/runtime logs. The two Qt probes are copied mode0500 at
build/bch-native-observer-qt-{preparation,relay}-probe-20261015.
The Python harness is a separate three-file snapshot at
build/bch-owned-observer-qt-harness-20261015, preserving the still-running direct
helper runner23444's original hashed source. It asserts both Qt result flags and
hashes both probes plus dependencies. After direct acceptance terminates, merge
this explicit Qt mode into the repository runner rather than maintaining duplicate
harness sources. Do not edit engine during release compilation or restart live runs.

Direct helper paired test still live without a final result. Refund-payout and
later cancellation/punishment regressions also remain pending. No new runtime pass
or public roadmap completion claim; no packaged helper replacement.

### 2026-10-15 — native cancellation helper owned-observer routing added

Private cancellation helper v2 explicitly selects owned-node and requires NativeFunder.
Existing v1 retains node-wallet routing. Dedicated strict CancelConfig avoids changing
other FundingConfig consumers; missing/null/wrong kind, unknown fields, relative paths
and malformed pins reject. Offline review/status remain credential-free. Submit checks
exact consent before descriptor/cookie reads, validates deployment and constructs the
explicit contract relay from the same observer. Observe requires indexed transaction
lookup and enforces native preparation lower time bound. No fallback or implicit retry.
Patch: upstream-bch-native-cancel-helper.patch. Design reviewed by Claude, disposition:
build/bch-native-cancel-helper-design-disposition-20261015.txt.
All-features/all-targets compile PASSED (1.36s); patch audit and release/helper-test
supervisor launched (bch-native-cancel-helper-{patch-audit,release,runtime}-20261015.log).
Full real-helper native cancellation, Qt configuration and runtime acceptance remain
unfinished. Do not edit engine during helper release compile.

Refund-payout frozen engine regression PASSED 1 test, 0 failed, 0 ignored, 3134.05s.
Evidence bch-native-walletless-refund-payout-validation-20261015.json and website
roadmap source updated; test predates later cancellation/punishment/RPC/helper work.
Direct-helper paired23444 and actual-Qt paired35216 now both run isolated chains,
using separate frozen binaries and separate harness source. Neither has final result.
Native cancellation/punishment frozen regression1326039 still pending. No packaged
helper change, public activation or live roadmap deployment performed.

### 2026-10-15 — native cancellation helper/desktop component tests passed

Helper release/tests PASSED32, no failures/skips (0.02s). Native cancellation v2
Qt worker now accepts the owned observer descriptor, recomputes the schema-tagged
identity and requires NativeFunder. Tests cover both formats for explicit approval,
confirmation, lost reply/no-resend and restart; malformed kind, wrong role,
wrong descriptor and changed observer deployment reject before submission.
Focused desktop build PASSED; worker12 +protocol20 PASSED, no failures/skips.
Evidence: build/bch-native-cancel-helper-desktop-validation-20261015.json.
Full app/probe build41510 pending; Claude desktop source review7063 pending.
These are component tests, not actual native cancellation helper/Qt acceptance.
Existing real-helper and Qt payment paired runs23444/35216 remain live; native
cancellation/punishment runtime1326039 also live. No restart of quiet tests.
Next: disposition desktop review and finish build, then wire native cancellation
into a real-helper recovery acceptance fixture; keep bundled helper unchanged.

### 2026-10-15 — atomic desktop cancellation configuration; paired harness fix

Claude cancellation desktop review completed. Fixed confirmed partial-configuration
bug and mismatched reread hazard: validate on temporary candidate, compare exact
wrapper/admission/quote bytes, check regtest and deployment before committing fresh
job fields. Added failed-config and preconfigured changed-wrapper assertions.
Reviewed cancellation worker13 and protocol20 PASSED. Initial full app build passed;
reviewed app/probe rebuild98391 running. Disposition:
build/bch-native-cancel-desktop-review-disposition-20261015.txt.

Direct helper paired23444 FAILED (0pass1fail787.26s): missing environment variable
in success_identity_fixture's mandatory Qt success review/signing stage. Original
runner omitted identity/success-review pins; no runtime completion claimed. Preserved
failure logs. Corrected runner build/run-bch-owned-observer-paired-retry-20261015.py
reuses the same frozen Rust/helper executables, adds disposable encrypted test
identity, frozen success identity/review probes, and successful signer preflight.
Retry18096 now runs isolated chains; logs/status use bch-owned-observer-paired-retry.
Original Qt paired35216 remains live and has the same omitted later-stage pins; do
not mutate its hashed files or launch its retry until terminal. Then reuse the
corrected identity setup with its Qt snapshot and frozen reviewed machine, avoiding
an unnecessary rebuild. Neither paired variant has passed yet.
Cancellation/punishment engine1326039 remains pending. Roadmap retains scoped
component passes only; no real Qt/native cancellation helper acceptance yet.

### 2026-10-15 — real native cancellation helper/Qt recovery fixture added

Final reviewed cancellation app/probe build PASSED. New optional helper fixture is
called by native cancellation tests after copying private stores without BCH funder
recovery bundle. It reads retained native admission pins/time, creates private helper
configs, exercises real Qt/helper review/status/approved relay/confirmation and lost
reply/no-resend paths against the loopback mock BCH server. Fresh metadata per mode,
exact broadcast count, intent-before-relay and unchanged private-store checks remain.
This is process integration over simulated chain evidence, not a paired-chain swap.
Patch: upstream-bch-native-cancel-helper-fixture.patch.
First compile caught fixture-only missing contents helper/private method use; fixed
with local store snapshot and public received-message API. Corrected all-features/
all-targets compile PASSED; ordered patch reproduction PASSED. Initial failure log
retained, not a passing check.
Supervisor52882 runs build/run-bch-native-cancel-helper-fixture-20261015.py: builds
real helper then frozen lib fixture, copies production cancellation Qt probe, sets
MONZERO_BCH_NATIVE_CANCEL_HELPER and MONZERO_BCH_QT_CANCEL_PROBE pins, then runs
full recovery regression. Status bch-native-cancel-helper-fixture-status-20261015.json.
Do not edit engine during compilation. Claude fixture source review running in
claude-bch-native-cancel-helper-fixture-review-20261015.log.
Direct-helper corrected paired18096 and original Qt paired35216 remain active.
Original Qt run still lacks mandatory later-stage identity probe setup; wait for
terminal result before corrected retry, preserving logs and live hashed files.
Native recovery1326039 still pending. No new runtime completion for roadmap.

### 2026-10-15 — both paired acceptance runners corrected and live

Original actual-Qt paired35216 terminated FAILED0pass1fail798.50s, with the same
missing mandatory success identity/review environment as direct23444. Failures
preserved in build/bch-owned-observer-paired-initial-failures-20261015.json.
Corrected actual-Qt runner build/run-bch-owned-observer-qt-paired-retry-20261015.py
is live in supervisor66362/PID1379616 after successful encrypted identity signer
preflight. It reuses frozen Qt-reviewed machine, actual helper, preparation/relay
probes and separate harness; adds disposable identity and frozen signing/review
probe pins. Status/logs prefix bch-owned-observer-qt-paired-retry.
Direct corrected runner18096/PID1371873 remains live. Neither is a passing result.
Do not rebuild or restart their frozen fixtures because logs are quiet.
Native cancellation helper/Qt fixture52882 is building its frozen lib test after
helper CLI release; status bch-native-cancel-helper-fixture-status-20261015.json.
Claude fixture review3806/PID1378106 remains live with no output yet; do not relaunch.
Original cancellation/punishment engine1326039 remains live (~46minCPU).
Final reviewed desktop cancellation app build passed. No new roadmap completion
claim is warranted until remaining runtime evidence arrives.

### 2026-10-15 — native cancellation fixture review hardening compiled

Claude fixture review complete. Added scoped restoration of the helper's mutated
server fields (broadcast counters remain cumulative), helper subprocess environment
allowlist, expected-rejection BrokenPipe handling, third acknowledgement outcome,
explicit private-bundle exclusion count, offline no-chain-call assertion, original
metadata/immature-rejection store invariants and historical confirmed=false check.
Disposition: build/bch-native-cancel-helper-fixture-review-disposition-20261015.txt.
Reviewed all-features/all-targets compile PASSED17.71s; ordered patch audit logged
in bch-native-cancel-helper-fixture-reviewed-patch-audit-20261015.log.
These source follow-ups POSTDATE initial frozen real-helper/Qt cancellation runtime:
supervisor52882 completed both builds and is now running. Do not claim its result
covers the reviewed fixture changes; later runtime acceptance remains required.
Current helper SHA2565fe7d7e48d457d8be27212c80217e104fa57137568706653b1cbc84c92734521.
Original native cancellation/punishment1326039 remains live without final result.
Direct corrected paired18096 and Qt corrected paired66362 remain live. No duplicate
restart or new runtime completion on roadmap. Production cancellation helper and Qt
code unchanged this turn; only test evidence strengthened.

### 2026-10-15 — offline punishment helper added; cancellation fixture timestamp fixed

Added review_bch_punish_relay_for_deployment sharing original node/owned review
validation. New private --punish-review/status-stdio-v1 helper returns public exact
terms and validated dispatch history without credentials, RPC, maturity claim or
broadcast. Strict config/request tests added; shared engine fixture checks equality
with observer review and rejects changed pin/backwards time. Punishment broadcast,
window/confirmation helper and desktop controls remain unfinished.
Patch upstream-bch-punish-offline-helper.patch. Compile PASSED17.74s initial and
16.98s with engine assertions; Claude review30451 pending.

Initial native cancellation helper/Qt runtime52882 FAILED0pass1fail145.45s at review.
Confirmed fixture timestamp mismatch: original wrapper used execution admitted_at
(1005), while quote record admittedAt is1000. Qt expectedReview and Rust restore
require the quote admission timestamp. Fixed wrapper to use quote-record admittedAt
and assert execution admission is not earlier. No production validation weakened.
Corrected compilation PASSED17.11s; ordered patch audit passed.
New runner build/run-bch-native-cancel-helper-reviewed-fixture-20261015.py now builds
and runs helper unit tests (including new punishment tests), then freezes a reviewed
lib fixture and runs real cancellation helper/Qt integration. It reuses existing
frozen cancellation CLI and Qt probe (production behavior unchanged), includes
reviewed fixture isolation and timestamp fix, and keeps old failure logs intact.
Status: bch-native-cancel-helper-reviewed-fixture-status-20261015.json.
Do not edit engine during its compile. Direct corrected paired18096, Qt corrected
paired66362 and original recovery1326039 remain pending. No new roadmap completion.


## 15 October 2026 — paired payment results and roadmap reconciliation

Verified terminal evidence (supersedes earlier running statuses):
- Direct owned-observer paired payment: PASS, 1 test, 1116.85s.
  Evidence: `build/bch-owned-observer-paired-retry-result-20261015.json`.
- Desktop owned-observer paired payment: PASS, 1 test, 1120.86s.
  Evidence: `build/bch-owned-observer-qt-paired-retry-result-20261015.json`.
  Both verify approved native deposit preparation/relay and confirmation across
  isolated BCH/XMZ chains, including restart; fullCrossChainSettlement remains false.
- Original native cancellation/punishment engine regression: PASS, 1 test, 3438.34s.
  Frozen version predates the latest helper/desktop additions.
- Offline helper suite: PASS, 34 tests, 0 failures, 0 ignored.
- Reviewed cancellation helper/Qt fixture: FAIL, 1 test, 141.61s, cancellation
  review rejected. The timestamp correction alone did not resolve the failure.
  Evidence: `build/bch-native-cancel-helper-reviewed-fixture-runtime-20261015.log`.
- New desktop punishment protocol suite remains failing (2 pass, 13 fail).
  The valid-baseline rejection needs diagnosis; temporary synthetic qWarning
  diagnostics in its test must be removed after diagnosis. No acceptance claimed.

Website roadmap source updated with the verified payment milestone and explicit
recovery limitations. Public deployment has not been verified. No public market
activation, revised delivery estimate, or overall goal completion is claimed.


## 15 October 2026 — punishment review fixture corrected

The desktop punishment protocol baseline mixed quote punishBlocks/xmzConfirmations
of 1 with policy/review values of 100. Corrected the synthetic quote to match policy,
retaining its recomputed commitment. Removed temporary diagnostics and added a
changed-policy rejection test. Production validation was not relaxed.
Build passed; runtime: 16 passed, 0 failed, 0 skipped (7ms).
Evidence: `build/bch-punish-protocol-corrected-runtime-20261015.log`.
The initial invocation used the wrong executable directory (bin); the successful
run used the actual build output under src. This is validator-only acceptance;
punishment worker, submission, confirmation and UI remain unfinished.
Roadmap source updated, no deployment verified. Source-only Claude review is
running as tool session 29082, output `build/claude-bch-punish-protocol-review-20261015.log`.
Re-poll that session before starting any duplicate review.


## 15 October 2026 — cancellation fixture uses reviewed admission

Static diagnosis: parent message fixture used AdmittedExecution::admit, which does
not save the approval record required by the production helper's
saved_admission_review. Changed parent to review_admission/admit_reviewed and
assert reopening returns the exact approval commitment. Production helper checks
remain unchanged. Included parent diff in upstream-bch-native-cancel-helper-fixture.patch.
All-features/all-targets offline compile and ordered patch audit PASS.
New frozen integration runner: build/run-bch-native-cancel-reviewed-admission-fixture-20261015.py.
Tool session 37499; status build/bch-native-cancel-reviewed-admission-fixture-status-20261015.json.
Building at checkpoint; do not modify engine while compiler lives. Reuses pinned
production helper and Qt probe. No runtime pass claimed; earlier failures preserved.

Claude protocol review completed (session29082 exit0). Output:
build/claude-bch-punish-protocol-review-20261015.log.
Needs disposition before worker integration: policy-consistent synthetic terms,
amount bounds, independent retained transaction/pin context, stronger mutation tests.
Its fee-accounting concern must be checked against recovery_plan semantics:
engine cancel subtracts lock_fee_atoms, then punishment subtracts recovery_fee_atoms;
current desktop formula matches this structure. Review was limited to four files,
so do not accept conditional claims without inspecting engine/shared validators.
No live deployment or broader DEX completion claimed.


## 15 October 2026 — punishment review amount policy

Addressed Claude's concrete amount-policy concern in BchPunishmentProtocol::review:
checks maximum BCH principal (including supply cap), both covenant fees against
policy, minimum output equality with the quote, and final payout against that
minimum. Synthetic fixture now uses distinct lock/recovery fees (1000/2000),
principal100000, payout97000, floor546 and compatible amount-policy limits.
Payout arithmetic reads quote terms instead of duplicated fixture literals.
Added exact/one-unit-outside fee and principal bounds and payout floor boundary
cases; quote mutations recompute the synthetic quote commitment.
Build PASS; runtime26pass0fail0skip11ms:
`build/bch-punish-policy-final-runtime-20261015.log`.
These tests validate public review structure and amounts only, not signatures,
chain evidence or complete user recovery. Fixture records are synthetic/in-memory.

Claude fee-formula concern resolved by source inspection of swap-core/src/bch/recovery.rs:
recovery_atoms=locked_atoms-lock_fee_atoms and
final_recovery_atoms=recovery_atoms-recovery_fee_atoms. No third deduction needed.
Transaction/pin provenance remains engine-owned; desktop worker integration must
pin helper/config and compare retained review context through approval/dispatch.
Do not treat public hashes or a recomputed commitment as independent proof of
those transactions. No worker integration completed in this checkpoint.

Cancellation reviewed-admission runner session37499 confirmed live (PID1406554)
and still building. Do not change engine while compiler runs; do not restart it
on quiet output. Roadmap source updated to26checks, public deployment unverified.


## 15 October 2026 — offline punishment desktop worker

Added BchPunishmentJob.h/.cpp following cancellation worker lifecycle. Supports only
review/status. Pins executable and exact owner-only wrapper/admission/quote bytes,
checks sources before/after subprocess, bounded canonical response, exact request/
trade, protocol review, relay history, executable=false/chainObserved=false/null
observation. Unknown responses clear displayed state. No submit or RPC methods.
Shared synthetic PunishContext extracted to tests/BchPunishmentTestFixture.h.
CMake worker target added; app source glob includes worker after configure.
Initial test13pass1fail: synthetic print appended newline; corrected to match
production helper compact output and added newline-negative test.
Final worker15pass0fail0skip672ms and protocol26pass0fail0skip.
Logs build/bch-punish-worker-final-runtime-20261015.log and
build/bch-punish-worker-protocol-runtime-20261015.log.
Full app build running session18347, log build/bch-punish-worker-app-build-20261015.log.
Claude source-only review running session68491, log build/claude-bch-punish-worker-review-20261015.log.
Poll these existing handles; do not duplicate. Real-helper test, controller/QML,
submission and confirmation remain unfinished; no end-to-end acceptance claimed.
Cancellation rerun37499 is now running real helper/Qt recovery (compile complete),
log build/bch-native-cancel-reviewed-admission-fixture-runtime-20261015.log.
Local roadmap source records only synthetic worker checks. No live deployment.


## 15 October 2026 — reviewed punishment worker and real-helper fixture

Full app build passed initially and after Claude review fixes. Claude completed
session68491; disposition build/claude-bch-punish-worker-review-disposition-20261015.txt.
Worker now retains verified review/history across requests, rejects changed valid
transaction reviews or history regressions, queues notifications, and reports
availability only after process exit. Synthetic fee mutation recomputes commitment.
Reviewed worker17pass0fail0skip979ms, full app/probe build PASS (session9565).

Added test adapter tests/bch_punishment_probe.cpp and CMake probe target.
Added engine punish/helper.rs fixture, invoked before engine punishment dispatch:
optional pinned real helper plus optional pinned Qt adapter, exact engine/helper/
Qt review equality, offline status, rejected approvals/submit and unchanged private
stores. No user data or private proof material printed. Patch
upstream-bch-punish-helper-fixture.patch added to ordered verifier.
All-features/all-targets compile PASS16.16s and patch audit PASS.
New runner build/run-bch-punish-real-helper-fixture-20261015.py, session84716,
status build/bch-punish-real-helper-fixture-status-20261015.json, builds frozen
helper then frozen engine fixture and freezes reviewed Qt probe. Runtime result
pending. Do not edit engine during build or restart quiet runtime.
Cancellation runner37499 remains active independently using its frozen binary.
Roadmap source updated17 workerchecks; no live deployment or full recovery claim.


## 15 October 2026 — punishment controller startup and serialization

DexEngine owns the offline punishment worker, exposes status/review/check, shares
bchOperationIdle, queues saved-status startup and notifies other recovery controls
of availability. Worker changes invalidate publication-repair consent through the
existing dependency mechanism. Private --dex-bch-punishment option requires the
DEX prototype flag. No submit/observe API or user panel added.
Full app/controller build PASS session47407. Synthetic worker fixture extracted
to BchPunishmentWorkerTestFixture.h for controller reuse with logged actions.
Focused controller runtime4pass0fail0skip1225ms (2actual cases plus init/cleanup):
startup only --punish-status, mutual exclusion with admission checks, plus existing
multiworker startup serialization. Worker17checks pass after fixture extraction.
Logs build/bch-punish-controller-{runtime,worker-runtime}-20261015.log.
Broader controller suite just started: build/bch-punish-controller-full-runtime-20261015.log.
Claude controller review session71408 running, output build/claude-bch-punish-controller-review-20261015.log.
Real-helper punishment runner84716 building fixture; cancellation37499 running
its frozen helper/Qt recovery. Do not restart quiet jobs or edit engine midbuild.
Roadmap source records scoped controller tests; no public deployment claim.


## 15 October 2026 — read-only timeout recovery panel

Dex.qml adds private-configured BCH timeout recovery panel with explicit offline
review/status actions, transaction/payout/fee/destination script/delay display,
unknown and historical attempted/acknowledged messages. No submission, no claim of
chain confirmation. Pair switch hides/disables panel; errors clear on status/pair
change. QML fake engine and interaction test added.
App resource-aware runner with fonts import:95pass0fail0skip7385ms, including new
panel test. Initial raw/runner launches lacked fonts import and exited1 without
useful diagnostics; passing command used build/dex-qml-runner -import monzero-gui/fonts
and MONZERO_TEST_RESOURCE_BUNDLE=build/bch-punish-panel-20261015.rcc (absolute path).
Evidence build/bch-punish-panel-qml-final-results-20261015.txt.
Broader controller suite43041 terminal65pass0fail21skip52652ms.
Claude controller review71408 complete, disposition saved. Added explicit
bchPunishmentChanged after synchronous light-wallet binding clears its guard.
Final resource/app/controller build and focused checks running; log
build/bch-punish-panel-app-build-20261015.log.
Cancellation37499 and punishment84716 both now running frozen real-helper fixtures.
No acceptance claim for either until terminal result. Roadmap source updated,
public deployment not verified. User-facing broadcast/confirmation still missing.


## 15 October 2026 — explicit punishment confirmation helper

Final panel app build9852 PASS and focused controller4pass0fail1174ms.
New punishment --observe-stdio-v1 helper action: async operation with explicit
4-field observation configuration, mandatory owned-node kind, absolute private
wrapper/RPC paths, retained quote/admission/review validated before credentials,
pinned deployment, exact engine confirmation reviewed commitment and public block/
height/depth/tip/time. Review/status retain offline behavior; no submission route.
Added strict config/request test (unknown fields, missing/null backend/path,
relative paths, approval rejected). Patch upstream-bch-punish-observation-helper.patch
and verifier entry. All-feature/all-target compile PASS1.59s; patch audit PASS.
Helper tests build running session93633; status
build/bch-punish-observation-helper-test-status-20261015.json. Do not edit engine
while this compiler is live. Claude observation review running97856, output
build/claude-bch-punish-observation-helper-review-20261015.log.

IMPORTANT: original offline punishment real-helper fixture84716 is terminal FAILED,
0pass1fail207.62s, Error: Litecoin journal is busy. Log
build/bch-punish-real-helper-fixture-runtime-20261015.log. Caller refund::exercise
still owns native execution on original metadata, and new punish/helper::exercise
tries AdmittedExecution::resume on that same store. Needs copying metadata (and
snapshots) into isolated PrivateStores before reopening/helper calls, as cancellation
fixture already does. Keep original stores fingerprinted and unchanged. Fix after
compiler93633 finishes, regenerate fixture patch, compile/audit, then unique frozen
retry; do NOT restart original runner or treat this as accepted integration.
Cancellation37499 remains running. Roadmap source explicitly records failed
punishment integration and compilation-only confirmation helper. No live deployment.


## 15 October 2026 — isolated punishment helper retry

Helper35tests PASS0fail0ignored0.02s (session93633 terminal); frozen executable
build/bch-punish-observation-helper-tests-20261015. Claude observation review97856
completed; disposition build/claude-bch-punish-observation-helper-review-disposition-20261015.txt
cross-checked against Journal ownership, strict structs, derived txids, deployment
review and common active-chain confirmation checks. Confirmation runtime pending.

Fixed punish/helper::exercise: copy original metadata and snapshots into separate
PrivateStores before helper/Qt or engine resume. Check both copies and original
stores remain unchanged. Original parent retains its exclusive journal normally.
Regenerated upstream-bch-punish-helper-fixture.patch. All-target compile PASS16.06s,
patch audit PASS. New runner build/run-bch-punish-isolated-helper-fixture-20261015.py,
session83438 building frozen lib fixture; reuses frozen original offline helper
and reviewed Qt probe. It validates offline review/status only, not new observe
command. Status build/bch-punish-isolated-helper-fixture-status-20261015.json.
Original failure preserved. Cancellation37499 still live, no quiet restart.
Roadmap source updated35helperchecks and isolated retry, no deployment claim.


## 15 October 2026 — desktop punishment chain check

BchPunishmentJob now supports explicit configureObservation/observe, after offline
configuration. Strict four-field wrapper and six-field owned-node descriptor;
canonical private files, owned-node kind, BCH regtest genesis/network, exact
schema-tagged deployment digest, backend/version/port/checkpoint validation.
No credentials read by desktop. Descriptor/wrapper bytes checked before/after
request; failures invalidate observation configuration, leaving offline status
available. Evidence clears before every request; response requires current request
interval, exact txid, depth, block/tip, retained-review identity and attempted or
acknowledged history. No send authority.
Worker24pass0fail0skip1445ms: build/bch-punish-observe-desktop-runtime-20261015.log.

DexEngine explicit configuration/action guarded by bchOperationIdle, hidden
--dex-bch-punishment-observation requires earlier --dex-bch-punishment. Startup
remains offline. Panel adds explicit chain-check button, confirmation count/block/
time separately from historical attempted/acknowledged state. QML95pass0fail0skip
7266ms: build/bch-punish-observe-panel-qml-results-20261015.txt.
Focused controller case added for startup-only-status and explicit observation.
App/controller build+focused tests running59623, logs
build/bch-punish-observe-controller-{build,runtime}-20261015.log.
Claude desktop observation review running25885, log
build/claude-bch-punish-observe-desktop-review-20261015.log.

Isolated offline real-helper retry83438 now running frozen fixture9d77b49e...
It predates observation worker; cannot establish confirmation acceptance.
Cancellation37499 remains running. No quiet restarts. Roadmap source updated
component scope; real-helper confirmation and punishment send remain unfinished.


## 15 October 2026 — confirmation integration and review corrections

Desktop observation app/controller59623 PASS, focused controller5pass0fail1282ms.
Claude25885 review completed; disposition saved
build/claude-bch-punish-observe-desktop-review-disposition-20261015.txt.
Exposed savedReview/savedRelayState separately from current request result and
confirmation. Failed check preserves labelled previously verified history, clears
current evidence. Confirmation cap1000 matches engine; delay cap65535 unchanged.
Worker25pass0fail1550ms, protocol27pass0fail11ms, QML95pass0fail8097ms.
Logs build/bch-punish-observe-reviewed-{runtime,protocol-runtime}-20261015.log,
build/bch-punish-observe-reviewed-qml-20261015.txt.
Full app rebuild64150 running; build/bch-punish-observe-reviewed-app-build-20261015.log.

Extended real-helper punishment fixture with explicit MONZERO_BCH_PUNISH_OBSERVATION=1:
pre-intent confirmation fails; post-ack engine/helper/Qt review/history agree,
confirmation exacttxid/depth3; staleactivechain rejects, originals/copies unchanged,
no extra broadcasts. Qt adapter now accepts observe and configures both wrappers.
Patch upstream-bch-punish-observation-fixture.patch and verifier entry. All-target
compilePASS16.70s, ordered patch auditPASS. This is compile evidence only.
New runner build/run-bch-punish-confirmation-helper-fixture-20261015.py, session62314,
status build/bch-punish-confirmation-helper-fixture-status-20261015.json, builds
fresh actual helper and frozen lib, then freezes current reviewed Qt probe. Avoid
changing probe until it is frozen; do not edit engine during compile.
Existing offline isolated retry83438 and cancellation37499 continue independently
on frozen binaries. No quiet restarts. Roadmap source updated counts/scope; no live
deployment or complete recovery/settlement acceptance claimed.


## 15 October 2026 — recovery panel visual check / submission preparation

Final reviewed observation app build64150 PASS. Active jobs revalidated through
live tool handles and CPU processes: cancellation37499 (~28min76%CPU), isolated
offline punishment83438 (~5min95%CPU), confirmation62314 compiling lib fixture.
Do not restart quiet proof tests; do not edit engine while compiler is live.

Changed panel observation timestamp from raw Unix seconds to local readable date/
time. Focused QML panel3pass0fail171ms (one test plus init/cleanup). Saved and
inspected build/bch-punishment-review-panel-20261015.png (synthetic reviewed state).
Log build/bch-punish-panel-readable-time-results-20261015.txt. Full app resource
rebuild started: build/bch-punish-readable-time-app-build-20261015.log.
No broader acceptance claimed from screenshot or presentation check.

Submission engine path inspected: exact commitment/deployment/time binding,
two-chain native payment and mature unspent recovery checks, deadline, durable
intent before one send, acknowledged idempotence and uncertain retry rejection.
Claude submission design review running6085, output
build/claude-bch-punish-submit-design-20261015.log. Proposed strict wrapper, approval
before credentials, existing isolated NativePreparationRpc and owned BCH relay;
implementation still outstanding. Check review before adding helper submission.


## 15 October 2026 — explicitly approved punishment helper submission

Submission design review6085 completed, disposition
build/claude-bch-punish-submit-design-disposition-20261015.txt. Compiler62314 was
confirmed finished before engine mutation; its frozen confirmation runtime active.
Implemented --punish-submit-stdio-v1, strict5field submission wrapper with mandatory
owned-node and absolute private paths; request requires exact digest. Restore quote/
admission/review, reject mismatch or any prior attempt before credentials. Validate
BCH deployment, strict local native descriptor, isolated fakechain before private
login. Dispatch time captured after setup; engine fresh2chain proof/unspent-maturity,
deadline, durableintent then single broadcast. Re-read acknowledged state; response
chainObservedfalse, no confirmation. No desktop submit exposed yet.
Patch upstream-bch-punish-submit-helper.patch/verifier entry. All-feature/all-target
check PASS1.48s; patch audit PASS. Added request/config positive/negative tests.
Helper test release build86542 running, status
build/bch-punish-submit-helper-test-status-20261015.json; do not edit engine midbuild.
Frozen tests target build/bch-punish-submit-helper-tests-20261015. Logs corresponding
build/bch-punish-submit-helper-tests-{build,runtime}-20261015.log.
Last readable-time app rebuild42307 PASS. Cancellation37499, offline isolated83438,
confirmation62314 remain running on frozen fixtures. No quiet restarts. Roadmap
source marks submission compilation only, no live deployment/full acceptance.


## 15 October 2026 — one-shot punishment submission worker

Helper suite86542 terminal36pass0fail0ignored0.02s; compilation/patch audit already
passed. Actual helper submission runtime still unverified.
BchPunishmentJob adds configureSubmission/submit. Requires existing pinned offline
and observation configs, strict5field submit wrapper, same BCH RPC path, strict
4field nativeRPC descriptor with nonzero distinct ports and canonical absolute
login path. Does not read credentials. Pins wrapper/native bytes and checks all
sources before/after. Fresh successful review and exact digest required; changing
submission config clears review. Latch set before process start, never cleared
within worker; errors/lost replies cannot enable retry. Response must acknowledge
exact approved review. Observation still separate and false after submit.

Synthetic tests31pass0fail0skip2583ms: normal/lost/changedreview/stderr/RPCmutation,
wrongapproval, prelaunchchangedsource, exactly1submit and restarted worker refuses
savedattempted/acknowledged. Final full app buildPASS40082.
Logs build/bch-punish-submit-desktop-final-{build,runtime}-20261015.log.
Claude worker review running1361, build/claude-bch-punish-submit-desktop-review-20261015.log.
Controller/privateCLI/QMLsubmit and realhelpersubmissionfixture remain next work;
no user-facing submit exposed. Existing cancellation37499, offline83438,
confirmation62314 still running frozen integration fixtures. Do not restart quiet
jobs. Roadmap source updated36helper/31workerchecks with correct limits; no deploy.


## 15 October 2026 — confirmation-depth fixture and killed-helper notification

Confirmation runner62314 terminal FAILED0pass1fail293.81s: helper observation
returned30confirmations, assertion expected3. Fixture server wallet_locks.rs uses
punishment_mature=true => height171/depth30 at tip200. Corrected expected depth30
and added height171 assertion, no production validation weakened. Patch regenerated.
All-feature/all-target check PASS16.78s; patch audit PASS.
New runner build/run-bch-punish-confirmation-depth-fixture-20261015.py, session53544,
status build/bch-punish-confirmation-depth-fixture-status-20261015.json, builds unique
frozen fixture and reuses prior pinned confirmationhelper/probe. It predates submit
helper and cannot prove submission acceptance. Old failure preserved.

Claude submission worker review1361 complete; disposition
build/claude-bch-punish-submit-desktop-review-disposition-20261015.txt. Fixed final
QProcess finished callback notification after earlier overflow/deadline kill so
availability updates after actual exit. Regression oversized/hanging submit kills
child, reportsunknown, restores checkavailability, holds1attempt latch.
Worker32pass0fail2692ms and fullappbuildPASS73621:
build/bch-punish-submit-reviewed-desktop-{build,runtime}-20261015.log.
Journal retain atomic publication/durability verified; exact descriptor-byte
binding across processes remains additional hardening, not claimed implemented.
Cancellation37499/offline83438 still live; noquietrestarts. Controller/QMLsend and
realhelpersubmitfixture remain. Roadmap source updated32checks and failed depth
expectation/retry; no live deployment or full integration claim.


## 15 October 2026 — private recovery submission controls verified

Connected DexEngine, the private --dex-bch-punishment-submission option and QML
approval controls. Sending requires a current exact review commitment and separate
consent; consent resets on review/pair changes. Uncertain outcomes instruct users
not to resend; acknowledgement remains distinct from chain confirmation.

Full app build passed. Four focused controller cases plus setup/cleanup passed
(6 total, 0 failures, 1440ms), covering explicit submission, observation, operation
guards and read-only startup. Evidence:
build/bch-punish-submit-controller-{build,runtime}-20261015.log.
All 96 QML tests passed (0 failures, 9074ms):
build/bch-punish-submit-panel-qml-20261015.txt.
The focused approval test also passed; rendered panel inspected in
build/bch-punishment-submit-panel-20261015.png.
These are synthetic-helper component checks, not real-helper submission acceptance.

Website roadmap source updated with these results and explicit remaining scope.
Public deployment has not been verified. Cancellation, isolated offline punishment
and confirmation-depth integration supervisors remain active at this checkpoint;
no passing result inferred from their running status. Real-helper submission
integration and the continuous recovery journey remain outstanding.


## 15 October 2026 — maintained paired-chain desktop runner

Merged the previously separate Qt acceptance mode into
 tools/dex-engine-port/test_bch_native_relay_paired.py using explicit --qt with
--owned-observer. Probe digests are checked before chain startup and after execution;
Qt evidence flags must be true. Direct mode now rejects inherited probe variables,
including empty values, and rejects Qt evidence. Optimization is explicitly refused
so Python -O cannot remove acceptance assertions. Helper evidence uses strict booleans.
The recovery fixture dependency is included in recorded source hashes.

New test_bch_native_relay_modes.py passes both tests (six invalid mode/pin cases
plus optimized-Python rejection). Python compilation and diff checks pass.
Claude source review: build/claude-bch-canonical-qt-runner-review-20261015.log.
The production helper remains required underneath the desktop probes. Engine fixture
preparation_helper.rs checks the selected probe hashes and invokes them; result flags
alone are not the only evidence. test_wallet_recovery.py is a transitive dependency
of bch_native_funding_fixture_v2.py. Caller-provided pins identify test artifacts,
not trusted release signatures or protection against hostile same-user replacement.
No distinct-binary requirement is imposed on compatible test adapters. The restart
claim remains BCH node restart, not XMZ daemon restart or full settlement.

New frozen paired runtime session13595 / supervisor1471714 is active; status/logs
use build/bch-canonical-qt-paired-*20261015. It uses the merged Qt mode before the
later strict-boolean/direct-mode/Python-optimization hardening; do not claim it
validates those later edits. Do not mutate its frozen three-file harness or restart
quiet processes. Existing recovery supervisors1406554,1436753,1462448 remain live.
Website source notes maintained runner work separately from pending runtime acceptance.


## 15 October 2026 — desktop punishment submission probe

Extended test-only bch_punishment_probe.cpp to accept submit requests with an
explicit exact commitment. It reads the private submission descriptor, derives a
private temporary observation wrapper without copying credentials, configures the
production job, obtains a fresh offline review and submits only the supplied
commitment. Saved attempted/acknowledged history prevents sending. No auto-approval
or retry is added. Successful output requires acknowledged state and preserves
outer request correlation; acknowledgement is not chain confirmation.

Added six process-adapter cases: approved, wrong approval, missing approval, saved
attempt, saved acknowledgement and lost reply. Worker/probe suite:38pass0fail0skip,
2910ms. CMake builds the probe as a dependency of its tests. Initial build exposed a
missing protocol include; fixed, rebuild and runtime passed. Logs:
build/bch-punish-submit-probe-{build,rebuild,runtime}-20261015.log.
Diff checks pass. These are synthetic-helper tests, not actual helper relay acceptance.
Claude source review session56996 remains pending at this checkpoint:
build/claude-bch-punish-submit-probe-review-20261015.log.

Real engine helper fixture extension remains next. Its source is
swap-machine/src/bch_key_recovery/authenticated/native_wallet/tests/wallet_locks/signing/relay/message/native/walletless/refund/punish/helper.rs.
Existing supervisors1406554,1436753,1462448 and1471714 confirmed live; do not restart.
Roadmap source updated with six adapter checks; no live deployment or market activation.

Claude probe review56996 completed. Findings checked against production worker; no approval bypass found. Disposition: build/claude-bch-punish-submit-probe-review-disposition-20261015.txt. Failed operations clear current result; submission config explicitly cross-checks prior paths. Durable engine intent remains essential; synthetic adapter acceptance is not real-helper validation.


## 15 October 2026 — native RPC fixture for actual recovery submission

Added test-only Fixture::rpc_ports and daemon get_info response for the helper's
mandatory offline fakechain/no-peers preflight. The fixture now echoes JSON-RPC
request ids so the isolation request is correlated correctly. No production
isolation check changed. New native_fixture_exposes_isolated_daemon_without_wallet_calls
checks daemon response and verifies no wallet call was made.

All-feature/all-target compilation PASS18.23s:
build/bch-punish-submit-native-fixture-check-20261015.log.
Ordered patch audit PASS: upstream-bch-punish-submit-native-fixture.patch added after
submit-helper patch. Baseline: build/bch-punish-submit-native-fixture-baseline-20261015.
Runtime release build/test session16158 still active (cargo1479797/rustc1479815 at
checkpoint); log build/bch-punish-submit-native-fixture-runtime-20261015.log.
Do not edit engine until compilation finishes. Not yet a runtime pass; roadmap
completion counts unchanged for this pending prerequisite.

Next integrate actual submission into punish/helper.rs using this native Fixture.
Its parent holds original exclusive journal, so helpers must use copied stores.
Run helper processes asynchronously/spawn_blocking: native fixture servers are Tokio
tasks and a synchronous subprocess wait can starve a current-thread runtime.
Configure BCH cancel mature/unspent and native incoming+confirmed only for owned
fixture; bind server relay_intent to each copy. Cover correct/wrong/missing approval,
immature/spent recovery, native unconfirmed, lost acknowledgement and repeat refusal.
Use fresh copies per helper/Qt mode and restore server state. No real-user funds.
Existing frozen supervisors1406554/1436753/1462448/1471714 remain active, not blocked.


## 15 October 2026 — real punishment submission integration implemented

Native isolation prerequisite runtime PASSED1test0fail0.01s (session16158).
Added punish/helper/submission.rs and parent hook gated by
MONZERO_BCH_PUNISH_SUBMISSION=1. Both real helper and optional Qt probe run on copied
stores with offline review, exact externally supplied approval, missing/wrong
approval rejection, native-unconfirmed/maturity/spent-output rejection, accepted
relay, lost reply and wrong-txid acknowledgement scenarios. Fresh processes must
refuse repeats and report durable history. Server checks exact bytes and intent
before broadcast; fixture also checks persisted approval, txid, unchanged secrets
and read-only native RPC methods. Async spawn_blocking prevents fixture RPC starvation.
RAII restores mutated fixture state. Original stores remain unchanged.

All features/all targets PASS17.85s; ordered patch audit PASS. New patch
upstream-bch-punish-submission-integration.patch follows native-fixture patch.
Baseline build/bch-punish-submission-integration-baseline-20261015.
Claude review disposition build/claude-bch-punish-submit-integration-review-disposition-20261015.txt.
No runtime acceptance claim yet. New launcher
build/run-bch-punish-submission-integration-fixture-20261015.py builds actual helper,
freezes helper/test/probe copies0500 with hashes, then runs fixture. Check status
build/bch-punish-submission-integration-fixture-status-20261015.json and matching logs.
It enables both observation and submission. Do not edit engine while build active.
Existing cancellation1406554,offline1436753,confirmation1462448,paired1471714 remain
live at this checkpoint; no restart. Roadmap source distinguishes implemented
integration checks from still-pending acceptance.


## 15 October 2026 — broader controller verification after submission wiring

Full dex_tests run session9046 completed0:67passed0failed21skipped53025ms.
Evidence build/bch-punish-submit-broad-controller-runtime-20261015.log.
Skipped cases require separately configured owned-chain fixtures; this run does not
replace those integration gates. Website roadmap source updated with exact counts.
No deployment performed. The source/test changes from prior turns remain uncommitted.

Submission integration supervisor1486583/session63082 remains active, now building
fixture after successful helper release build1m39s. Existing recovery and paired
supervisors1406554/1436753/1462448/1471714 remain live; no restarts.

Next app gap confirmed from current engine source: dedicated XMZ refund wallet
restore/open/check APIs are implemented but have no public review/status API or
private helper/GUI routing. Their payout has public exact relay review already.
Before exposing wallet restoration, bind user approval to recovered account,
trade/admission, native payment, BCH refund and local wallet identifier; preserve
one-attempt intent and explicit open/check on lost response. No keys in public DTOs.
Claude design review running22745, output
build/claude-bch-refund-wallet-helper-design-20261015.log. Inspect and disposition
before implementing. Do not edit engine while submission fixture compilation runs.


## 15 October 2026 — refund-wallet approval design checked

Claude review22745 completed. Source-backed disposition:
build/claude-bch-refund-wallet-helper-design-disposition-20261015.txt.
Native RPC already enforces loopback/no proxy/no redirect. Reuse existing durable
intent and exclusive journal; do not create competing helper idempotency state.
Saved status must validate account/refund binding, not expose raw intent as ready.
Explicit restore/open/check action must also match session state; attempted state
alone does not mean wallet is closed. Approval must bind exact public account, trade,
refund and local endpoint without keys/passwords. Implementation remains pending.


## 15 October 2026 — native cancellation helper/Qt acceptance PASSED

Supervisor1406554/session37499 is terminal PASS,exit0. Frozen test ran1case,
0fail0ignored3846.91s. Evidence:
build/bch-native-cancel-reviewed-admission-fixture-status-20261015.json
build/bch-native-cancel-reviewed-admission-fixture-runtime-20261015.log.
All three helper/test/probe hashes were rechecked against the passed status record.
The production Qt probe drives the actual helper (not a full GUI click-through).

Scope: native-funder cancellation review/status without chain observations; wrong
approval and immature-lock rejection without intent or broadcast; three relay modes
(success, lost reply, wrong txid reply); durable attempted/acknowledged history;
fresh-process repeat rejection without second relay; separate confirmed observation,
stale-chain rejection and unchanged private/original records. Owned loopback RPC
fixtures supply chain observations, so this is not real-node/public-chain acceptance
or complete two-chain settlement. Earlier failures remain preserved as history.

Roadmap source now records this pass and removes the stale current-failure/pending
claim for cancellation. Other punishment/paired supervisors remain separate pending
checks. Do not restart completed cancellation or treat this old frozen engine build
as acceptance of newer punishment/helper changes.


## 15 October 2026 — bound refund-wallet status and maintained paired acceptance

Added public bch_native_refund_wallet_status(refund,now): offline historical status
from retained agreement/native payment, exact expected wallet binding and strictly
validated intent/ready records. States not-attempted/attempted/ready, no RPC,
chainObservedfalse/executablefalse. Caller txid is explicitly refundHint, not fresh
chain evidence. Wrong saved binding, backwards time and malformed/orphan readiness
fail. Refactored shared record validation to return digest+ready from one validated
read; both status and required_refund_wallet_intent consume it.

Expanded existing native refund wallet fixture to check before/after restore,
lost-reply attempted state, reopened ready state, mismatched refund, backward time,
corrupt readiness and missing intent; status makes no native/wallet RPC calls.
Initial compile missing Value qualification fixed. Reviewed allfeature/alltarget
check PASS17.98s; patch audit PASS. New upstream-bch-refund-wallet-status.patch.
Baseline build/bch-refund-wallet-status-baseline-20261015.
Claude review36410 completed: single-read readiness fixed; refund hint labelled.
Recover and status derive address/payment from the same retained agreement/proof
(native_refund.rs), not separate mutable sources. Keys are not returned. Existing
loopback-only session transport and zeroizing buffers unchanged.
Runtime launcher build/run-bch-refund-wallet-status-fixture-20261015.py now builds
and freezes unique test binary then runs wallet-lock regression. Check corresponding
status/log files; no runtime pass claimed. Do not edit engine during compilation.

Canonical Qt paired supervisor1471714/session13595 TERMINAL PASSED:1test0fail
1149.15s. Result build/bch-canonical-qt-paired-result-20261015.json, all evidence
hashes rechecked. Covers frozen merged --qt mode. Later direct-mode/boolean/-O
hardening has focused tests but is not in this frozen run. Real BCHNregtest and
XMZfakechain payment stage; fullCrossChainSettlementfalse. Roadmap source updated
with precise scope and pending refund-status work; no public deploy.


## 15 October 2026 — refund wallet approval draft while runtime builds

Status fixture supervisor1498784/session68586 remains building, rustc1498804
confirmed live. Submission1486583,offline1436753,confirmation1462448 remain live.
No engine edits during compiler activity and no quiet-job restarts.
Prepared source draft build/bch-refund-wallet-review-draft-20261015.rs and integration
notes build/bch-refund-wallet-review-integration-notes-20261015.txt outside engine.
Not compiled or installed. Review binds action/state, opaque recovered NativeRefund,
public account/trade/payment/refund, local session endpoint, restore height0 and
current intent. Excludes passwords/keys/volatile observation block/time. Adds30s
freshness requirement. Must implement approval-gated execution before helper exposure,
using same freshly verified refund and repeating freshness check after preflight.
Caller must exclusively own dedicated wallet RPC session; per-trade journal alone
cannot serialize a wallet endpoint shared across trades. Inspect existing ownership
mechanism before introducing helper wallet mutations. No server custody.
Claude draft review launched; output build/claude-bch-refund-wallet-review-draft-20261015.log.
No roadmap completion added for an uncompiled draft.


## 15 October 2026 — approval-gated local refund-wallet lifecycle implemented

Status fixture1498784/session68586 finished compilation and runs frozen binary
SHA05174f322eae1d38cbd947ecddb8d29c46046cfb7ee3ef32117e52f7337fcc87.
Added wallet/review.rs: opaque recovered NativeRefund -> public exact review of
explicit restore/open/check, record state, quote/admission, shared wallet/address,
native payment/refund, historical intent, restore height, confirmation policy,
BCH deployment/network and concrete loopback session endpoint. No keys/passwords
serialized. Fresh observation age bounded30s; not a universal suspend/reorg proof.
Shared RESTORE_HEIGHT constant used by review and execution.

New approved light restore/open/check APIs recompute exact review after fresh
recovery and reject mismatched digest before any wallet RPC. Restore checks again
after preflight, before create-only durable intent and generate_from_keys. Existing
internal-compatible APIs retained; future helper must route only approved APIs.
Uses same recovered object between check and execution. No auto retry/fallback or
payout permission. Dedicated endpoint ownership remains required for helper wiring.

Expanded owned lifecycle fixture for review stability, wrong action/endpoint,
wrong approval with zero wallet calls/intent, restored/uncertain state, and separately
approved check/open. Compiler allfeatures/alltargets PASS; ordered audit PASS.
Evidence build/bch-refund-wallet-approval-{check,audit}-20261015.log.
New patch upstream-bch-refund-wallet-approval.patch; baseline
build/bch-refund-wallet-approval-baseline-20261015. Claude draft review disposition
build/claude-bch-refund-wallet-review-draft-disposition-20261015.txt.
New runtime launcher build/run-bch-refund-wallet-approval-fixture-20261015.py builds
and freezes a separate binary. Check corresponding status and logs. Runtime pending;
no helper/desktop lifecycle completion or public readiness claimed. Don't edit
engine during compile. Existing offline/confirmation/submission/status runs remain
independent. Roadmap source records compilation-only approval progress.


## 15 October 2026 — desktop refund-wallet status decoder

Added BchRefundWalletProtocol.h and dex_bch_refund_wallet_protocol_tests target.
Validates exact17-field engine historical DTO, configured native-funder quote and
network/deployment, expected refund hint, UUID-derived wallet filename, public
address/txid format, intent/state consistency and recomputed wallet-binding digest.
Requires chainObservedfalse/executablefalse and rejects extra fields. Engine pinned
helper remains responsible for private-record/address provenance; this decoder
is not independent network verification or operation permission.
20tests passed0fail0skip7ms. CMake configure/build passed after regenerating new
target. Evidence build/bch-refund-wallet-protocol-{configure,rebuild,runtime}-20261015.log.
Worker/controller/UI wiring remains unfinished. Roadmap source updated precise scope.

Prepared read-only helper draft build/bch-refund-wallet-helper-draft-20261015.rs
outside engine while approval fixture compiles. Proposed strict four-field private
wrapper binds admission config/refund txid/deployment; status-only request rejects
approval and mutating fields, loads historical admitted quote/approval, calls engine
status and checks retained deployment. Six-field public response has no secrets or
chain grant. Includes request/config tests. Not compiled/installed yet.
Approval supervisor1507031/session4238 still builds. Do not edit engine until it
freezes runtime binary. Existing punishment/status runtimes remain separate.


## 15 October 2026 — offline punishment acceptance and refund status helper

Offline punishment supervisor1436753/session83438 TERMINAL PASSexit0:
1passed0failed0ignored3500.60s. Status/runtime
build/bch-punish-isolated-helper-fixture-{status,runtime}-20261015.
All3 frozen helper/test/probe hashes reverified. Scope is actual helper+Qt offline
review/status on isolated copied stores, exact expected review/history, rejection
of inappropriate approval/submission, unchanged originals/private copies. Frozen
helper predates observation/submit; this does NOT accept newer confirmation/send
paths. Earlier exclusive-journal failure remains documented. Roadmap source updated.

Added native-bch-admission/refund_wallet.rs and private
--native-refund-wallet-status-stdio-v1. Dedicated2field status request denies all
unknown keys (including commitment:null), strict4field config binds admission,
canonical nonzero refund txid and deployment. Restores original admitted quote and
saved approval, resumes exclusive journal, checks engine historical status and
retained deployment. Public six-field response carries only allowlisted17field
wallet status, never chain observation or spending authority. No RPC credentials
accepted and no wallet operation route. New request/config/public-output tests.

Initial include! inner-doc comment compile error fixed. Allfeatures/alltargets
PASS1.52s; patch audit PASS. Patch upstream-bch-refund-wallet-status-helper.patch;
baseline build/bch-refund-wallet-status-helper-baseline-20261015.
Claude review23529 disposition build/claude-bch-refund-wallet-status-helper-review-disposition-20261015.txt.
Helper suite runner build/run-bch-refund-wallet-status-helper-tests-20261015.py
session61900 now builds/freezes from cargo JSON executable record, then runs suite.
Check corresponding status/logs; no runtime pass yet. Actual helper/status/desktop
integration remains next. Existing approval/status/punishment runtimes independent.


## 15 October 2026 — read-only refund-wallet desktop worker

Helper test runner61900 TERMINAL PASS39tests0fail0ignored0.02s. Frozen test SHA
73fbbdb919fd280fc68e350185c85bef2f926fbe923bbf41fca7d0c49a2dfaf8.
Evidence build/bch-refund-wallet-status-helper-tests-{status,runtime}-20261015.
These are helper request/config/public-shape checks, not actual status integration.

Added BchRefundWalletJob.h/.cpp: configure once with exact private wrapper,
admission/quote bytes and trusted helper digest; status-only request, bounded output,
request/trade correlation, strict DTO validation. No restore/open/check-wallet/send
route (refresh reads saved status only). Current wallet/result clears on start and
failure; savedWallet is labelled retained history. Identity/binding/intent changes
or backwards ready/attempted transitions reject. Source mismatch invalidates worker
until reopen; read-only failure can be explicitly retried without mutation authority.
Queued notifications and actual process state guard handle kill/oversize completion.

13worker tests passed0fail0skip723ms;20protocol tests passed0fail7ms. New shared
BchRefundWalletTestFixture.h and test_bch_refund_wallet.cpp. Evidence
build/bch-refund-wallet-worker-{build,runtime,protocol-runtime}-20261015.log.
Controller/CLI/QML wiring and real-helper adapter acceptance remain undone.
Fullapp build running16096:build/bch-refund-wallet-worker-app-build-20261015.log.
Claude worker review running97604:build/claude-bch-refund-wallet-worker-review-20261015.log.
Roadmap source updated39helper/13worker counts with synthetic/unfinished scope.
Existing confirmation1462448,submission1486583,status1498784,approval1507031 runtimes
confirmed live. Do not restart quiet jobs or claim they passed from older fixtures.

Full app build16096 now PASS; new worker compiles into application. Controller/UI wiring is still pending.


## 15 October 2026 — refund-wallet status controller/private CLI

Connected BchRefundWalletJob to DexEngine property/signal/configure/refresh and
bchOperationIdle. Added direct worker cross-notifications with other BCH jobs,
publication-repair dependency invalidation and read-only queued startup check.
Hidden --dex-bch-refund-wallet requires prototype and valid private config. No
wallet restore/open/payout actions added. DexBchLightFunding now emits refund-wallet
notification after scoped binding flag resets to restore availability correctly.

Added bchRefundWalletStatusStartupAndGuard controller case using shared
BchRefundWalletWorkerTestFixture.h. Final app/controller build PASS, focused3cases
plus lifecycle5pass0fail1337ms; two light-binding cases plus lifecycle4pass316ms.
Worker fixture refactor13pass0fail retained. Evidence:
build/bch-refund-wallet-controller-final-{build,runtime}-20261015.log,
build/bch-refund-wallet-controller-binding-runtime-20261015.log,
build/bch-refund-wallet-controller-worker-runtime-20261015.log.
Claude worker review97604 disposition completed:
build/claude-bch-refund-wallet-worker-review-disposition-20261015.txt.

QML panel and actual helper/status/desktop interoperability remain next. Engine
retained records are authority; desktop doesn't independently verify admission or
chain proof. Historical savedWallet must stay labelled separate from current result.
Roadmap source updated controller milestone; no live deployment or public enablement.
Confirmation1462448/submission1486583/status1498784/approval1507031 remain separate
live runs, not passing claims or reasons to restart.


## 15 October 2026 — saved refund-wallet status screen

The private prototype now displays saved BCH/XMZ refund-wallet status with an
explicit read-only refresh. Busy and uncertain results hide current details and
label retained records as historical. A ready record does not claim a current
balance or chain confirmation. Wallet restoration, reopening and payout controls
remain unfinished; this screen cannot perform those operations.

Validation: 97 QML recovery tests passed, 0 failed, 0 skipped (8789ms), including
saved history, disabled actions, uncertain results and market switching. Evidence:
`build/bch-refund-wallet-panel-qml-20261015.txt`. Inspected the isolated panel
capture `build/bch-refund-wallet-status-panel-20261015.png`; this is not full-window
visual acceptance. Actual-helper status interoperability remains pending.

Website roadmap source updated with this milestone and corrected the obsolete
cancellation-failure next step. Live website deployment has not been verified.

Application rebuild also passed after the QML changes. Evidence:
`build/bch-refund-wallet-panel-app-build-20261015.log`.


## 15 October 2026 — refund-wallet desktop process adapter

Added a test-only adapter around the production BchRefundWalletJob status worker.
It validates the outer request before starting a helper, waits for worker validation,
and forwards only the accepted response with the fixture request correlation.
Six adapter cases cover saved/attempted/ready history, malformed requests with no
helper launch, spending-authority rejection and mismatched response correlation.
The combined worker/adapter suite passes 19 tests, 0 failed, 0 skipped (878ms).
Build passed; evidence: build/bch-refund-wallet-adapter-{build,runtime}-20261015.log.
These tests use a synthetic helper. Actual engine/helper/desktop interoperability
still requires connecting this adapter to the retained-wallet integration fixture.
Claude source review started; not treated as completed verification.

Next actual-helper integration: extend native_wallet/tests/wallet_locks/signing/relay/message/native/walletless/refund/wallet.rs at initial/recorded status checkpoints. Follow punishment/helper.rs configuration and copied-store pattern so the parent execution keeps its exclusive lock. Pin both actual helper and new dex_bch_refund_wallet_probe. Compare response wallet exactly to engine status, cover not-attempted/attempted/ready, check unchanged copied/original records and zero wallet RPCs. A dedicated private wrapper binds refundTxid and deploymentCommitment. Current runtime binaries are frozen copies and must not be restarted. Claude adapter review is session53636, log build/claude-bch-refund-wallet-adapter-review-20261015.log; still pending at this checkpoint.


## 15 October 2026 — real refund-wallet status integration fixture

Added helper/desktop integration at initial and post-restore wallet status checkpoints
for all three restore outcomes. Independent private copies avoid reopening the
parent's locked journal. Requires pinned helper and Qt adapter, compares exact public
response to engine status, and checks copied/original metadata and secrets unchanged.
Existing wallet/native RPC counters surround these checks. No mutation capability added.

All-feature/all-target cargo check passed; ordered patch reproduction passed.
New patch: upstream-bch-refund-wallet-helper-integration.patch.
Runtime build/launch supervisor PID1537402, session29861:
build/run-bch-refund-wallet-helper-integration-fixture-20261015.py;
build/bch-refund-wallet-helper-integration-fixture-status-20261015.json.
Acceptance remains pending; frozen binaries will be hashed before and after runtime.
Do not change engine sources while this supervisor is compiling.

Claude adapter review completed and disposition recorded in
build/claude-bch-refund-wallet-adapter-review-disposition-20261015.txt.
Added adapter self-reference rejection and focused test. Combined worker/adapter now
20pass0fail0skip892ms; build passed. Evidence:
build/bch-refund-wallet-adapter-final-{build,runtime}-20261015.log.
Source-only Claude integration review started, log
build/claude-bch-refund-wallet-integration-review-20261015.log.
Four earlier long-running fixtures still confirmed live; no acceptance promotion.
Website source updated; no deployment or public activation.


## 15 October 2026 — punishment confirmation integration passed

The corrected confirmation-depth fixture completed: 1pass0fail0ignored3648.33s.
Real helper and production Qt adapter check retained review/status, explicit
confirmation at height171/depth30, stale observation rejection, no added relay,
and unchanged private records. Isolated RPC fixtures only, not paired public nodes.
All3 frozen executable hashes rechecked against status evidence. This binary predates
submission integration; no submit acceptance is inferred.
Evidence build/bch-punish-confirmation-depth-fixture-{status,runtime}-20261015.
Earlier depth expectation failure remains historical; this corrected run passed.

## 15 October 2026 — desktop refund-wallet approval validation

Added public review validation for restore/open/check with exact20field details,
quote/account/refund binding, state/action compatibility, restore height, confirmation
policy, selected loopback daemon/wallet endpoint and recomputed review commitment.
Engine remains responsible for fresh recovery proof and mutation authority. No helper
mutation route or UI action enabled. 29protocol tests pass0fail0skip11ms, including
rehashed changed policy/account/refund/service, illegal state/action and wrong digest.
Initial run failed because inherited synthetic fixture had quote/policy XMZ depth
mismatch; test setup now matches policy to quote, production check preserved.
Evidence build/bch-refund-wallet-review-protocol-final-{build,runtime}-20261015.log.
Application rebuild session84915 ongoing, log build/bch-refund-wallet-review-app-build-20261015.log.

Status integration supervisor1537402/session29861 still compiling; no engine edits
until compiler terminal. Claude integration review complete: most issues are scope
limits (conditional integration gate with runner pins, terms already String map,
production worker deliberately invokes helper, frozen build identity not hostile-process
attestation). Follow-up: config directory immutability is not currently checked by the
fixture; consider adding after frozen run build completes. No general network-isolation
claim; wallet/native fixture call counts and no RPC config constrain tested behavior.
Review log build/claude-bch-refund-wallet-integration-review-20261015.log.

Application rebuild84915 passed after adding the review validator. No wallet action is enabled by this component-only change.


## 15 October 2026 — fresh refund-wallet helper review

Added private --native-refund-wallet-review-stdio-v1 route with explicit restore/open/check
action, no approval/password/key input, and strict private status/BCH/native descriptors.
Restores retained quote/admission and checks refund/deployment binding before RPC,
uses owned BCH observer plus isolated fakechain native connection, recovers opaque
refund proof, and returns only public engine approval details/digest with observation
time. Descriptor-only NativeWalletSession makes no wallet lifecycle call. No wallet
restore/open/check mutation or payout route added. Public output has exact top-level
and backend allowlists, preventing accidental extra secret fields.

All-feature/all-target cargo check PASS after fixing a digest-formatting scope error;
ordered patch audit PASS. New upstream-bch-refund-wallet-review-helper.patch reproduced.
Evidence build/bch-refund-wallet-review-helper-final-check-20261015.log and
build/bch-refund-wallet-review-helper-patch-audit-20261015.log.
Helper test supervisor launched via build/run-bch-refund-wallet-review-helper-tests-20261015.py;
status/build/runtime prefix bch-refund-wallet-review-helper-tests. Runtime acceptance
pending; actual fresh-review/helper/desktop integration not implemented yet.
Claude source-only review session70693 ongoing, log
build/claude-bch-refund-wallet-review-helper-20261015.log.

Status helper integration1537402/session29861 now running frozen engine/helper/probe,
not compiling; hashes are in build/bch-refund-wallet-helper-integration-fixture-status-20261015.json.
That run predates new review route and cannot validate it. Submission1486583,
status1498784, approval1507031 still verified live. No restarts, mining or bundle changes.


## 15 October 2026 — Litecoin wallet visibility prioritized

User requested Litecoin GUI before further BCH recovery development. DEX now defaults
to LTC/XMZ and has a persistent Litecoin wallet overview with an explicit return-to-LTC
button from other markets. Unconfigured wallets show create/restore/open guidance and
existing local setup controls; unavailable helper sessions say so. Loaded wallet shows
network and only validated provisional observations; unknown/busy data is not a balance.
Existing receive, setup, backup and key/recovery controls remain below. No wallet was
created, unlocked or funded, and no DEX trading gate changed.

98QML tests pass0fail0skip8700ms. Screenshot inspected:
build/litecoin-wallet-overview-20261015.png (synthetic test balance, not user funds).
Evidence build/ltc-wallet-overview-qml-20261015.txt. App rebuild session72628 in progress,
log build/ltc-wallet-overview-app-build-20261015.log. Running GUI must reopen for resource changes.

BCH review helper pre-hardening test suite finished passing (status
build/bch-refund-wallet-review-helper-tests-status-20261015.json); verify exact count before
promoting roadmap. Claude public-output guard tightening was drafted only in
build/refund-review-guard-20261015.txt; NOT applied. Prioritize Litecoin per latest user.

Litecoin overview application rebuild72628 PASS. Rebuilt GUI contains the updated QML resources.


## 15 October 2026 — Litecoin wallet quick actions

Added Receive, Refresh balance and Sync headers directly to the top Litecoin card,
using existing production backend methods and availability flags. Explicit handler
guards reject forced clicks while disabled/busy. Current receiving address appears
on the card only for a validated wallet-address result and clears on uncertain/busy
state. Changed loaded wording to configured (configuration alone is not an opened vault).
99QML tests pass0fail0skip8777ms; screenshot inspected after adding actions.
Evidence build/ltc-wallet-actions-qml-20261015.txt. Initial Flow read-only implicitHeight
assignment caused QML load failure and was removed; final suite passes.
App build session97195 log build/ltc-wallet-actions-app-build-20261015.log.
Existing GUI PID1553058 still open with prior resources; do not kill user's wallet.


## 15 October 2026 — visible Litecoin wallet local-custody acceptance

Current production Qt light-wallet probe rebuilt and copied mode0500; current bundled
helper copied mode0500 without replacing installed bundle. Ran existing mnemonic
custody fixture for LTC with guided setup enabled under bwrap --unshare-net, using
only disposable private folders. PASS: guided preparation, creation/open/receive,
initial backup verification, private mnemonic reveal/hide, bad password/approval
rejection, invalid import rejection, mnemonic restoration, exact retry preservation,
and saved-folder reopening. Report asserts no plaintext phrase persisted.
Both executable hashes rechecked. This is production Qt worker/helper acceptance,
not full-window user interaction, public chain validation, or DEX settlement.
Evidence build/ltc-visible-wallet-custody-result-20261015.json and
build/ltc-visible-wallet-custody-runtime-20261015.log; runner session62354 completed.
Original user wallet untouched; no keys/funds generated outside disposable fixture.
Claude Litecoin card source review session23730 pending:
build/claude-ltc-wallet-overview-review-20261015.log.


## 15 October 2026 — Litecoin overview observation boundaries

Applied verified Claude review findings: overview distinguishes no wallet, busy and
no current balance; validates atomic amount format before displaying observations;
requires nonempty address and network before showing receiving instructions.
Tests cover missing/invalid/numeric amounts, legitimate zero, busy and missing network.
100QML tests pass0fail0skip9024ms. Evidence build/ltc-wallet-state-qml-20261015.txt.
Review disposition build/claude-ltc-wallet-overview-review-disposition-20261015.txt.
Application rebuild session27471, log build/ltc-wallet-state-app-build-20261015.log.
No user-wallet operation, mining, live deployment or bundle replacement.


## 15 October 2026 — prominent Litecoin creation entry

Added Create wallet to top Litecoin overview when unconfigured. It focuses the
existing network selector and scrolls the containing Flickable to the creation form.
Disabled without verified setup helper; retains review/password/consent steps and
never creates keys merely by navigating. Existing wallet controls unaffected.
100QML tests pass0fail0skip9007ms, including actual Flickable scroll/focus. BuildPASS.
Evidence build/ltc-create-entry-final-qml-20261015.txt and build/ltc-create-entry-app-build-20261015.log.
Screenshot inspected build/ltc-create-wallet-entry-20261015.png. Open GUI still needs
restart to load new resources; it was not killed. Roadmap source updated.


## 15 October 2026 — Litecoin creation continuation

Top Create wallet action now changes to Continue setup while setupConfigured is true,
so the prepared-settings/password stage remains reachable. Navigates to existing form
and focuses its password input; busy setup disables the button. Configured operational
wallets hide the creation entry. No implicit creation, unlock or network operations.
101QML tests pass0fail0skip8851ms; app rebuildPASS. Initial focus assertion checked the
LineEdit wrapper rather than its delegated input; corrected test targets input.activeFocus.
Evidence build/ltc-create-continuation-final-qml-20261015.txt and
build/ltc-create-continuation-app-build-20261015.log. Running GUI not restarted.
Funding review confirms production selection still requires local regtest wallet and
negotiation context; visible wallet is not evidence of complete swap acceptance.


## 15 October 2026 — refund-wallet review output validation

Earlier review-helper suite verified41pass0fail0ignored0.02s and frozen hash rechecked.
That binary predates this stricter output guard. public_review now checks requested
action/trade/refund/deployment, identity/state compatibility, fixed digest field types,
transaction/address shapes, depth bounds, explicit regtest/loopback service shape,
and actual selected daemon/wallet ports. Nested private-material injection and changed
identity/state tests added. No mutation route or key export introduced.
Allfeatures/alltargets checkPASS; patch auditPASS. New patch:
upstream-bch-refund-wallet-review-output.patch.
Evidence build/bch-refund-wallet-review-output-{check,patch-audit}-20261015.log.
New runtime supervisor session70915 started:
build/run-bch-refund-wallet-review-output-tests-20261015.py;
status/build/runtime prefix bch-refund-wallet-review-output-tests. Compiling: do not
edit engine until terminal build. Runtime not yet accepted.
Claude disposition build/claude-bch-refund-wallet-review-helper-disposition-20261015.txt.
Four long integration runners remain verified live; do not restart for quiet runtime.


## 15 October 2026 — fresh refund-wallet desktop review worker

Strict-output helper suite PASS43tests0fail0ignored0.02s; frozen hash rechecked.
Added desktop reviewResponse correlation/schema/authority/freshness guard, rejecting
pre-request/future/>30s observations and mismatched action/trade/endpoint. Protocol suite
30pass0fail0skip13ms including response boundary negatives.
Extended BchRefundWalletJob with configureReview and requestReview(restore/open/check),
strict private wrapper/native descriptor, exact descriptor pinning before/after process,
selected local endpoint binding and separate current review/status responses. Fresh
review clears on new request/error/source mismatch and expires through a 30s timer.
Existing savedWallet history remains separate. No mutation or payout method added;
controller/CLI/QML wiring for review not done yet.
Initial combined worker/adapter suite25pass0fail1145ms. Added actual timer expiry case;
full rerun session51153, build/runtime prefix bch-refund-wallet-review-expiry.
App rebuild33053 PASS. Evidence build/bch-refund-wallet-review-worker-{build,runtime,app-build}-20261015.log,
build/bch-refund-wallet-review-response-final-runtime-20261015.log.
Claude worker review63650 pending:build/claude-bch-refund-wallet-review-worker-20261015.log.
Actual-helper fresh review interoperability still pending; synthetic process tests only.

Review worker expiry rerun PASS26tests0fail0skip, including actual timed clearing of review/result/chainObserved. Evidence build/bch-refund-wallet-review-expiry-runtime-20261015.log.


## 15 October 2026 — punishment submission and saved-wallet engine acceptance

Frozen punishment submission integration PASS1test0fail0ignored3965.72s through real
helper and Qt adapter on isolated RPC fixtures. Exact approvals, immature/unconfirmed/
spent negatives, acknowledged/lost-reply/wrong-txid dispatch, durable intent before
relay, restart repeat refusal and unchanged original records. All3 hashes rechecked.
Evidence build/bch-punish-submission-integration-fixture-{status,runtime}-20261015.
This is isolated fixture acceptance, not full paired-chain/GUI settlement.

Saved refund-wallet status engine regression PASS1test0fail0ignored3524.92s; frozen
hash rechecked. Evidence build/bch-refund-wallet-status-fixture-{status,runtime}-20261015.
Predates approval/helper/review changes; no acceptance inferred for those additions.

## 15 October 2026 — refund-wallet review controller and history consistency

Connected explicit review configure/request methods and private CLI
--dex-bch-refund-wallet-review requiring prototype and saved-status config. Shared
BCH busy guard masks review availability and dispatch. Startup still reads saved
status only, never launches fresh reviews automatically.
Claude review found review/history mismatch; worker now requires saved baseline,
checks compatible action before launch, and compares reconstructed review history
using same monotonic identity/state/intent checks as status. Added strict BCH RPC
descriptor shape/port/checkpoint/subversion checks before review configuration.
No lifecycle or payout mutation action enabled. Saved history remains labelled,
current records clear during operations; configure changes require job reopening.
Helper validates credential files and full deployment; desktop does not read secrets.

App/controller buildPASS. Focused controller tests completed; evidence
build/bch-refund-wallet-review-controller-runtime-20261015.log.
Worker history regression suite running63112, log
build/bch-refund-wallet-review-history-runtime-20261015.log (includes real 30s expiry).
GUI review controls remain unfinished; status-only production UI unchanged.

Final review/history worker suite28pass0fail0skip31718ms; controller focused suite5pass0fail579ms. Claude disposition: build/claude-bch-refund-wallet-review-worker-disposition-20261015.txt.


## 15 October 2026 — private refund-wallet review screen

Added explicit Review restoration/reopening/verification buttons, visible only for
configured private review backend. Enabled actions follow accepted saved state and
worker availability, with handler guards. No automatic chain review or mutation.
Fresh details display account, local filename, selected service and confirmation
requirements with explicit no-balance/no-payout-authority wording. Expired reviews
hide even stale injected details; saved records remain labelled historical in reviewed
and expired states. Busy outcome distinguishes fresh chain checks from saved reads.
102QML tests pass0fail0skip8725ms including no automatic request, disabled wrong-state
actions, review/expiry/history and coin switching. Screenshot inspected:
build/bch-refund-wallet-fresh-review-panel-20261015.png (isolated component only).
Evidence build/bch-refund-wallet-fresh-review-qml-20261015.txt.
App build66408 pending, log build/bch-refund-wallet-fresh-review-app-build-20261015.log.
Actual helper fresh-review integration and wallet-changing operations remain unfinished.


## 15 October 2026 — fresh-review process adapter

Extended dex_bch_refund_wallet_probe to accept strict review requests in addition to
saved status. Review mode resolves private status wrapper, rejects self-reference,
configures production worker, performs saved-status baseline read, then explicit fresh
review. Inner request validation remains in worker; outer request ID adaptation is
labelled. Requires reviewed/chainObserved output and bounded result, no wallet mutation.
Synthetic process tests now exercise adapter for normal/expiry, wrong request/stale/
authority/extra-secret, history-ready restore refusal and history downgrade refusal.
Combined worker suite28pass0fail0skip31709ms, buildPASS. Evidence
build/bch-refund-wallet-review-adapter-{build,runtime}-20261015.log.
Actual-helper fresh review acceptance not yet implemented/launched. Existing frozen
status-only run1537402 continues unchanged.

Next integration: pass Server into refund/wallet.rs from refund.rs; use wallet
payout_session(observer) to obtain expected loopback endpoint via recovery_endpoint,
then prepare private native descriptor with fixture:fixture login. This combines the
live observer daemon with dedicated wallet fixture port. Fresh review subprocess must
use spawn_blocking so Tokio RPC listeners progress. Reuse copied journal/snapshot
configuration pattern and compare exact engine review details/commitment plus no wallet
calls and unchanged records. Keep static status checks scoped separately from fresh
chain calls. Source files remain unchanged for this next step.


## 15 October 2026 — approved refund-wallet engine pass; fresh helper review integration

Frozen approval engine regression passed: 1 passed, 0 failed, 0 ignored, 3575.94s.
Binary SHA256 b7d7ec7b3e8b0d1b2fb55feb6484569bc61f74673c76e0800b2d15b5b86e5452
was rechecked against build/bch-refund-wallet-approval-fixture-status-20261015.json.
Runtime: build/bch-refund-wallet-approval-fixture-runtime-20261015.log.
This verifies approved restore/open/check, changed approvals, interrupted restoration
and restart reconciliation at the engine fixture level, not the graphical recovery flow.

New actual-helper review integration uses isolated live RPC fixtures and private copied
journals. It compares restore/open/check reviews through the helper and production Qt
worker against engine details/commitments, requires fresh observer calls, rejects invalid
BCH confirmation and unconfirmed XMZ payment, and checks unchanged configuration,
records, snapshots and dedicated-wallet call history. No wallet mutation is requested.
Expected engine output is an interoperability oracle, not independent protocol validation.
All-features/all-targets compile passed; ordered patch audit passed.
Patch: upstream-bch-refund-wallet-review-integration.patch.
Claude source review and disposition are under build/claude-bch-refund-wallet-review-integration*.
The new supervisor build/run is pending: build/run-bch-refund-wallet-review-integration-fixture-20261015.py;
status: build/bch-refund-wallet-review-integration-fixture-status-20261015.json.
Previous frozen status-only integration is kept separate and continues unchanged.
No real-helper fresh review runtime pass or complete trading claim yet.


## 15 October 2026 — app-owned BCH native connection descriptor

SwapWalletRpcProcess now provisions a private BCH-native descriptor using only its own
running, locked service ports and service-created login path. No credentials are copied
into the descriptor. Provisioning is refused during operations, uncertainty or shutdown;
ownership checks retain exact descriptor bytes and reject changed files, wrong modes,
symlinks, multiple links or a stopped/unowned service. Existing operation tokens remain
required for future mutation dispatch. Cleanup removes descriptor after process exit;
wallet data remains retained. No BCH mutation helper/worker/controller enabled yet.

Focused process tests passed: 5 passed, 0 failed, 9572ms (three test functions plus setup/
teardown), covering descriptor bindings, uncertain completion, tampering, shutdown,
existing wallet dispatch and unfinished-operation restart prevention.
Evidence build/bch-owned-native-session-final-tests-20261015.log; target build passed.
Initial test wrongly expected SIGTERM of the synthetic child to count as clean exit;
fixture expectations corrected, production shutdown semantics unchanged.
Claude reviewed the source without tools; no concrete correctness bug reported.
Source review build/claude-bch-owned-native-session-20261015.txt.
App rebuild and additional cleanup/restart tests running at this checkpoint.
Actual fresh-review integration supervisor1596070 advanced from engine to helper build;
frozen status-only integration1537402 remains running. Do not restart quiet fixtures.
Next: connect this owner/operation-token contract to explicit approved BCH refund-wallet
helper and worker operations, then validate real-helper mutation and interrupted replies.

Follow-up verification: full app rebuild passed (build/bch-owned-native-session-app-build-20261015.log).
Additional cleanup/restart suite passed 5/0 in443ms (three functions plus setup/teardown),
including retained wallet data and refusal to resume uncertain services.
Evidence build/bch-owned-native-session-cleanup-tests-20261015.log.
Fresh-review integration1596070 is now running frozen binaries, not compiling:
engine1495ebb1aaef3ecc62a036e66f6980c05f0ccb75e3aac2e4fd94fe9bb339bf89,
helper861eadc6ff5a9612e5ff1460a70fdf840a34327ffabc4376bb99a95142f51495,
probe6591f3d8563d2065d3f6426e35a3b607b321a62971ca302280d8528fc0c383b4.
Runtime acceptance remains pending. Existing GUI process not restarted.


## 15 October 2026 — explicitly approved refund-wallet helper operations

Added --native-refund-wallet-submit-stdio-v1 with strict request schema, nonzero exact
review commitment, explicit restore/open/check action and action-bound password rules.
Restore/open require a bounded password; check rejects nonempty passwords. Input buffers
and deserialized password use Zeroizing; passwords travel only through stdin, then move
into existing engine APIs which zeroize them. No password in response, arguments or files.
Review and submit share private configuration loading and fresh recovery checks; submit
compares the current engine review before using only approved engine APIs. Those recheck
recovery and retain restore intent before mutation. Success returns public saved-ready
status, explicitly no current-chain/balance or payout authority. Errors remain ambiguous
and must be reconciled, never automatically retried.

The private caller must exclusively own the dedicated service and retain its operation
token through verified completion. A configuration-file lease alone does not prove this.
The app-owned connection descriptor is implemented separately; desktop submission worker,
controller wiring and real-helper mutation integration remain unfinished. No public GUI
mutation controls or packaged-helper replacement enabled by this checkpoint.

All-feature/all-target compile passed. Ordered patch audit passed.
Patch upstream-bch-refund-wallet-submit-helper.patch.
Full helper suite45pass0fail0ignored0.02s; frozen SHA256
 d64abc9bc17b15999dd6fd33b0830828d073236e4794c0f45187d61fb8fb8ca3.
Evidence build/bch-refund-wallet-submit-helper-tests-{status,runtime}-20261015;
compile build/bch-refund-wallet-submit-helper-check-20261015.log.
The running fresh-review integration predates submission; it cannot prove mutation.
Claude source review requested, pending at this checkpoint.

Follow-up: Claude review disposition recorded in
build/claude-bch-refund-wallet-submit-helper-disposition-20261015.txt.
Corrected review-only comments and added explicit post-operation identity/payment/
refund/network/retained-intent binding checks. Final all-feature/all-target compile and
ordered patch audit passed. Final helper suite45pass0fail; frozen SHA256
5a5b3e72c3420aefb04caa4d7a2996615bfa69f75ab61443bd8fc82428a1d72e.
Final evidence build/bch-refund-wallet-submit-helper-final-tests-{status,runtime}-20261015.
Existing long integrations confirmed live through child processes1544537 and1602220,
both using CPU; no restart. Neither run includes this submission route.
Next required work is production desktop response validation/owned-token dispatch and
actual helper restore/open/check tests including wrong approval and lost replies.


## 15 October 2026 — desktop refund-wallet submission response validation

BchRefundWalletProtocol::submissionResponse now validates the strict eight-field saved
result against request/trade/action, exact approved commitment, independently selected
local service, quote/policy and refund binding. It requires ready history, retains the
prior wallet intent for open/check, permits a new non-null intent only after restore,
and rejects authority claims, extra secret fields or a substituted account/payment.
It validates the approved review again; dispatch must separately enforce expiry and
exclusive app-owned service operation tokens. No submission worker/controller enabled.

Protocol suite32pass0fail0skip23ms; build passed.
Evidence build/bch-refund-wallet-submission-protocol-{build,tests}-20261015.log.
Two new functions cover allowed/forbidden transitions, correlation, exact schema,
unknown fields, authority bits, missing fields and independently self-consistent wallet
identity changes with recomputed binding hashes. They prove desktop acceptance rules,
not actual helper mutation or paired-chain recovery.
Claude source review requested. Long saved-status and fresh-review fixture child
processes1544537/1602220 confirmed running with CPU; their scope remains separate.
Next: connect an explicit submit method to BchRefundWalletJob with owned-service
validation, durable operation tokens, zeroized password stdin, failure uncertainty,
review expiry, changed-source guards and this result validator; then process tests.

Update: fresh-review integration1596070 is terminal FAILED (exit101), not running.
Frozen runtime build/bch-refund-wallet-review-integration-fixture-runtime-20261015.log:
0passed1failed329.28s, first successful-review helper call returned its generic error.
The separate saved-status integration1537402/child1544537 remains live.

Source investigation found a real connection-model error: NativeFundingRpc implements
FundingObserver::observe using wallet get_address/check_tx_key/get_transfer_by_txid.
The fresh-review helper currently constructs observer and recovery session from the
same nativeRpcFile. The new fixture supplies the dedicated empty recovery wallet, which
cannot provide the existing incoming-payment observation; its get_address fixture also
rejects the observer's different request shape. This is not a reason to weaken the
fixture or enable those observer methods on the empty target wallet.

Required fix: add a separate explicit observerRpcFile descriptor to review configuration,
load and pin it separately in helper/Qt worker, use it only for fresh payment observation,
and retain nativeRpcFile for the exclusively owned recovery target. Validate common
chain/daemon and distinct wallet endpoints. Update synthetic fixture and process adapter
configuration, preserve immutable failed-run evidence and start a new frozen run only
after correction/compile. Keep no-mutation assertions on the recovery target; observer
read calls are expected. Current submission route inherits this issue and is not ready
for desktop activation. No end-to-end recovery claim.

Claude submission-validator review found no concrete false acceptance within its caller
contract. Restore introduces the engine's first intent; future worker must retain it.
Check may promote attempted to ready after verified reconciliation. Raw pin equality is
intentional because helper wire pins use canonical arrays. Remaining suggested cases
are coverage improvements, not reasons to relax the validator.


## 15 October 2026 — separate payment observer from recovery wallet

Corrected the failed fresh-review integration connection model in helper and desktop:
review configuration now requires observerRpcFile alongside nativeRpcFile. The observer
uses the existing payment wallet for read calls; the target remains the dedicated recovery
wallet. Both descriptors must select the same local daemon and distinct wallet ports.
All descriptor fields are strict, private and pinned before/after desktop operations.
Changed observer configuration invalidates a pending review. No fallback to the old
four-field wrapper: private review configs must add the explicit observer descriptor.
Shared credential paths are allowed; credential reads are not configuration-file locks.
This remains a trusted owned-wallet observation model, not independent chain verification.

All-feature/all-target check and ordered patch audit passed.
Patch upstream-bch-refund-wallet-separate-observer.patch.
Helper46pass0fail0.02s; frozen SHA256
5587783effa0f1454b2871807a581c408e4aac0c01b100e67de6209d23a7a4ff.
Worker/adapter28pass0fail32283ms, including incompatible observer descriptors and changed
observer bytes. Controller dispatch3pass0fail211ms (one case plus setup/teardown).
Full app rebuild passed; running GUI not restarted.
Evidence build/bch-refund-wallet-separate-observer-{helper-tests,worker-tests,controller-tests,app-build}*.
Claude source review and evidence-based disposition recorded under the same build prefix.

New immutable integration supervisor1617292/session68196 is building engine/helper and
will run separate-observer fixtures. Status build/bch-refund-wallet-separate-observer-integration-fixture-status-20261015.json.
Earlier failed run1596070 and its hashes/log remain preserved. New runtime acceptance is
pending. Original saved-status run1537402/child1544537 remains separate; do not restart it.
Next: inspect new integration outcome and continue owned-token desktop submission worker,
then actual helper restoration/reopening/checking with interrupted replies.


## 15 October 2026 — actual helper and Qt saved refund-wallet status accepted

Frozen integration1537402 completed successfully: 1passed0failed0ignored3545.68s.
All three binary hashes rechecked against authoritative status JSON:
engine a76890b47090f281bf143d773582cea4de987c27a733e2698902573b14bb7dcd;
helper 58cbadc67206bd321f8831f18bf053ab7ab1f0fadb90a21763530142b29a7f78;
Qt probe bf1997fe1787257c3c47eb53fc479238a2b391f73be1ca59874166fab7d1c6e7.
Evidence build/bch-refund-wallet-helper-integration-fixture-{status,runtime}-20261015.
This verifies saved historical wallet status through real helper and production desktop
adapter with copied private journals; it predates fresh review, separate observer and
submission changes. It is not mutation, public-chain or full graphical recovery acceptance.


## 15 October 2026 — owned-service desktop refund-wallet submission worker

BchRefundWalletJob::submit now requires a live current review, exact commitment,
validated action/account/policy/service, unchanged sources, an app-owned native descriptor
and a durable SwapWalletRpcProcess operation token. Passwords are bounded UTF-8, action
bound and passed on stdin; worker-owned input and text buffers are cleared after use.
Review authority is cleared on dispatch; no automatic mutation or repeat submission.
Success must pass strict submissionResponse and monotonic history validation, and the
owner must accept token completion. Lost/malformed/mismatched replies, changed sources,
service stop or worker destruction retain uncertainty and block service reuse.
Saved history stays labelled; result gives no payout/current-chain/balance authority.

Final worker/adapter suite35pass0fail0skip34248ms with synthetic helper and owned fake
service. Covers wrong/missing owner, changed approval, invalid passwords, busy owner,
expired review, success, lost reply, wrong correlation/commitment, changed observer,
service stop and deleting a worker during submission. No real wallet funds touched.
Evidence build/bch-refund-wallet-submit-worker-final-tests-20261015.log; build passed.
Full app rebuild passed. Existing controller startup/review guards4pass0fail372ms
(two cases plus setup/teardown); no mutation wired into startup or public GUI.
Evidence build/bch-refund-wallet-submit-worker-{app-build,controller-tests}-20261015.log.
Claude source review is still running session51095; output
build/claude-bch-refund-wallet-submit-worker-20261015.txt. Do not duplicate this review.

Fresh-review separate-observer integration supervisor1617292/child1623221 now runs
frozen engine b0c99d32d58cc739b8b47e55a2a18d0238735039fd5a7ca988169ccd8895c919,
helper dfb7d1601ca292fab4922e8cf61501138612ed408f492764c7d760273c20e5bd,
probe 09bb71fa8fd6fd1cbbb37e51cdc72fb434b196dbabe3031aa3aa17e62695abf6.
This run requests review/status only; it cannot establish mutation acceptance.
Next: finish Claude disposition, add real-helper submission integration and controller
ownership/approval wiring, then verify full recovery and interrupted operation journey.


## 15 October 2026 — consume password buffers before submission dispatch

Addressed Claude worker review: submit now takes QByteArray&& and clears the supplied
buffer on all paths. Callers must move an owned buffer and retain no shared aliases.
UTF-8 normalization uses a wiped named buffer; password serialization writes directly
into reserved bounded storage with correct quote/backslash escaping instead of a
password-bearing QJsonObject. Size validation precedes durable operation-token creation.
Synchronous helper startup failure now returns false. QProcess has internal transport
copies; this is not a guarantee of allocator-wide erasure.

Full worker/adapter suite36pass0fail0skip34896ms, including moved-buffer clearing on
accept/reject and Unicode/quote/backslash delivery. No password written to fixture logs.
Evidence build/bch-refund-wallet-submit-secret-tests-20261015.log.
Full app/controller build passed: build/bch-refund-wallet-submit-secret-build-20261015.log.
Claude disposition build/claude-bch-refund-wallet-submit-worker-disposition-20261015.txt.
Ready-state and review-age concerns were checked against strict protocol validators;
existing guards already enforce them. No public mutation controls enabled.

Separate-observer fresh-review integration1617292/child1623221 remains live with CPU.
Next substantive work: actual-helper submission fixture with a separate copied journal
and fresh target wallet per case, preserving read-only fixture state; exact approval,
restore/reopen/check success and interrupted restore/scan outcomes. Then controller/UI
ownership and explicit confirmation wiring, and full recovery journey verification.


## 15 October 2026 — actual-helper refund-wallet mutation fixture

Implemented isolated real-helper restoration/reconciliation cases. Each copies the
admission/journal/snapshot records and creates a separate disposable target wallet,
using the existing payment-observer service separately. Cases cover acknowledged restore,
lost generate_from_keys reply and lost scan_tx reply. Wrong approval must make no target
calls or intent; correct approval must persist intent before generation (fixture checks).
After uncertainty, status is read through a new helper process, second restore and wrong
check approval are rejected without target calls, then separately reviewed check/open
must reach engine-verified ready history. Restored keys/address/filename/password/height
are enforced by the authenticated fixture; exactly one generate_from_keys, no transfer
or relay methods, unchanged original records/snapshots/config and pinned helper digest.

Opt-in MONZERO_BCH_REFUND_WALLET_MUTATION=1 with explicit executable pins; no real funds
or user wallets used. This exercises the helper directly plus saved-status Qt checks,
not Qt submission ownership, real wallet filesystem or public-chain settlement.
All-feature/all-target compile passed and ordered patch audit passed.
Patch upstream-bch-refund-wallet-mutation-fixture.patch.
Evidence build/bch-refund-wallet-mutation-fixture-{check,patch-audit}-20261015.log.
New build/run supervisor script build/run-bch-refund-wallet-mutation-integration-fixture-20261015.py;
status build/bch-refund-wallet-mutation-integration-fixture-status-20261015.json.
Runtime acceptance pending. Claude source review running session77851, output
build/claude-bch-refund-wallet-mutation-fixture-20261015.txt; do not duplicate.
Separate fresh-review run1617292/child1623221 remains live and unchanged.
Next: inspect fixture/review outcomes, wire dedicated service ownership into controller
and explicit UI approval, and handle restarting a service whose operation was uncertain.

Claude mutation-fixture review completed; main actionable gaps are stronger per-action
call assertions, observer read-only call auditing, canonical bad-approval construction,
readonly saved() verification and exact metadata file-set checks. A staged replacement
implementing these is build/bch-refund-wallet-mutation-assertions-draft-20261015.rs.
It is NOT applied or compiled yet because supervisor1635620/session29754 is compiling
the engine. After the supervisor freezes its binaries and rustc is no longer live,
apply the draft to .../refund/wallet/helper/mutation.rs, make a new ordered patch from
the pre-application source, compile/audit and scope any new runtime to that version.
Do not silently promote the current run to acceptance of these pending assertions.

Review caveats: generic public errors intentionally do not expose private engine reasons;
negative cases are paired with successful requests and unchanged-call/journal assertions.
No digest prefix exists (64 lowercase hex); draft adds an explicit format assertion.
WalletFixture allocates a new ephemeral listener and Arc<State> per instance and aborts
its task on Drop; it does not reuse a wallet directory or live service. Key-derivation
correctness is separately asserted in parent refund.rs; draft repeats known spend-share
comparison, while this test remains helper interoperability rather than an independent
cryptographic proof. RPC credential validation and concurrent/killed-helper contention
are NOT proven by this fixture. Independent real-wallet/paired-chain and complete GUI
recovery acceptance remain unfinished. Add wrong-key reconciliation and further owner
restart cases in subsequent integration work; do not treat current coverage as exhaustive.


## 15 October 2026 — open/check desktop coverage and stronger actual-helper assertions

Added successful desktop open/check submission rows from attempted history. Check rejects
nonempty passwords and sends an explicit empty password; open retains its password and
both preserve wallet intent while reaching ready. Full worker/adapter suite38pass0fail
0skip35123ms. Evidence build/bch-refund-wallet-open-check-worker-tests-20261015.log.
These remain synthetic-helper process checks, not real wallet mutation acceptance.

After compiler exit and immutable freeze, applied the previously staged mutation checks:
status saved() is proven read-only, canonical wrong-approval digest is required, recovered
spend is compared with known fixture shares, check/open must query the spend key and scan
the refund payment, open must actually call open_wallet, target must remain open, original
metadata plus exactly intent/ready files is enforced, and observer calls must be read-only.
Added wrong-spend-key reconciliation rejection after lost restoration/scan, with no ready
record or metadata changes, followed by successful explicit check after correcting the
fixture key response. Fixture still uses synthetic RPC services, not public chains.

All-feature/all-target check and ordered patch audit passed.
Patch upstream-bch-refund-wallet-mutation-assertions.patch.
Evidence build/bch-refund-wallet-mutation-assertions-{check,patch-audit}-20261015.log.
New supervisor build/run: build/run-bch-refund-wallet-mutation-assertions-fixture-20261015.py;
status build/bch-refund-wallet-mutation-assertions-fixture-status-20261015.json.
Earlier mutation supervisor1635620 is running frozen engine4b453c4e3c574fb2f2b51bfa74eba7fa451e2a1c83ffcaaf0b9054b47a736913,
helperdfb7d1601ca292fab4922e8cf61501138612ed408f492764c7d760273c20e5bd,
probee76c95949cfd9f35271e0cdcc8bf32606d8a29646005ccd23dd8f3615b90e3b6.
Its result cannot cover the later stronger assertions. Fresh-review run1617292 also stays
unchanged. No runtime mutation pass claimed yet. Next functional work: native controller
attachment of the dedicated owned service and explicit approval UI; interrupted-service
restart reconciliation and actual paired-chain/GUI acceptance remain required.


### 2026-10-15 — BCH recovery-wallet controller ownership
The native DEX controller now attaches only the configured live app-owned recovery
service and accepts a submission only with a fresh matching approval and an available
durable operation token. Failed/lost replies remain recovery-required, with no automatic
retry. Password buffers are consumed on rejected controller submissions as well.
Owner availability notifications are queued to prevent reentrant operations. Controller
teardown disconnects the owner before destroying child jobs. Claude reviewed this wiring;
disposition: build/claude-bch-refund-wallet-owned-controller-disposition-20261015.txt.
Verification: app rebuild passed; focused controller/ownership suite 9 passed, 0 failed
(7 cases plus init/cleanup); refund-wallet worker suite 38 passed, 0 failed.
Evidence: build/bch-refund-wallet-controller-{teardown-build,final-tests,worker-tests}-20261015.log.
These are synthetic-service/controller checks, not complete live-chain acceptance.
Native startup provisioning, explicit GUI approval/password entry, interrupted-service
reconciliation and payout remain unfinished. Three frozen real-helper recovery runs were
confirmed live; no runtime pass is claimed for them. Website source updated, not deployed.


### 2026-10-15 — Explicit BCH recovery-wallet approval screen
Added restore/reopen/verify approval controls gated on a fresh review and attached,
app-owned service. Restoration requires matching password confirmation; all actions
require consent. Status changes, expiry, pair changes and submission clear the form.
The native approval bridge rechecks consent, exact commitment and submission availability
before delegating to the existing durable operation worker. Uncertain submissions keep
historical wallet details and explain that the operation may have completed.
Validation: QML suite 103 passed; focused controller suite 6 passed (4 cases plus
init/cleanup); app rebuild and GUI git diff --check passed.
Evidence: build/bch-refund-wallet-approval-ui-{qml,controller,build}-20261015.log.
QML/QString and QProcess password copies cannot guarantee memory erasure; no credentials
are persisted by this form. Native startup provisioning/attachment remains to be wired,
so this is not yet normal-session GUI acceptance. Interrupted-service reconciliation,
payout and full settlement remain unfinished. Claude review launched separately.

Claude approval-UI review completed; disposition in build/claude-bch-refund-wallet-approval-ui-disposition-20261015.txt. Added visible-review replacement regression, matching review-details visibility gate, native action whitelist and uncertainty-message precedence.


### 2026-10-15 — Owned recovery-wallet review provisioning
Added native provisionBchRefundWalletService and job provisionOwnedReview: generate a
private session-local review wrapper using the app owner's actual native descriptor,
with a distinct payment-observer wallet. The controller attaches the same owner. No
helper or wallet operation starts during provisioning. Wrapper bytes and ownership are
rechecked after parsing; failed attachment rolls back generated review configuration.
QTemporaryFile lifetime removes only this job's wrapper on rollback/destruction; wallet
files are untouched. Crash leftovers are not automatically reused. This config is not
a durable wallet journal. Tests cover invalid observer/same-target refusal, rejected
provisioning cleanup, duplicate refusal, explicit rollback/retry and destructor cleanup;
owned submission then runs through generated config. Seven focused checks passed
(5 cases plus init/cleanup). Full app rebuild passed.
The first controller fixture failed because its dummy helper assumed config and test
data share a directory; corrected to follow statusConfigFile as the real helper does.
Evidence: build/bch-refund-wallet-provision-final-{build,controller}-20261015.log.
Claude review/disposition: build/claude-bch-refund-wallet-provision-disposition-20261015.txt.
Next: dedicated startup coordinator with persistent service-directory binding and clean
resume. Do not create a fresh service blindly on every startup or reuse a service marked
recovery-required. Automatic startup wiring and interrupted-service recovery remain
unfinished. Existing frozen real-helper runs remain active, with no new runtime pass.

Provisioning follow-up: complete refund-wallet worker suite passed 38/38 in 34.982s; evidence build/bch-refund-wallet-provision-final-worker-20261015.log. GUI diff whitespace check passed.
