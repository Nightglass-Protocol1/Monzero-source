# Monzero release status

Status: not ready for production release
Assessment date: 2026-09-17
Candidate line: Genesis prerelease

## Genesis pre14 local build and test evidence (unpublished)

This section records a local build/test pass on the release workstation,
requested as a rebuild of the current tree covering the daemon, CLI wallet,
wallet RPC, GUI, and miner. It does **not** record a published candidate; no
artifact has been placed on `website/downloads/`, no VPS or webspace was
touched, and none of it has been independently reproduced or signed.

- All test/build evidence below was produced at commit `9158d1c30`. The
  `docs/RELEASE_NOTES.md`/`RELEASE_STATUS.md` draft was then committed as
  `14cf088a4` (`fork/main`, pushed to the `github` remote and force-synced
  onto its `main` branch) so that `package-linux.sh`/`package-source.sh`
  would accept a clean, non-dirty tree; no file under `src/` changed between
  the two commits, so the test results below still apply, and the packaged
  binaries were rebuilt at `14cf088a4` to embed its exact version string.
- `monzerod`, `monzero-wallet-cli`, and `monzero-wallet-rpc` were rebuilt from
  commit `14cf088a4` and report matching version `0.18.5.1-14cf088a4`.
- All 20 `ctest` suites pass (100%, 3,599 seconds total), including a full
  `core_tests` consensus replay (~3,599 of that in `core_tests` alone) and
  `cnv4-jit`, `cncrypto`, `wallet-crypto-bench`, `block_weight`,
  `difficulty`/`wide_difficulty`, and the RandomX/hash-family suites.
- The `unit_tests` binary independently reports 1,305 of 1,305 tests passing
  across 160 test cases, matching the pre13 count (no `src/` file changed
  between pre13 and this commit).
- `functional_tests_rpc` did not run: the build host is missing the
  `monotonic`, `zmq`, and `deepdiff` Python modules. This is an environment
  gap on this workstation, not a result of any code change.
- **Linux CLI, source, and Windows CLI archives were packaged** from the
  clean tree at `14cf088a4`:
  - `monzero-genesis-pre14-linux-x86_64.tar.gz`, SHA-256
    `aff64882b80e2b3fd528e071c1d56b5605e73ce311b26a16dbb3eb2a8bcdc0d1`. Built
    with the ordinary system toolchain, not the pinned depends environment;
    has not passed strict package verification (`RELEASE_STRICT=1`) or the
    digest-pinned container smoke test.
  - `monzero-genesis-pre14-source.tar.gz`, SHA-256
    `56f46a83a382dbfa37c55821724cb5991f22dc13e2e932b609aef27270db915a`.
  - `monzero-genesis-pre14-windows-x64.zip`, SHA-256
    `1050e444c24e8e5a19c26e2c710080d17a2c5a08990e8b5818b4c39ec4ed6bcf`. Built
    with the project's pinned `contrib/depends` cross-toolchain
    (`x86_64-w64-mingw32`, posix threading). Has **not** been run on a native
    Windows host; the `WINDOWS_NATIVE_TEST.md` checks (native version,
    daemon startup, offline RPC, clean shutdown) remain outstanding.
  - None of these archives has been placed on `website/downloads/`,
    referenced from published JSON release metadata, independently
    reproduced, or signed.
- **A Windows GUI archive was produced** for this candidate:
  `monzero-genesis-pre14-windows-gui-x64.zip`, SHA-256
  `639bf9f0a073196d833ee165810d442b56769c3698a64bb4394251553e0e2b0e`,
  containing `monzero-wallet-gui.exe` and matching `monzerod.exe`,
  `monzero-wallet-cli.exe`, `monzero-wallet-rpc.exe` all embedding
  `0.18.5.1-06180b4ff`. Cross-compiled in the project's pinned static
  Windows build-environment container (image `monzero:build-env-windows`,
  id `8d4384bd1e48`); passed `verify-windows-gui-package.sh` in full. Has
  not been run on a native Windows host, independently reproduced, or
  security audited.
  - Before building this, `monzero-gui`'s checked-out tip (`e947617b6`) was
    found to have diverged onto unmerged work (an asset transfer/burn UI)
    pinned to a core snapshot from a deleted repository path
    (`~/Projects/Monero-Fork`), unrelated to this candidate's `fork/main`
    core line, and would not compile against it (missing
    `createAssetTransferTransaction` in this core's `wallet2_api.h`).
    `monzero-gui`'s own working tree also carries substantial unrelated,
    uncommitted DEX-prototype work; none of it was touched.
  - The archive was instead built from GUI commit `0018a58f` (parent of the
    commit that introduced the asset-transfer/burn dependency), the newest
    GUI commit compatible with this candidate's actual core wallet API.
  - Built via a flat `git archive`/`rsync` export of GUI @ `0018a58f` plus
    this repository's working tree copied into its `monero/` subdirectory
    (`MANUAL_SUBMODULES=1`, `.git` metadata included so the build correctly
    embeds `06180b4ff` rather than falling back to `-unknown`), rather than
    `monzero-gui`'s usual nested-worktree submodule mechanism, which
    corrupted its own working tree when tried directly.
  - `make -j8` crashed the mingw linker (`ld`, SIGSEGV) building the
    unrelated `monero-gen-ssl-cert` utility; building only the four needed
    targets (`monzero-wallet-gui daemon simplewallet wallet_rpc_server`) at
    lower parallelism avoided it. That utility was not built and is not
    part of this package (it never was, historically).
  - No packaging script exists in this repository for GUI archives, only
    `verify-windows-gui-package.sh`; this archive's file layout, launcher
    scripts, and `GUI_BUILD_MANIFEST.txt` were assembled by hand to match
    the structure of the previously published pre13 Windows GUI archive.
- The Linux GUI was also rebuilt locally from GUI commit `0018a58f` against
  this exact core commit (native build, not cross-compiled) and links
  successfully, but was **not packaged**: this project has never had an
  archive format or verification script for a distributable Linux GUI
  package (only Windows GUI archives have ever been published), so this is
  local build verification only, not a release artifact.
  - This corrects a mistaken premise in earlier pre14 notes, which assumed
    `monzero-gui` was a submodule of this repository that "had not changed
    since pre13." It is not a submodule (it is listed in `.gitignore`) and
    its own history had in fact moved substantially past the pre13 baseline.
- The miner is not a separately compiled artifact; RandomX mining is built
  into `monzerod` and driven by `start-monzero-miner.sh`
  /`stop-monzero-miner.sh`, both unchanged since pre13.

None of the above authorizes publication, promotion, or deployment. Genesis
pre13 (below) remains the most recent candidate with complete, verified
packages on `https://monzero.org`.

## Genesis pre13 qualified unsigned prerelease

Genesis pre13 was built from core commit
`bddd92e435d66cef92478618b35c3812dfc34328` and revised GUI package commit
`2e40a3a40c8a34494a6d67d7d26953897fd58181`. It retains the pre10 through
pre12 chain identity. The consensus configuration and hard-fork schedule are
unchanged, and the public asset hard fork remains inactive.

All 1,305 unit tests passed, including the new synchronization-timer
regression. The exact Linux archive passed clean-container startup, a
three-node restart/propagation/competing-fork/reorganization/transfer test, and
wallet creation and deterministic restoration. The exact Windows CLI archive
passed native Windows version, daemon startup, mainnet/offline RPC, and clean
RPC shutdown checks. The Windows GUI archive passed its manifest, PE64,
subsystem, ASLR/NX, and noninteractive native startup checks; interactive GUI
wallet workflows remain pending.

The combined publication verifier passed all three binary archives and both
complete source archives. Their SHA-256 digests are:

- Linux x86-64: `6e280ff46b45c7065dd9089e773c0ecd3954d618d0bdda1f686a0940a8b10aa4`
- Windows x64 CLI: `96425939b70f5de84a00855a45f92002f1901b4d5e70fba35515f9d681bb7d13`
- Windows x64 GUI revision 2: `5935799aca65b68b6fec4318a05ddbfa97b495d85c85e9927f937b009227d171`
- Core source: `1f005d7fa27942b32bb4a821c5c554b3870adf09f4b242547d46e5035d2b5166`
- GUI source revision 2: `68571df159342d8f9d2c0ebbc2c4a523ce900fb6f111a350ae73945421d720e2`

Genesis pre13 was published on `https://monzero.org` on 2026-09-06 as an
explicitly unsigned, independently unaudited prerelease; its GUI package and
GUI source were revised on 2026-09-07 to add automatic local-daemon startup and
node selection without changing chain identity. Complete HTTPS
downloads of all five artifacts reproduce the metadata digests. The preceding
web files are retained at
`/home/www/Monzero-backup-20260906T171858Z-pre13` for rollback.

Both public nodes were upgraded one at a time from revision `519654692` to the
exact packaged daemon `0.18.5.1-bddd92e43`, whose SHA-256 is
`9286f86c2e25c5efbfbb65d81d12f83704078b59d4a6a057ed0460402fdfa7e0`.
The EU rollback is `/var/backups/monzero-pre13-20260906T1725Z`; the US rollback
is `/var/backups/monzero-pre13-20260906T1728Z`. After deployment both services
were active and synchronized at height 1,043 with top hash
`1ccdb5387b61c8b964023b14441f0fd74700d130938b9a33f68e4fb55f109e69`,
two peer connections, active health/readiness timers, and no warning-or-higher
daemon journal entries since restart. The tip itself is stale because no new
block has been mined, so network readiness continues to fail truthfully.

Independent binary reproduction, release and Windows code signing,
interactive Windows GUI testing, sustained live-network observation, and an
independent security review remain outstanding production gates.

## Genesis pre12 local qualification

Genesis pre12 is a locally qualified prerelease candidate built from core
commit `19b947119d71b88c330421a1d69ec7fa31c8f672` and GUI commit
`b0fee657ce7f079de36995ddb5c6053ce7bf7d4f`. It retains the fresh-chain
identity introduced by Genesis pre10: mainnet UUID
`94834264-d0b2-41dd-b0f2-0ada675c7710`, genesis nonce `2271206363`, and
genesis hash `84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4`.
It is the same chain as pre10 and pre11; the public asset hard fork remains
inactive.

All 1,303 unit tests passed, including protected consensus and release-
hardening regressions. Focused DNS/OpenAlias, HTTP Origin, asset, and RPC
tests passed. The sanitizer-backed asset-wire fuzz target completed 10,000
executions without a finding. Linux and Windows CLI targets and the static
Windows GUI target built with embedded core version `0.18.5.1-19b947119`;
the GUI reports source `b0fee657c`. All packaged Windows executables have
high-entropy ASLR, ASLR, and NX enabled, enforced by the package verifiers.

The Linux, Windows CLI, Windows GUI, core source, and GUI source archives passed
the then-current non-production package verifiers and combined publication gate.
The GUI source archive recursively includes GUI submodules and the exact
pre12 core rather than the GUI commit's older core gitlink. Three local core-
source packaging runs are byte-for-byte identical. The exact Linux archive
starts and stops correctly in the pinned, network-disabled Ubuntu smoke
environment, binds HTTP RPC to loopback, and imports no GLIBC symbol newer
than 2.29.

Pre12 is not ready for production promotion. Native Windows CLI and GUI
execution, independently operated binary reproduction with a distinct signed
attestation, release and Windows code signing, and an independent security
review remain incomplete. Local deterministic packaging is not independent
reproduction, and local security testing is not an independent audit.

The qualified pre12 artifacts remain unchanged. Later commits described below
belong to the next candidate and do not retroactively qualify or alter pre12.
Post-qualification review found that all five pre12 archives embed the stale
Genesis pre9 release notes. Those notes incorrectly call the candidate
deliberately untested and omit pre12 changes and evidence. The binaries and
their recorded hashes are unchanged, but pre12 must not be promoted; the new
release-notes binding gate intentionally rejects each old archive so that a
correctly documented next candidate supersedes it.

## Post-pre12 development hardening

Core commit `9eb5382ad` removes undefined behavior from the circular
`Blockchain`/`tx_memory_pool` construction pattern. The pool now supports an
explicit, checked two-phase binding, and core, offline utilities, and test
fixtures preserve valid construction and destruction order. The core tests,
block-weight tests, all seven affected blockchain utilities, and the focused
20-test binding/weight/distribution suite build or pass locally. The expanded
full unit suite passes all 1,304 tests after the change.

Core commit `d702017dd` prevents a local wallet CLI null dereference when wallet
creation is cancelled before `wallet2::make_new()` returns a wallet. The
wallet CLI target rebuilds, starts for `--help`, and passes the user-facing
branding gate after the change.

CI commit `ed9984466` runs the branding, hostile-archive, and signing-subkey
release regressions in the Linux test job. It also replaces the unpinned PyPI
source archiver with Monzero's deterministic recursive source packager and
verifier, and corrects inherited `monero-*` binary artifact paths. A local
source-package simulation produced an archive that passed the repository's
source-package verifier; hosted CI execution is still required.

The next-candidate working tree also binds every binary and source archive's
package label to an exact `Release label:` declaration in its embedded release
notes. Positive and stale-label regressions pass locally, and the strengthened
Linux CLI, Windows CLI, Windows GUI, core-source, and GUI-source verifiers all
reject the stale pre12 archives at this gate. Genesis pre13 notes now describe
the post-pre12 changes and distinguish source-tree tests from future exact-
artifact qualification.

These commits have not been packaged. A clean next-candidate build must repeat
the complete qualification, platform, package, and publication gates rather
than reusing pre12 evidence.

## Genesis pre11 continuation

Genesis pre11 remains a prerelease and is not a production candidate. Core
commit `a99f85b357ee687b524bb06ea3cc933b8047a4a0` and GUI commit
`94ec2a7c36efcd265956253a771ce9a6dbd9d1ab` retain the Genesis pre10 chain
identity. The continuation audit found no demonstrated critical or high-
severity vulnerability; the release-hardening working tree passes all 1,303 unit
tests, including the protected consensus vectors, corrected asset-ID fixed
vector, ban-list test without port contention, and HTTP Origin enforcement.

Pre11 must not be promoted to stable. Its Windows CLI archive predates the
miner-reporter correction and fails the strengthened verifier. Strict Linux
verification rejects its dynamically loaded Boost libraries, and its GLIBC
2.42 floor is above the documented production portability target. The GUI
source archive lacks the standard per-file source manifest. Native Windows
CLI/GUI execution, independent reproduction, release signing, a completed
independent security review, and production HTTPS/network-readiness evidence
remain outstanding.

The next candidate must be built from one clean reviewed commit after these
working-tree remediations are committed. Final metadata must pass
`verify-publication.sh`; production promotion additionally requires signed
metadata, distinct signed reproduction evidence, strict package verification,
and the live deployment/rollback gates in `RELEASE_CHECKLIST.md`.

Genesis pre10 starts the fresh Monzero mainnet at network UUID
`94834264-d0b2-41dd-b0f2-0ada675c7710`, genesis nonce `2271206363`, and
genesis hash `84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4`.
Both public nodes run the portable core build from commit `519654692657855445c0ab490380dd2465ea5c91`, are synchronized at height 1,
and report that genesis hash as their common tip. Pre9 chain data was moved to
timestamped backups before the coordinated reset. The EU rollback is
`/var/backups/monzero-pre10-20260825T192921Z`; the US rollback is
`/var/backups/monzero-pre10-20260825T192926Z`.

The unsigned pre10 artifact SHA-256 values are Linux CLI
`7eb3d2d0c527b58e2dc3854317d2abcf49bc42222206d522d36994007e7fbb2b`,
Windows CLI `3e50890ab6b44eec27aac1fc73e5bc9101ad841499e50941f90a45c3f61662f2`,
Windows GUI `04a8e59c9603d9e1c849e905eb6f9ed39f03787123ff742fb150af54b6aad659`,
core source `97215f624eac4edae9b6ec33bf320fe71370e43a2fc6c6633335c818f3282d9e`,
and GUI source `dcb281e2d52dd11b6274a189b2b84a2920375fed9ee65f2c7618e06df1e2e063`.
The Linux build has a minimal runtime dependency surface but requires GLIBC
2.42 or newer; both public Ubuntu hosts provide GLIBC 2.43. Package validation
passed, but the release remains unsigned, independently unreproduced,
unaudited, and a prerelease.

Genesis pre10 was published to `https://monzero.org` on 2026-08-25. Live
availability checks passed for the homepage, whitepaper, release metadata and
notes, and all five binary/source archives. The web node-status and explorer
proxies report height 1 and the expected pre10 genesis hash. The webspace
rollback snapshot is
`/home/www/Monzero-backups/20260825T201122Z-pre10-site`.

Genesis pre9 publication is authorized as an explicitly untested prerelease.
No earlier test result is attributed to its exact binaries. Compilation,
packaging, checksum generation, and publication availability checks are not
treated as functional or security test evidence.

Genesis pre9 is built from core commit
`782d9c28645816aabb4f558e43e9ce5497fa1ef3` and GUI commit
`f31aba2332009ebc0b4a278a1291f98ec2bb868a`. The generated unsigned artifact
SHA-256 values are Linux CLI `275d0d3d64de57d0d5516e7836c8709b850c8cbae4554b618fb2cb7f70e7434d`,
Windows CLI `7b99c32ce25260393b5147311b2bdef75a536b67a2a0a127e11843712960c294`,
Windows GUI `546760e22e14efcf73e9897c9601cedd063bd82449689b6f4483cbea76606b3b`,
core source `27ef17275fd5e5296f41b49d1cdcd00fb0c11d9bb70beaf5fe80f14975aadf7d`,
and GUI source `8f47abce67c0c0e7e7e382ae0757b305996c097f6094ffaf6dc7c68a43166488`.
Their compilation completed, but no test suite, release verifier, native
startup, wallet lifecycle, network, or platform smoke test was run for the
exact artifacts.

Genesis pre9 was published to `https://monzero.org` on 2026-08-25. Public
availability checks succeeded for the homepage, updated whitepaper, release
metadata and notes, and all five binary/source archive URLs. These checks only
establish that the files are reachable; they are not functional test evidence.
The atomic webspace deployment retains the preceding live site at
`/home/www/Monzero-backup-20260825T134000Z-pre9` for rollback.

This file records evidence for the current release assessment. It complements
`RELEASE_CHECKLIST.md`; unchecked requirements remain release blockers even
when they are outside the source repository.

## Genesis pre8 deployment evidence

- Source revision `6ea49557542cfa45883adb3af229775159738e45` passed the
  complete core replay: 175 tests run, 0 failures. The broader gate also
  passed 1,297 unit tests, 16 adversarial core tests, the pruning race test,
  and a three-node propagation, restart, reorganisation, transfer, and seed-
  restoration scenario.
- Strict verification passed for the Linux, Windows, and source packages.
  The Linux package was rebuilt in the digest-pinned Ubuntu 20.04 environment
  after rejecting a host-linked candidate; its highest imported GLIBC symbol
  is 2.29 and the published conservative compatibility floor is GLIBC 2.31.
- The exact Linux archive passed clean Ubuntu 24.04 daemon startup/shutdown
  and wallet create, export, restore, and refresh testing. Evidence file
  `pre8-linux-wallet-evidence-release.json` has SHA-256
  `c3f32c43eaa7f3bab9b418b4f5c13b8923fdabf700c414d210027dc39870ec36`.
  The Windows package passed strict structural verification, but native
  execution was not repeated because the Windows test host was unavailable.
- Both public nodes run `0.18.5.1-6ea495575`, report synchronized at height
  1042, and share top hash
  `76cbc51636b282da330878300504aa45ea90e28ab16245e430307521aabe1199`.
  Rollback copies are `/var/backups/monzero-pre8-20260824T2204Z` on the US
  node and `/var/backups/monzero-pre8-20260824T2205Z` on the EU node.
- The atomic website deployment retains rollback directory
  `/home/www/Monzero-backup-20260824T2206Z-pre8`. Complete post-deployment
  HTTPS downloads reproduced the published SHA-256 values: Linux
  `9adee89066046bb24f84dc1f2d739b8025a4ef6cb933a40754110088a384ed79`,
  Windows `4745a650d0050767438b60d3b1676b8a7d75cb66431b03ce1a2173416ff91bbe`,
  source `15b2e1bc4851dea2a275607cb78550cc77bedb9b78446f77e9877555fe7ccec9`,
  and whitepaper
  `c58ef76ec73004f2b947e3800f633260a847116fddc261a8882886831d780789`.
  Genesis pre8 remains explicitly unsigned, unaudited, and a prerelease.
- A separate Windows GUI archive was cross-built from GUI revision
  `e207630859d5709986af0752259a86e8a4c6c185` against the exact pre8 core
  revision. Its static PE executable imports only Windows system DLLs and its
  internal checksum manifest passes. The archive SHA-256 is
  `8ac7f4d1cdf6c920beec1198f2441efc7021b2930b404938e1a15067b1cb7bcc`.
  Native Windows GUI startup and wallet lifecycle testing remains pending
  because the Windows test host was unavailable.
  Corresponding recursively complete GUI source is published with SHA-256
  `b7f0715b671d9eec55f2d9a8fadbe66296640ea8aa5c7bf2130c9e678983e58e`.
- Mining telemetry is intentionally separate from consensus mining. The
  homepage now labels opt-in reported speed separately and displays the
  difficulty-derived network estimate (`difficulty / target`) even when no
  miner is reporting. The Windows reporter passed an authenticated live
  heartbeat test, stores its credential outside public packages, exposes no
  wallet or machine identifier, and runs as a restartable per-user task.

## Verified in the current working tree

- The development mainnet now has a fresh consensus identity: network UUID
  `94834264-d0b2-41dd-b0f2-0ada675c7710`, genesis nonce `2271206363`, and
  derived genesis block hash
  `84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4`.
  The premine-free genesis transaction, address prefixes, and ports are
  unchanged. These values are not deployed: Genesis pre9 and all live nodes
  remain on the superseded chain pending qualification and a rollback-safe
  coordinated reset.
- GUI development commit `49aa38a6` replaces the packaged application icons,
  title-bar marks, mining indicators, and history mining icon with Monzero
  artwork. The Windows icon is a multi-resolution ICO and the macOS icon is a
  valid ICNS container. The native Linux `monzero-wallet-gui` target rebuilt
  successfully after the QML resource changes. These development assets are
  not part of the published Genesis pre9 archives, and no replacement download
  has been published.
- Core development commit `7240934c7` exposes a public wallet API that
  constructs, but never automatically relays, a fixed-supply asset issuance
  transaction. It validates the asset class, decimal range, metadata hash,
  collection identifier, and recipient before construction. The focused
  invalid-input regression and the `wallet_api` build pass. GUI development
  commit `4c13905f1` adds an asynchronous fungible issuance form and retains
  the existing password and transaction-confirmation path before relay. The
  native Linux GUI target links successfully, but runtime QML interaction
  testing and the inactive-HF17 release gates remain outstanding.
- New GUI wallets default to the branded `Monzero/wallets` directory on
  supported desktop/mobile paths and `Persistent/Monzero/wallets` under Tails
  persistence. The wallet picker scans both that directory and the legacy
  `Monero/wallets` location, without moving or deleting existing wallet or
  keys files. The affected GUI target builds successfully. The headless QML
  suite is currently blocked before test execution because this build host
  lacks the QtTest QML module; it must pass in the qualified GUI build
  environment before this change is packaged or released.
- Ref10 carry normalization no longer relies on signed left-shift behavior:
  each carry is multiplied by the equivalent power of two. Wallet binary
  varints now convert enumeration values through their defined underlying
  unsigned type instead of aliasing enum storage. The ordinary unit binary
  builds successfully with both changes.
- The current ordinary unit suite contains 1,300 tests. In the latest run,
  1,291 passed in the default environment; the remaining nine ring database
  and spent-output cases failed only because the default temporary filesystem
  could not extend LMDB. All nine passed immediately when rerun with the
  established workspace-backed `TMPDIR`. Focused regressions confirm legacy
  varint bytes, enum-varint round trips, and wallet recognition of a modern
  view-tagged mined reward.
- The complete CMake build succeeds with the configured local toolchain.
- All 1,298 unit tests pass in one run with workspace-backed temporary storage,
  including protected Monzero consensus vectors and
  the inactive asset, recipient-restoration, and persistent-state tests.
- All 18 default RPC functional tests pass in one clean run. This covers wallet
  creation/restoration, transfers, mining, txpool/ZMQ, multisig, cold signing,
  P2P propagation and reorganisation, RPC payment, URI handling, and restart-
  sensitive wallet operations. The harness now launches Monzero executables,
  uses Monzero network address fixtures, and applies XMZ precision and genesis
  expectations.
- A separate regtest-only HF17 scenario passes the complete live NFT lifecycle:
  wallet RPC issuance, confirmed discovery, confidential transfer, explicit
  burn, restoration from seed, and reorganisation rollback. It also splits a
  fungible holding into two one-unit outputs and spends both in one burn,
  proving activated multi-input wallet selection. The opt-in fixture does not
  modify any public-network hard-fork schedule.
- A dedicated asset-wire fuzz target directly exercises arbitrary payload
  decoding, successful encode/decode round trips, and activated native asset
  envelope parsing. It builds and completes local smoke inputs; a sustained
  sanitizer-backed fuzz campaign is still required for the release gate.
- A disposable stagenet software wallet successfully creates and re-opens both
  fungible-token and NFT issuance artifacts through `monzero-wallet-cli`.
  Inspection rejects a tampered declared asset ID, and creation rejects an NFT
  without its required metadata hash. These artifacts remain offline-only.
- The inactive v2 asset wire constructor now creates a signed fixed-supply
  issuance whose output is both commitment-conserving and recipient-decodable.
  It can attach the envelope to a native transaction prefix before native
  signing while preserving the non-circular carrier commitment. Obsolete v1
  payloads are rejected instead of being reinterpreted.
- The software-wallet transaction path can now attach a fungible or NFT
  issuance after native inputs, outputs, and public keys are finalized but
  before RingCT signs the prefix. `asset_issue` exposes this as a confirmation-
  gated CLI workflow and reparses the completed envelope before returning it.
  It fails closed while HF17 is inactive and on light, watch-only, background,
  multisig, hardware, and cross-network wallets.
- Wallet payment URIs use `monzero:` consistently in the core and functional
  tests; inherited `monero:` payment links are no longer accepted as Monzero
  links.
- Update discovery and URL generation fail closed instead of contacting Monero
  infrastructure. A regression test protects this behavior.
- The CLI donation command cannot transfer funds and does not advertise an
  inherited Monero donation address.
- The root README identifies Monzero as an independent, experimental network,
  warns against cross-fork seed reuse, and describes the inactive status of
  hard-fork version 17 assets.
- Website and explorer JavaScript and PHP pass syntax checks. Local homepage
  and explorer smoke tests pass, and the node-status API returns a successful
  response from the configured public node.
- The explorer includes asset registry and detail routes backed by the bounded
  asset RPCs, exact fixed-supply formatting, confidential-output summaries,
  inactive-network warnings, and distinct issuer, collection-controller, and
  external-metadata trust labels. It degrades cleanly while the public node is
  still running a pre-asset RPC build.
- Full and view wallets now discover confirmed owned asset outputs across
  derived subaddresses by decoding recipient data and verifying its
  commitment. The versioned encrypted cache persists output IDs, openings,
  heights, and subaddress indices; rescans deduplicate them, pool-only outputs
  are excluded, and reorg/cache reset paths remove stale records.
- Issuance seeds a complete 16-member same-asset anonymity set, preventing a
  singleton NFT from becoming unspendable under the fixed-ring ownership
  rules. A verified multi-input primitive now constructs confidential asset
  transfers and explicit burns, pads the successor anonymity pool, proves
  amount/mask conservation, and emits one stable-key-image CLSAG ownership
  proof per selected input.
- The full software wallet can construct activation-gated multi-input asset
  transfers and irreversible burns after authenticating every real ring member.
  Selection is deterministic, scoped to the requested account/subaddresses,
  and bounded by the existing 64-input consensus limit.
  `asset_transfer`, `asset_burn`, and `asset_list` expose submission and raw
  confirmed-balance workflows in the CLI; spend-restoration material is stored
  in the versioned encrypted wallet cache.
- A fresh software wallet reconstructed from the same account keys rediscovers
  the identical asset opening and ownership key image from confirmed chain
  data. The restoration test spends that output into confidential change with
  an explicit burn, then verifies reorg rollback removes the detached change
  and restores the original output to unspent. The dedicated activated-daemon
  scenario additionally proves the RPC, relay, mining, seed-restoration, and
  competing-tip rollback path.
- Wallet RPC applications can now create fungible tokens, NFTs, collections,
  and editions, enumerate confirmed holdings, and construct transfers or burns
  through `create_asset`, `get_assets`, and `transfer_asset`. Mutating calls
  reject restricted mode, integrated destinations, unsupported wallet modes,
  and inactive HF17; non-relay raw transaction export is supported. The live
  wallet functional test covers holdings and the inactive activation gate.
- Website copies of the Genesis pre2 Linux and Windows archives are byte-for-
  byte identical to `dist/`; their outer SHA-256 files verify and all website
  download links resolve locally.
- Linux release, packaged launcher, and utility shell scripts pass `bash -n`.
- A native Linux GUI build succeeds against pinned core commit `8afa04b46`,
  its bundled daemon links successfully,
  and the headless QML suite exits successfully without QML type/reference
  errors. The startup banner reports `0.18.5.1-release` and logs under
  `.monzero`. Upstream update and development-submodule modes fail closed.
  Desktop metadata is renamed and validated, payment dispatch uses only the
  `monzero:` scheme, and the unsafe inherited Windows installer is removed.
- Two development Linux packages built from the same binaries are byte-for-
  byte identical and pass the normal package verifier. Strict verification
  correctly rejects them when their manifests are dirty or reproducibility is
  not independently verified.
- The packager includes README, release status, checklist, and explicit
  migration/rollback guidance. It refuses mixed daemon/wallet versions and the
  verifier binds their identical reported version into the build manifest,
  screens unsafe archive paths before extraction, and requires the release
  documents. A deliberately mixed historical binary set is rejected.
  Packaged ELF binaries are deterministically stripped; strict verification
  permits only minimal glibc-family dynamic dependencies and rejects the
  broader host-library set produced outside the pinned depends environment.
- A clean static Release configuration at source commit `be2fb8313` produced
  version-consistent daemon, CLI, and wallet RPC binaries. Two Genesis pre3
  archives created from that binary set are byte-for-byte identical with
  SHA-256 `c0ee269628b4d05729bdf3a4db4eb7e11ae74159858a0fca74cfbdb6a9b65aa0`;
  normal verification passes and the manifest records a clean tree, stripped
  binaries, inactive public asset consensus, and unverified reproducibility.
  Strict verification rejects the host build at `libgssapi_krb5.so.2`, proving
  that a pinned depends build and independent reproduction are still required.
  A crafted `../` archive entry is rejected before extraction.
- The pinned Linux depends environment now builds completely on GCC 15 and
  glibc 2.34+ without host-package leakage. It bootstraps gperf, pins legacy C
  sources to C17 where required, carries the established Boost thread fix, and
  disables ZeroMQ transports that are absent from the pinned dependency set.
  A clean build at source commit `4c5cd3353` produced version-consistent daemon,
  CLI, and wallet RPC binaries whose only ELF dependencies are `libm.so.6`,
  `libc.so.6`, and `ld-linux-x86-64.so.2`. Two Genesis pre4 archives are
  byte-for-byte identical with SHA-256
  `0bbc3571db4530076ab863d1f6f6ca291322dd6fe1bda977ecf4453be229a433`.
  Normal verification passes. Strict verification checks all three binaries,
  accepts their stripped/minimal runtime surface, and stops only because the
  required independent binary reproduction remains honestly `unverified`.
- The pinned Windows dependency environment now builds the daemon, CLI wallet,
  and wallet RPC with mandatory Trezor support using the same source baseline
  as Linux. The PE32+ executables import only allowlisted Windows system DLLs,
  carry one identical source revision, and are deterministically stripped.
  Two Genesis pre5 Windows archives produced from the same binaries are byte-
  for-byte identical; the outer digest is published in the adjacent checksum
  file and on the website rather than embedded recursively in the archive.
  Normal verification passes; strict verification stops at the required
  independent reproduction attestation. Execution testing on a clean Windows
  system and the separately pinned GUI package remain outstanding.
- Linux was rebuilt at the same pre5 source baseline. Two packages are byte-
  for-byte identical, with the outer digest published separately.
  Normal verification passes, the three binaries retain only the permitted
  glibc-family dependencies, and strict verification stops only at independent
  reproduction. The website candidate links now identify both packages as
  unsigned, unaudited prereleases rather than production releases.
- Genesis pre5 is deployed on `https://monzero.org` as an explicitly unsigned,
  unaudited prerelease. A post-deployment HTTPS download of each complete
  archive matched its published SHA-256 digest: Linux
  `99e4be1dda90dad94ca624fb19f734f8a5faf9ad6c8cbcb3391da7269c507675`
  and Windows
  `7870ed552f4c1d4b92260d8cfbd25260b1c983edc36c5136965e555162cdcc42`.
  The explorer remained reachable and the node-info endpoint returned status
  `OK` after deployment, but the node reported `synchronized: false` at height
  1023. The prior homepage is retained server-side in the timestamped rollback
  directory `Monzero-backup-20260822T194244Z-pre5`.
- A subsequent public verification downloaded both complete archives over
  HTTPS and reproduced the same published digests. HTTP metadata reports the
  expected `application/gzip` and `application/zip` MIME types and exact sizes
  of 28,530,347 and 25,739,082 bytes. Both normal package verifiers pass;
  strict verification rejects both packages solely because independent binary
  reproduction remains `unverified`. The public explorer RPC still responds,
  but its latest block is height 1022 with timestamp 1786833652 and the node
  reports zero peers and `synchronized: false`; public-network freshness and
  independent-node synchronization therefore remain unproven.
- The prerelease homepage now publishes the exact packaged source commit and
  links `/releases/genesis-pre5.json`. The live JSON is byte-for-byte identical
  to the repository copy and binds both artifact names, sizes, SHA-256 hashes,
  signing state, audit state, and reproducibility state. The prior homepage is
  retained server-side as `index.html.before-release-metadata-20260822`, in
  addition to the full pre5 rollback directory. Post-deployment checks confirm
  that the explorer still returns valid JSON and both artifact endpoints retain
  their expected MIME types and byte sizes.
- The homepage node indicator now distinguishes RPC reachability from network
  readiness: `status: OK` with `synchronized: false` is displayed as “Node
  unsynchronized” with the offline warning state, rather than “Node online”.
  The updated JavaScript and cache-busted homepage are deployed and match the
  committed files byte-for-byte. Server-side pre-change copies are retained as
  `index.html.before-node-state-20260822` and
  `app.js.before-node-state-20260822`.
- The public VPS was rebuilt from a fresh Ubuntu 26.04 image after its former
  SSH host key material was exposed. Its new ED25519 host key was verified
  through the provider console before deployment. Password and direct-root SSH
  authentication are disabled, a dedicated unprivileged administrator key is
  in use, and the host firewall permits only SSH plus Monzero P2P and restricted
  RPC ports. The systemd service runs as the unprivileged `monzero` account.
  The deployed daemon came from the published Genesis pre5 Linux archive; the
  archive and every packaged file passed SHA-256 verification on the VPS, and
  the daemon reports source revision `ab143bbaf`.
- The recovered public node and an operator node both report height 1023, top
  block hash `995a7d95b7c3ca12acacd6fd6dbadd00c0920d917d3bf67dd0f1464d021c530f`,
  `synchronized: true`, and one P2P connection between them. Public P2P and
  restricted RPC are reachable through `node.monzero.org`. The production
  network-readiness gate still fails: it requires at least two peers and a tip
  no more than 900 seconds old, while the restored network has one peer and its
  latest block is approximately seven days old. This recovery establishes
  service availability, not production network readiness.
- The final Genesis pre5 Windows archive was extracted read-only in a disposable
  Ubuntu 20.04 container with Wine 5. The daemon, CLI wallet, and wallet RPC
  executables each loaded, reported the manifest-bound `ab143bbaf` version, and
  exited successfully for `--version`. A full daemon smoke under Wine could not
  initialize LMDB on either the container overlay filesystem or tmpfs, returning
  an I/O error before RPC startup. This is useful PE loader coverage but does
  not satisfy the clean Windows system test requirement.
- The website node-status API now obtains the latest block header and applies
  the same 900-second tip-age and 300-second future-skew thresholds as the
  release-readiness gate. Local PHP and JavaScript syntax checks pass, and a
  live proxy smoke reports the current synchronized daemon as
  `network_ready: false` and `tip_fresh: false`. The homepage and explorer are
  deployed with the cache-busted “Network tip stale” state instead of implying
  that a synchronized process necessarily means a ready network.
- The VPS now separates its RPC surfaces: restricted public RPC remains on
  `0.0.0.0:6175`, while unrestricted operator RPC is bound only to
  `127.0.0.1:6177`. The on-host readiness gate confirms the local endpoint is
  unrestricted and the public endpoint is restricted. A hardened systemd
  timer runs that gate every five minutes and records failures in the journal;
  its first deployed run reported exactly the known one-peer and stale-tip
  failures. Port 6177 is not permitted through the host firewall.
- Independent reproduction now has a fail-closed evidence path. The package
  comparator verifies all four reference/reproduced Linux and Windows inputs,
  requires byte-for-byte equality and one clean source commit, and emits a
  canonical attestation for the second builder to sign. Production publication
  additionally requires that attestation to bind the exact metadata artifacts,
  verifies it against an explicitly trusted reproducer fingerprint, and rejects
  reuse of the release-signing key. Positive matching, valid mismatch,
  missing-identity, overwrite-refusal, and throwaway two-key integration tests
  pass. The integration test reaches and correctly stops at pre5's honest
  `binary_build_reproducibility=unverified` manifest; same-host candidate pairs
  remain determinism evidence only, not independent reproduction.
- A deterministic Genesis pre5 source package now supplies the otherwise
  unavailable public commit plus all six pinned recursive submodule revisions.
  Two packaging runs are byte-for-byte identical at SHA-256
  `a958217d92418d35f59fedb2734dbb1162562876ef0896f74a7d42e94789beca`;
  the source verifier passes over 4,703 archive entries and the release
  publication verifier binds its exact 21,182,274-byte size, digest, and source
  commit alongside the two binary artifacts. The source package, checksum,
  download surface, and byte-identical pre5 JSON are deployed publicly.
- The homepage no longer hard-codes “Public node online” in its hero
  banner. It uses a static experimental-prerelease label and delegates live
  readiness claims exclusively to the stale-tip-aware status panel.
- The 2026-08-23 webspace deployment was performed from a hash-verified staging
  directory, with HTML entry points replaced last. Public HTTPS checks confirm
  the cache-busted homepage and explorer scripts, truthful stale-tip API state,
  exact metadata equality, and the expected MIME types and byte sizes for all
  three downloads. Complete public downloads reproduce the published Linux,
  Windows, and source SHA-256 hashes, and the downloaded source package passes
  full verification. The six replaced live files are retained at
  `/home/www/Monzero-backup-20260823T094426Z-source-status` for rollback.
- Webspace deployment access now uses a dedicated ED25519 key with fingerprint
  `SHA256:zBU44whbE/eHScuAlbue/zcFufKRL5kTmqGrT/IMeFQ`. It was appended without
  replacing the existing operator key, and fresh key-only SSH and SFTP sessions
  both succeed. The password disclosed during deployment is no longer required
  and must be rotated.
- A locally mined block advanced the public network to height 1,024, removing
  the stale-tip failure while preserving one-thread battery-aware background
  mining. The local daemon and public node reported the same top hash,
  `c621d3172dbbe1e941674a28df47f6ebb10b05dfe6381774c3cee3a3533e894e`.
  The remaining live readiness failure is the single connected peer. The public
  API, homepage, and explorer now apply the same minimum of two peers as the
  server release gate: at height 1,024 the verified HTTPS response reported
  `tip_fresh=true`, `peer_ready=false`, and `network_ready=false`, and both UIs
  label the condition “Network under-peered”. The five replaced files can be
  rolled back from
  `/home/www/Monzero-backup-20260823T095331Z-peer-readiness`.
- Clean-system testing of the exact published pre5 Linux archive found that all
  three executables require `GLIBC_2.42`; they fail at process loading on the
  digest-pinned Ubuntu 24.04 image before `--version` can run. The strict Linux
  verifier now enforces a default maximum requirement of `GLIBC_2.35`, and a
  repeatable test runs the final archive in a network-isolated, read-only
  container. Both gates correctly reject pre5. The public homepage and release
  JSON disclose the `GLIBC 2.42+` limitation and Ubuntu 24.04 incompatibility;
  the prior files are retained at
  `/home/www/Monzero-backup-20260823T100042Z-linux-compatibility`. The rebuild
  audit also found that the Linux depends set relied on an undeclared host zlib
  development package. zlib 1.3.2 is now pinned by its official SHA-256 in the
  depends graph. The first clean relink then exposed the same undeclared-host
  dependency for zstd; zstd 1.5.7 is now pinned as well. A new Linux candidate
  must be built and pass both gates before replacing pre5.
- The Genesis pre6 candidate closes that Linux portability regression. Two
  clean pinned-dependency Linux builds produced byte-identical daemon, wallet
  CLI, and wallet RPC executables. Their packages are byte-identical, require
  no non-system shared libraries, require at most `GLIBC_2.29`, pass strict
  package verification, and start successfully from the exact final archive
  in the digest-pinned, network-isolated, read-only Ubuntu 24.04 smoke-test
  container.
- Two clean MinGW builds of the matching Genesis pre6 commit produced
  byte-identical daemon, wallet CLI, and wallet RPC executables and
  byte-identical Windows ZIP archives. The final ZIP passes strict PE format,
  embedded-version, manifest, checksum, and imported-DLL verification. Trezor
  support and its pinned HIDAPI and libusb dependencies were enabled during
  configuration. Native execution on a clean Windows host, independent
  reproduction by a separately trusted person, code signing, and an external
  security audit remain outstanding; pre6 therefore remains an explicitly
  unsigned and unaudited prerelease.
- Genesis pre6 is published at `https://monzero.org` from source commit
  `d4cac36271387af2b6589208bf2be74e10e5a01f`. The public Linux, Windows,
  and recursively complete source archives have SHA-256 digests
  `77b7299a7532093eec94411a59f6663451566a6964feecb742a17029eaa0ebaa`,
  `9d6fb20055a1e1e89625f8565af13be6f9140275de943c5a41a886d8aea87405`,
  and `9ebdf1bf68357fc784d6c92370eff94bc9b104ed1e668ae0110de5fcedf48104`
  respectively. Full HTTPS downloads reproduce those hashes, the publication
  verifier accepts the downloaded metadata and artifacts, and the server
  returns the expected gzip and ZIP media types and exact byte sizes. The
  pre6 website rollback is retained at
  `/home/www/Monzero-backup-20260823T124259Z-pre6`.
- Genesis pre6 is deployed on the VPS from the exact published Linux archive.
  The archive hash and every internal package checksum were verified on-host
  before installation. The prior binary, configuration, systemd unit,
  operator-local RPC evidence, and a complete stopped-state chain database are
  retained at `/var/backups/monzero-pre6-20260823T141935Z`, together with
  tested rollback instructions. The deployed binary reports
  `0.18.5.1-d4cac3627`, reopened the existing chain, synchronized at height
  1,036, and remained enabled, active, correctly versioned, and synchronized
  after a second systemd restart. Operator-local RPC, restricted public RPC,
  and the website API agreed on top hash
  `0d6fea8274daa1b6a14c91211bdbeb3bf08c0417e75fe7a0c744099f92195cbf`.
  The deployment key uses key-only root SSH; password root SSH remains
  disabled. The production readiness gate still fails solely on the live
  network side because the public node has one peer rather than the required
  minimum of two.
- A second, isolated Genesis pre6 daemon was started locally from the published
  Linux build on alternate P2P, RPC, and ZMQ ports. It successfully handshook
  with the existing node, synchronized from genesis through all 1,025 blocks,
  reported version `0.18.5.1-d4cac3627`, and matched the canonical top hash
  `e4baa0aadf87c555f169ca9377b5a0e261a3963a88bfd8b6dd01d64dfe167f1c`.
  This supplies live-chain startup, handshake, import, and synchronization
  evidence for the exact pre6 code in addition to the offline clean-system
  smoke test. It does not satisfy the independent-public-peer gate because both
  local daemons share one public network origin.
- One-thread mining temporarily ran in active mode to resolve the stale-tip
  gate, found block 1,024, and propagated the new top hash to the existing
  local node, the isolated pre6 node, the VPS, and the website API. The miner
  was then restored to its prior battery-aware background mode. The website
  now reports `tip_fresh=true`; the exact pre6 readiness check fails solely
  because its single local peer is below the required minimum of two.
- A second public Genesis pre6 node was deployed from a fresh Ubuntu 26.04
  installation in the IONOS US region at `node2.monzero.org` (`67.217.247.219`).
  It runs as the unprivileged `monzero` user under the same hardened systemd
  unit as the European node, accepts key-only SSH administration, exposes only
  P2P port 6174, and keeps unrestricted RPC on loopback port 6177. Its daemon
  SHA-256 is
  `5b9646fa17a94650d8898783f255ef5d4c7ea28d96b50ffe134edcca1ca50156`,
  identical to the verified pre6 daemon deployed in Europe. Both nodes maintain
  priority connections to each other, and public DNS resolution plus inbound
  P2P reachability were verified through the new hostname. The repository's
  hardened five-minute readiness timer is enabled on both VPSs; its first US
  run completed successfully at height 1,040 with three peers and a 249-second
  tip age.
- After block 1,039 was mined with one temporary local CPU thread, the exact
  repository readiness gate passed independently against each VPS's
  operator-local RPC: both reported height 1,040, three P2P connections, and
  tip ages of 34 and 36 seconds. The local miner was immediately returned to
  its prior battery-aware background mode. The live website API then reported
  `network_ready=true`, `peer_ready=true`, `tip_fresh=true`, height 1,040, and
  three connections. This closes the two-public-peer operational gate; it does
  not satisfy the stronger production requirement for three independently
  administered seed nodes across at least two providers.
- The dedicated `security@monzero.org` mailbox was authenticated successfully
  over TLS against both the IONOS IMAP and SMTP services without sending a
  message. `SECURITY.md` and the website now publish the private reporting
  address, scope, prohibited secret material, acknowledgement targets, and
  coordinated-disclosure process. Email is explicitly identified as lacking
  end-to-end encryption until a dedicated security OpenPGP key is published.
  A TLS-authenticated self-delivery test succeeded; the received message was
  signed with IONOS DKIM selector `s1-ionos` for `monzero.org`, and its
  authentication results reported DKIM, SPF, and DMARC alignment passing. The
  domain's DMARC policy remains monitoring-only (`p=none`); enforcement must
  not be raised until normal mail flow is observed and the remaining
  IONOS-recommended DKIM selector is published.
- The production publication verifier now resolves a valid signing-subkey
  signature to its trusted primary OpenPGP fingerprint instead of incorrectly
  requiring the rotating subkey fingerprint as the release identity. A real
  disposable GnuPG integration test creates a certification-only Ed25519
  primary key plus a distinct Ed25519 signing subkey, signs a payload, proves
  the parser returns the primary fingerprint, and rejects missing or ambiguous
  `VALIDSIG` evidence. `RELEASE_SIGNING.md` documents the matching offline key
  ceremony, backups, revocation, multi-channel fingerprint publication,
  metadata signing, verification, rotation, and compromise response. No
  production key has been generated on this development machine.
- The webspace now redirects every plain-HTTP path and query to the same
  canonical `https://monzero.org` URL with a permanent redirect. A conservative
  one-day HSTS trial, same-origin Content Security Policy, restrictive browser
  permissions policy, frame denial, MIME sniffing protection, and referrer
  policy are active on pages, APIs, and release downloads. Post-deployment
  checks followed the redirect without a loop, retained the release archive's
  exact gzip type and size, loaded homepage/explorer assets, and received valid
  node-status JSON. The prior `.htaccess` is retained server-side as
  `.htaccess.before-https-20260823` for rollback.
- A checksum-pinned PowerShell harness completed the native Windows pre6
  command-line smoke test without weakening it into Wine or cross-build evidence. It
  verifies the exact public ZIP and every inner package hash, launches all
  three executables, creates a fresh offline daemon on random loopback ports,
  validates local RPC and mainnet identity, requests clean shutdown, and emits
  machine-readable host and result evidence. On 2026-08-23, the exact published
  archive passed on native 64-bit Windows 11 build 26200 under Windows
  PowerShell 5.1: all three executables reported source revision `d4cac3627`,
  the fresh offline mainnet daemon returned RPC status `OK` at height 1, and it
  shut down cleanly through RPC. The retained evidence SHA-256 is
  `8deaefa9e82be79e9dc5be7d77839708e6d47d384c29c16da169ff3bfc5de5e9`.
  This closes the exact archive's native command-line launch/RPC gate, but does
  not cover mining scripts, GUI behavior, wallet recovery, synchronization, or
  independent reproduction.
- Genesis pre7 fixes the packaged Windows mining launcher pipeline at source
  commit `38914bf1347781dc6e0ead4a786e3607cc8ebeac`. Two packages from each
  platform were byte-for-byte identical: Linux SHA-256
  `83476fd3e57a6953003874cac2409916ca1cfaa9677144418de6e535561b789e`,
  Windows SHA-256
  `049b4db553d52f592e0a0b05956fbafbec1e37c2672de50d0cde2bfbdffce284`,
  and complete source SHA-256
  `a60c776a70087a997e4c0501638f21fd193d53cb8d56f4704b122e1b1c9f62e6`.
  The Linux binaries were rebuilt in the pinned dependency environment on a
  controlled Ubuntu 20.04 userspace, require no glibc symbol newer than 2.29,
  expose only the minimal glibc-family runtime dependencies, pass normal
  package verification, and pass the digest-pinned isolated Ubuntu 24.04
  launch smoke test. Strict verification stops only at the required external
  independent-reproduction attestation.
- The exact pre7 Windows ZIP passed a native Windows 11 build 26200 network
  and launcher test under Windows PowerShell 5.1. All outer and inner hashes
  matched; all three executables reported revision `38914bf13`; a fresh data
  directory synchronized from height 1 to 1040 with canonical top hash
  `44029426d863f954745a73322dafdae3370299772e2990abd9721ea0b88067af`;
  the exact `start-mining.bat` and `stop-mining.bat` activated and stopped one
  RandomX thread; and the exact node launcher shut down cleanly through its
  console. The retained JSON evidence has SHA-256
  `c89c3dbcd41bc5632d3a5a96c24b5f38718d7e0dcd63e901caf93cb43796d6a1`.
  This remains operator evidence, not independent reproduction or audit.
- A second native Windows harness exercised the exact pre7 wallet RPC from the
  same published archive. It created a new deterministic software wallet,
  exported its recovery seed only in process memory, closed it, restored a
  second wallet from that seed, reproduced the identical Monzero address,
  refreshed 1,039 blocks through the restricted public node, stored both
  wallet states, and shut down the RPC cleanly. The successful harness removes
  its temporary keys and diagnostic directory; the retained seed-free evidence
  has SHA-256
  `96efaa1f1091d3a529ff67b0c805f4496b45663e469fdce90456d092245528fb`.
  This closes native creation and deterministic-restoration coverage for the
  command-line package, but does not establish funded transfer history, GUI,
  hardware-wallet, multisig, or offline-signing coverage on Windows. The live
  homepage and byte-identical release metadata now identify this additional
  test without changing the artifact digest or overstating production status;
  their preceding versions are retained at
  `/home/www/Monzero-backup-20260823T1830Z-wallet-evidence`.
- The exact pre7 Linux archive passed the corresponding native wallet-RPC
  lifecycle on the release workstation after its full inner manifest was
  verified. A fresh software wallet was created, its in-memory recovery seed
  restored a second wallet with the identical address, 1,041 blocks were
  refreshed through the restricted public node, both wallets were stored and
  closed, and SIGTERM produced a clean RPC save and shutdown. Successful runs
  erase the temporary wallet directory; retained seed-free evidence has
  SHA-256
  `74b22898b1da384b2eb96fdbb497c1c40d9b74602408f624a30992db1e7423f6`.
  This complements, but does not replace, the digest-pinned clean Ubuntu 24.04
  loader/startup test or independently operated clean-system testing. The live
  homepage and release metadata now report this result; their prior state is
  retained at `/home/www/Monzero-backup-20260823T1838Z-linux-wallet-evidence`.
- Genesis pre7 is deployed on `https://monzero.org` as an explicitly unsigned,
  unaudited prerelease. The live metadata names source commit
  `38914bf1347781dc6e0ead4a786e3607cc8ebeac`, and complete HTTPS downloads of
  the Linux, Windows, and source archives reproduce the published SHA-256
  digests and expected byte sizes. The public Windows network-smoke harness is
  byte-identical to the tested script. HTTP security headers, download MIME
  types, the homepage, explorer, and both POST-only status APIs were verified
  after deployment. The preceding webspace state is retained at
  `/home/www/Monzero-backup-20260823T1812Z-pre7` for rollback.
- Both IONOS public nodes now run the exact pre7 Linux daemon from the published
  archive and report `0.18.5.1-38914bf13`. The European deployment can be
  rolled back from `/var/backups/monzero-pre7-20260823T1814Z`; the US deployment
  can be rolled back from `/var/backups/monzero-pre7-20260823T1815Z`. Each
  systemd service is active and synchronized at height 1,040 with canonical
  top hash
  `44029426d863f954745a73322dafdae3370299772e2990abd9721ea0b88067af`,
  and the nodes maintain their bidirectional P2P relationship. Operator-local
  readiness checks and the public website API agree that synchronization and
  peer-count requirements pass but the latest block is more than three hours
  old, so the freshness gate correctly fails and the website reports
  `network_ready=false`. This is a live mining/network condition rather than a
  daemon deployment failure.
- One temporarily active local RandomX thread mined blocks 1,040 and 1,041,
  after which the miner was restored to its prior one-thread, battery-aware
  background policy. The local node and both pre7 public nodes converged at
  height 1,042 with top hash
  `76cbc51636b282da330878300504aa45ea90e28ab16245e430307521aabe1199`.
  The exact packaged readiness gate then passed independently on both VPSs
  with three peers and tip ages of 36 and 37 seconds. The public HTTPS API
  independently reported `status=OK`, `synchronized=true`,
  `peer_ready=true`, `tip_fresh=true`, and `network_ready=true`. This proves
  the deployed pre7 topology can propagate a fresh tip; it is not evidence of
  sustained independent mining, and freshness will lapse again without
  continuing hash power. The next timer-driven readiness cycles also passed on
  US and EU at height 1,042 with three peers and tip ages of 300 and 319
  seconds, proving the scheduled checks recovered from their earlier truthful
  stale-tip failures without manual service intervention.
- Both pre7 VPSs now run a second hardened five-minute systemd timer for
  operational health, separate from consensus/network readiness. It checks the
  service main process, loopback operator RPC identity and synchronization,
  height and top-hash shape, resident memory, data-volume usage, and recent
  daemon error-log visibility. Initial sandboxed runs passed at height 1,042:
  EU used 287 MiB RSS and 3% disk; US used 285 MiB RSS and 3% disk, with no
  error-level lines in either retained 200-line log window. A forced 1 MiB RSS
  ceiling produced the required nonzero failure and diagnostic. These journal
  checks improve host monitoring but do not provide an external alert receiver
  or close the required 24-hour and seven-day observation periods.
- After publishing the Linux and Windows wallet-lifecycle results, the normal
  publication verifier again passed both exact binary artifacts and the
  complete source package at commit
  `38914bf1347781dc6e0ead4a786e3607cc8ebeac`. Its production mode correctly
  fails closed at the first invariant because the truthful metadata remains
  `production_ready=false` on the `prerelease` channel. That state must not be
  changed until the separately trusted release signature, independent binary
  reproduction attestation, resolved security/consensus audit, independent
  infrastructure, and observation-period evidence exist.
- The website, explorer, security page, and Studio now share the GUI's Monzero
  Nightshade palette and responsive dark-purple layout. The broken external
  Studio hostname link was replaced by the deployed same-origin `/app/` route,
  and its node card now calls the real stale-tip-aware `/api/node-info/`
  endpoint. Studio's formerly disabled mock forms are a keyboard-accessible
  local design workspace: token, NFT, collection, and swap inputs update an
  inspectable preview and download an explicitly unsigned, non-submittable JSON
  draft. Browser tests cover one-panel visibility, arrow-key tabs, live node
  data, preview updates, and draft download. No wallet connection, secret-key
  handling, transaction signing, or chain submission was added. The complete
  Nightshade surface is deployed at `https://monzero.org`, including the new
  working `/app/` route; every changed production file is byte-identical to the
  repository copy, all four public routes return HTTP 200, and a live browser
  run reports no severe console or CSP errors. The preceding files are retained
  at `/home/www/Monzero-backup-20260823T1957Z-nightshade` for rollback.
- Genesis pre7 now has canonical human-readable release notes that bind its
  codename and exact source commit, define the qualified Linux x86-64 and
  Windows x64 baselines, enumerate user-visible changes, and state unsupported
  platforms and unresolved safety limitations. Future Linux and Windows
  packages include `RELEASE_NOTES.md`; the immutable published pre7 archives
  retain their original hashes and continue to verify under the historical
  package contract. The website exposes an equivalent plain-text copy beside
  the machine-readable metadata.
- Active CLI runtime defaults now create `monzero-wallet-cli.log`,
  `monzero-wallet-rpc.log`, and `monzero-wallet-rpc.<port>.login`; daemon
  recovery and disconnect messages name `monzerod`. Fish completions are
  registered under the three Monzero executable names and document the actual
  6174/6175/6176 mainnet, 16174/16175/16176 testnet, and
  26174/26175/26176 stagenet ports. A focused source-level gate rejects
  regressions on these release-facing surfaces while leaving required upstream
  attribution and protocol citations intact.
- The remaining core translation catalogs were audited against active source
  call sites. Their inherited donation, executable, and wallet-lock strings
  are dormant catalog entries with no matching runtime source message; active
  user-facing C++ strings contain no inherited Monero service endpoint or
  executable instruction. The retained `MoneroAsciiDataV1` wallet export magic,
  research citations, protocol field names, and copyright notices are
  compatibility or attribution identifiers and are intentionally unchanged.

Live DNSSEC validity is an integration check because it depends on the build
host exposing signatures and a usable trust anchor. Run the unit suite with
`MONZERO_TEST_LIVE_DNSSEC=1` on the release validation host.

## Known package limitations

Genesis pre2 is a private prerelease, not a production candidate. Its Linux
archive predates `BUILD-MANIFEST.txt`, so the current package verifier rejects
it. Genesis pre3 is a clean, deterministic local packaging candidate, but its
host-built binaries have non-permitted runtime dependencies and have not been
independently reproduced or signed. Genesis pre4 replaces that host-built
candidate with pinned-dependency binaries and passes every local strict check,
but still lacks independent reproduction and signing. Genesis pre5 aligns the
pinned Linux and Windows command-line packages at the manifest source commit;
both pass normal verification and deterministic same-machine packaging, but
neither has been independently reproduced, signed, or tested on a clean
Windows system. Genesis pre6 replaces pre5's compatibility-limited Linux
archive with matching pinned Linux and Windows builds that are reproducible
across clean local build directories and pass the strict package gates. It is
still not a production release because separately trusted reproduction,
signing, a security audit, broader independent clean-system testing, and the remaining
network-readiness requirements have not been completed. None of these archives
may be promoted as stable without satisfying the remaining external release
gates. Genesis pre7 supersedes pre6 because the exact pre6 mining launcher had
an invalid PowerShell pipeline escape. Pre7 corrects that launcher and passes
the exact native network, mining, and shutdown workflow, but remains an
unsigned, unaudited prerelease with the same external production gates.

## Repository work still required

- Qualify and coordinate deployment of the new mainnet genesis identity before
  changing any live node data. This consensus migration is distinct from a
  wallet recovery seed. The currently published pre9 chain remains unchanged
  until rollback backups, matching cross-platform binaries, node migration,
  and height-zero wallet-rescan instructions are prepared.
- Reproduce the passing core, GUI, functional, and website/explorer tests in
  CI and on clean supported systems. Security-specific testing, including
  fuzzing and sanitizer campaigns, is outside the currently authorized
  operational scope and has not been used to qualify this working tree; any
  such evidence must come from a separately authorized independent review.
- Add the asset creation/inspection workflow to the desktop GUI. The CLI can export and
  validate canonical signed fungible, NFT, collection, and edition artifacts,
  and its direct issuance command becomes usable only after HF17 activation.
- Complete authenticated metadata retrieval and richer ownership workflows for
  fungible tokens, NFTs, collections, and
  editions. Direct fixed-supply creation, confirmed owned-output discovery,
  multi-input spending, burns, cache round trips, seed restoration, and reorg
  handling are wired and covered, but the broader asset wallet is not yet
  complete.
- Independently reproduce both strict-verifier-compatible pinned Linux and
  Windows candidates on separate clean systems.

## External evidence required before production release

- Release version/codename, supported platforms, and final source commit.
- An offline release-signing key with a publicly verified fingerprint.
- At least one independent reviewer and a second reproducible-build operator.
- Three independently administered seed nodes across at least two providers
  and regions, followed by empty-directory synchronization and failover tests.
- Public bootstrap height/allocation disclosure and team-controlled-address or
  equivalent accountability disclosure.
- Clean-system Windows and Linux package tests and post-deployment monitoring.
- Independent consensus/implementation review. Asset activation additionally
  requires the cryptographic review, isolated network, public testnet, audit,
  and coordinated hard-fork gates in `MONZERO_ASSETS_V1_SPEC.md`.

None of the evidence in this document authorizes asset activation or a
production release.
