# Engine-port components (not a swap engine)

## Current integrated acceptance entry point

Patch 77 adds `native_topup_dispatch` and `native_exact_net_claim`, ignored
integration tests exercising patches 73-76 against a real fakechain
wallet-rpc for the first time -- everything before this was unit-tested
(pure Rust, no live RPC) only. `native_topup_dispatch` drives
`dispatch_topup` against an open buyer wallet, checking wrong-chain
rejection and that a completed dispatch's durable intent blocks a repeat.
`native_exact_net_claim` drives `claim_exact_net_with_topup` against a
recovered swap wallet (restored from spend/view keys), checking wrong-chain
rejection leaves the wallet closed and that a successful claim closes it too.

`../../tools/dex-engine-port/test_wallet_recovery.py` gained
`verify_topup_dispatch_and_claim`, called from the existing
`--native-spendable` fixture right after `verify_raw_restart_candidate`.
It mints a lock payment and a buyer-side top-up budget from the fixture's
disposable "restored" wallet (native mechanics only, matching
`verify_exact_net_relay`'s own methodology -- not authenticated protocol
negotiation or a Bitcoin-observed redemption), dispatches the top-up, runs
the two-source claim, and verifies the payout wallet receives exactly the
locked principal, the reserve-return wallet receives a positive residual,
and the recovered swap wallet ends at zero balance.

**This is the first real end-to-end run of the delayed-top-up candidate**,
executed 2026-09-20 against the full `engine_redeem_is_accepted_on_regtest`
fixture (real Bitcoin regtest negotiation via Docker/bitcoind/electrs,
feeding a real protocol-recovered XMZ key into the existing native-spendable
steps, followed by the new top-up/claim steps). It passed on the third
attempt: the first run correctly rejected a duplicate-dispatch retry (no
code defect) but a debugging pass was needed to see that the second failure
("full-proceeds payment and fee do not exactly consume lock") was the
combined `ensure!` in `check_response` failing on the fee-ceiling clause,
not the accounting clause -- the fixture's `MONZERO_TEST_CLAIM_MAX_FEE_ATOMS`
was set below the two-destination construction's actual required fee
(1_379_200_000 atoms observed against a 1_000_000_000 ceiling). Raised to
2_000_000_000 and the full run passed, including the untouched full Bitcoin
settlement fixture and every prior native-spendable/exact-net-relay/
raw-restart step. No production wallet, node, or live-trading gate was
touched; all funds were disposable regtest/fakechain. No Docker containers
were left running after the run.

Verified: `cargo check -p swap-machine --all-features --all-targets` passes;
`cargo test -p swap-machine --lib` is unaffected (91/0/2, these are
`#[ignore]`d integration tests, not part of the default `--lib` run).
`check-upstream.sh machine-test` reproduces the unit-test result; the
77-patch chain was independently replayed and reproduces the current engine
sources with zero drift. The real fixture run above is the actual evidence
for these two functions, not `cargo test --lib`.

Patch 76 adds `native_funding_rpc::exact_net_claim::claim_exact_net_with_topup`,
the final step of the delayed-top-up candidate: once the buyer's top-up
(patch 75) is dispatched and confirmed, restore the recovered swap wallet,
import both the original lock and the top-up transaction into it
(`NativeWalletSession::import_transaction`, called twice), then journal
(`ExactNetJournal::reserve` before construction, `authorize_relay` after)
and relay (`NativeTransferRelay::relay_exact_net`) the single two-source
transaction that delivers the exact quoted principal and returns unused
reserve. This composes only existing primitives (patches 65-71's exact-net
construction/journal/relay, `spendable.rs`'s restore/import/close shape);
no new construction, journaling or relay logic was added. Mirrors
`spendable.rs` closely enough to share its "least reviewed, newly designed
orchestration" caveat -- read both together.

Verified: `cargo check -p swap-machine --all-features --all-targets` and
`cargo test -p swap-machine --lib` both pass (91/0/2, unchanged --
`claim_exact_net_with_topup` has no unit test of its own, matching
`swap_wallet_spendable`). `check-upstream.sh machine-test` reproduces this;
the 76-patch chain was independently replayed and reproduces the current
engine sources with zero drift. Entirely untested beyond compiling --
needs a real fixture (funded lock, dispatched top-up, recovered wallet)
before being relied on. No GUI wiring, real fund movement, or live-trading
gate is affected.

This completes the delayed-top-up candidate's three engine-side pieces
(journal, confirmation-bound authorization, dispatch, claim) named in
`DEX_BUYER_RESERVE_CANDIDATE.md`. What remains before the "Full settlement
engine" roadmap milestone: a real fixture proving the whole path end to end
on disposable chains (funded lock -> confirmed redemption -> top-up
dispatch -> two-source claim, with restart/crash coverage at each boundary),
the v2 signed-quote schema binding these fields into authenticated
negotiation (`DEX_BUYER_RESERVE_QUOTE_V2.md`), and GUI execution approval.
None of that exists yet; treat every piece above as a source-reviewed,
unit-tested building block, not a working feature.

Patch 75 adds `native_funding_rpc::topup_dispatch::dispatch_topup`, the
orchestration entry point for the buyer's top-up leg: reserve a
`TopUpJournal`, `NativeTransferRelay::prepare` an ordinary single-destination
transfer from the buyer's own wallet to the recovered destination,
`record_prepared`, `relay_prepared`, then `record_relay`. It mirrors
`spendable.rs`'s journal/prepare/record/relay/record sequence, but the
buyer's wallet is the caller's own already-open wallet, so no restore or
lock-transaction import happens here (unlike the seller-side recovery spend).
Callers must already have run `topup_journal::authorize_recovered_
destination` (patch 74) against the same request; this function does not
re-derive or check the recovered swap key.

Implementing this exposed that patch 73's `TopUpJournal::record_prepared`/
`record_relay` took a minimal `{txid, fee, weight}` struct with no way to
check it actually corresponded to *this* journal's approved wallet,
destination or amount -- unlike `SpendJournal::record_prepared`, which
cross-checks every field of `relay::PreparedTransfer::journal_terms()`
against its own intent. Patch 75 fixes this in place: both methods now take
`relay::PreparedTransfer`/`relay::RelayedTransfer` directly and
`record_prepared` rejects a prepared transfer that doesn't match the
journal's genesis/wallet/destination/amount/account/priority, closing the
gap before anything actually calls it. This removed the
`prepared_fee_must_not_exceed_journal_approval` unit test (no public
constructor for `PreparedTransfer` outside a real RPC call, matching
`SpendJournal`'s own convention of not unit-testing `record_prepared`);
that behavior is exercised via the integration fixture instead.

Verified: `cargo test -p swap-machine --lib` passes at 91/0/2 (net -1 test
count from removing the now-untestable-in-isolation prepared-fee test, +0
net new: `topup_dispatch` has no unit test of its own, matching
`spendable.rs`). `check-upstream.sh machine-test` reproduces this; the
75-patch chain was independently replayed and reproduces the current
engine sources with zero drift. `dispatch_topup` itself is untested beyond
compilation -- it needs a fixture run against a real wallet-rpc pair before
being relied on, same as `swap_wallet_spendable`. No GUI wiring, real fund
movement, or live-trading gate is affected. Next: import the confirmed
top-up into the recovered swap wallet and run the two-source claim
(`full_proceeds::prepare_exact_net_with_top_up` + `exact_net_journal` +
`relay_exact_net`) using the resulting top-up transaction id, then a real
negotiated two-party fixture.

Patch 74 adds `bob::State4::watch_for_redeem_btc_confirmed`, alongside patch
73's journal, `native_funding_rpc::topup_journal::authorize_recovered_destination`.
The new state method mirrors the existing `watch_for_redeem_btc` but uses
`Subscription::wait_until_confirmed_with` instead of `wait_until_seen`, so it
recovers the swap key only once the redeem transaction has reached a
caller-specified confirmation depth -- the "Pinned depth required before
reserve funding; not absolute finality" requirement from
`DEX_BUYER_RESERVE_QUOTE_V2.md`. `authorize_recovered_destination` derives
the XmzAddress from the resulting `State5::xmr_keys()` and rejects unless it
exactly matches the buyer's already-approved `TopUpRequest.destination`,
so a top-up can never be journaled against the wrong address. Both pieces
reuse only existing upstream/engine primitives (`Subscription`,
`check_for_tx_redeem`, `XmzAddress::new`); no new Bitcoin observation
capability, RPC surface or relay path was added. One new unit test
(`authorization_binds_the_exact_recovered_address_and_rejects_any_other`)
covers acceptance, a substituted spend key, a substituted view key and a
network mismatch. `cargo test -p swap-machine --lib` passes at 92/0/2 and
`check-upstream.sh machine-test` reproduces this; the 74-patch chain was
independently replayed and reproduces the current engine sources with zero
drift. `watch_for_redeem_btc_confirmed` itself has no isolated unit test
(consistent with `watch_for_redeem_btc`, exercised only via the real-chain
fixtures) -- it still needs a regtest-backed fixture run before being relied
on. Still outstanding: actually dispatching the top-up transfer, importing
the confirmed top-up, running the two-source claim through it, and a real
negotiated two-party fixture; see `DEX_PROGRESS_20260919.md`.

Patch 73 adds `native_funding_rpc::topup_journal`, a durable single-use journal
for the buyer-funded delayed top-up dispatch described in
`DEX_BUYER_RESERVE_CANDIDATE.md` (the selected candidate, since sharing the
buyer's reserve into the swap lock before outcome resolution would expose it
to the seller on cancellation; see `DEX_FULL_PROCEEDS_ACCEPTANCE.md`). This
journal reserves and records the buyer wallet's own payment to the recovered
swap destination -- the top-up leg, not the final claim. It reuses the same
write-once directory-descriptor primitives as `SpendJournal`/`ExactNetJournal`
(`native_funding_rpc::spend_journal::{reserve_intent,create_record}`) but is
keyed by the buyer's own wallet address, so it can never collide with or
substitute for the seller-side lock/claim journals for the same trade; a test
confirms both journals coexist for one lock without interference. It exposes
no relay or construction capability itself -- callers still use
`preview_exact_net_with_top_up`/`prepare_exact_net_with_top_up` (patches
65/67) to construct the two-source claim once both sources are confirmed.
Four unit tests cover self-destination rejection, the private-directory/
write-once receipt requirement, the prepared-fee ceiling, and the buyer/seller
identity separation. `cargo test -p swap-machine --lib` passes at 91/0/2
(93 total, 2 pre-existing ignored) and `check-upstream.sh machine-test`
reproduces this. No top-up dispatch, source-funder proof, GUI wiring or
Bitcoin-outcome authorization is provided by this patch alone; see
`DEX_BUYER_RESERVE_CANDIDATE.md` for what remains (steps 2-6 of the delayed
top-up candidate: Bitcoin-outcome observation binding, actual dispatch/import,
the two-source claim using this journal's recorded top-up, and a real
negotiated two-party fixture).

Patch 72 adds a consuming, journal-backed exact-net relay capability.
The isolated fixture confirms the complete principal at one destination and
unused reserve at the other, zero source balance, and fresh-process read-only
reconciliation without a receipt. These are locally funded test wallets, not
the complete negotiated buyer-top-up protocol. Live trading remains gated.
The fixture also drops a real successful relay reply, requires an unknown native
outcome and confirms the same journaled payment in a fresh process.
Pre-dispatch crash recovery remains outstanding; historical records do not
authorize automatic retries.

The raw-byte restart experiment confirms that two fresh processes can submit
one identical transaction, but the source wallet loses destination bookkeeping.
It is not a native recovery implementation. See
`../../DEX_EXACT_NET_RESTART_DESIGN.md` for evidence and the unresolved design boundary.
The encrypted-metadata comparison fixture additionally requires Python
`cryptography` (AESGCM), alongside `requests`. It uses only a temporary parent-held
key, not production key storage; native restored relay remains disabled.
The rebuilt wallet RPC now validates saved output indexes against the open
wallet's key images before metadata relay. Both funded fixtures reject metadata
on a populated wrong wallet without broadcast, then succeed on the correct one.
Evidence and remaining recovery limitations are in the restart design above.
If overriding TMPDIR for these Linux fixtures, use a short private absolute path:
the peer socket adds approximately 52 bytes and must fit the Unix socket limit.

The following patch summaries describe their state when introduced.

Patch 71 adds read-only exact-net reconciliation against trusted local RPC data.
Unit tests cover valid/altered two-destination confirmation evidence; the real
fixture checks that unbroadcast preparation remains unresolved. Real positive
confirmation after controlled relay is still outstanding.

Patch 70 adds read-only historical exact-net record loading against retained
caller terms, including fresh-process fixture coverage. Loaded preparation does
not imply broadcast, confirmation, retry permission or private transaction recovery.

Patch 69 adds write-once exact-net intent and prepared-accounting records,
sharing the original lock reservation with the fixed-principal mode.
New-format restart reconciliation and relay authorization remain unfinished.

Patch 67 adds a private prepared exact-net transaction type with public accounting
but no relay API. The two-source fixture prepares and drops it, checking that
no transaction broadcasts. Patch 69 adds public journaling, not a relay path.

Patch 65 adds an explicit two-source exact-net preview for a delayed buyer top-up.
Patch 66 adds real-fakechain construction coverage using two disposable payments
and checks no balance/pool changes. This is not actual protocol top-up funding,
authentication of its buyer or reserve-return settlement. No live relay is enabled.

Patch 64 adds a non-broadcasting exact-net XMZ construction experiment to both
funded outcomes: the recipient amount remains unchanged and fees come only from
an explicit reserve-return output. This is not yet signed reserve funding or
full settlement; the subsequently relayed recovery transfer remains partial.
See `../../DEX_FULL_PROCEEDS_ACCEPTANCE.md` for the user's exact-net requirement.

The dated sections below record the incremental port history.
For the current native-engine/Qt bridge, use the consolidated isolated runner
from the core checkout:

```sh
python3 monzero-gui/tools/dex-engine-port/verify_native_settlement.py
```

It verifies ordered patch reconstruction, runs all engine library tests including
Qt interoperability and compile-fail documentation tests, rebuilds the settlement test, selects its executable from
Cargo's machine-readable build output and runs funded redemption and refund
sequentially on disposable Bitcoin regtest/XMZ fakechain services.
It requires the existing local Rust toolchain/cache, Docker, built core binaries,
Qt quote driver and Qt recovery receiver; it neither installs dependencies nor
contacts production wallets or nodes.
Inherited `MONZERO_TEST_*` variables are removed before explicit fixture settings
are supplied, so stale test keys/paths/modes cannot silently select a prior run.
Cargo/Rust overrides, compiler wrappers and subprocess injection settings are
also removed; Docker is explicitly pinned to the local `/var/run/docker.sock`.
Both `RUSTC` and `RUSTDOC` are pinned to the project-local toolchain.
This runner rejects a missing local socket rather than falling back to a remote
Docker context, and does not change the user's saved Docker configuration.

A pass does **not** mean live readiness: the fixtures still transfer partial XMZ
proceeds, and production execution, full restart ownership, full-proceeds fee
semantics and deployment approval remain unfinished.
Current details are in `../../DEX_NATIVE_TRADE_INTEGRATION.md`.

The full-proceeds preview experiment additionally constructs, but never relays,
a candidate that consumes the exact lock principal as recipient payment plus
native fee, with all-and-only lock input checks.
The funded fixtures exercise this before their existing partial payment.
This is not full-proceeds settlement: quote gross/net policy, durable preparation
and actual relay/reconciliation integration remain separate work.
`upstream-full-proceeds-preview.patch` is patch 50 in the reconstruction sequence.

Patch 51, `upstream-recoverable-execution-context.patch`, retains the signed
reservation and policy in v2 execution attempts and adds historical read-only
reload without reopening an execution claim.
Legacy and damaged records continue blocking new attempts.
This is commercial-context persistence, not complete application/protocol recovery.

Patches 52–53 add v3 process ownership and guarded pending-checkpoint resume.
V2 remains inspection-only; a live claim or recovery lease excludes other
cooperating recovery processes on the same private attempt record.
The funded child/parent handoff uses this guard and retained signed terms,
without changing payout policy or authorizing a fresh execution attempt.

Patch 54 adds private, write-once pending-checkpoint storage anchored to the
execution directory descriptor, with file and directory synchronization before
returning a digest.
Recovery requires that digest from independently retained trusted context and
checks the complete envelope before decoding and rechecking signed terms.
The funded fixtures use this storage under the existing ownership guard.
Checkpoints contain plaintext secrets; permissions are not encryption.
Durable application-level digest retention, key management and latest-state
reconciliation remain unfinished; a storage error never authorizes deletion
or a new execution attempt.

Patch 55 separates in-memory checkpoint preparation from persistence.
`PreparedCheckpoint` borrows its original execution claim and exposes its digest
before writing anything, enabling a future coordinator to retain trusted
metadata first and then persist that exact preparation.
It is consumed by persistence and does not expose secret bytes or implement
Debug/Clone/serialization.
The compatibility `save_pending` API does not establish this ordering.
This does not implement the durable manifest itself or make torn writes retryable.

Patch 56 adds `CheckpointPinStore`, an explicitly trusted, private local metadata
directory distinct from the checkpoint directory.
`persist_with_pin_store` synchronizes the write-once swap/quote/digest record
before creating the checkpoint.
Recovery checks that identity and loads the checkpoint against its retained hash.
The funded recovery child now reloads the digest from disk, not an environment
variable supplied by its parent.
This is local persistence, not same-user tamper resistance, rollback protection,
encryption, complete application recovery or authorization to retry a torn write.
Historical commercial context still comes from the separate trusted fixture
context; production discovery/key management and pre-funding states remain open.

Patch 57 saves Bob's negotiated `State3` before signing or broadcasting the
Bitcoin lock in the signed funded fixture.
It uses a distinct pre-lock envelope and filename with the existing private
checkpoint/pin ordering; pending-state storage does not overwrite it.
Recovery checks the pinned bytes, signed economics and expected lock txid.
The refund fixture uses this earlier checkpoint to construct its actual refund.
Loading it does not authorize funding or prove broadcast/confirmation status.
The signed lock transaction, wallet input reservations, Alice's pre-funding
state and whole-application cold-start recovery remain separate requirements.

Patch 58 adds Alice's historical economic-state validation and invokes it when
her isolated fixture process reloads its negotiated-state checkpoint.
Fresh admission and historical recovery share economic checks without making
recovery depend on quote expiry.
The funded fixture now saves, kills and restarts Alice before Bitcoin signing
or either swap asset's funding, then repeats fresh quote preflight.
Alice's storage here remains the test-only checkpoint mechanism, not a new
production pin/encryption/ownership implementation or whole-app recovery.

Patch 59 moves signed Alice fixture recovery onto the engine's guarded
checkpoint/pin store using a distinct Alice envelope and file.
Alice owns her own execution namespace; the live claim excludes recovery, and
restarted processes hold a recovery lease while loading and using her state.
Signed restart metadata now contains only public association/sequence data;
the original secret state is immutable and is not recopied for each restart.
The explicit unsigned fixture retains its old test-only format and cannot load
a signed association through that path.
This still depends on trusted parent-supplied context and private plaintext
storage, not whole-app restart, secret encryption or live deployment approval.

Patch 60 retains the exact native-witness Bitcoin lock submission before RPC.
Preparation reads the previously pinned pre-lock checkpoint, checks the lock
txid and every non-witness transaction field, and requires witnesses to be present.
The stored consensus bytes preserve the witnesses too.
Recovery owns a lease, reloads the intent and checks it against the pre-lock state;
the funded fixture compares it with the mined transaction.
This does not verify witness scripts, implement broadcast/retry, or persist a
network-acceptance receipt. A missing receipt never proves the lock was rejected.

Patch 61 adds read-only submission observation under a recovery lease.
It reloads the retained intent, checks the expected genesis and matching sampled
chain tips around lookup, and compares the entire transaction including witnesses.
Results distinguish exact observation from non-observation; neither permits retry.
RPC failure, a wrong chain, changed tip or different bytes remains an error.
The disposable Bitcoin harness now enables txindex for post-mining lookup;
its strict RPC adapter does not reinterpret node errors as absence.
This is not confirmation, spendability, finality, an outcome journal or a
lost-acknowledgement test.

Patch 62 injects a lost acknowledgement at the funded fixture's client boundary:
the real node accepts the lock but the caller receives a deliberate error.
The fixture does not retry; it mines that transaction and the restarted child
uses retained submission data and read-only reconciliation before proceeding.
This is not TCP fault injection or whole-application crash recovery.

Patch 63 persists historical Bitcoin observations under recovery ownership.
Each caller-retained observation ID has a write-once record and pin binding the
trade, txid, wtxid, sampled genesis/tip and observation result.
Loading returns `HistoricalBitcoinObservation`, never a fresh observation or
permission to fund/retry; there is no latest-record selection algorithm.
The second funded recovery child reloads the first child's record from disk.
This is an audit history, not a broadcast acceptance receipt or finality proof.

## Historical initial boundary

`xmz_amount.rs` is the first standalone Rust boundary for the approved BTC/XMZ
engine adaptation. It is not yet linked into upstream eigenwallet or the GUI.
No signing, wallet loading, broadcasting or upstream build scripts are involved.

Run from the GUI repository with a Rust compiler available on PATH:

```sh
rustc --edition=2021 --test tools/dex-engine-port/xmz_amount.rs -o build-dex-private/xmz-amount-tests
build-dex-private/xmz-amount-tests
```

Verification status (2026-09-12): compiled with Rust 1.95.0; all four tests passed,
zero ignored. The compiler was absent from PATH, so Kali packages `rustc`,
`libstd-rust-dev` and `libstd-rust-1.95`, version `1.95.0+dfsg1-2`, were downloaded
with APT and extracted into the core build directory, without system installation.
The existing system LLVM 21 library satisfies the runtime dependency.

Reproduce with that project-local compiler from the GUI repository:

```sh
LD_LIBRARY_PATH=../build/dex-rust-toolchain-1.95.0/sysroot/usr/lib/x86_64-linux-gnu \
  ../build/dex-rust-toolchain-1.95.0/sysroot/usr/bin/rustc \
  --edition=2021 --test tools/dex-engine-port/xmz_amount.rs \
  -o build-dex-private/xmz-amount-tests
build-dex-private/xmz-amount-tests
```

These are module tests, not upstream integration tests or independent vectors.
The toolchain is local build infrastructure and must not be bundled as a wallet
runtime requirement. No production process or wallet was accessed for testing.

The type uses XMZ's 11 decimal places and preserves raw `u64` atoms across the
wallet boundary. Bridge atom counts are decimal strings, not JavaScript numbers.
Parsing rejects signs, whitespace, leading whole-part zeroes, exponent notation,
extra precision and overflow. Formatting trims fractional zeroes. No conversion
to/from an XMR amount type is supplied: implicit cross-asset conversion would hide
the upstream 12-decimal mismatch. Checked arithmetic rejects overflow/underflow.

This deliberate strict input contract is for a future versioned engine interface;
do not reinterpret historical V6 quotes using it. Integration must replace every
upstream display/rate boundary and test actual wallet/RPC atom counts, not just
rename `Amount` or change one constant. The core amount definition remains the
authority; recheck `src/cryptonote_config.h` when updating the pinned core.

## Native wallet API contract

`wallet_api_contract.cpp` checks signatures against the actual core public wallet
header, without linking or running any wallet operation:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only -I ../src \
  tools/dex-engine-port/wallet_api_contract.cpp
```

Passed on 2026-09-12: integer balances, scan, restore height, refresh, persistence,
transaction-key check and key-based wallet creation declarations are available.
This is compile-time API evidence, not runtime/ABI compatibility or successful
restoration. `Monero::MAINNET` in the unevaluated expression is a C++ enum used by
the fork; this check neither selects nor connects to a running mainnet.

## Real fakechain wallet restoration

Run from the core repository (not the GUI directory):

```sh
python3 monzero-gui/tools/dex-engine-port/test_wallet_recovery.py \
  --daemon build/bin/monzerod --wallet-rpc build/bin/monzero-wallet-rpc \
  --temp-parent build
```

Passed on 2026-09-12 with core `58f10f796`: create a disposable wallet, mine one
worthless block, restore from its test spend/view keys, verify address and raw
balances, close/reopen the restored wallet and verify balance persistence.
Wallet RPC was rebuilt using target `wallet_rpc_server`; the previously present
binary was older. No upstream swap engine is integrated by this test.

The fixture pins fakechain genesis, verifies zero peers, and uses private fresh
data with loopback-only RPC and an offline daemon. RPC authentication is disabled
only for these short-lived, worthless local fixtures. Other processes on the same
host could access them; run on a trusted development host. Never use production
wallets or reuse this configuration for public deployment.

Initial execution rejected fakechain HF16 at height 1 because the wallet applies
the regular network fork schedule. After verifying the owned fakechain, the test
supplies `--allow-mismatched-daemon-version`, which in this core also bypasses that
wallet schedule check (`wallet2.cpp`). No production setting was changed. This
does NOT verify production hard-fork compatibility. The temporary processes stop
before the automatically created test wallets, keys and chain data are deleted.

The extended fixture also passed: transfer exactly 2 XMZ to a fresh address,
restore that address into a wallet with a later restore height, import the
transaction with `scan_tx`, verify the full unlocked amount, rescan to check
idempotence, and sweep into a third test wallet. The destination balance equals
the reported sweep amount, and sweep amount plus native fee equals the 2 XMZ
input. No application trading fee is charged by this fixture.

The fixture now also cleanly terminates wallet RPC after saving the imported
transaction, starts a new wallet-RPC process with the same isolated directory,
opens the saved wallet without re-supplying spend/view keys, verifies its unlocked
balance and successfully sweeps. This passed on 2026-09-12. It establishes wallet
state persistence across a graceful process restart, not crash durability at
arbitrary write boundaries or recovery of an upstream swap engine's state.

The initial 71-block chain failed transaction creation with insufficient eligible
decoys. Increasing the fixture to 131 blocks supplied enough mature outputs;
ring requirements were not reduced. Extra confirmation blocks unlock the normal
transfer and sweep. All transfers are confined to this disposable fakechain.

Still untested here: a jointly controlled swap lock, adaptor-based secret exchange,
adversarial refunds, reorgs and swap-engine process recovery. A normal one-party
transfer/scan/sweep validates wallet mechanics, not atomic swap settlement.

## Isolated upstream build preparation

The nonrecursive checkout at `build/eigenwallet-engine-port` in the core repository
is pinned to `0e17c7f7cd8f0657af176c8852aa4c9949586051`. Its wallet submodules
are deliberately not initialized or linked to the working Monzero source.
Upstream's wallet build applies source patches; it must not run against our
existing core worktree.

Local Cargo 1.95.0 now runs from `build/dex-rust-toolchain-1.95.0/sysroot/usr/bin`
with that sysroot's `usr/lib/x86_64-linux-gnu` in `LD_LIBRARY_PATH`.
Cargo, libgit2-1.9 and libllhttp9.4 were downloaded using configured APT metadata
and extracted locally, not installed system-wide. Their SHA256 values matched
the package metadata. Upstream requests Rust 1.90.0; this local 1.95.0 toolchain
is an investigation tool, not a reproduced upstream release environment.

`cargo metadata --no-deps --format-version 1 --locked --offline` successfully
parsed all 28 workspace packages, including the `swap_core` library target.
The subsequent `cargo check -p swap-core --locked -j 2` passed on 2026-09-12
in 3m49s, with the upstream checkout and lockfile unchanged. The environment used
the local sysroot's `rustc` via `RUSTC`, a separate core `build/dex-cargo-home`
via `CARGO_HOME`, `CARGO_INCREMENTAL=0`, and `CARGO_PROFILE_DEV_DEBUG=0`.
This establishes a compilation-check baseline for the unmodified upstream
library and its dependencies, not linked binaries, executed tests, XMZ support
or native wallet integration. The next verification is the library test suite.
Approximately 5.9 GiB remained on the workspace filesystem at preparation time;
check space before dependency downloads or compilation. Do not run a full GUI or
native wallet build blindly on that remaining capacity.

Repeat the local baseline with `bash monzero-gui/tools/dex-engine-port/check-upstream.sh`
from the core root, or pass `test` for the upstream library tests. The helper
checks the pinned HEAD, local compiler availability and at least 3 GiB free
before starting. The space check is a starting threshold, not a disk quota;
monitor long builds. It does not install tools, change source, or enable trading.

The first upstream `cargo test -p swap-core --lib --locked -j 2` completed with
62 passed and 8 failed (0 ignored) on 2026-09-12. Seven failures reached missing
test-wallet implementations of `new_address` or `network` in
`bitcoin-wallet/src/wallet.rs:3022,3088`. The remaining
`monero::primitives::tests::parse_monero_overflows` rejected the overflowing
amount, but its error downcast assertion panicked. These are not evidence of
successful settlement or of accepted overflowing amounts. The upstream checkout
remained clean. Repairing the test harness and checking the error type contract
are required before treating this as a green baseline.

After the local `upstream-test-harness.patch`, all 70 library tests passed
(0 failed, 0 ignored; 23.12s execution). Four test-wallet methods delegate to
the existing wallet implementation: address creation, network, transaction
construction and signing. Broadcasting remains unimplemented in that fixture.
The overflow assertion checks the parser's actual error instead of downcasting
to an unrelated local type; the overflowing amount must still be rejected.
The patch is saved beside this README and its reverse applicability to the
tested checkout was verified. On a fresh pinned checkout, review it and apply
with `git apply` before using `check-upstream.sh test`.
This is a repaired upstream library-test baseline, not an end-to-end XMZ swap
test, a full-workspace build or an independent security audit.

`upstream-xmz-amount.patch` adds the exact amount boundary as the public
`swap_core::xmz::XmzAmount` type. Apply it after the harness patch to the pinned
checkout. Its reverse applicability was verified. All 74 library tests passed
after this addition (0 failed, 0 ignored; 21.96s execution). The four added tests
exercise 11-decimal parsing, full-u64 string round trips, invalid precision and
checked arithmetic inside the engine library build. Existing Monero amount and
protocol types are intentionally unchanged; the swap state machine does not yet
use this new type. This is a typed port boundary, not completed XMZ settlement.

The engine patch now also contains `XmzBtcRate`, an exact ratio of quoted XMZ
atoms to Bitcoin satoshis. It supports sub-satoshi-per-XMZ prices, rounds
purchasable satoshis down and required XMZ atoms up, rejects zero rate components
and out-of-u64 results, and uses u128 intermediate products. It adds no fees.
All 76 library tests passed after this change (19.97s); the new tests include
rounding-minimality checks and full-width arithmetic. This API is not yet wired
into the GUI quote path or signed swap negotiation. The standalone
`xmz_amount.rs` remains the original dependency-free amount probe; the rate API
resides in the engine patch and uses its actual Bitcoin amount type.

Use `check-upstream.sh machine-test` to build and run the `swap-machine` library
tests. These exercise the two-party message exchange and signing with test
wallets, not broadcast settlement. The first run completed with 12 passed and
1 failed: `calculate_transaction_weights` requests a signed full refund without
the necessary signature (`swap-machine/src/lib.rs:119`). The dedicated early
refund construction/signing test passed. Investigate whether the weight fixture
selects an incompatible refund mode before changing any protocol behavior.

Resolved locally with `upstream-refund-fixture.patch`: the original fixture
negotiated nonzero amnesty (partial refund) and requested a full refund signature,
which that mode intentionally does not provide. The weight fixture now runs
both zero-amnesty/full-refund and nonzero-amnesty/partial-refund configurations.
Each signs its negotiated refund and asserts that signing the other refund mode
fails. All 14 state-machine library tests passed (0 failed, 0 ignored; 6.69s)
on 2026-09-12. No signature checks or production state transitions changed.
The saved patch's reverse applicability to the tested checkout was verified.

The refund fixture additionally verifies that extraction from either signed
refund yields a spend key whose public key matches the joint lock request.
Removing the witness fails extraction. Altering an output while preserving the
valid signature still yields the same key: these extractors validate signature
evidence against their stored digest, not the surrounding transaction or chain
confirmation. An initial expectation of rejection was incorrect; no production
extractor was changed to force that expectation. Integration must not treat
successful extraction as proof of a valid or confirmed Bitcoin transaction.
All 14 tests passed with these assertions (6.59s). No native-chain lock was
funded in this test.

Redeem-side coverage was subsequently added to the same saved patch. It now
also contains a production-path refactor: `State4::check_for_tx_redeem` delegates
to `recover_from_redeem`, which rejects a wallet response with the wrong txid
before recovering the key. Tests exercise this actual helper, check the recovered
public key against the joint lock, and reject altered outputs and missing
witnesses in both negotiated refund configurations. All 14 tests passed (6.40s).
The txid check is response-identity validation, not proof of confirmation or full
Bitcoin script validity. This patch is no longer test-fixture-only and requires
integration testing before deployment; no running service uses it yet.
The port must update the amount carried through Alice/Bob states and their
persisted representation together: Alice's `State0::new` currently accepts
`monero::Amount`, and `State3::lock_xmr_transfer_request` returns that amount
with joint public keys. Adding an XMZ constructor that merely casts into this
old representation would not isolate networks or preserve protocol identity.

Address-port inspection found another compatibility constraint: the pinned
`monero-address` dependency supports configurable prefixes, but serializes its
prefix as a single byte. Monzero's `base58::encode_addr` serializes a varint.
Mainnet 86/87/88 and testnet 111/112/113 fit one byte, whereas stagenet
131/132/133 require two-byte varints. A mere custom-prefix alias is therefore
insufficient for all Monzero networks. Do not change the chain's existing
prefixes to accommodate this dependency; adapt the address boundary and verify
against the native wallet instead.

`upstream-xmz-address.patch` adds a distinct `XmzAddress` type after the amount
patch. It preserves core prefix bytes and canonical varint encoding, validates
keys/types with the pinned upstream parser, bounds input length, and rejects
wrong networks and unsupported featured addresses. Its private normalized
Monero address must not be exposed as a payment destination. The only lockfile
change adds the already pinned `monero-base58` dependency to `swap-core`; no
version changes. Reverse patch applicability and compilation were verified.
New round-trip tests pass for all three networks and standard/integrated/subaddress
types, including rejection of the incorrect one-byte stagenet encoding.
Cross-implementation encoding vectors now pass: `native_address_vectors.cpp`
uses the current core prefix constants and native `base58::encode_addr` with
fixed public Ed25519 base-point keys. The nine outputs are stored in the engine
patch and checked for exact encoding and parsing by Rust, across all networks
and standard/integrated/subaddress formats. These public test addresses must
never receive funds. This verifies address encoding, not native-wallet key or
subaddress derivation; swap-state wiring remains pending.

Reproduce the native vectors from the core root:

```sh
c++ -std=c++17 -I src -I contrib/epee/include \
  monzero-gui/tools/dex-engine-port/native_address_vectors.cpp \
  src/common/base58.cpp build/src/crypto/libcncrypto.a \
  -o build/native-address-vectors
build/native-address-vectors
```

Native wallet RPC validation also passed for all nine engine vectors, including
network/type identification and rejection of damaged checksums. Reproduce with:

```sh
python3 monzero-gui/tools/dex-engine-port/test_wallet_recovery.py \
  --daemon build/bin/monzerod --wallet-rpc build/bin/monzero-wallet-rpc \
  --temp-parent build \
  --address-vectors build/eigenwallet-engine-port/swap-core/src/xmz_address_vectors.txt
```

The combined run also passed restoration, transfer import, wallet-RPC restart
and sweep checks. Only owned offline fakechain processes and disposable wallets
were used; they stopped and their temporary data was removed. Address validation
uses `allow_openalias=false`, so no DNS alias resolution is requested. This does
not yet fund an engine-negotiated shared lock or perform a cross-chain swap.

Run `bash monzero-gui/tools/dex-engine-port/verify-upstream-patches.sh` from
the core root to reconstruct the ordered patch series in a separate Git index
and compare it against the engine worktree, including the added files. This
passed on 2026-09-12. It leaves the normal index, worktree and branch untouched
and retains a small audit index under `build/dex-patch-audit.*` for inspection.
This is source reconstruction evidence, not independent binary reproduction.

## Engine-key/native-wallet interoperability

The engine-key extension passed. Supply `--engine-test-binary` with the current
`swap_machine` test executable printed by Cargo to the Python recovery fixture.
It runs the exact two-party full-refund-mode fixture, exporting the joint XMZ
address and redeem-recovered spend/view keys into a fresh mode-0600 file inside
the private temporary directory. Output is suppressed and execution is bounded
to 60 seconds. The native fakechain sends 2 XMZ to that joint address, restores
using the recovered keys, imports the transaction, restarts wallet RPC and sweeps
successfully. The keys and wallets are deleted with the fixture.

This proves the recovered keys work with the native Monzero wallet, not atomic
settlement: Bitcoin transactions remain mocked, recovery occurs before native
funding, and no Bitcoin transaction is broadcast. All 14 state-machine tests also passed
after adding the opt-in export (6.37s). The saved patch sequence reproduces the
tested engine source, including this extension.

The fixture now carries 2 XMZ as exact atoms through the two-party state and
exports the resulting lock-request amount with the recovered keys. Python
validates the canonical positive u64 atom string and uses it for funding instead
of a separately chosen amount. The native transfer/import/restart/sweep run
passed again, as did all 14 state-machine tests (6.18s). Conversion into the
legacy Monero-named atom container is explicitly fixture-only; production XMZ
protocol identity, persisted-state separation and correct funding/reveal order
remain required. No application trading fees were introduced by this test.

The local fixture ordering was subsequently improved and verified: the engine
exports only the joint address and amount, then waits (bounded to 120 seconds)
before constructing the redeem signature. Python funds that address, mines,
checks the sender wallet's outgoing amount and at least ten confirmations, and
only then creates a release marker. The recovery-key file must not exist before
release. The engine completes, and the native restore/import/restart/sweep path
passes with those subsequently recovered keys. All 14 machine tests also pass
(6.21s). Earlier notes about recovery preceding native funding describe the
previous fixture, not this revised run.

This is a trusted local test coordinator, not a production confirmation oracle
or settlement gate. Bitcoin remains mocked, no BTC lock is funded on a chain,
and the engine's internal Monero proof is still a placeholder. A public swap
cannot rely on a release-marker file or the sender's wallet assertion.

Receiver-side verification now also passes in the fixture: the pre-release
owner-only file includes the shared view key, not the spend key. A new native
view-only wallet restores from the joint address/view key, scans the incoming
transfer and checks its exact amount, positive block height and at least ten
confirmations. The recovery-key export must remain absent until that receiver
check finishes. The subsequent recovery/restart/sweep succeeds. This is a
separate wallet view, not an independent node: both wallets use the owned offline
fakechain daemon. The release marker is still test coordination, and Bitcoin
remains mocked. All 14 machine tests passed (6.56s), and patch reconstruction
was verified for this source revision.

## Bitcoin integration baseline

`cargo test -p bitcoin-wallet --test integration --no-run --locked -j 2`
compiled successfully (47.31s) using the local toolchain environment. It did
not run tests, pull Docker images or start containers. The target covers wallet
funding/sync and broadcasting via Bitcoin Core plus Electrum. Its default
images are `coblox/bitcoin-core:0.21.0` and `vulpemventures/electrs:v0.16.0.3`.
`upstream-bitcoin-integration.patch` removes a harness override to `latest`;
the image tag still needs digest verification before reproducible execution.
The harness uses fixed test RPC credentials and automatic port publishing.
Restrict it to an isolated network with loopback host bindings before running;
compilation alone does not validate real-chain wallet behavior. Existing Docker
images and services were left unchanged. The patch reconstruction script now
includes this fifth patch and passes.

The fifth patch now creates a uniquely named internal Docker bridge with default
host binding `127.0.0.1`, verifies those properties through Docker inspect, and
owns its cleanup after container teardown. The focused
`harness::isolated_network_configuration` test passed (0.39s); Docker's network
list afterward contained only the original bridge/host/none networks. No
containers or images were started/downloaded for that test. This verifies network
configuration and cleanup, not actual container port exposure or wallet funding;
inspect published bindings when running the complete integration tests.

Execution preparation found the declared electrs tag absent from the registry.
The fifth patch now pins available images by digest: Bitcoin Core 0.21.0 at
`sha256:a223f25bff9fa4bf54fbfbc0fe5ba685efabd24c22e20f4f7a323312d3cd822f`
and electrs at `sha256:0b7d01aafbb432a0042529ded7c9de8057c5ca65e0d2aaded4a0e2bd6d479ae7`.
Both were downloaded; the latter reports Electrum Rust Server 0.4.1 in an
isolated no-network version check. These older images are test dependencies,
not production recommendations or independently audited binaries.

The funded-wallet test started Bitcoin Core but failed obtaining its published
RPC port, including after explicit expose-port declarations were added. No
wallet funding test has passed. Both attempts cleaned up their containers and
networks. Next investigate direct isolated bridge-IP connections rather than
weakening network isolation to obtain host publishing.

The harness now connects to the containers' internal bridge addresses and known
RPC ports instead of requesting missing host-port mappings. This path is for a
local Linux Docker daemon; remote Docker and Docker Desktop connectivity are not
validated. The receive/sync integration test passed (15.61s) after the change.
Inspection of both running containers during the subsequent suite showed empty
host port bindings and empty published-port maps. The ordered saved patches
reproduce the modified engine checkout. These are worthless Bitcoin regtest
transactions, not mainnet transactions or a completed BTC/XMZ atomic swap.

The complete `bitcoin-wallet` integration target subsequently passed: 4 passed,
0 failed, 0 ignored in 62.60s, run serially with `-- --test-threads=1`. Coverage:
isolated network configuration, receive/sync, sign/broadcast/confirmation, and
subscription idle cleanup. The deprecated `ExtendedPrivKey` alias and unused
`with_tag` helper still produce warnings. This does not establish full-workspace
compilation, refund settlement, crash recovery, or production readiness.

## Engine-generated Bitcoin settlement on regtest

`upstream-bitcoin-settlement.patch` adds `swap-machine/tests/bitcoin_settlement.rs`
and test dependencies already pinned in the workspace lockfile (no dependency
versions changed). Apply it sixth, after the Bitcoin harness patch. The ordered
patch reconstruction check passes, including this new integration-test source.

Using the same local Cargo environment, run:

```sh
cargo test -p swap-machine --test bitcoin_settlement --locked --offline -j 2 -- --test-threads=1
```

Passed on 2026-09-12: 3 tests, 0 failures, 0 ignored in 43.33s. Two tests negotiate
the engine's actual two-party messages using separately seeded regtest wallets,
sign and broadcast the funded lock, and exercise either redeem or timed
cancel/full-refund. The third checks the shared harness network configuration.
Bitcoin Core rejects premature cancellation with `non-BIP68-final`; after mining
the configured delay it accepts cancellation and full refund. Exact resulting
wallet balances account for lock/redeem or lock/cancel/refund fees. Both paths
retrieve the mined transaction through Electrum and recover a joint spend key
whose public key matches the negotiated native-chain lock request. No private
keys are printed or exported. The existing 14 machine library tests also passed
(6.91s). Unused-crate warnings remain because library and integration targets
use different subsets of package dependencies; no full-workspace build is claimed.

This is Bitcoin-side settlement evidence, not an atomic BTC/XMZ swap. The native
transfer proof is explicitly bogus and XMZ funding is not performed in these
tests. The real fakechain wallet fixture above has not yet been joined to this
regtest settlement fixture. Partial refunds, punishment, adversarial reorgs,
durable swap restart, production network identity, app fees and GUI execution
still require integration work. Mainnet trading remains disabled.

## Combined Bitcoin regtest / XMZ fakechain fixture

The Bitcoin settlement test now supports the same opt-in private file handoff as
the earlier machine fixture. After confirming the Bitcoin lock, it exports the
joint XMZ address, exact atom count and view key (not the spend key), then pauses.
The native fixture funds that address and verifies the amount and confirmations
with a view-only wallet before releasing Bitcoin settlement. Only after the
mined redeem/refund is retrieved through Electrum does the engine export the
recovered spend key. Export files are create-new, owner-only, never stdout.

The combined redeem run passed on 2026-09-12:

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_wallet_recovery.py \
  --daemon build/bin/monzerod --wallet-rpc build/bin/monzero-wallet-rpc \
  --temp-parent build \
  --engine-test-binary build/eigenwallet-engine-port/target/debug/deps/bitcoin_settlement-4cd3b52e123cefaa \
  --engine-test-name engine_redeem_is_accepted_on_regtest
```

Use the actual executable path printed by Cargo if its hash changes. Run from
the core root. The test validates a 2-XMZ fakechain payment to the negotiated
joint address, Bitcoin redemption, recovery from its observed signature, native
transaction import, wallet-RPC restart, and successful native sweep. This joins
real test-chain transactions on both sides; neither side uses real-money funds.

The release-file coordinator is trusted test infrastructure, not a production
protocol or cryptographic funding proof. The engine's native transfer-proof field
still contains an explicit mock, and wallet amounts still use the documented
fixture-only legacy raw-atom boundary. This success does not prove adversarial
atomicity, restart of the swap engine, reorg handling or live XMZ fee enforcement.
No live gate, website download or production wallet was changed.

The combined cancellation/full-refund run also passed on 2026-09-12 with the
same command and `--engine-test-name engine_cancel_and_full_refund_are_accepted_on_regtest`.
XMZ was funded and observed before release; premature Bitcoin cancellation was
rejected, the mature cancel/refund restored Bob's BTC minus transaction fees,
and the refund-derived joint key successfully recovered and swept the XMZ funds
after a native wallet-RPC restart. This is cooperative test coordination and
full-refund coverage only, not partial-refund/punishment or malicious-peer proof.

### Actual native transfer evidence in the combined fixture

The combined fixture now replaces the placeholder engine transfer proof with the
actual XMZ transaction hash, transaction key and observed funding height. It waits
in the pre-native-lock state until receiving a complete bounded JSON record.
The record binds the pinned fakechain genesis, negotiated destination, canonical
raw amount, transaction fields, positive height and at least ten confirmations.
The native wallet checks the transaction key against the destination and amount
before the record is atomically published. Negative native checks with a different
key and destination must each report zero received. Transaction keys remain in
private temporary files and are not logged.

The focused `native_funding_rejects_mismatched_evidence` test passed: wrong chain,
destination, noncanonical amount, malformed hash, zero key, zero height,
insufficient confirmations, unknown fields and oversized records are rejected.
The combined redeem/recovery/restart/sweep run passed again with these checks.
Standalone Bitcoin-only tests still use an explicit native-proof mock. The
combined record is trusted local fixture evidence, not a peer-authenticated proof;
the production engine still needs its own native wallet verification boundary.
This supersedes the earlier statement that the combined run always carries a
mock transfer proof. Patch reconstruction continues to pass.

The combined cancel/full-refund/recovery/restart/sweep run also passed with the
actual native transaction evidence and both wrong-key/wrong-destination checks.
Both combined paths stop their disposable processes and remove temporary wallet
data; the final Docker check showed no running containers or test networks.

### Engine-owned funding verifier

`upstream-xmz-funding.patch` adds the reusable `swap_machine::xmz_funding`
verification boundary (apply seventh). Its observer interface must be backed by
caller-owned trusted native wallet/chain data, never a peer's claimed result.
The verifier binds the exact transaction, XMZ address and atom count; requires a
mined payment with confirmation depth consistent with the native tip; and checks
the pinned genesis and unchanged tip hash/height before and after observation.
Normal chain growth during the check also fails closed, requiring a fresh attempt.
Its `VerifiedFunding` result has private fields and is deliberately not a
serializable everlasting authorization: revalidation is needed after delays or
restarts. Sixteen machine library tests passed (6.56s), including positive funding
and negative amount/address/txid, pool, height/depth, backend-failure and chain-change
cases. Saved patch reconstruction passes for all seven patches.

The combined Bitcoin test now invokes this library verifier before entering the
native-locked state. Its read-only test adapter calls the owned daemon and
view-only wallet itself: genesis/tip headers, `check_tx_key`, incoming transfer
details and wallet address. Responses are bounded to 1 MiB, requests time out at
15 seconds, proxies and redirects are disabled, and endpoints are fixed loopback
ports supplied by the fixture launcher rather than the peer/evidence record.
The combined redeem/recovery/restart/sweep run passed through this boundary.
The native observer wallet stays open until engine verification completes.

The HTTP implementation is still test support with authentication disabled on
isolated fixture RPC; it must not be deployed as a production service. The
production swap runner, authenticated wallet ownership, persisted protocol
identity and post-verification/reorg lifecycle remain unfinished. Existing
legacy state transitions are not globally replaced or made safe by this module.
No live execution gate has changed.

The combined cancel/full-refund path also passed through the engine-owned native
verifier, followed by native recovery, RPC restart and sweep. Final Docker checks
again showed no running fixture containers or remaining test networks.

### State-bound native lock entry

`xmz_funding::verify_state3` now derives the expected joint address and exact raw
amount from the negotiated Bob state, verifies native funding, and only then
returns the native-locked state. It borrows the prior state, so a failed check
does not consume the caller's recovery data. This is an explicit port boundary
for the existing raw-atom state, not a completed protocol/network serialization
migration. Local network/genesis/confirmation policy still needs durable binding.

The combined redeem path passed using this entry point. Before accepting correct
funding, the fixture rejects a wrong transaction key through the native RPC
observer and a mismatched expected amount through the verifier. The 16 library
tests also passed (6.68s). Negative integration assertions now require the exact
expected mismatch errors rather than treating any transport failure as a pass.
Legacy transitions and production execution remain unchanged; only the combined
fixture is routed through this new state-bound entry so far.

The combined full-refund path passed with the stricter native-RPC rejection
assertions, state-derived verification, native recovery, wallet-RPC restart and
sweep. The prior state remained usable after deliberate verification failures.
Patch reconstruction and whitespace checks passed; no live gate was changed.

### Versioned pending-funding recovery record

`PendingFunding` now encodes the negotiated pre-native-lock Bob state and transfer
proof in a versioned XMZ-specific envelope. It binds the swap UUID, native network,
genesis and confirmation policy; decoding additionally requires the expected
Bitcoin lock txid from trusted local state. Records are capped at 1 MiB and
unknown envelope/policy fields are rejected. A decoded record contains no cached
verification authorization: `resume` always calls the native verifier again.

The combined redeem path passed after encoding, writing a create-new owner-only
temporary checkpoint, syncing and closing it, decoding it from disk and resuming
with fresh native observations. The test also rejects mismatched swap/lock IDs,
changed confirmation policy and an injected `verified` flag. The existing 16
library tests passed (6.83s). All seven patches reconstruct the current sources.

This tests a protected disk round trip, not a swap-engine process restart or
power-loss durability. The checkpoint contains secret material and is not
encrypted or authenticated by this codec. Production protected storage, atomic
journal updates, integrity checks, recovery of all lifecycle phases and durable
binding of local policy remain necessary. No production database was migrated.

The combined full-refund path also passed after reloading the pending record
from disk: fresh native verification, Bitcoin cancel/refund, key recovery, native
wallet-RPC restart and sweep all completed. Final container/network checks were
clean. The next recovery milestone is a separate engine process loading this
record, not describing this in-process reload as a process-restart test.

### Separate-process pending-state reconstruction

The integration fixture now launches a new instance of its test executable to
load the pending checkpoint, match the expected swap/lock IDs and local policy,
perform native RPC verification and serialize the reconstructed locked state.
The parent uses that returned state for settlement. The child has a 45-second
deadline, is reaped on completion/failure, and never prints state or keys. Input
must be an owner-only regular file; reads and outputs are bounded to 1 MiB and
output is create-new/owner-only. The helper test is ignored in ordinary suite
runs because it requires private fixture paths; the combined test explicitly
invokes it with `--exact resume_pending_in_child --ignored`.

The combined redeem/native-recovery/restart/sweep path passed across this process
boundary on 2026-09-12. This establishes separate-process reconstruction and fresh
verification of the pending Bob state. It is not a full application restart:
the parent still owns the counterparty and Bitcoin wallet/test infrastructure,
and interruption at other lifecycle stages and durable journal semantics remain
untested. Production protected storage and live execution are unchanged.

The combined cancel/full-refund path also passed using the subprocess-returned
state, followed by native recovery, wallet-RPC restart and sweep. Both helper
processes exited successfully; final Docker checks showed no running containers
or remaining test networks. Saved patch reconstruction and whitespace checks
passed for this checkpoint.

### Recovery refuses unavailable native verification

The combined fixture now first launches recovery against an owned loopback HTTP
server returning 503. The server consumes a bounded request and the child must
reach native RPC, exit unsuccessfully with the specific HTTP-unavailable error,
and leave no reconstructed state. A private static rejection marker distinguishes
this from unrelated startup/decoding failures. The parent then launches a fresh
child against the real fixture RPC using the unchanged pending checkpoint.

The combined redeem path passed this fault/retry sequence, then completed Bitcoin
redemption, native recovery, wallet-RPC restart and sweep. This confirms the saved
pending record is not a cached approval and survives an unavailable verification
backend. It covers an explicit 503, not every timeout, connection-loss, reorg or
power-loss scenario. No production RPC configuration or live execution changed.

The combined cancel/full-refund path also passed the same unavailable-RPC refusal
and fresh-process retry before refund, native recovery, RPC restart and sweep.
The owned fault server and subprocesses were closed/reaped. Final Docker checks
showed no running containers or test networks; patch reconstruction passed.

### Recovery also verifies the Bitcoin lock

Pending records now use domain `monzero-btc-xmz-pending-funding-v2` and bind
Bitcoin genesis and minimum confirmations as well as native policy. V1 records
are not silently upgraded or given default Bitcoin policy. `PendingFunding::resume`
requires a trusted Bitcoin observer and checks the exact lock outpoint's unspent
output, script, integer amount, chain identity and confirmation depth before and
after native verification. A changed tip/output observation fails closed.

The test adapter queries Bitcoin Core `getblockhash` and `gettxout` with mempool
spends included. Its RPC decimal amount is preserved with serde JSON `RawValue`
and parsed directly into Bitcoin satoshis, never through f64. Endpoints are limited
to private/loopback IPv4 fixtures; proxying/redirects are disabled and response/time
bounds match the native test adapter. Fixed disposable RPC credentials are kept
out of URL error output. This is still test infrastructure, not a public service.

Seventeen library tests passed (7.06s), including wrong-chain/script/amount and
insufficient-confirmation cases. The combined redeem path passed separate-process
recovery and settlement, then correctly rejected resuming its old pending record
because Bitcoin Core reported the lock spent. The 503 native-outage refusal and
retry checks remain enabled. Actual reorg, expiry-race and broader lifecycle
recovery testing are still required; live execution remains disabled.

The combined full-refund path also passed Bitcoin/native revalidation, subprocess
recovery, cancellation/refund and native sweep, and rejected its pending record
after the lock was spent by cancellation. Final fixture cleanup checks were clean;
ordered patch reconstruction and whitespace checks passed.

### Cancellation-window recovery guard

Pending trade resumption now refuses a Bitcoin lock whose confirmation depth has
reached the negotiated cancellation delay, before and after native verification.
At that depth a relative-height cancellation can enter the next block. The
pending record remains available through `prepare_cancellation`, which constructs
offline cancellation/refund state without broadcasting or claiming maturity.
Native recovery scanning starts conservatively at genesis.

Eighteen library tests passed (6.34s), including immediately-before/at/after-boundary
checks. The combined full-refund fixture passed at the actual Bitcoin regtest
boundary: it mined to 100 lock confirmations for a 100-block delay, rejected trade
resumption while the lock was unspent, successfully broadcast cancellation, and
built the full refund from the saved pending record rather than the earlier
in-memory locked state. Native recovery, wallet-RPC restart and sweep completed.

This is not complete expiry-race protection: scheduling margins, revalidation at
signing/broadcast, reorgs and full application lifecycle handling remain required.
Production execution is still disabled. Patch reconstruction passed.

### Fresh verification at redeem-signature generation

`PendingFunding::redeem_signature` now re-observes both chains and the cancellation
window immediately before generating the adaptor signature. The combined path
uses this operation rather than generating a signature from its cached recovered
State4. Bitcoin-only fixtures still use their explicitly mocked native path.
The combined redeem/native-recovery/restart/sweep path passed, and subsequent
signature generation for the spent lock was rejected with the expected reason.
The 18 library tests passed (6.73s); all seven source patches reconstruct cleanly.

This narrows stale-approval exposure but is not a cryptographic expiry on an
already-issued signature. Publication timing, competing transactions, reorgs and
the production runner still need lifecycle handling. Legacy direct signing APIs
have not been globally removed, and live execution remains disabled.

The combined full-refund fixture also passed: signature generation was refused
at the cancellation boundary and after the lock was spent, while cancellation
and full refund from the pending record still completed with native recovery,
RPC restart and sweep. Final container/network cleanup checks were clean.

### Native wallet-RPC authentication contract

`test_wallet_rpc_auth.py` starts a disposable empty wallet-RPC service with its
default authentication enabled. The native server generates a private
`monzero-wallet-rpc.<port>.login` file; no credentials are passed in argv or
printed. The probe verifies that the file is bounded and owner-only, absent and
incorrect credentials receive a Digest 401, and correct generated credentials
can request the RPC version. A reserved, non-listening loopback daemon endpoint
ensures no existing daemon or wallet is used. The owned service stops before its
temporary directory and credential file are removed.

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_wallet_rpc_auth.py \
  --wallet-rpc build/bin/monzero-wallet-rpc --temp-parent build
```

Passed on 2026-09-12 using installed Requests 2.32.5 and `HTTPDigestAuth`, with
environment proxy settings and redirects disabled. The first attempt using
Python's urllib Digest handler rejected even correct credentials; switching the
probe client passed without weakening or changing the native server. The exact
urllib incompatibility has not been diagnosed. Do not treat HTTP Basic auth or
the unauthenticated combined-test adapter as equivalent to this native contract.

The test-only Rust native adapter now supports a bounded Digest challenge and one
authenticated retry using pinned `digest_auth = "=0.3.1"`.
The extended probe passed on 2026-09-12: absent and wrong credentials were rejected,
and two consecutive authenticated version requests succeeded.
Credentials came from the private generated file, not command-line arguments.

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_wallet_rpc_auth.py \
  --wallet-rpc build/bin/monzero-wallet-rpc --temp-parent build \
  --engine-test-binary build/eigenwallet-engine-port/target/debug/deps/bitcoin_settlement-5ab19b4ed9d73272
```

The executable hash is build-specific. This verifies test-adapter authentication,
not a deployed authenticated DEX adapter or live trading.
The combined settlement fixture now uses default wallet-RPC authentication too.
The Python coordinator reads the bounded owner-only generated login file for
wallet requests, including after a server restart regenerates credentials.
The Rust funding observer, child-process resume and fresh-signature checks require
the same private file; no missing-credential fallback is provided for this fixture.
Only the path and local ports are passed to the child process, not native credentials.

The authenticated combined redeem/recovery/restart/sweep test passed on 2026-09-12.
The authenticated cancellation/full-refund/recovery/restart/sweep test passed too,
retaining the cancellation-boundary and spent-lock rejection assertions.
The 18 swap-machine library tests also passed (11.83s), and the separate Rust
missing/wrong/repeated-credentials probe passed again against the updated binary.
This remains a local test adapter; production credential lifecycle, GUI integration
and the broader live-release checks are outstanding.
No real wallets or public services were changed.

### Reusable native funding observer

`upstream-native-funding-rpc.patch` applies eighth, after the funding-state patch.
It moves native read-only RPC observations into
`swap_machine::native_funding_rpc::NativeFundingRpc`; the integration tests now
import that library implementation rather than maintaining their own copy.
The constructor requires bounded nonempty credentials and distinct nonzero
daemon/wallet ports. Endpoints remain fixed to loopback, redirects and proxies
are disabled, requests have timeouts and response bodies have size limits.
Wallet responses without an authentication challenge are rejected.

The public interface exposes version lookup and the funding observer trait, not
arbitrary RPC commands, signing or broadcasting. Credential-file discovery stays
with the caller/test harness. Production process ownership is not yet implemented.
Callers must still use the funding verifier
for expected genesis, exact amount, depth and stable chain-state checks.

The updated library target passed 20 tests, including invalid constructor inputs
and refusal of an authentication-disabled endpoint (6.60s). The real disposable
wallet-RPC Digest probe passed against the library-backed integration executable.
The ordered eight-patch reconstruction check passed. No GUI/live gate was changed.

Both combined integration paths passed again using library-backed executable
`bitcoin_settlement-b42a161602a8a972`: redeem and cancellation/full refund,
including native key recovery, wallet-RPC restart and sweep. These runs used
disposable Bitcoin regtest and offline XMZ fakechain, not real funds or the GUI.

### Credential replacement and bounded memory hardening

The native observer owns username/password strings in `zeroize::Zeroizing`,
including validation-failure paths. Explicit `replace_credentials` requires
exclusive mutable access, constructs a new client/connection pool before swapping,
and leaves the old client intact if replacement validation fails.
Authorization header values are marked sensitive to redact their Debug output;
the temporary rendered authorization string is also zeroized on drop.
This does not guarantee erasure of copies within HTTP/Digest libraries, request
bodies, operating-system buffers or caller-owned credential data.

The library passed 21 tests (7.12s). The real wallet-RPC probe additionally checks
invalid replacement preservation, wrong-password replacement rejection and
successful restoration, using executable `bitcoin_settlement-7534ccc909806f1b`.
No credentials are printed or sent in command-line arguments. This API supports
caller-managed rotation; automatic server credential discovery is not implemented.

Both combined redeem and cancellation/full-refund fixtures passed again against
that executable, including authenticated native recovery, RPC restart and sweep.
The GUI's `DexEngine::activationReadiness` still returns `ready=false`;
`previewOrder` remains non-executable and uses a fixed native fee estimate.
A GUI connection must not turn that preview into a signing authorization: it
still needs engine-owned transaction fee estimation and role-bound trade state.

### GUI preview fee provenance

The Qt preview now carries `feeEstimateVerified=false` and
`feeEstimateSource=fixed-research-placeholder`. Debit/proceeds labels explicitly
say Estimated, and the local-offer fee display says unverified estimate.
This does not implement transaction fee estimation or make the preview executable.
The dedicated test checks both sides of all three demo markets and preservation
of this metadata in the local offer list.

The `dex_tests` target rebuilt successfully and all 11 Qt test entries passed
(145ms); `qmllint pages/Dex.qml` and whitespace checks passed.
This was not a rendered layout test or full GUI package rebuild.
No installed application or website package was replaced.

### Native non-relayed fee-construction probe

The combined recovery coordinator now exercises a disposable wallet's `transfer`
with explicit `do_not_relay=true`, without requesting transaction keys, hex or
relay metadata. It verifies exact principal, positive integer fee and weight,
empty signing-artifact fields, unchanged balance/unlocked balance, unchanged
chain height and an empty daemon transaction pool before and after construction.
The actual funded transaction is constructed separately; its fee is not assumed
equal to the preview. Native transaction construction can sign internally, so
this operation is deliberately not exposed by the read-only observer or GUI.

The new probe and subsequent combined redeem/recovery/restart/sweep path passed
on disposable Bitcoin regtest/XMZ fakechain. No real wallets were opened.
Source contract: `wallet_rpc_server::on_transfer` and `fill_response`, and
`COMMAND_RPC_TRANSFER::request_t` in the current Monzero core. Omission of
`do_not_relay` defaults to false; preview code must not omit it.
The combined cancellation/full-refund path also passed with the construction
probe, including authenticated recovery, wallet-RPC restart and sweep.

### Reusable fee-estimator implementation (integration pending)

`native_funding_rpc::fee_estimation::NativeFeeEstimator` is separate from the
read-only observer and consumes its authenticated client. Its request binds the
primary wallet identity, destination, exact XMZ atoms, account and explicit
priority. The caller supplies trusted genesis policy and must exclusively own
the wallet service; Rust object ownership alone cannot exclude other RPC clients.

The implementation checks chain/wallet identity before construction, explicitly
disables relay and artifact export, checks exact returned principal and positive
fee/weight, checks debit overflow and unlocked funds, and checks wallet balances
and chain snapshot again afterward. Its result retains the request and snapshot,
has no serialization or signing API, and is not a durable/executable quote.
It does not eliminate all concurrent wallet changes or provide a global RPC lock.

All 24 library tests passed (6.17s), including malformed construction responses
and exact outgoing non-relay flags. Ordered patch reconstruction passed.
The new Rust estimator itself has not yet been exercised against native wallet
RPC; the earlier native construction probe was Python-driven. That integration
test, fresh-result consumption, and GUI wiring remain outstanding. Live gates
and production wallets were not changed.

### Native Rust estimator integration

The combined coordinator now invokes ignored Rust test `native_fee_estimation`
while its owned funded wallet is open. The library estimator rejects a wrong
genesis and a wrong primary wallet identity with the expected errors, then
constructs a valid non-relayed estimate retaining destination, principal,
account, priority and chain identity. The coordinator independently checks
balance/unlocked balance and the empty transaction pool after the Rust call.
The native RPC integration passed on 2026-09-12.

A private completion marker is required in addition to child exit status, so a
binary without this exact ignored test cannot produce a false pass by running
zero tests. Only public addresses/amount and existing private credential-file
paths are supplied to the helper; no secrets are printed.
This supersedes the integration-pending status above, but does not establish
concurrent wallet ownership, estimate freshness at approval, or GUI integration.

Both combined redeem and cancellation/full-refund paths completed successfully
with this Rust estimator probe, including native recovery, RPC restart and sweep.
No test containers or fixture networks remained after completion; no production
wallets, website packages or live activation gates were modified.

### Display-only fee freshness

Fee estimates retain a monotonic timestamp from the start of construction and
the observed account balances. `revalidate_for_display` checks a caller-supplied
positive age limit before and after RPC work, primary wallet identity, balances
and matching chain snapshots. Estimates expire at the exact age boundary.
The timestamp is process-local; estimates are not serializable durable quotes.

These checks are explicitly not signing, reservation or broadcast authorization:
equal balances do not prove identical spendable outputs, and external wallet
clients remain outside the estimator's exclusive Rust borrow. Final transaction
validation and exclusive service ownership are still required.
All 25 library tests passed (5.47s), including deterministic age-boundary tests
without sleeps, zero-limit rejection and future timestamp rejection.
The native fixture also passed a current-estimate display recheck and rejected
a zero age limit. Both combined redeem and cancellation/full-refund paths then
completed authenticated recovery, RPC restart and sweep successfully.
Changed-balance and changed-tip revalidation branches still need dedicated fault
injection coverage; the successful native check does not prove those branches.

### Revalidation fault-injection coverage

A private `DisplayContext` seam now supplies observations to the same revalidation
routine used by the native estimator. It is not a public peer-supplied backend.
Tests inject total/unlocked balance changes, wallet mismatch, missing initial or
final snapshots, and genesis/hash/height changes both before and during rechecking.
Each rejects with the expected reason; unchanged observations succeed.
All 27 library tests passed (6.12s). This covers the comparison/error-propagation
branches left open above, but is not a live-node reorg or concurrent-wallet test.
The native transport remains covered separately by the owned RPC fixture.
The rebuilt integration target passed both combined redeem and full-refund runs
after this refactor, including fee estimation/revalidation, native recovery,
RPC restart and sweep. Ordered source reconstruction and whitespace checks passed.

### Engine fee-preview display response

`preview_for_display` rejects a nil caller request UUID, revalidates display
freshness, then exports `monzero-native-fee-preview-v1`. Its fixed 14-field
allowlist contains the request ID, source, wallet/destination, exact atom strings,
weight/account/priority strings, chain identity and `executable=false`.
Credentials, transaction keys, signed blobs, relay metadata and recovery secrets
are excluded. The live estimate itself remains non-serializable.

All 28 library tests passed (6.01s), including JSON round-trip preservation above
the JavaScript exact-integer limit. The native fixture supplies the request UUID
from its parent process, receives the bounded private JSON file, and verifies its
identity, wallet, destination, amount and non-executable status. That native
export/receive check passed. This is not yet a GUI receiver or authenticated IPC
service: the caller must correlate requests and enforce a receive deadline, and
a saved/replayed payload cannot grant signing or broadcast authority.
Both combined redeem and cancellation/full-refund paths passed with this export,
including authenticated native recovery, RPC restart and sweep. Source-patch
reconstruction and whitespace checks passed; GUI and live gates remain unchanged.

### Qt fee-preview receiver component

`NativeFeePreviewReceiver` creates a UUID for one outstanding request and a local
reply deadline of at most 30 seconds. Its bounded 2 KiB JSON decoder requires the
14-field schema, exact request inputs, canonical unsigned string integers and
`executable=false`. It rejects duplicate keys (including escaped equivalent names),
unknown fields, wrong scalar types, debit overflow, replay, cancellation and expired
replies. Any response consumes the outstanding request, including an invalid one.
Amounts and formatted fees above the JavaScript exact-integer range stay exact.

This is a receiver component, not yet a running GUI/engine connection. Accepted
output still has `transportAuthenticated=false` and a display-only label; schema
validation cannot authenticate a process. Its expected addresses need prior local
validation (the receiver checks shape and equality, not address checksum).
The owning GUI thread must manage process identity, cancellation and display
expiry after receipt; this component currently enforces the reply deadline only.

The `dex_tests` target rebuilt and all 12 Qt test entries passed (189ms).
Whitespace checks passed. CMake reported unrelated legacy functional tests skipped
because Python dependencies were unavailable; those were not counted as passed.
No full GUI package, visible UI workflow or live trading gate was changed.

The receiver now also exposes expiry-aware `current()`: after its original
request deadline it returns no fee fields and `valid=false`. Cancellation,
replacement requests and rejected/replayed responses clear the retained preview.
Receiving a response does not restart the deadline. Tests cover expiry after
successful receipt and invalidation on replacement/replay; all 12 Qt entries
passed (307ms). A copied QVariantMap cannot expire itself: the future GUI
controller must fetch current state and invalidate its display on a timer.

`NativeFeePreviewController` now owns that single-shot precise timer and exposes
an expiry-aware `preview` property. Receipt does not restart the request deadline;
expiry clears state and emits a change signal. Cancellation stops the timer, and
QObject ownership cancels pending callbacks when the controller is destroyed.
`DexEngine.nativeFeePreview` forwards the read-only state and notifications to
future QML bindings. Begin/accept methods are native-code-only, not QML invokables.

The controller/property integration rebuilt and all 13 Qt entries passed (484ms),
covering accepted-preview expiry, signal forwarding, cancellation and pending
destruction. No running backend connection or visible fee-estimation action has
been added yet. CMake again skipped unrelated legacy functional tests with missing
Python dependencies; only the explicitly executed Qt target is reported here.

Controller callbacks now require the request ID captured by the owning backend
job, separately from JSON. Obsolete responses are ignored without consuming a
newer pending request or clearing its accepted preview. `cancelRequest(id)` gives
backend failure/cancellation callbacks the same protection; unconditional cancel
remains available for explicit user/context cancellation. Tests inject late old
responses before and after the newer response, and verify obsolete cancellation
does not clear it. All 13 Qt entries passed (492ms); whitespace checks passed.
No asynchronous backend has been launched by this controller yet.

### Asynchronous fee-preview process transport

`NativeFeePreviewJob` now launches a caller-selected absolute executable with
`--fee-preview-stdio-v1`, sends the request as bounded JSON on stdin, and consumes
stdout only after a successful normal exit. The tested Linux environment is
restricted to LANG=C; request data and credentials are not placed in arguments.
Stdout is capped at 2 KiB and stderr at 4 KiB; stderr content is never logged.
Timeout, malformed/oversized output and nonzero exit fail closed. Cancellation
targets only this job's child and uses its captured controller request ID.
Reentrant starts are rejected, and an old job cannot cancel a newer preview.

All 15 Qt test entries passed (1362ms), using a controlled helper embedded in the
test executable. Tests cover valid JSON, invalid JSON, stdout/stderr overflow,
timeout, nonzero exit despite valid output, reentrant starts and overlapping old
and new jobs. This helper simulates transport only: it is not the Rust estimator.
The real Rust stdio entry point, trusted executable provisioning/code-signature
verification, RPC session configuration and exclusive wallet-service ownership
remain unimplemented. Killing a child cannot guarantee cancellation of a wallet
RPC operation already executing in another process. No live gate was changed.

The process timeout now uses the receiver's remaining request time after UI
callbacks, rather than restarting the full timeout. A request already expired
before launch fails without starting the child. A test blocks a synchronous UI
notification past its deadline without processing timers and verifies rejection.
All 16 Qt entries passed (1369ms); whitespace checks passed. This remains tested
with the controlled transport helper, not the unfinished Rust stdio executable.

### Rust request decoder

`decode_preview_request` implements the Qt runner's eight-field request schema.
It caps input at 2 KiB and deserializes directly into a deny-unknown-fields struct,
rejecting duplicate fields instead of silently overwriting them. UUIDs must be
canonical and non-nil; atom/account/priority strings must be canonical and within
range. Wallet and destination are checksum/network validated using the caller's
trusted network, and the request genesis must match trusted session policy.
Requests cannot supply RPC endpoints or credentials.

All 30 library tests passed (5.89s), including exact amounts above 2^53, wrong
network/genesis, duplicate/extra fields and numeric boundary rejection. Ordered
source reconstruction passed. The decoder is not yet called by a Rust stdio
executable: trusted session provisioning and the executable entry point remain
outstanding. No native RPC or production wallet was accessed by these unit tests.

### Bounded native preview operation

`NativeFeeEstimator::preview_request` now combines strict request decoding,
non-relayed construction and display-freshness revalidation under one timeout.
The caller supplies trusted network/genesis policy; request JSON cannot choose
RPC endpoints or credentials. The time budget must be positive and at most
30 seconds. Dropping a timed-out request does not undo construction already
running inside wallet RPC, so exclusive wallet-service ownership is still needed.

The combined Bitcoin-regtest/XMZ-fakechain redeem fixture passed with this path.
Its native helper rejects a zero budget and an injected endpoint field, then
exports a request-bound, non-executable preview. The coordinator requires the
helper completion marker and verifies unchanged wallet balances and an empty
transaction pool before constructing the actual funding transaction separately.
The fixture also recovered and swept the joint XMZ funds across a wallet RPC
restart. No production wallet or funds were used.

The matching `engine_cancel_and_full_refund_are_accepted_on_regtest` combined
fixture also passed with the bounded preview operation, including recovered XMZ
funds remaining spendable after wallet RPC restart. Both commands completed with
exit status zero. This is disposable integration evidence, not an independent
security audit or proof of all failure schedules.

The latest scoped rerun passed all 30 Rust library tests and all 16 Qt entries.
Ordered patch reconstruction and GUI whitespace checks passed. The Qt subprocess
tests still use a controlled helper, not a deployed native estimator executable;
these results do not establish a working GUI-to-native-service connection or
authorize live trading.

### Async request/response stream handler

`NativeFeeEstimator::serve_preview` accepts one EOF-framed request from caller-owned
async I/O and emits a bounded JSON response. One outer deadline covers reading,
native estimation, response writing and flushing. Both directions are capped at
2 KiB; input is read with a 2049-byte limit to detect overflow without unbounded
allocation. Credentials and endpoint configuration remain outside this stream.
Consumers must reject partial output and require successful child exit; a failed
write or timeout cannot retract bytes already written or undo wallet RPC work.

All 31 library tests passed, including malformed/oversized input and a held-open
duplex input timing out without output. The native integration helper now routes
its successful request through this handler using in-memory async I/O. This is
the reusable handler, not a standalone stdio executable or GUI connection: the
trusted session launcher and exclusive wallet-service ownership remain necessary.

After rebuilding the integration target, both combined redeem and full-refund
fixtures completed with exit status zero through the stream handler. Each verified
native fee construction and subsequent recovered-fund spending across wallet RPC
restart. Ordered patch reconstruction and whitespace checks passed; these scoped
tests are not a full workspace/platform build or release certification.

### Native Linux fee-preview executable

The `swap-machine` binary target `native-fee-preview` now runs the handler over
actual nonblocking stdin/stdout pipes. Invocation is
`native-fee-preview --fee-preview-stdio-v1 --session-file /absolute/private/session.json`.
Credentials are not arguments, environment variables or public request fields.
The caller must provision the session locally; never accept its path from a peer.

Session JSON has exactly `schema`, `network`, `genesis`, `daemonPort`, `walletPort`,
`username`, and `password`. The schema is `monzero-native-fee-session-v1` and network
is `mainnet`, `testnet` or `stagenet`. The helper enforces local RPC ports through
the existing client and checks chain policy through the request/estimation path.
It requires an absolute path, a regular file owned by its effective user, mode
0600, one hard link and at most 2 KiB. It opens without following a final symlink
and with nonblocking mode (including rejection of FIFOs). An exclusive file lock
is held until completion; it only coordinates helpers using that same file.
It does not establish exclusive ownership against other wallet RPC clients or
helpers using different session files. The future launcher must enforce that.

Errors produce one fixed diagnostic and no intentionally exported secret data.
Owned session input and credential buffers are cleared on drop, but this is not
a guarantee of erasing parser/library/OS copies or the session file on disk.
The launcher must protect and remove its session file. The helper is Linux-only;
other platforms reject execution. It is not packaged, signed or connected to the
GUI yet, and does not enable live trading.

The executable built successfully with the local investigation toolchain.
`test_native_fee_helper.py` passed rejection checks for unsafe file permissions,
symlinks, hard links, FIFOs, duplicate session fields, oversized data and competing
session locks. Unsafe-session cases hold stdin open to prove rejection before
request processing. A separate held-open valid-session case verifies that the
30-second timeout also permits process shutdown. The combined redeem fixture
passed using `--fee-preview-helper`, with a real native fee preview over OS pipes,
unchanged balances/empty pool, followed by recovered-fund spending after restart.
The library rerun passed 31 tests. Full workspace/platform validation remains open.

The combined full-refund fixture also passed with the executable enabled, including
the OS-pipe preview and recovered native funds remaining spendable after restart.
Both integration commands exited zero; ordered patch reconstruction and whitespace
checks passed. The initial redeem attempt failed in Python session-file creation
before helper launch (`Path.open` does not accept `opener`); the fixture was corrected
to use builtin `open` with owner-only creation and the successful rerun is reported.

### Qt launcher session support

`NativeFeePreviewJob::startWithSession` now passes a caller-provisioned absolute
session path to the helper as a separate argument. It rejects empty, relative,
missing, directory and symlink paths before beginning a request. It does not read
credentials; ownership, permissions and locking remain the native helper's job.
The caller must keep the session alive and unchanged until the job completes and
must own the wallet service exclusively. Paths cannot come from offers or peers.

All 17 Qt entries passed, including a session filename with spaces, rejected path
forms and checking that neither the session path nor its fixture contents enter
the eight-field public request. The existing timeout, cancellation and stale-job
protections remain shared by both launch methods.

The test executable additionally provides `--native-fee-job-fixture` for the
disposable coordinator. `test_wallet_recovery.py --qt-fee-driver <dex_tests>`
(with `--fee-preview-helper` and the engine fixture) exercises the actual Qt job,
Rust helper and native wallet RPC, rather than the controlled transport helper.
This test entry point does not provision real-wallet sessions or add a visible
UI action. The preview remains non-executable and its transport-authentication
flag remains false; executable provenance and production session ownership have
not been established by this integration.

Both combined redeem and full-refund runs exited zero with the Qt driver enabled.
The coordinator verified the returned display-only amount/fee, unchanged wallet
balances and an empty transaction pool, then completed engine recovery and native
fund spending across wallet RPC restart. These are owned Linux fixture results,
not testing of the visible wallet application or real-money trading.

### Job destruction and wallet ownership boundary

Destroying `NativeFeePreviewJob` now cancels its captured request ID after stopping
its own child, including clearing an accepted preview. Obsolete jobs cannot clear
a newer request. Starts are rejected during destruction so cancellation observers
cannot relaunch the dying job. Tests cover pending destruction, preservation of a
newer request, accepted-preview invalidation and reentrant start rejection.

Source inspection confirmed that `src/libwalletqt/WalletManager.cpp` opens the GUI
wallet through the in-process wallet library; `Wallet.cpp` constructs transactions
through that library too. It is not an existing wallet RPC session that the new
helper can simply reuse. Session management must not open the active GUI wallet
file in a second process. The next implementation direction is a dedicated owned
swap-wallet RPC service and separate wallet data, with explicit funding/backup UX,
serialized access, credential provisioning and shutdown/recovery handling. That
service and UX are not implemented by the fee-preview launcher changes.

### Fresh separate RPC process owner

`SwapWalletRpcProcess` adds a one-shot Linux process owner. It requires a private
parent directory owned by the current effective user, creates a fresh private
subdirectory, and launches the caller-verified executable with a separate
`--wallet-dir`. Daemon and wallet endpoints are explicit distinct loopback ports.
It does not open the GUI wallet, create a wallet, pass a password, disable RPC
authentication, or declare RPC/chain readiness when the process starts.

Process output is suppressed and logs stay in the private directory. Explicit
stop requests termination, then kills only the owned child after ten seconds if
needed. Destruction also attempts owned-child shutdown. Credentials are removed
only after the child is stopped; cleanup failure makes the stopped result unclean.
Wallet data and logs are deliberately retained, including on startup failure.
They must not be recursively deleted as ordinary UI-object cleanup. The caller
still needs persistence/discovery of the directory, wallet backup/funding UX,
authenticated readiness, restart recovery and exclusive operation scheduling.
The class is not wired into the visible DEX UI and does not enable live execution.

The Qt target rebuilt and all 19 entries passed. The controlled child test checks
fixed launch arguments, one-shot ownership, credential removal and wallet-data
retention. `test_wallet_rpc_auth.py --qt-process-owner <dex_tests>` passed against
the real empty native service: missing/wrong credentials rejected, correct Digest
authentication accepted, owned child stopped, login file removed, wallet directory
retained. No existing wallets or daemon were used. CMake skipped unrelated legacy
functional tests because Python dependencies are missing; they are not counted as
passed. Production crash/restart and full-package validation remain outstanding.

### Clean service restart

`resumeClean` reuses the separate directory only under an exclusive `QLockFile`
and with an exact bounded `service.state` marker recording a clean stop. The
owner writes `active` before launching; abnormal exit or credential-cleanup failure
records `recovery-required`. A stale process lock alone never authorizes resuming
an active/unclean directory. Wallet directories must also be private, owned and
not symlinks. The marker is local coordination data, not cryptographic evidence or
protection against another process controlled by the same user. Atomic file
replacement is used; power-loss recovery has not been verified.

All 20 Qt entries passed, including competing-lock rejection, active-state
rejection and clean directory reuse. The real empty-RPC authentication fixture
also restarted through the Qt owner: it retained a disposable file in the wallet
directory, generated new credentials, rejected the previous login, accepted the
new login and removed the login file after clean shutdown. This does not test
reopening a funded wallet through this class. Interrupted-service reconciliation,
wallet opening, chain checks and trade checkpoint recovery remain outstanding.

The Qt-owned restart fixture now creates and stores a real disposable wallet with
a random password before shutdown. After clean restart and RPC credential rotation,
it rejects the wrong wallet password and confirms no wallet address is accessible
after that failed unlock. The correct password reopens the original address with
the same zero balance, then closes the wallet. This stronger test passed against
the native RPC executable; passwords and addresses were not printed, and no real
daemon or existing wallet was used. It supersedes the marker-only restart coverage
above, but does not verify funded-wallet recovery, UI password handling or backup.

The process-owner failure tests now inject both a nonzero child exit and a
credential-cleanup failure after an otherwise successful exit. Both produce an
unclean result, retain wallet data, record `recovery-required`, and refuse
`resumeClean`. Cleanup failure is simulated with a nonempty directory at the
disposable login path, without changing system permissions. All 21 Qt entries
passed; these controlled failures do not establish power-loss or orphan-process
recovery behavior.

### Typed native wallet-open observation

`NativeFundingRpc::opened_wallet` returns a checksum/network-validated primary
address when a wallet is open, or `None` only for the native `NOT_OPEN` error code
(-13) without a simultaneous result. Authentication/transport failures, other RPC
errors, missing fields and malformed addresses remain errors, not empty-wallet
observations. The shared authenticated transport now retains the correlated RPC
envelope privately; existing RPC callers still reject every error response.

All 32 library tests passed, including malformed/error/status parsing and wrong
address network rejection. Native authentication fixtures passed with both an empty
service and a Qt-owned open disposable wallet, checking the expected state through
the Rust client. This observation is not exclusive ownership, a readiness token or
permission to switch wallets: the service could change after it is observed.
Controlled create/open operations and their UI/password lifecycle are still needed.

After this shared-RPC change, both combined redeem and full-refund fixtures exited
zero with the rebuilt engine, native helper and Qt driver. Fee previews remained
non-relayed and recovery spending succeeded across wallet RPC restart. Ordered
patch reconstruction and whitespace checks also passed. This does not make the
new wallet-state observation an authorization boundary for live trading.

### Controlled native wallet lifecycle

`wallet_session::NativeWalletSession` now provides separate create/open/close
operations. Filenames derive only from a non-nil `SwapWalletId` UUID; callers
cannot supply arbitrary paths. Create/open require an observed empty service,
open requires the expected network/address, and close checks the expected wallet
before explicitly requesting autosave. No transaction construction, signing or
broadcast API is exposed by this type.

The object marks itself uncertain before dispatching a mutating operation and
clears that flag only after validating the result. Errors or cancellation leave
further operations blocked on that object. This is not a durable recovery record
or a global lock: constructing another wrapper is not reconciliation, and the
caller must still exclusively own the service and coordinate all clients. Genesis
verification, backup/funding approval and GUI password lifecycle remain separate
unfinished integration requirements. Owned password buffers are cleared, but
JSON/HTTP/parser copies are not guaranteed erased.

All 33 library tests passed. The native authentication fixture also exercised
create/close/open with matching identity, rejected creation over an open wallet,
and rejected retry on the same session after a wrong-password failure. Its Qt-owned
open-wallet variant confirmed refusal to replace the existing disposable wallet
and retained the earlier clean-restart checks. Both fixtures exited zero. Ordered
patch reconstruction and whitespace checks passed; no production wallet was used.

To regain the build script's free-space margin, the superseded test binary
`bitcoin_settlement-7534ccc909806f1b` was moved, not deleted, to
`/var/tmp/monzero-test-artifact-SN6Yc4hU/bitcoin_settlement-7534ccc909806f1b`.
The current integration target is `bitcoin_settlement-bdda458b960cf68b`.

The native authentication/lifecycle fixture now emits a private completion marker
only after all assertions finish. The coordinator requires its exact contents to
match the expected empty/open-wallet mode, as well as a successful exit; it rejects
missing, oversized, nonregular or nonprivate markers. This prevents an unmatched
Rust test filter (zero tests executed) from being reported as a successful native
lifecycle test. A zero-exit `/usr/bin/true` stand-in was rejected for the missing
marker; both rebuilt native modes then passed, including the Qt-owned restart and
encrypted-wallet reopening checks. Ordered patch reconstruction and whitespace
checks passed. No production wallet or live-trading setting was changed.

### Process-level operation accounting

`SwapWalletRpcProcess::beginWalletOperation` records `operation-pending` before
returning a unique callback token. A second operation is refused while pending;
completion requires the exact current token and a running, non-stopping service.
Verified completion restores `active`. Failure or state-write failure requires
recovery, and a pending/failed operation prevents a clean shutdown marker even
when the child exits zero. Late callbacks cannot clear another operation.

All 22 Qt entries passed, covering pending, failed, verified and obsolete-token
paths. The existing real native lifecycle/authentication/restart regression also
passed with its completion marker. The wallet-control dispatcher must still call
these methods around every native mutation: the current standalone native fixture
does not automatically feed its operation results into the Qt owner. This is local
operation accounting, not a durable trade journal or verified crash recovery.

### Structured private wallet-control commands

`NativeWalletSession::execute_control` accepts a bounded six-field private command
schema (`monzero-wallet-control-v1`) for create/open/close. It rejects duplicate or
unknown fields, arbitrary filenames, unsupported operations, noncanonical UUIDs
and irrelevant credentials. Inputs are limited to 4 KiB; the operation budget is
positive and at most 30 seconds. This is private wallet-control data containing a
password, not the public offer/fee-preview request schema. Callers must protect
and clear their input buffers and must not log raw requests or error chains.

Successful replies contain only schema, correlated request ID, operation, address
and `readyForTrading:false`. No password, keys, signing artifacts or trading
authorization are returned. Timeout does not roll back an RPC operation. All 34
library tests passed. The native lifecycle fixture created a disposable wallet
through this command handler, validated the five-field reply, then completed its
close/reopen/error checks; both empty and Qt-owned open-wallet fixture modes passed
with completion markers. The control schema is not yet exposed by a native stdio
entry point or connected to the Qt operation accounting/GUI dispatcher.

### Private wallet-control pipe entry point

The Linux helper now also accepts `--wallet-control-stdio-v1 --session-file <path>`.
Its session-file checks and exclusive file lock are shared with fee preview, but
stdin contains the private six-field wallet command, not public offer data. Reads
are capped at 4097 bytes to reject requests over 4 KiB, replies at 2 KiB, and an
outer 30-second deadline covers input, execution and output. Control failures emit
only `Native wallet control failed`; private JSON parser details are discarded
rather than retained in an error chain. Input buffers owned by the helper are
cleared on drop, without claiming erasure of library/OS copies.

All 35 library tests passed. The real RPC fixture used OS-pipe commands to create,
close, reopen and close the same disposable wallet, checking correlated reply
fields and absence of the fixture password. The Qt-owned service then passed its
restart and encrypted-wallet checks. Wallet-control mode supports offline wallet
lifecycle and does not verify daemon genesis or establish funding readiness;
`readyForTrading` remains false. Each invocation constructs a new native session,
so the GUI dispatcher must record a pending operation before launch and preserve
uncertainty on every failed/unverified response. That dispatcher integration is
still unfinished; the helper is not a live trading interface.

### Qt wallet-control dispatcher

`NativeWalletControlJob` now records a tokenized pending operation with its
`SwapWalletRpcProcess` before launching the private helper mode. It bounds request,
stdout and stderr sizes; passes no password in argv/environment; checks the
receive deadline explicitly; and requires successful exit plus a canonical,
five-field reply matching the request ID, operation and expected address. Invalid
output, timeout, cancellation or destruction leaves the owner requiring recovery.
Successful verification completes only the captured owner token. Local password
and serialized-input buffers are cleared, without claiming erasure of Qt's
internal copies. The dispatcher never logs raw requests or stderr.

All 23 Qt entries passed using a controlled helper, covering success, wrong reply
ID, extra fields, timeout, single-use jobs and clean/recovery shutdown outcomes.
This dispatcher has not yet been tested end to end with the real helper and RPC
service. Callers must provision a session belonging to the supplied owner and
verify the executable; this class does not independently bind the session file's
credentials/ports to that owner. Address checksum/network checks are delegated to
that verified native helper. No visible wallet UI action or trading activation was
added, and the result's `readyForTrading` value must remain false.

### Owner-provisioned wallet-control sessions

The RPC process owner now provisions a private helper session from its own ports
and generated login, rather than accepting credentials chosen separately by the
dispatcher caller. `NativeWalletControlJob` rejects session paths not tracked by
that owner. Provisioning requires a running, non-stopping service with no pending
operation or recovery requirement; it refuses changing the bound genesis after
provisioning. The native login may be owner-only 0400 (the actual daemon default)
or 0600; helper sessions remain 0600. Symlink, ownership, hard-link, type and size
checks remain enforced. Credential/session files are removed only after child
exit, while wallet data is retained.

Verification: all 23 Qt test entries passed. The real native RPC/helper fixture
created, closed and reopened a disposable encrypted wallet using the Qt owner's
session file, then verified clean restart, rotated credentials, a fresh matching
session, rejection of the old login and wrong wallet password, and credential
cleanup after shutdown. The initial integration run exposed the native login's
0400 permission mode; the compatibility check and controlled fixture were corrected
before the passing rerun. `git diff --check` passed.

This is owner/session integration, not a complete Qt-dispatcher-to-real-helper
end-to-end trade test. The private directory and same-user process boundary remain
trusted; provisioning does not authenticate daemon readiness or prove genesis.
The fixture's dummy genesis is used only for offline wallet lifecycle operations.
Fee-preview job ownership integration, visible GUI controls, durable trade recovery
and release verification remain outstanding. No live gates, user wallets, website
packages or production services were changed.

### Real Qt dispatcher wallet-lifecycle integration

The disposable RPC fixture now optionally accepts the native control helper and,
after the Python harness verifies RPC authentication, drives four separate
`NativeWalletControlJob` instances through create, close, open and close. Each job
uses the process owner's private session and real helper executable. The fixture
requires verified replies with an unchanged wallet address and
`readyForTrading:false`; it writes a bounded, non-secret completion marker only
after all four operations succeed. The harness independently checks that RPC
reports no open wallet and the owner's pending-operation state has returned to
`active`, before running the existing encrypted-wallet restart checks.

Passed with the rebuilt Qt test executable:

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_wallet_rpc_auth.py \
  --wallet-rpc build/bin/monzero-wallet-rpc --temp-parent build \
  --qt-process-owner monzero-gui/build-dex-private/src/dex_tests \
  --wallet-control-helper build/eigenwallet-engine-port/target/debug/native-fee-preview
```

All 23 Qt test entries also passed, and `git diff --check` passed. This supersedes
the earlier missing real-dispatcher lifecycle test, but not the outstanding GUI,
funding, signing, trade recovery and release integration work. Failure/recovery
dispatch tests still use a controlled helper; this real-helper run covers the
successful lifecycle and subsequent RPC restart. It uses no funded wallets and
does not establish readiness for live trading.

### Owner-accounted native fee preview

`NativeFeePreviewJob::startWithOwner` provisions the session through the RPC owner,
binding the requested genesis to that session, and acquires the same pending-wallet
operation token used by wallet control before helper launch. A competing owned
preview or wallet operation cannot acquire the service while construction is
pending. Only an accepted, correlated response and successful owner completion
release the operation; failed/unverified responses or destruction preserve the
recovery requirement. This is conservative because a timeout cannot undo a
server-side nonrelayed transaction construction. The job also blocks reentrant
starts while completion signals from the preview controller are being handled.

All 24 Qt test entries passed, including owner serialization, successful release,
malformed helper output and timeout requiring recovery. The actual native
wallet-control dispatcher/RPC lifecycle and restart fixture also passed again.
The new owned fee-preview path was tested with the controlled helper, not yet
with funded fakechain construction. Existing caller-session entry points remain
for isolated fixtures and require their own exclusive wallet-access coordination;
they do not automatically acquire an owner token. No visible GUI integration or
live trading activation is implied by this API addition.

### Fee-preview cancellation during completion

Completion now rechecks that the controller still holds this job's valid request
after `accept()` emits UI notifications. A listener cancelling the job or replacing
the request during that signal cannot turn the stale result into successful owner
completion. Cancellation during completion is recorded without recursively
entering the completion handler. Cancelling an obsolete request does not cancel a
newer controller request.

All 25 Qt test entries passed. The added owned-preview cases cover immediate
cancellation, destruction with a pending operation, cancellation from a valid
preview notification, and replacement from that notification. All four require
recovery on owner shutdown, leave no valid old preview, and the replacement case
retains its new request deadline. These are controlled-process lifecycle tests,
not funded transaction or live trading tests. `git diff --check` passed.

### Funded integration rerun after Qt cancellation changes

The existing disposable fakechain/Bitcoin-regtest harness passed the
`engine_cancel_and_full_refund_are_accepted_on_regtest` path with the updated Qt
fee job. It verified nonrelayed native construction and the Qt/native preview
round trip without balance/pool changes, followed by importing the funded joint
wallet idempotently, retaining it across RPC restart, and sweeping with the
engine-recovered spend key. This used the caller-session fee fixture, not the new
`startWithOwner` API; funded owner-bound preview testing remains outstanding.

Tested artifact SHA-256 values (local test artifacts, not release attestations):

- `dex_tests`: `8c36c7f02593ab72729ba5f1aa5482c683f945f319197f55f4a73d58de63032a`
- `native-fee-preview`: `06f499d84310e406fcfc761c248e82d8ee8c9fbac83c04a848dda74a82bdc031`
- `bitcoin_settlement-bdda458b960cf68b`: `3d523b7fa940cc40a23be16e71c9032b5ce03f806119700c45fa58f1ac1ad705`

The Qt executable was rebuilt for the current changes; the native helper and
engine integration executable were existing local artifacts. This is not a fresh
full-source rebuild, independent reproduction, audit or live execution approval.

The same harness then also passed `engine_redeem_is_accepted_on_regtest` with these
artifacts, including the funded native preview checks and recovered-key sweep
after restart. Both commands exited zero. These two outcomes cover the existing
local engine success/refund fixtures; they do not establish a complete GUI trade,
peer-network deployment, or XMZ protocol-fee enforcement.

### Isolated launcher for upcoming owner-bound funded fixtures

`fakechain_wallet_launcher.py` is a test-only executable, not an application or
release component. It accepts the process owner's fixed loopback wallet arguments
and a bounded owner-only manifest in the disposable parent directory. Before
execing the native RPC with fakechain compatibility flags, it probes the configured
loopback daemon without proxies or redirects and requires offline fakechain mode,
zero peers and the expected fakechain genesis. `exec` retains the PID tracked by
the process owner. No compatibility override was added to `SwapWalletRpcProcess`.

The manifest schema is `monzero-fakechain-launch-v1` with `walletRpc` (the locally
verified executable path) and `daemonPort`. The manifest must be a regular,
single-link, effective-user-owned 0600 file of at most 2 KiB; duplicate fields are
rejected. Its name is `fakechain-wallet-launch.json`. Wallet, service and parent
directories must be private and the wallet path canonical. The disposable harness
remains responsible for owning the daemon and verifying the executable; RPC
self-reports alone are not cryptographic identity or process ownership proof.

Four unit tests passed (including negative subcases for mainnet, connectivity,
wrong genesis, endpoint/path changes, permissive manifest mode, hard links,
duplicates and symlinks):

```sh
python3 monzero-gui/tools/dex-engine-port/test_fakechain_wallet_launcher.py
```

The launcher has not yet been integrated into the funded owner/Qt fixture. This
addition provides that fixture's compatibility boundary without loosening the
production launch policy; it does not itself establish a funded integration pass.

### Funded owner-bound fee-preview integration

`test_owned_fee_preview.py` now starts an owned offline fakechain and invokes the
Qt test executable's `--owned-fee-job-fixture` mode. Qt starts and tracks the native
wallet RPC through the isolated launcher, provisions its helper session and uses
`NativeFeePreviewJob::startWithOwner` for funded nonrelayed construction. The Python
harness creates disposable wallets and mines rewards before publishing the request;
it performs no concurrent wallet mutations during construction. The returned
preview must be valid, non-executable, for the requested amount and have a positive
bounded fee. The harness checks unchanged balance/unlocked balance, an empty pool,
completed owner operation state and clean RPC shutdown with both credential files
removed. Private test data is removed only after owned processes stop.

Passed:

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_owned_fee_preview.py \
  --daemon build/bin/monzerod --wallet-rpc build/bin/monzero-wallet-rpc \
  --qt-driver monzero-gui/build-dex-private/src/dex_tests \
  --helper build/eigenwallet-engine-port/target/debug/native-fee-preview \
  --temp-parent build
```

All 25 Qt entries and all four launcher unit tests also passed, as did
`git diff --check`. This supersedes the earlier missing funded owner-preview test.
Wallet setup in this fixture is still performed by the Python harness; it is not
a complete GUI wallet-control/funding/swap sequence. No production launch checks,
live gates, real wallets, website packages or nodes were changed.

### Native wallet-session coordinator

`SwapWalletSession` coordinates create/open/close and owned fee-preview jobs for
one caller-owned RPC service. It tracks Unknown, Closed, Open, Busy,
RecoveryRequired and Stopped states; rejects conflicting operations; binds fee
requests to the verified open wallet address; clears old previews on wallet
changes; and does not retain wallet passwords. Open denotes checked wallet
identity only, not daemon/funding/trade readiness. The caller must still verify
the helper and ensure exclusive access to the RPC service. No QML invokable,
signing, broadcast or live trading interface was added.

The coordinator keeps a successful preview job alive until the next operation so
that its result retains the original expiry; prematurely deleting it cleared the
preview, which the first test run caught. After correcting that lifecycle, all 26
Qt entries passed. The new controlled-helper cases exercise create → preview →
close, conflicting-operation rejection, result retention/clearing and failed
preview recovery. `git diff --check` passed. CMake still skips unrelated legacy
functional suites with missing Python dependencies; those are not included in
this test claim. Real-helper and funded tests of the new coordinator itself remain
outstanding, even though its component jobs have separately passed those tests.

### Funded coordinator lifecycle

The owned-fee harness now accepts `--coordinator`. In this mode the Qt
`SwapWalletSession` creates the disposable encrypted wallet using the real helper,
previews a funded fee, closes, reopens the same wallet, and closes again. The
fixture retains its random disposable password only to exercise reopening; the
coordinator does not retain it. The Python harness prepares a destination, mines
offline rewards and refreshes while the coordinator is idle, then checks the
resulting balance, address, pool and shutdown state. No external wallet mutation
runs concurrently with a coordinator operation.

Passed with the rebuilt Qt executable:

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_owned_fee_preview.py \
  --daemon build/bin/monzerod --wallet-rpc build/bin/monzero-wallet-rpc \
  --qt-driver monzero-gui/build-dex-private/src/dex_tests \
  --helper build/eigenwallet-engine-port/target/debug/native-fee-preview \
  --temp-parent build --coordinator
```

Both the initial create/preview/close run and the extended reopen run exited zero;
the latter verified unchanged address and funded balance after reopening. The 26
Qt test entries also passed before the fixture-only reopen extension, and
`git diff --check` passed. This supersedes the coordinator's missing successful
real-helper lifecycle test, but not failure-injection coverage, visible GUI
integration, trade signing/settlement, protocol fees or production verification.

### Read-only GUI wallet status bridge

`DexEngine::attachSwapWalletSession` exposes the native coordinator's state and
preview to QML without transferring ownership or making wallet actions invokable.
It rejects cross-thread sessions and replacement of an existing attachment. The
status always reports `readyForTrading:false`; Open only reports wallet identity
verification. Destruction clears the attachment and cannot resurrect a legacy
preview. `Dex.qml` now renders unattached, unchecked, closed, open, busy, recovery
and stopped states separately from live activation checks.

All 27 Qt entries passed, including attachment lifetime, open/recovery status,
preview forwarding/clearing and disabled trading status. `qmllint` on `pages/Dex.qml`
and `git diff --check` passed. The complete GUI was not rebuilt or visually tested.
Native application startup still needs to provision and attach a verified backend;
the status line alone does not create a session or enable wallet/trade actions.

### Full local GUI build check

The complete `monzero-wallet-gui` target in `build-dex-private` compiled and linked
with the new session coordinator, status bridge and QML resource. The resulting
`monzero-gui/build-dex-private/bin/monzero-wallet-gui` has SHA-256
`7bfa3cd0d99e16b92dd1ac794021b32766ddad4ca5f83d677582016c440bd798`.

Its embedded DEX self-test exited zero with 14 checks and no failures:

```sh
cmake --build monzero-gui/build-dex-private --target monzero-wallet-gui -j2
QT_QPA_PLATFORM=offscreen monzero-gui/build-dex-private/bin/monzero-wallet-gui --dex-self-test
```

The self-test path returns before wallet startup and runs arithmetic/preview and
state-transition checks, not live settlement. The ordinary wallet interface was
not launched and the installed wallet was not replaced. This is an incremental
local build using its configured nested core, not a release package, independent
binary reproduction, full visual test or approval to enable trading. Native
backend provisioning/attachment in application startup remains outstanding.

### Coordinator cancellation

`SwapWalletSession::cancelPendingOperation` now targets only its active wallet
control or fee helper. It returns false while idle or after completion, does not
stop the RPC service, and does not claim rollback. A cancelled pending operation
completes unverified and leaves the owner requiring recovery; further wallet
operations are refused. Active-job queries distinguish a running job from a
retained successful preview. No QML invocation was added in this step.

All 28 Qt entries passed, including cancellation during wallet control, fee
construction and the valid-preview completion notification. They verify failed
completion, cleared previews, blocked retries and recovery-required shutdown.
The funded real-helper coordinator create/preview/close/reopen/close fixture also
passed again, retaining wallet identity and funds without preview broadcast.
Cancellation tests use controlled helpers; actual interrupted-RPC fault injection
and recovery reconciliation remain outstanding.

The full local GUI target was rebuilt successfully after this change. Its
offscreen embedded self-test remains a narrow arithmetic/state check, not a
wallet-opening or live-trading test. The installed wallet was not replaced.

### GUI cancellation bridge

`DexEngine::cancelNativeWalletOperation` is now the narrowly scoped QML invocation
for cancelling an attached session's pending helper operation. The DEX page shows
the button only for a busy attached backend and warns that cancellation does not
undo RPC work and may require recovery. It cannot select a process, supply wallet
credentials, start funding, sign or broadcast. It returns false with no attached
session, while idle, or in recovery state. Backend provisioning remains native
startup work, not a QML action.

All 28 Qt entries passed, including calling cancellation via Qt's invokable
interface, verifying recovery after cancellation and confirming disabled trading
status. `qmllint pages/Dex.qml` and `git diff --check` passed. The button has not
been visually exercised in a running wallet; no installed wallet was replaced.

### GUI network-fee preview action

`DexEngine::requestNativeFeePreview` now accepts a bounded destination and decimal
XMZ amount only when the attached coordinator is Open. It performs an address
shape check and exact 11-decimal/u64 amount validation, then calls the owned
nonrelayed estimator. Checksum/network validation remains in the native helper;
helper failures conservatively require recovery. The action does not select a
wallet, unlock, sign for trading, broadcast or collect an app/protocol fee.

The DEX page has an attached-backend-only preview form, with controls disabled
outside Open state and a non-broadcast/recovery warning. Accepted preview data now
includes its validated destination and formatted amount, displayed alongside the
fee, so changing form inputs does not relabel an older result. Original expiry and
non-executable status remain enforced. XMZ network fees are explicitly distinct
from the still-unimplemented DEX protocol fee.

All 28 Qt entries passed, including invokable-interface preview dispatch, exact
atomic conversion, rejection of malformed/zero/negative/overprecision inputs
without changing Open state, and destination/amount presentation binding. The
funded real-helper coordinator regression passed again. `qmllint pages/Dex.qml`
and `git diff --check` passed. Verified backend startup/attachment is still missing
from the normal application, so this does not activate a live wallet or DEX.

### Explicit coordinator teardown

The coordinator destructor disconnects owner/controller callbacks and destroys
direct child control and preview jobs before its members disappear. This keeps
job cleanup and pending-operation accounting ahead of QObject child teardown,
without forwarding preview/session callbacks from a partially destroyed object.
It includes jobs already queued for deferred deletion, not only active pointers.

All 29 Qt entries passed. New controlled-helper tests destroy the attached
coordinator during wallet control and fee construction, require no completion
notification during destruction, verify the presentation detaches, and confirm
that the owner refuses further operations and records recovery-required shutdown.
`git diff --check` passed. The full GUI and funded fixture were not rerun for this
destructor-only change; live execution remains disabled.

### Selected native core source parity

Before backend startup wiring, `check-native-core-parity.py` compared the main
core against the GUI's nested core. Their HEADs differ (`58f10f7969b05a12d2e6e733ea5a8883e8b35eee`
and `a99f85b357ee687b524bb06ea3cc933b8047a4a0`), but all six selected files matched
byte-for-byte: chain configuration, hardfork source, basic address implementation,
wallet2, wallet RPC server and RPC command definitions. The checker emits SHA-256
values and fails on differences, missing files or symlinked source files.

```sh
python3 monzero-gui/tools/dex-engine-port/check-native-core-parity.py \
  --core . --gui-core monzero-gui/monero
```

The command passed; disposable fixture checks also verified rejection of changed,
missing and symlinked files. This is a narrow source compatibility check, not
full-tree parity, binary provenance, consensus equivalence or independent review.
The wallet process/session APIs still require a caller-verified executable; the
existing crypto-verifier's binary checks apply to its separate sandboxed helper,
not automatically to wallet control. Verified backend startup remains unfinished.

### Local wallet-control preflight

The coordinator and native wallet-control job now share `validRequest` checks.
Malformed IDs, empty/oversized/NUL-containing passwords and invalid address shapes
are rejected before provisioning or changing coordinator state. Invalid input
does not imply an uncertain RPC operation and no longer puts an idle session into
recovery. Checksums/network verification remain in the native helper; failures
after dispatch retain conservative recovery accounting.

All 29 Qt entries passed with the shared validation change. After adding an early
return to avoid converting rejected password buffers, the coordinator and control
dispatcher tests were rebuilt and rerun successfully (four entries including
init/cleanup). Full GUI and funded integration were not rerun for this change.

### Fee-request coordinator preflight

`NativeFeePreviewReceiver::validExpected` now shares the request-field checks with
`SwapWalletSession` before it changes state or retires the existing preview job.
Malformed native-caller requests cannot discard a still-current estimate or put
the idle wallet into recovery without RPC work. This checks address shape, genesis
format, positive u64 atoms, u32 account and priority 1–4; it does not replace native
checksum, chain or funding checks. The receiver's existing begin/cancel semantics
are unchanged for direct callers.

All 29 Qt entries passed, including preservation of Open state and the exact
current preview after invalid destination, zero/overflow amount, overflowing
account and unsupported priority requests. `git diff --check` passed. No full GUI
rebuild or funded fixture rerun was performed for this validation refactor.

### Backend executable digest preflight

Linux `BackendExecutable::matches` checks a canonical regular executable through
an open descriptor, hashes at most 512 MiB, checks metadata stability and path
identity, and compares against a supplied lowercase SHA-256. It rejects symlinks,
multiple links, group/other write access and setuid/setgid files. The owner must be
root or the effective user. `startFreshWithDigest` applies it before creating a
service directory. Existing caller-verified APIs are unchanged.

This is not signature/provenance verification or race-free execution. Trusted
metadata, protected installation directories through launch, dependency checks and
startup integration are still required. No expected release digest is selected
automatically from the file being checked.

All 30 Qt entries passed initially. A subsequent positive-launch test exposed
group-writable (0775) build output; the fixture now uses a private 0500 copy rather
than weakening checks or changing build permissions. Both digest-boundary and
positive-launch tests passed after rebuilding (four entries including init and
cleanup). Digest mismatch creates no wallet directory. The positive test confirms
owned-child shutdown, credential removal and retained wallet data. The fixture
computes its expected hash locally only for testing, not as production trust.
`git diff --check` passed; full GUI/release verification remains outstanding.

### Digest-checked clean restart

`resumeCleanWithDigest` applies the same executable preflight before entering the
existing locked clean-restart path. A wrong digest leaves the stopped state file
unchanged and does not claim the directory; a correct digest still requires the
exclusive service lock and exact clean-stop marker. It does not permit recovery
of interrupted operations or replace provenance verification.

The rebuilt targeted tests passed (five entries including init/cleanup): restart
with competing lock, refusal of active state, wrong digest with unchanged stopped
state, successful digest-checked restart, executable boundary rejection and fresh
launch retention. `git diff --check` passed. Full GUI, full suite and funded
restart integration were not rerun for this wrapper addition. Existing native
caller-verified launch APIs remain available; production startup integration must
explicitly choose and supply trusted digest metadata.

### Session helper digest pin

Native startup can call `SwapWalletSession::pinHelperDigest` while its state is
Unknown. Pinning verifies the local helper and cannot subsequently be rebound to
a different digest. Pinned sessions recheck executable content/permissions before
wallet-control or fee dispatch, rejecting mismatches before changing state or
passing private input. Existing caller-verified sessions remain compatible; this
does not force production startup to pin until that integration is implemented.
Protected installation paths and independently trusted digest metadata are still
required, and the check is not race-free execution or signature verification.

Targeted digest-boundary and coordinator tests passed (four entries including
init/cleanup). They cover pin mismatch/rebinding rejection and successful
create/preview/close through a protected pinned helper copy, plus the existing
failed-preview recovery path. `git diff --check` passed. Full suite, full GUI and
funded real-helper integration were not rerun for this change.

### Post-open helper replacement regression

The controlled coordinator test now replaces its private pinned helper after a
successful wallet create and preview. Both close and another fee request must be
rejected without completion callbacks or pending RPC work, while preserving the
existing wallet state and original preview. Restoring the exact original fixture
helper permits clean close. This verifies per-operation checking, not just initial
pinning. The coordinator and digest-boundary targeted tests passed (four entries
including init/cleanup), as did `git diff --check`. It does not simulate a
concurrent replacement during exec or prove protection against same-user attackers.

### Funded pinned-helper integration

The funded coordinator fixture now supports `--coordinator --pin-helper`. It
hashes the selected test helper, makes a private 0500 fixture copy, and passes the
expected digest to Qt before RPC startup. Qt requires successful session pinning
and rechecks it through the create/preview/close/reopen/close sequence. The test
passed with the real native helper, retaining address and funds, leaving the pool
empty and cleaning up credentials after owned RPC shutdown.

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_owned_fee_preview.py \
  --daemon build/bin/monzerod --wallet-rpc build/bin/monzero-wallet-rpc \
  --qt-driver monzero-gui/build-dex-private/src/dex_tests \
  --helper build/eigenwallet-engine-port/target/debug/native-fee-preview \
  --temp-parent build --coordinator --pin-helper
```

The locally computed fixture digest tests content matching, not release trust or
independent reproduction. Production metadata selection, protected installation
and actual application startup remain unfinished. This test does not enable live
trading or protocol fee collection. `git diff --check` passed.

## Draft: restore-from-recovered-keys (unreviewed, not applied)

`upstream-xmz-wallet-restore.patch` drafts a `restore` operation for
`NativeWalletSession`/`decode_control`, alongside the existing `create`/`open`/
`close`. It restores a wallet via `generate_from_keys` from an
already-recovered spend/view key pair (the shape a swap participant would hold
after a counterparty's secret is revealed) and verifies the resulting address,
matching `open`'s pattern exactly. Like the rest of this module, it
constructs, signs and broadcasts nothing.

This patch has NOT been applied to the checked-out engine, built, or tested.
Writing/pushing it hit this session's own guardrail for security-sensitive
changes (it is the first code in this module to parse and handle raw private
key material), so it stops here for human review rather than being applied
automatically. Before applying: review the hex/length validation on
`spend_key`/`view_key`, confirm `generate_from_keys`'s parameter names against
the exact core RPC version in use (checked here against
`src/wallet/wallet_rpc_server_commands_defs.h` in this repo, unchanged from
upstream Monero), and extend `wallet_session.rs`'s existing test module with a
live-RPC restore round trip once a fakechain wallet-rpc fixture is available,
the same way `create`/`open` are covered today.

## Restore applied; relay primitive drafted — 2026-09-19

`build/eigenwallet-engine-port` and the local Rust 1.95.0 sysroot had been
removed since 2026-09-12 and were rebuilt from scratch: same pinned commit
(`0e17c7f7cd8f0657af176c8852aa4c9949586051`), same package versions/hashes for
the local toolchain, same eight patches in the same order. `check-upstream.sh
test` (79/79), `check-upstream.sh machine-test` (35/35) and
`verify-upstream-patches.sh` all passed with no deviation from what this file
already claimed, confirming the 2026-09-12 baseline was reproducible and not
just previously reported.

`upstream-xmz-wallet-restore.patch` was checked against
`src/wallet/wallet_rpc_server_commands_defs.h`'s actual `COMMAND_RPC_GENERATE_FROM_KEYS`
field names (`restore_height`/`filename`/`address`/`spendkey`/`viewkey`/`password`/
`autosave_current`/`language` — all matched), applied to the rebuilt checkout,
and passed all 37 `swap-machine` library tests including its own restore-specific
ones. This is the same AI assistant that drafted the patch checking its own
work again, not the independent human review the patch's own guardrail asked
for; it is applied and self-tested, not independently reviewed.

`upstream-xmz-relay.patch` adds `native_funding_rpc::relay::NativeTransferRelay`,
the module's first and only broadcast-capable primitive: the same construction
`NativeFeeEstimator` already previews with `do_not_relay:true`, except `false`.
It has no decode-from-untrusted-bytes entry point — no GUI/peer JSON reaches it,
by design, matching how `WALLET_BRIDGE_CLASSIFICATION.md` already described the
one remaining primitive both Alice's initial lock and Bob's final sweep need.
It is explicitly single-shot: no idempotency key, no retry, and the caller must
guarantee it is invoked at most once per intended transfer, since blindly
retrying could double-spend the wallet's remaining unlocked balance into a
second transaction rather than recover from a lost result. The module-level doc
comment on `native_funding_rpc.rs`, which previously claimed the module does
not broadcast, was corrected to name `relay` as the exception rather than left
stale now that the claim would otherwise be false.

Compiled and tested against the rebuilt checkout with the restore patch also
applied: 40/40 `swap-machine` library tests passed, including new tests for
`relay`'s construction parameters (relay flag never omitted, signing-artifact
export always disabled) and response parsing (exact principal, positive
fee/weight, canonical lowercase-hex txid, rejection of any leaked signing
artifact in the response). One test failure surfaced during development —
`tx_hash` validation initially accepted uppercase hex via `is_ascii_hexdigit()`
instead of requiring the same canonical-lowercase form as this module's other
hash/key fields — and was fixed before the patch was finalized.

The first extracted copy of this patch was broken and was caught and replaced
before being reported as done: `git diff` scoped to `native_funding_rpc.rs`
and `relay.rs` silently produced no hunk for `native_funding_rpc.rs`, because
that file (like `fee_estimation.rs` and `wallet_session.rs` before it) is
untracked in this disposable checkout — the eight base patches were applied
with `git apply`, which doesn't stage new files. The resulting patch therefore
added `relay.rs` without ever wiring `pub mod relay;` into the module tree or
fixing the module doc comment, and would have applied "successfully" to a
fresh checkout while silently failing to compile the new module in. Rebuilt
by diffing reconstructed before/after trees directly (bypassing git's
tracked/untracked distinction) and verified three ways against a scratch
checkout containing only the base + restore patch: `git apply --check`,
forward apply reproducing the live tree byte-for-byte, and `git apply --check
--reverse`. `upstream-xmz-wallet-restore.patch` and every patch before it were
generated by an earlier session using the same `git diff`-on-untracked-files
approach and were not re-verified against this specific failure mode; they
happened to be self-contained new files with no sibling edit to an untracked
file in the same patch, which is presumably why it didn't surface earlier.
Worth an independent check before trusting any of this series' "reverse
applicability verified" claims at face value.

This patch has not been applied to any wallet outside this disposable local
checkout, has not been exercised against a real wallet-RPC process, and has
not received human review; treat it with at least the same caution as the
restore patch, more, since it is the one piece of code in this module that can
actually move funds.

`upstream-xmz-swap-wallet-spendable.patch` adds the orchestration function
wiring restore → estimate → relay into one `swap_wallet_spendable`-shaped call
(`native_funding_rpc::spendable::swap_wallet_spendable`) Bob's side can invoke,
plus the one supporting primitive it needed that didn't exist yet:
`NativeWalletSession::import_transaction`, which wraps `scan_tx` to import a
specific known transaction into the restored wallet without a full rescan,
mirroring upstream's `scan_transaction` step. `bob::State5` in
`swap-machine/src/bob/mod.rs` supplies every field upstream's version needs —
`xmr_keys()` for the combined `s_a + s_b` spend key and view key,
`monero_wallet_restore_blockheight` for the restore height, and
`lock_transfer_proof.tx_hash()` for the Monero lock txid — and both
`monero_oxide_ext::PrivateKey` and `swap_core`'s `PrivateViewKey` already
`Display` as the exact canonical lowercase hex `restore` requires, so no new
serialization code was needed there.

Unlike `restore` (a direct mechanical port of a named upstream operation) and
`relay` (a narrow, single-purpose extension of an existing pattern), this
function's exact shape is this session's own design choice: upstream's
`Wallets` type holds a live wallet object across calls, while this port talks
to wallet-rpc statelessly per call, so `swap_wallet_spendable` here
constructs three independent `NativeFundingRpc` clients (one each for the
wallet session, the fee estimator and the relay primitive) from the same
daemon/wallet ports and credentials, cloning the credential strings once per
client since none of those three types support a shared/borrowed client.
Compiled and tested against the rebuilt checkout with restore and relay both
applied: still 40/40 `swap-machine` library tests passed (this patch adds no
new tests of its own — there is no pure, network-free logic in it left to unit
test beyond what `restore`, `import_transaction`, `estimate` and `relay`
already cover individually). Verified the same three ways as the relay patch
fix above, from a scratch checkout containing the base plus the restore and
relay patches. Not applied to any wallet outside the disposable local
checkout, not exercised against a real wallet-RPC process end to end (no
fixture yet drives it with a real restored wallet, a real lock transaction and
a real relay together), and not reviewed by anyone independent, including for
whether this three-client composition is the right shape at all rather than
just a shape that compiles.

## `swap_wallet_spendable` exercised end to end against a real fixture — 2026-09-19

Built `test_swap_wallet_spendable.py`, a new fixture alongside
`test_wallet_recovery.py` and driven the same way `native_fee_estimation` is:
a real fakechain daemon and wallet-rpc process, with the Rust side run as an
`--ignored` test (`native_swap_wallet_spendable`, added to
`swap-machine/tests/bitcoin_settlement.rs`) rather than a standalone binary.
`swap-machine/tests/native_rpc.rs`'s existing `authenticated_fixture()` was
refactored into a shared `fixture_connection()` returning the raw
`(daemon_port, wallet_port, username, password)` tuple, since
`swap_wallet_spendable` builds three independent `NativeFundingRpc` clients
itself rather than accepting a pre-built one.

The fixture: create a funded miner wallet; create a second disposable wallet
("shared") and extract its keys to stand in for `bob::State5::xmr_keys()` —
the shape a swap participant would hold after a counterparty's secret is
revealed, minted directly here since there is no real two-party swap
negotiation in this fixture, only the wallet-bridge primitive that runs after
one; send an ordinary transfer from the miner to that shared address (Alice's
initial lock, in miniature); mine confirmations; then hand the shared
wallet's recovered keys, the lock txid and a destination address to
`swap_wallet_spendable` twice — once with a deliberately wrong chain genesis
(must be rejected by the fee-estimate step, after restore and import have
already succeeded) and once for real.

Reproduce from the core repository root:

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_swap_wallet_spendable.py \
  --daemon build/release/bin/monzerod --wallet-rpc build/release/bin/monzero-wallet-rpc \
  --engine-test-binary build/eigenwallet-engine-port/target/debug/deps/bitcoin_settlement-<hash> \
  --temp-parent build
```

Use the actual executable path Cargo prints for the `bitcoin_settlement` test
binary; its hash is build-specific, matching every other fixture in this
document that drives a Cargo-built test executable directly.

All five assertions passed on the first real run: the wrong-genesis attempt
rejects before relaying; a second `swap_wallet_spendable` call (different
swap-wallet id, same keys) succeeds, proving the first attempt didn't leave
the wallet-rpc service wedged; the destination wallet shows the exact
requested amount confirmed on-chain; and the shared wallet's remaining
balance correctly accounts for the relayed amount plus its native fee.

Building this fixture caught a real bug in `spendable.rs` that no unit test
would have: `swap_wallet_spendable` never closed the wallet it restored, on
either success or failure. Since a wallet-rpc process holds at most one open
wallet at a time, the very first call — success or failure — would have
wedged the service for every subsequent one, including a caller's own retry
after a failure. This is exactly the retry scenario the function's own
single-shot warning already called out, just not the specific failure mode
that warning anticipated. Fixed by always attempting a best-effort close on
the way out of `swap_wallet_spendable`, explicitly not letting a close
failure override a successful relay result: the relay having already
broadcast is a fact, and reporting it as an error just because cleanup
afterward also failed could push a caller into retrying and double-spending
the wallet's remaining balance, which is worse than leaving one wallet-rpc
process wedged. There is no logging facility in this module to report a
silent close failure through instead, so it stays silent by design.

Regenerating `upstream-xmz-swap-wallet-spendable.patch` after this fix, and
saving the new `upstream-xmz-swap-wallet-spendable-fixture.patch`
(`native_rpc.rs` refactor + the new ignored test), used a fresh, truly
disposable clone of the pinned commit rather than the live checkout's
untracked-file diffing, specifically to avoid repeating the extraction bug
caught earlier tonight: all ten patches (eight base + restore + relay) were
applied in order to a brand-new `git clone`, the two new patches were
generated against that known-good tree, applied back in sequence, and every
touched file was verified byte-for-byte identical to the live checkout before
being saved. `check-upstream.sh machine-test` was rerun after the close-on-exit
fix and still passes 40/40.

This fixture proves the wallet-bridge primitive itself works against a real
wallet-rpc process with a real broadcast transaction. It does not yet prove
anything about a real two-party swap: `bob::State5`'s actual key derivation,
Alice's real initial lock via the negotiated protocol, and the real Bitcoin-side
redeem/refund paths are not exercised by this fixture at all, which is why the
"first acceptance milestone" in `DEX_FIRST_RELEASE_SCOPE.md` is still a
distinct, larger piece of work, not something this closes out.

The release-checklist and adversarial-review sign-off gates remain untouched
and out of scope, as this file already said before tonight.

## Protocol-recovered native spending — 2026-09-19

`test_wallet_recovery.py --native-spendable` connects the existing combined
Bitcoin settlement/fakechain fixture to the Rust `swap_wallet_spendable`
primitive. It requires one of the two real Bitcoin settlement test names,
not the mocked transaction-weight fixture. The successful redemption path
uses Bob's actual State5-derived spend key; the refund path uses Alice's
key recovered from the observed Bitcoin refund transaction.

After negotiated funding, native transaction verification, Bitcoin settlement,
and wallet import/restart checks, the driver closes the observer wallet and
invokes the native spending test with those recovered keys. The native test
rejects a wrong genesis first, then restores/imports/relays on the expected
chain. The driver checks its completion marker, confirms the exact payment
at the destination and checks the source wallet's recorded outgoing fee
against conservation of the original locked amount.

Run each path separately from the core root, replacing the test binary suffix
with the current Cargo-produced executable:

```sh
python3 -u monzero-gui/tools/dex-engine-port/test_wallet_recovery.py \
  --daemon build/release/bin/monzerod \
  --wallet-rpc build/release/bin/monzero-wallet-rpc \
  --engine-test-binary build/eigenwallet-engine-port/target/debug/deps/bitcoin_settlement-<hash> \
  --engine-test-name engine_redeem_is_accepted_on_regtest \
  --native-spendable --temp-parent build
```

For the refund path, substitute
`--engine-test-name engine_cancel_and_full_refund_are_accepted_on_regtest`.

This mode spends 90% of the locked XMZ principal, leaving explicit native-fee
headroom and a residual balance. It does not prove full-balance recovery.
Negotiation is driven in-process and key handoff uses private fixture files;
it does not prove two separately deployed peers, a production transport,
durable recovery around every broadcast boundary, or GUI trading. The fixture
uses existing native and Rust executables; it is not a fresh binary reproduction.
Live execution and release approval remain gated.

## Broadcast uncertainty boundary — 2026-09-19

`upstream-xmz-relay-outcome.patch` gives failures after entering the transfer
dispatch path a typed `BroadcastOutcomeUnknown` error. Transport failure, RPC
rejection and malformed success replies are conservatively treated as possibly
broadcast; the public error carries no RPC response or signing material.
Preflight failures remain distinguishable because they precede dispatch.

The relay instance marks itself dispatched before awaiting the RPC. A second
submission on that instance is rejected after success, error or cancellation.
Unit tests exercise classification/redaction and cancellation against an owned
nonresponding loopback listener. This is an in-memory guard only: constructing
another instance or restarting a process does not preserve it. Production must
persist intent before dispatch and reconcile wallet/chain evidence rather than
blindly reconstructing and sending another transfer. Neither this guard nor
the typed error constitutes durable recovery or permission to enable trading.

## Durable native spend intent — 2026-09-19

`upstream-xmz-spend-journal.patch` makes the native spending orchestration
require a caller-controlled private journal directory. It reserves a record
identified by a domain-separated SHA-256 of chain genesis, source wallet and
locked transaction ID before entering relay. Mutable amount/destination and
retry UUID do not create a different reservation. The record contains public
transfer terms, not keys or credentials, but still requires private storage.

On Unix, the directory must be canonical, owned by the effective user and not
accessible to other users. Record creation uses `openat` against the checked
directory descriptor with exclusive creation and no symlink following. Intent
file and directory are synced before relay. Any existing record, even empty or
truncated, blocks another reservation. Receipts are likewise exclusive and
synced; they record the validated relay hash, fee and weight.

`SpendOutcome` separates the successful transfer from `receipt_persisted` and
`wallet_closed` health flags. Failure to store a receipt or close a wallet must
not turn an already-successful broadcast into an apparent failed payment.
On an error/cancellation, callers still must reconcile wallet and chain state.

The journal has no delete/reset operation and does not yet implement a recovery
reader or reconcile uncertain transactions. It assumes the directory and its
ancestors remain under trusted exclusive control, persist across restarts, and
are not rolled back. A different directory or direct use of the lower-level
relay can bypass this guard; production startup must enforce the storage and
API boundary. This is not a complete trade journal or live-trading approval.
Native Windows journaling is explicitly unsupported until implemented/tested.

Unit coverage includes a new process rejecting an existing intent, changed retry
UUID/terms, a torn record, directory permissions and write-once receipts. The
combined fixture additionally requires successful receipt persistence and wallet
close, and verifies a new retry UUID cannot reserve the same lock after payment.

## Prepared transaction identity and fee ceiling — 2026-09-19

`upstream-xmz-prepared-relay.patch` changes native spending to prepare a
transaction with `do_not_relay=true`, validate its principal and an explicit
`maximum_fee`, and persist its public hash/fee/weight before broadcast.
The journal checks the prepared chain, wallet, destination, principal, account,
priority and fee ceiling against its intent. `relay_prepared` uses `relay_tx`
with that same opaque metadata instead of constructing another payment and
requires the returned transaction hash to match the prepared hash.

Unlike the display-only fee estimator, preparation intentionally requests
private signed metadata. It stays in a non-Debug, non-serializable native type
with a zeroizing metadata buffer; this does not promise that all temporary
JSON/HTTP copies are zeroized. No private metadata is put in the public journal,
test output, QML, or a peer message. The journal's `.prepared` record has only
schema, transaction ID, fee and weight. The existing preliminary estimator is
still advisory; the fee cap is enforced against the actual prepared transaction.

The integration fixtures use a test-only 0.1 XMZ maximum network fee; this is
not a production fee policy or a protocol fee. Both Python drivers now verify
intent, prepared record and receipt against the confirmed transfer's identity,
principal and recorded fee. Approval UI, a recovery reader, encrypted/private
payload persistence if required, and a vetted rebroadcast policy remain future
work. If the process dies before broadcast, metadata may be lost; the intent
must not simply be deleted or absence on-chain treated as permission to retry.

## Read-only restart reconciliation — 2026-09-19

`upstream-xmz-spend-recovery.patch` adds `RecoveryRecord`. The private journal
reader rejects unsafe files, noncanonical/duplicate-key JSON and mismatched
intent/prepared identities. New intents include the generated wallet filename;
older intents without that field fail closed. No automatic migration or reset
is provided. Reconciliation requires the expected open wallet, chain genesis,
exact outgoing payment and sufficient confirmations at an unchanged chain tip.
It does not open wallets, refresh, sign, broadcast or write journal records.

Both combined `--native-spendable` redemption and full-refund fixtures passed
with the success receipt deliberately withheld. After wallet-RPC restart, a
separate `native_recover_confirmed_spend` test process confirmed the prepared
transaction using the remaining intent/prepared records. The drivers checked
unchanged balances, an empty transaction pool, no recreated receipt and unchanged
journal bytes. An excessive confirmation requirement was rejected as well.

These checks trust dedicated local wallet/daemon accounting; they are not an
independent inclusion proof. Receipt withholding is not a comprehensive
power-loss test. Missing transactions remain unresolved, never retry authority.
GUI recovery presentation, trusted service startup and broader two-peer crash
recovery acceptance are still required before live trading.

## Native recovery command boundary

`upstream-xmz-recovery-command.patch` adds helper mode
`--recovery-stdio-v1 --session-file <private-session> --journal-directory <trusted-directory>`.
The launcher, not a peer or request payload, supplies the journal directory.
The existing private session lock and authentication rules still apply.
Input and output are bounded to 2048 bytes with a 30-second overall deadline.

Request schema `monzero-native-recovery-request-v1` requires string fields
`requestId`, `genesis`, `wallet`, `lockTxid`, `txid`, `minimumConfirmations`.
The UUID must be canonical and nonzero; confirmations must be a positive
canonical uint64 string. Unknown/duplicate fields and a session-genesis mismatch
are rejected. The prepared transaction must match the requested transaction ID.

A successful compact JSON response uses schema `monzero-native-recovery-v1`,
echoes the bound request/chain/wallet/transaction identities, reports string
`confirmations`, `status: confirmed`, and `source: local-wallet-reconciliation`.
Both `retryAllowed` and `executable` are false. Errors produce no success response
and only a generic stderr diagnostic. The command never opens a wallet, signs,
broadcasts or changes the journal. The owner must first open the correct wallet.

Verified at this checkpoint: 49 engine library tests, affected-package
all-features/all-targets compilation, and eighteen-patch reconstruction.
The new helper pipe mode has not yet been exercised end to end against a real
disposable wallet or connected to the GUI process owner. Existing recovery
integration evidence predates this command wrapper and does not prove its
transport integration.

The combined fixture now exercises this pipe mode when both `--native-spendable`
and `--fee-preview-helper` are supplied. After restart with the receipt withheld,
it rejects wrong genesis/transaction IDs, excessive confirmations and a
request-selected journal directory, then checks the exact compact success
response. Existing balance, pool and journal immutability checks follow these
commands. Set `MONZERO_TEST_QT_RECOVERY_DRIVER` to the dedicated compiled
`dex_recovery_receiver_tests` executable to additionally launch the helper from
Qt and decode its real response with `NativeRecoveryReceiver`. This is a
disposable integration driver, not production process-owner wiring.

Both extended redemption and refund runs passed, including the actual native
helper response accepted by the Qt recovery receiver after restart with a
missing receipt. Rejected commands left no stdout and only the expected generic
stderr diagnostic. Balance, pool and journal immutability checks passed.
The Qt decoder regression executable was rebuilt
with `-Wall -Wextra -Werror` and passed, and the Rust integration binary/helper
were rebuilt successfully. These tests use a synchronous Qt test driver, not
the new asynchronous GUI job; its lifecycle and session wiring remain separate
verification work.

## Separate Alice process fixture

`upstream-isolated-alice-peer.patch` moves Alice's negotiation state and Bitcoin
cancel/redeem signing into a child process for Unix settlement tests. Bob remains
in the parent. A private UUID directory holds the Unix socket; bounded frames
bind the fixture schema, swap ID, sequence and command, and ambiguous JSON is
rejected. State is not serialized back to Bob. Child exit is acknowledged and
checked, with owned-child cleanup on parent errors.

This is test infrastructure, not an authenticated production peer protocol.
The local harness still controls both Bitcoin wallets and both chain fixtures,
and has the same user privileges as the child. Refund recovery keys may cross
this private test channel for the existing disposable native spending test.
Negotiated values are fixed regtest fixture terms, not user-approved live quotes.
It does not prove independent operators, remote machines, quote authentication,
durable Alice restarts or public peer safety.

The first combined redemption run passed with the separate child. The additional
frame-context rejection test passed after adding canonical JSON checks. Both
combined redemption/refund runs against that final binary both exited zero.
Each passed native spending with protocol-recovered keys, exact fee accounting,
restart reconciliation with a withheld receipt and the real helper response
accepted by the Qt recovery receiver. This does not exercise Alice state restart
or a GUI-driven trade. Nineteen ordered patches
reconstruct the indexed engine sources.

### Alice checkpoint/crash fixture

`upstream-alice-checkpoint-fixture.patch` adds a twentieth reconstruction patch.
After funding verification and before redeem/cancel, the trusted harness asks
Alice to save her negotiated State3 in a new private file and sync both file and
directory. It then kills that child and starts a new child, which restores the
checkpoint and resumes the sequence-bound socket exchange. The parent compares
the restored public funding keys; it never reads the saved private state.

The fixture binary and affected package all-features/all-targets checks compile,
and twenty-patch reconstruction passes. Combined native redemption and refund
runs both exited zero with the forced Alice restart, native spending and Qt
recovery checks. This tests one
acknowledged boundary twice: sequence-numbered immutable checkpoints allow two
consecutive forced restarts without overwriting the earlier checkpoint. Restore
checks the expected sequence and bounds the file read before parsing. The final
two-restart redemption and refund runs both passed. This remains one
acknowledged checkpoint boundary, not torn writes, all irreversible boundaries,
independent operators or a production trade journal. Checkpoint files contain
disposable test secrets and are removed with the owned fixture directory.

The fixture also passes Alice restart after cancel/redeem signing, reconstructing
the transaction with the same transaction ID and broadcasting the reconstructed
version. Refund additionally restarts Alice after the Bitcoin refund confirms,
before XMZ key recovery. Both combined native settlement paths passed. This
does not verify witness-byte identity or durable peer message delivery: Bob's
encrypted signature is still retained by the trusted parent test process.

## BCH admission helper boundary (10 October 2026)

`native-bch-admission` provides `--admission-review-stdio-v1` and `--admission-submit-stdio-v1`, each followed by an absolute private configuration path.
It reads one bounded JSON request from stdin through EOF and emits one JSON response on success; failures return a generic stderr message and no stdout.
Request schemas are `monzero-bch-admission-review-request-v1` and `monzero-bch-admission-submit-request-v1`, with a non-nil `requestId`; submit also requires the exact lowercase 64-character review `commitment`.
Unknown fields, mismatched modes and requests over 512 bytes are rejected.

The `monzero-private-bch-admission-v1` configuration retains the trusted executable path/digest, quote record path, quote admission time/commitment, trade ID, ordered BCH/XMZ identity pins, local policy, local role, separate metadata/snapshot directories and five negotiation pins.
The policy uses its Rust snake_case fields, explicit `xmz-mainnet`/`xmz-testnet`/`xmz-stagenet` strings and rejects unknown fields.
Configuration and quote records must be canonical absolute paths to singly linked, current-user-owned regular files with mode 0600, at most 16 KiB; the helper holds exclusive file leases for the operation.
The app launcher must independently authenticate the executable against its trusted digest before launching it; the configured digest is not self-authentication.

Review revalidates the retained jointly signed quote against independent pins and current expiry, without opening secret storage.
Submission checks the complete role/policy/quote/negotiation review before delegating to the existing admission validator.
An `admitted` response means only that negotiation admission succeeded; `executable` remains false, and these admission operations perform no wallet RPC, funding, signing or broadcast.
It is restricted to a locally configured BCH regtest policy and is not wired into the app's controls yet.

`test_bch_admission_helper.py --helper /absolute/path/to/native-bch-admission` runs offline subprocess tests using Python cryptography to sign independent quote fixtures.
It covers successful quote review, request/response correlation, malformed requests, unsafe files, altered signed records/pins, changed consent and rejection of missing negotiation state.
It does not prove successful admission with a full retained negotiation or a real-node swap.

The helper also provides `--admission-status-stdio-v1` with schema `monzero-bch-admission-status-request-v1` and a non-nil `requestId`; no approval field is accepted.
Status revalidates the saved execution admission, its complete negotiation and the configured negotiation pins, and returns the original `admittedAt` and review commitment without writing storage.
It uses the original admission time, so expiry prevents a new review/submit but does not hide a previously admitted exchange.
After a lost submit reply, use status to reconcile; do not automatically resubmit with a new timestamp.
A missing, corrupt, busy or differently pinned admission produces an error, not a fabricated “not started” state.

The two ignored library tests named `bch_admission_helper_roundtrip_retained_negotiation` and `bch_admission_helper_roundtrip_expired_negotiation` explicitly require `MONZERO_BCH_ADMISSION_HELPER` and `MONZERO_BCH_ADMISSION_HELPER_SHA256`.
They authenticate the provided binary before invoking it, construct complete local cryptographic negotiation records, and exercise both roles through actual helper processes.
Run the `bch_admission_helper_roundtrip_` filter with `--ignored --test-threads=1` against the matching library test binary.
These fixtures are offline; no nodes, wallets, mining or network services are started.
Their runtime outcome must be read from their specific validation log, independently of the fast helper tests.

Reviewed admission retains the exact approved review and commitment before publishing its admission record, under the same metadata lease.
Status requires that saved approval and rejects any changed local policy, even if the changed policy would still accept the signed quote.
Legacy low-level admission records without the saved full review are insufficient for the app status response.
Conflicting admission timestamps are rejected before creating consent records; the helper does not silently retry admission or reconstruct missing consent.
The complete-negotiation fixtures additionally exercise changed policy, missing/corrupt consent and conflicting prior admission.

## Private BCH admission in the Qt app

With the existing private-prototype opt-in enabled, `--dex-bch-admission /absolute/private/config.json` attaches the Linux admission helper configuration and performs a read-only status check.
Select BCH / XMZ to review the signed agreement, explicitly approve saving it, or check its retained status.
The configuration must refer to an independently trusted helper binary and an already retained negotiation; this option does not create peer negotiation records or start a wallet/node.
The app checks executable/configuration/quote contents before and after each bounded operation, validates the exact response context and public consent digest, and never retries a submit automatically.
After an uncertain response, use the saved-agreement check.
Only admission is connected here; funding, settlement and live trading remain unavailable.

The launcher rejects numeric configuration values outside the exact Qt JSON integer range instead of rounding them.
Signed quote amount strings are formatted without floating-point conversion.
`test_bch_admission_qt.py --helper /absolute/path/to/native-bch-admission --qt-tests /absolute/path/to/dex_bch_admission_tests` supplies an independently signed quote to the Qt suite and exercises actual helper review plus missing-negotiation rejection.
The remaining Qt cases use a pinned local fixture process to test malformed responses, changed sources, one-shot consent, lost replies and restart status.
For QML presentation tests, supply the application resource bundle and the `fonts` import path; use the test-only QML runner so no wallet or daemon is constructed.

For the full retained-negotiation fixtures through Qt, build `dex_bch_admission_probe` and supply `MONZERO_BCH_QT_ADMISSION_PROBE` and `MONZERO_BCH_QT_ADMISSION_PROBE_SHA256` alongside the existing Rust helper path/digest variables.
Run the same `bch_admission_helper_roundtrip_` library-test filter with `--ignored --test-threads=1`.
The Qt probe is a test executable around the production admission job; the fixture still independently pins the real Rust helper in its configuration.
The probe checks the app's review and submit states and returns only responses already validated by the Qt job.
The two complete fixtures passed through this path on 10 October, including both roles and expiry recovery.
They save and inspect negotiation admission only, and provide no evidence of a funded Qt-driven BCH swap.

## Private BCH retained funding review

The Linux `native-bch-admission` helper also accepts `--funding-review-stdio-v1 /absolute/private/funding.json`.
The separate wrapper contains exactly `schema: "monzero-private-bch-funding-v1"`, `admissionConfigFile` (the existing absolute admission configuration path), and `deploymentCommitment` (the independently retained, nonzero, lowercase SHA-256 deployment digest).
It uses the same strict private-file ownership, permissions, identity and lease checks as admission.
No RPC credentials belong in this wrapper.
The caller must still authenticate the helper executable independently.

Send `{"schema":"monzero-bch-funding-review-request-v1","requestId":"<non-nil UUID>"}` on stdin.
The request does not accept an approval commitment or a broadcast instruction.
The response correlates `requestId` and `tradeId`, contains the exact engine funding `review.details` and `review.commitment`, and sets `executable` and `chainObserved` to false.
The BCH-funder role, saved full admission consent, original negotiation pins, current quote, signed funding, deployment and retained private recovery must all validate.
The response exposes public transaction/recovery identifiers and commitments, never secret recovery bytes.

This is an offline review of already prepared funding; it neither prepares funding nor reserves inputs, signs or broadcasts.
The controller's existing relay still compares the actual backend deployment and performs fresh wallet/input checks before any broadcast.
The private Qt submission controls described below use this exact review as their approval boundary.

For the complete retained-funding fixture, set the existing `MONZERO_BCH_ADMISSION_HELPER` and `MONZERO_BCH_ADMISSION_HELPER_SHA256` pins and run the library-test filter `bch_funding_helper_reviews_retained_transaction_without_rpc_or_writes --ignored`.
It prepares/signs only against a local HTTP mock and stops that mock before running the production review helper.
The fixture checks exact response details/commitment, deployment mismatch, missing consent, corrupt recovery and unchanged retained records.
This fixture passed on 10 October; it provides no real-node or funded Qt swap verification.

The private Qt app now accepts `--dex-bch-funding /absolute/private/funding.json` with the existing `MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE` opt-in.
Attaching the configuration starts no operation; select BCH / XMZ and request the saved funding review explicitly.
`BchFundingJob` pins the wrapper, admission configuration, signed quote and helper bytes before and after each bounded request.
It validates the exact response context, deployment, quote, principal, bounded BCH fee/outputs, recovery identifiers and canonical engine review digest before displaying anything.
Changed sources invalidate the job; failed reviews clear previously displayed results.
The review-only attachment distinguishes retained transaction data from current chain evidence; sending requires the separate submission attachment described below.
The helper must already have an admitted negotiation, prepared/signed funding and retained recovery from the engine; this UI does not create those records.

For the complete funding fixture through Qt, build `dex_bch_funding_probe` and supply `MONZERO_BCH_QT_FUNDING_PROBE` and `MONZERO_BCH_QT_FUNDING_PROBE_SHA256` alongside the Rust helper pins.
The fixture continues to pin the real helper in its private admission configuration and independently authenticates the test-only Qt process adapter.
The complete Qt/helper funding review passed on 10 October, with the mock RPC server stopped and retained records unchanged during each review.
The app still needs funding preparation/signing, chain reconciliation, settlement controls and peer transport integration before a full BCH swap can run through the GUI.

## Approved BCH funding relay and retained status

The helper additionally implements `--funding-submit-stdio-v1` and `--funding-status-stdio-v1`.
Submission uses a separate private wrapper containing exactly `schema: "monzero-private-bch-funding-submit-v1"`, `fundingConfigFile` and `rpcFile`, both absolute paths.
The referenced funding configuration is the existing review wrapper and must match the already retained deployment.
The RPC descriptor contains `schema: "monzero-private-bch-funding-rpc-v1"`, `port`, `wallet`, `anchor`, `anchorScript`, `cookieFile`, `checkpointHeight`, `checkpoint` and `subversion`.
The network is fixed to BCH regtest and the endpoint to IPv4 loopback.
The trusted caller must authenticate and own that local process before providing this descriptor; private files and RPC identity checks alone do not establish process ownership.
The cookie is a separate bounded owner-only regular file, permits mode 0400 or 0600, and is read without taking the producer's lease.
No credentials are passed on the command line or returned in responses.

A submit request has schema `monzero-bch-funding-submit-request-v1`, a non-nil `requestId`, and the exact lowercase `commitment` returned by funding review.
It requires original saved admission consent, unchanged negotiation and funding/recovery records, current terms and the exact pinned backend.
Relay uses the existing controller's fresh chain/input/lock checks and immutable intent before its single RPC attempt.
The helper refreshes wall-clock time immediately before entering relay so time spent restoring cryptographic records does not extend the funding deadline.
An uncertain result fails without a success response; it does not authorize resubmission.

Status takes the original funding-review wrapper, with schema `monzero-bch-funding-status-request-v1` and `requestId`; no commitment or RPC file is accepted or needed.
It returns the original retained review, including after quote expiry, and a `relay` object with `state` (`not-attempted`, `attempted` or `acknowledged`), the exact `txid`, and `confirmed: false`.
An attempt may have been interrupted before or after sending, and acknowledgement is not confirmation.
Both status and successful submission retain `executable: false` and `chainObserved: false`; they provide no current funding confirmation or live-market enablement.
Historical status cannot renew an expired quote's submission approval.
The private Qt funding panel now exposes explicit submission and retained-status controls, as described below.

The four `bch_funding_helper_` ignored process fixtures passed on 10 October with the pinned new helper: read-only review, historical status after expiry, one acknowledged relay and lost-reply refusal to retransmit.
They prepare against a local RPC mock, exercise real retained cryptographic records and invoke separate production helper processes.
The status cases remove the RPC descriptor and credentials and stop the mock before checking retained state.
The existing Qt admission checks and complete Qt funding-review fixture also passed with this helper.
These checks do not demonstrate real-node consensus or complete GUI settlement.


## Private Qt funding submission and saved status

Use `--dex-bch-funding-submit /absolute/private/submit.json` with the existing private-DEX opt-in to attach the submission wrapper above. Startup requests saved status only. If `--dex-bch-funding` is also supplied, both attachments must reference the same funding configuration. The caller must independently authenticate and own the BCH regtest process; the GUI does not establish process ownership from the descriptor.

In BCH / XMZ, check saved funding status, request a fresh funding review, inspect the exact transaction, outputs and fee, then check the explicit consent box before sending. Submission requires saved state `not-attempted`, a current review and its exact commitment, and unchanged funding, admission, quote, helper, submission and RPC-descriptor bytes. The job independently reconstructs the pinned deployment from the nine-field RPC descriptor. Credentials remain helper-owned.

Consent resets when the market or funding status changes. An attempt consumes this job's submission permission before launching the helper. A restored attempted or acknowledged transaction cannot be sent again through Qt. A lost reply leaves the outcome unknown until saved status is checked; it never enables another send. Acknowledgement is explicitly not confirmation.

Saved-status checks use the original funding wrapper and remain available after quote expiry or removal of the submission descriptor, RPC descriptor and credentials. Historical review details are displayed separately from a fresh approvable review. These checks read retained records, not current chain state; fresh-chain reconciliation and complete funded GUI settlement remain unfinished.

The funding-job suite passes 51 reported Qt cases, and the QML presentation suite passes 35. The complete Qt/helper process run uses the four `bch_funding_helper_` fixtures with the existing helper and Qt probe pins. All four passed on 10 October in 347.91 seconds, followed by the all-features/all-targets Rust check. Source and copied-binary hashes match provenance. The verdict is recorded in `build/bch-qt-relay-full-validation-status-20261010.json`; do not infer real-node verification from these local mock fixtures.


## Read-only BCH funding chain observation

`--funding-observe-stdio-v1` uses the same private submission wrapper and pinned RPC descriptor as funding relay, but accepts only `schema: "monzero-bch-funding-observe-request-v1"` and a non-nil `requestId`. An approval commitment is rejected. The caller must still independently authenticate and own the local BCH regtest process.

The helper restores original admission consent and exact retained funding/recovery, validates historical relay records, then calls the existing read-only signed-funding observer against the pinned deployment. Commercial expiry does not prevent observation. It never prepares, signs, broadcasts, clears an attempt or writes a chain-status cache.

A successful response retains the original funding review and historical `relay` object, sets `chainObserved: true`, and adds `observation` with exactly `state`, `txid`, `tip`, `confirmations`, `lockOutputUnspent` and `observedAt`. States are `confirmed-unspent`, `mempool` and `unobserved`. Confirmation depth is a decimal string only for confirmed-unspent funding; otherwise it is null. Unspent status is boolean for confirmed or mempool observations and null when unobserved. The timestamp is a decimal Unix-seconds string sampled after observation. Historical `relay.confirmed` stays false; confirmation evidence belongs to the separate point-in-time observation.

Unobserved can mean unknown, evicted, conflicted or already spent. It never proves that sending did not happen. A changed chain or inconsistent output is an error, not absent funding. Observation does not apply the native funding window or authorize settlement; every later action must obtain fresh evidence and enforce its own policy. Private Qt observation controls and their complete retained-transaction fixtures are verified against local RPC mocks.

All four complete helper process fixtures passed on 10 October (459.92 seconds), including observation after expiry, confirmed/mempool/unobserved output states, changed-output/tip rejection and no new relay after observation of a lost-reply attempt. The 10 helper unit tests, seven subprocess checks, both retained-negotiation fixtures and 20 Qt admission checks also passed. The all-features/all-targets Rust check and source/binary provenance checks passed. Evidence is recorded by `build/run-bch-funding-observe-helper-validation-20261010.py`. The ordered patch is `upstream-bch-funding-observe-helper.patch`.


The private funding panel now provides **Check funding on the node**, using the existing submission attachment. `BchFundingJob::observe` validates the exact review and transaction, pinned configuration bytes before/after RPC, strict observation state/depth/unspent fields and an observation timestamp inside the current request interval. The engine exposes `requestBchFundingObservation()` separately from saved-status inspection. The displayed node tip, depth and check time are point-in-time evidence, not settlement permission.

Each operation clears earlier observation evidence. Failed responses or changed RPC sources clear both display evidence and the cached response. Once a job observes the funding transaction in the mempool or as confirmed unspent, it will not offer another funding submission, even if a later observation is absent. Saved-status inspection stays available if RPC configuration disappears. Sixty-five Qt funding-job cases and 36 QML cases pass; the app build passes. All four complete Qt/helper observation fixtures passed on 10 October (471.37 seconds), followed by the all-features/all-targets Rust check. Source and copied-binary hashes match recorded provenance. Evidence is retained by `build/run-bch-qt-observe-full-validation-20261010.py`; the fixture patch is `upstream-bch-qt-observe-fixture.patch`. These are local RPC mock checks, not real BCHN consensus or a complete funded GUI swap.


The next BCH funding-setup integration needs a wallet preparation adapter; current helpers consume retained prepared/signed funding. The pinned BCHN review in `bchn-funding-preparation-source-review.json` records its exact source hashes and preparation gates. In particular, BCHN's `FundTransaction` appends unsigned inputs with sequence `UINT32_MAX - 1`, whereas our `FundingPreview` requires `Sequence::MAX`. Do not pass wallet output straight through or weaken that verifier to make fixtures pass: define and test explicit unsigned preparation semantics first. Signing, wallet locking and broadcast remain separate approvals/operations. This source review adds no runtime preparation feature.


## Wallet-selected BCH funding candidate

`BchRpc::prepare_funding(FundingPreparationRequest)` now requests an unsigned candidate from the pinned wallet with explicit change destination, BCH/kB fee rate, no fee subtraction, no watch-only/unsafe selection and `lockUnspents: false`. The empty-input request uses BCH legacy serialization explicitly. Only unsigned `MAX-1`/`MAX` input sequences are accepted and finalized to `Sequence::MAX` before validation; nonzero lock times, other versions, signatures and witness data are rejected.

The adapter obtains each full parent through the owned wallet, verifies canonical transactions, exact contract principal/change, input scripts and the independently calculated fee, then checks live output ownership, token absence, maturity, confirmations and a stable chain tip. Node selection does not substitute for those checks. The returned `WalletFundingCandidate` contains unsigned bytes, parent bytes, verified preview and point-in-time input observation. It does not retain a trade record, lock inputs, sign or broadcast. Admitted-execution retention and helper/Qt preparation integration remain to be implemented.

All nine BCH RPC tests passed, including 22 wallet-preparation scenarios, and the all-features/all-targets Rust check passed. Evidence: `build/bch-wallet-preparation-tests-20261010.log`, `build/bch-wallet-preparation-validation-status-20261010.json`, `build/bch-wallet-preparation-provenance-20261010.json` and `build/bch-wallet-preparation-check-20261010.log`. Patch: `upstream-bch-wallet-preparation.patch`. These are local HTTP mocks, not real-node verification.


## Admitted wallet-preparation retention (verified local fixture)

`AdmittedExecution::prepare_bch_funding_from_wallet` binds the wallet candidate to the original admission, backend deployment and exact `WalletPreparationPolicy`. It writes the complete candidate snapshot, then a metadata digest pin, before publishing the existing unsigned template, cooperative input claims and preparation pin. A restart with the same policy resumes those exact bytes and original preparation time without another wallet selection. This cached result is not fresh chain evidence or signing permission.

Changed policy/deployment, backward time, missing or corrupted candidate data and candidate bytes without a metadata pin are errors. An unpinned partial candidate never triggers automatic reselection. A pinned candidate can complete interrupted preparation-pin publication without replacing inputs. The wallet-lock operation additionally checks the retained backend, change address and minimum input confirmations. Existing manually retained preparations continue through their original path.

The new `bch_wallet_preparation_retains_candidate_before_funding_and_resumes_without_rpc` ignored fixture exercises the complete retained negotiation, one wallet selection, restart, changed policy/backend, damaged records and interrupted publication, followed by locking/signing/recovery retention and the production helper review. The runner `build/run-bch-wallet-preparation-retention-validation-20261010.py` completed successfully: the full preparation fixture passed in 43.64 seconds, ten helper unit tests, seven subprocess checks and two retained-negotiation fixtures passed, and the all-features/all-targets Rust check passed. Source and all three copied-binary hashes match `build/bch-wallet-preparation-retention-provenance-20261010.json`; the terminal verdict is in `build/bch-wallet-preparation-retention-validation-status-20261010.json`. Ordered patch reconstruction passes for `upstream-bch-wallet-preparation-retention.patch`. Helper/Qt preparation commands are not connected yet.


## Saved wallet-preparation review (verified local fixture)

`AdmittedExecution::saved_bch_wallet_preparation_review` returns historical preparation details without RPC or journal mutation. It checks the candidate digest, caller-pinned deployment and exact policy, reconstructs the candidate's template commitment, and requires the published preparation and input claims to match. The result includes all input outpoints, amounts and scripts, all outputs, the fee, original preparation time and quote context. It deliberately supplies no unsigned transaction ID as a spend target. Its domain-separated commitment identifies the summary; it is not signing or relay consent. An incomplete publication fails review, and completed historical review remains accessible after quote expiry.

The expanded preparation fixture passed in 44.59 seconds under `build/run-bch-preparation-review-validation-20261010.py`; ten helper unit tests, seven subprocess checks and two retained-negotiation fixtures also passed. Patch reconstruction passed for `upstream-bch-preparation-review.patch`; runtime and all-features/all-targets checks passed. Source and copied-binary hashes match `build/bch-preparation-review-provenance-20261010.json`. No helper command or Qt control consumes this summary yet.


## BCH preparation helper commands — saved-status fixture verified, 10 October 2026

Added `--funding-prepare-stdio-v1` and `--funding-preparation-status-stdio-v1`. Both require the original saved admission, exact preparation policy and independently pinned deployment. Preparation refreshes the wall clock before calling the retained controller and checks the backend digest before RPC. Saved status opens neither the RPC descriptor nor credentials. Responses contain the complete historical preparation review with `executable:false` and `chainObserved:false`; the preparation-specific output bound is 64 KiB to accommodate every supported input. Existing command limits remain 16 KiB. Neither command signs or broadcasts.

The strict private wrapper uses schema `monzero-private-bch-preparation-v1`, `fundingConfigFile`, `rpcFile` and the existing snake_case `WalletPreparationPolicy`. Requests use `monzero-bch-funding-prepare-request-v1` or `monzero-bch-funding-preparation-status-request-v1`, a nonnil `requestId` and no approval commitment. The wrapper references the existing pinned funding/admission configuration. A successful saved review is historical evidence, not current input availability.

The first helper compilation identified a library-private deployment digest method. It is now exposed as a documented read-only configured-identity digest, preserving its existing implementation. The next unit run caught an omitted preparation-action allowlist entry in the shared request parser (ten tests passed, one failed). Both explicit preparation actions are now accepted while approval fields remain rejected. Failed-run artifacts are preserved with `.request-test-failure` suffixes. The corrected runner `build/run-bch-preparation-helper-validation-20261010.py` passed: eleven unit tests, seven subprocess checks, two retained-negotiation fixtures and the full preparation/saved-status fixture (51.10 seconds), plus all-features/all-targets checking. Source and copied-binary hashes matched `build/bch-preparation-helper-provenance-20261010.json` before the next fixture-only change. The expanded fixture invokes saved status with the mock server stopped and an absent RPC descriptor, checks unchanged stores and rejects changed policy/missing admission consent. This verifies offline saved status through the production helper; first-selection preparation through the helper and Qt integration still need dedicated coverage. Patch reconstruction passed for `upstream-bch-preparation-helper.patch`. Mining remains off.


### First wallet selection through the BCH helper — verified local fixtures, 10 October 2026

Added a separate fixture that invokes the actual `--funding-prepare-stdio-v1` command before any candidate exists. It checks rejection of a mismatched deployment and missing consent without retained-state changes, exactly one wallet selection, a preparation timestamp within the helper call, independently restored review equality, restart without reselection, changed-policy refusal, no locking/signing/broadcast, and offline status after removing the mock node, RPC descriptor and credentials. This initial-selection fixture complements the existing complete preparation-to-signing/recovery fixture; it does not claim a complete new GUI path.

`build/run-bch-preparation-initial-validation-20261010.py` passed both preparation fixtures through the production helper in 63.50 seconds. Eleven helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets Rust checking also passed. Ordered reconstruction passed for `upstream-bch-preparation-initial-fixture.patch`. Source and all three copied-binary hashes match `build/bch-preparation-initial-provenance-20261010.json`; the terminal verdict is `build/bch-preparation-initial-validation-status-20261010.json`. Qt preparation controls and real-node verification remain unfinished. Mining remains off.


### Qt BCH preparation job — focused checks and full fixture passed, 10 October 2026

Added `BchPreparationJob` with explicit unsigned preparation and read-only saved-status operations. The job pins private wrapper/funding/admission/quote bytes and the helper executable, bounds stdout to 64 KiB, rejects stderr or malformed/cross-request responses, and clears displayed results before a new operation or on failure. Review validation checks exact quote/deployment/policy, all input outpoints and scripts, duplicate inputs, output order, principal/change, exact fee arithmetic, original preparation time and the domain-separated review digest. Preparation requires an unexpired quote and a stable private RPC descriptor; saved status does not open that descriptor or credentials and remains available after expiry. The helper independently checks the backend deployment before RPC. No signing or relay is added.

All 32 focused Qt checks passed, including expiry, stale-result clearing, absent RPC status, malformed amounts, duplicate inputs, altered policy/scripts/context, oversized output and configuration replacement. The wallet application and test-only probe build passed; the wallet was not launched. Evidence: `build/bch-qt-preparation-tests-20261010.log`, `build/bch-qt-preparation-build-20261010.log` and `build/bch-qt-preparation-app-build-20261010.log`.

The initial-selection fixture can now invoke the production Qt job through a separately digest-pinned probe while retaining the independently pinned Rust helper in its admission configuration. `build/run-bch-qt-preparation-validation-20261010.py` passed both full preparation fixtures in 63.72 seconds, with initial selection/restart/offline status passing through the production Qt job and Rust helper. Eleven helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets checking also passed. Rust/Qt source and copied helper/probe hashes match the recorded provenance. Ordered reconstruction passed for `upstream-bch-qt-preparation-fixture.patch`. The following panel milestone connects the verified job to DexEngine; broader controller suites remain on their original live processes. Mining remains off.


### Private BCH preparation controls — verified local build, 10 October 2026

DexEngine now exposes the preparation job and prevents overlapping BCH helper operations. Hidden `--dex-bch-preparation` configuration requires the existing explicit private-prototype opt-in. Startup queues saved admission/preparation/funding checks sequentially; it never selects coins automatically, and a failed check does not prevent the next configured status check. Preparation still requires its explicit button. The BCH panel displays every retained input and output, the exact fee, preparation ID and original time. Starting or failing a check clears stale details; saved status remains usable after quote expiry. It grants no signing or relay permission.

All 37 QML checks passed. Six targeted engine checks passed, including a new three-helper startup-order test proving no preparation command is launched and overlapping actions are refused. The final wallet build passed without launching it. The preparation panel was captured and inspected in `build/bch-qt-preparation-panel-20261010.png`. Evidence: `build/bch-qt-preparation-panel-qml-tests-20261010.log`, `build/bch-qt-preparation-engine-tests-20261010.log`, `build/bch-qt-preparation-panel-build-20261010.log` and `build/bch-qt-preparation-panel-provenance-20261010.json`.

Signing/locking controls, protected peer transport, complete recovery/settlement integration and real-node verification remain unfinished; all live markets remain disabled. The public roadmap still describes the preceding retention milestone until the next deployment. Mining remains off.


### Separate BCH wallet-lock approval — verified local fixtures, 10 October 2026

Added a current-quote wallet-lock review over the exact saved preparation, deployment and policy, with a distinct `monzero-bch-wallet-lock-review-v1` commitment and `lock-retained-wallet-inputs` action. The approved controller method rejects other review commitments before wallet RPC and delegates to the existing immutable-intent locking/reconciliation flow. A separate read-only method validates historical lock intent without asserting that advisory locks still exist. None means no retained intent, not proof that the wallet has no locks. These operations neither sign nor broadcast.

The complete preparation/recovery fixture now checks preparation-review refusal as lock consent, wrong deployment and expired/backward-time refusal, unchanged records and zero wallet mutations on rejection, original intent retention, and simulated loss/restoration of advisory wallet locks without reselection. It then continues through the existing signing/recovery/helper-review flow. `build/run-bch-lock-approval-validation-20261010.py` passed both preparation fixtures in 68.43 seconds, including the new controller lock-approval/reconciliation checks. Eleven helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets checking also passed. Current source and all four copied-binary hashes match `build/bch-lock-approval-provenance-20261010.json`; the terminal verdict is `build/bch-lock-approval-validation-status-20261010.json`. The lock approval itself is tested at the controller boundary; it is not yet exposed through a helper or Qt control. Ordered reconstruction passed for `upstream-bch-lock-approval.patch`. Lock helper commands, Qt approval controls and separate signing integration remain unfinished. Mining remains off.


### BCH wallet-lock helper commands — verified local fixtures, 10 October 2026

Added `--funding-lock-review-stdio-v1`, `--funding-lock-submit-stdio-v1` and `--funding-lock-status-stdio-v1` on the existing private preparation wrapper. Requests use their corresponding `monzero-bch-funding-lock-*-request-v1` schemas and correlated request IDs; submission alone requires the exact separate lock-review commitment. Review/status do not open RPC credentials. Approved submission verifies the pinned backend, refreshes dispatch time, and calls the reviewed controller; it neither signs nor broadcasts.

Review/status responses carry validated historical `lockIntent` (null when absent). Only an approved successful lock/reconciliation response reports `chainObserved:true` and a separate point-in-time observation with tip, unsigned-template hash, observation time and `walletInputsLocked:true`. Historical intent never guarantees current advisory locks. All commands retain the 64 KiB preparation-response bound and original admission/policy/deployment checks.

The initial-selection process fixture now also invokes lock commands through the production Rust helper. It checks missing/wrong approval and mismatched backend without mutations, an applied lock with lost reply, offline intent reporting without automatic retry, explicit reconciliation without another mutation when locks still exist, simulated lock loss and approved restoration, and offline status after removing the server/descriptor/credentials. Preparation still runs through the pinned Qt probe; lock commands do not yet have Qt controls. `build/run-bch-lock-helper-validation-20261010.py` passed both full preparation/locking fixtures in 91.79 seconds. Twelve helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets Rust checking also passed. Current source and all four copied-binary hashes match `build/bch-lock-helper-provenance-20261010.json`; the terminal verdict is `build/bch-lock-helper-validation-status-20261010.json`. Lock commands are verified through the production helper, but are not yet connected to Qt controls. Ordered patch reconstruction passed for `upstream-bch-lock-helper.patch`. Mining remains off.


### Qt wallet-lock job — focused checks and full fixture passed, 10 October 2026

Extended `BchPreparationJob` with offline lock review/status and explicitly approved lock/reconciliation. The job validates both the nested preparation review and the distinct lock-review digest, consumes the displayed approval before launch, and requires a fresh review for every later mutation. A signed-off response must match the exact approved review; a separately hashed changed review is rejected. Historical intent has its own validated record/time fields, while live wallet evidence checks exact shape, nonzero tip/template hashes, a timestamp within the current request and a boolean lock result. New operations clear old approval, history and observation data. Missing RPC credentials do not block saved status, including after expiry.

All 48 focused preparation/locking Qt checks passed, including changed-review refusal, lost-result approval consumption, unavailable descriptor handling, malformed or stale lock evidence, historical status without live evidence and expiry. The wallet application and process probe build passed; the wallet was not launched. Evidence: `build/bch-qt-lock-tests-20261010.log` and `build/bch-qt-lock-build-20261010.log`.

The full initial-selection fixture now routes lock operations through the actual Qt job and production Rust helper when its separately pinned Qt probe is enabled. `build/run-bch-qt-lock-validation-20261010.py` passed both full fixtures in 101.77 seconds, including initial selection and lock review/reconciliation/status through Qt and the production Rust helper. Twelve helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets Rust checking passed. Source and all four copied-binary hashes match `build/bch-qt-lock-provenance-20261010.json`. Ordered reconstruction passed for `upstream-bch-qt-lock-fixture.patch`. The following milestone wires lock methods and visible controls; signing integration remains unfinished. Mining remains off.


### Private wallet-lock controls — verified local build, 10 October 2026

DexEngine now exposes separate lock review, historical status and approved reconciliation operations under its BCH busy guard. Startup queues four read-only checks (when configured): admission, preparation, lock intent and funding status. It never submits a lock request. The preparation panel displays exact inputs/outputs, fee, deployment, change address and minimum confirmations; a separate checkbox approves the current lock review. Consent resets on state or market changes and is cleared before submission. Historical intent remains distinct from point-in-time wallet evidence, and failed/new checks clear old evidence.

All 38 QML checks and six targeted engine cases passed, including startup order/no mutation and overlapping-operation rejection. The final wallet build passed without launching it. The panel screenshot `build/bch-qt-lock-panel-20261010.png` was inspected. Evidence: `build/bch-qt-lock-panel-qml-tests-20261010.log`, `build/bch-qt-lock-engine-tests-20261010.log`, `build/bch-qt-lock-panel-final-build-20261010.log` and `build/bch-qt-lock-panel-provenance-20261010.json`.

Signing approval/reconciliation controls, protected peer transport, complete settlement/recovery and real-node verification remain unfinished. Broader controller suites remain on their original running snapshots. The public roadmap includes this wallet-lock milestone, verified against the deployed SHA-256 `d3592b98dd0e401cfaccd78ebb5ecc450175f73d2509e8bcfbfe33697a7b77f4`; desktop/mobile browser evidence is recorded in `build/roadmap-bch-lock-panel-public-check-20261010.log`. All markets remain disabled; mining remains off.


### Offline BCH signing state — verified local fixtures, 10 October 2026

`review_bch_funding_signing_for_deployment` provides the existing exact signing review without opening wallet credentials. `saved_bch_signing` returns its validated historical review, optional retained intent and optional verified signed funding, bound to the pinned deployment. Intent without signed funding requires reconciliation; it does not authorize another signature. Orphan signed results and corrupt retained results are rejected. These methods do not contact the wallet or broadcast.

Both complete preparation fixtures passed in 109.31 seconds, including missing-result/retry-refusal and corrupt-result checks. Twelve helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets compilation passed. Source and binary hashes were independently rechecked against `build/bch-signing-status-provenance-20261010.json`; the terminal verdict is `build/bch-signing-status-validation-status-20261010.json`. Ordered reconstruction includes `upstream-bch-signing-status.patch`. Helper signing commands and Qt signing controls remain unfinished; mining remains off.


### BCH signing helper — verified local fixtures, 10 October 2026

Added separate `--funding-signing-review-stdio-v1`, `--funding-signing-submit-stdio-v1` and `--funding-signing-status-stdio-v1` commands on the private preparation wrapper. Review/status validate original preparation policy and deployment without opening RPC credentials. Only submit accepts the distinct signing review commitment, checks backend binding and dispatch time, and invokes the existing one-attempt signing controller. Responses keep historical signing intent and retained signed transaction identity separate; all report `chainObserved:false` and grant no broadcast permission.

The full process fixture checks missing/wrong approval and backend refusal, a signed transaction with lost reply, historical status and refusal of another signature, explicit controller recovery of the original bytes, retained-result submission without another signature, and saved status after removing the server and credentials. Helper recovery commands and Qt signing controls remain unfinished. Ordered reconstruction passes for `upstream-bch-signing-helper.patch`; `build/run-bch-signing-helper-validation-20261010.py` passed both complete preparation/signing process fixtures in 139.65 seconds, plus 13 helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets compilation. The terminal verdict is `build/bch-signing-helper-validation-status-20261010.json`; current source and all four copied-binary hashes independently match `build/bch-signing-helper-provenance-20261010.json`. Signing commands run through the production helper; preparation/locking still run through the Qt probe. Signing is not yet exposed through Qt controls. Mining remains off.


### BCH signing recovery helper — verified local fixtures, 10 October 2026

Added `--funding-signing-recovery-review-stdio-v1` and `--funding-signing-recovery-submit-stdio-v1`. Their strict private wrapper uses schema `monzero-private-bch-signing-recovery-v1`, `preparationConfigFile` and `recoveredTransactionFile`. The candidate is a private binary transaction file capped at 1,000,000 bytes, read under the same ownership, mode, canonical-path, file-type, exclusive-lease and stability checks as other private inputs. Existing configuration and credential size limits are preserved.

Both actions validate the original admission, preparation policy, deployment and signing intent offline. Submission requires the separate exact recovered-candidate review and calls the existing immutable reconciliation controller; it never signs or relays. Responses contain the reconciliation review, historical signing intent and optional retained signed transaction identity, with `chainObserved:false`. The process fixture now uses these production helper commands after the lost signing reply, with the RPC descriptor removed, checking missing/wrong consent, altered candidate refusal and idempotent exact retention. Qt recovery controls remain unfinished.

Ordered patch reconstruction passed for `upstream-bch-signing-recovery-helper.patch`. `build/run-bch-signing-recovery-validation-20261010.py` passed both complete preparation/signing/recovery process fixtures in 160.53 seconds, 14 helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets compilation. The terminal verdict is `build/bch-signing-recovery-validation-status-20261010.json`; current source and all four copied-binary hashes independently match `build/bch-signing-recovery-provenance-20261010.json`. Recovery is now tested through the production helper with the RPC descriptor absent. Signing/recovery Qt controls remain unfinished. Mining remains off.


### Qt signing job — focused and full fixtures passed, 10 October 2026

`BchPreparationJob` now exposes separate signing review, historical status and explicitly approved signing methods. It validates all 17 signing-review fields, digest arrays, exact trade/deployment/quote/policy bindings, input uniqueness and scripts, output principal/change, fee arithmetic and the domain-separated commitment. Signing responses validate historical intent times and the retained transaction identity, remain non-chain observations, and must return the exact approved review. Each operation clears previous evidence; submission consumes consent before launch. An intent with no result disables another signing submission even after a fresh review. Offline saved status remains available after expiry.

All 68 focused preparation/locking/signing Qt checks passed, including changed validly hashed review refusal, missing/wrong consent, lost-result retry prevention, invalid digest arrays, malformed intent/result evidence, fee/input/output tampering, source replacement and offline status. Evidence: `build/bch-qt-signing-tests-20261010.log`. The process probe now routes signing commands through the actual Qt job; recovery imports continue through the production Rust helper pending Qt recovery integration. Ordered reconstruction passed for `upstream-bch-qt-signing-fixture.patch`.

`build/run-bch-qt-signing-validation-20261010.py` passed both full process fixtures in 170.97 seconds, including signing review, submission and saved status through the actual Qt job and production helper. Fourteen helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets compilation passed. The terminal verdict is `build/bch-qt-signing-validation-status-20261010.json`; source and all four copied-binary hashes match `build/bch-qt-signing-provenance-20261010.json`. The wallet application build passed without launching the wallet. DexEngine/QML signing controls are recorded below; Qt recovery import remains unfinished. Mining remains off.


### Private signing controls — UI checks passed, 10 October 2026

DexEngine exposes signing review, saved status and explicit submission under its shared BCH operation guard. Startup now serializes five read-only checks when configured: admission, preparation, wallet-lock intent, signing status and funding status. Startup never signs. The preparation panel displays the exact signing review, deployment and preparation/lock commitments, quote expiry, fee, change policy and every input/output/script. A separate checkbox approves signing only, resets on state/market changes and clears before submission. Saved signed funding is distinguished from chain confirmation; an uncertain saved attempt explicitly directs recovery without another signature.

All 39 QML checks and six targeted engine cases passed, including startup ordering, overlapping-operation refusal, consent reset and uncertain-versus-signed display. The final wallet build passed without launching it. The screenshot `build/bch-qt-signing-panel-20261010.png` was inspected; it shows the full reviewed data and unchecked signing consent. Evidence: `build/bch-qt-signing-panel-qml-tests-20261010.log`, `build/bch-qt-signing-engine-tests-20261010.log`, `build/bch-qt-signing-panel-final-build-20261010.log` and `build/bch-qt-signing-panel-provenance-20261010.json`. Full Qt/helper process validation also passed as recorded above; Qt recovery import and full settlement remain unfinished. The public roadmap deployment for signing and recovery is recorded below. Mining remains off.


### Qt signing recovery — full fixture and UI checks passed, 10 October 2026

The preparation job can attach a strict recovery wrapper for the same preparation configuration, review its private binary candidate and explicitly import it. Candidate reads enforce owner-only mode, regular-file and canonical-path checks, a 1,000,000-byte bound and stable file identity/metadata. Review binds the actual bytes to the signed-funding domain hash and independently computed legacy transaction ID. The nested original signing review, original approval time and retained intent are validated; import requires the exact reviewed candidate and consumes consent before launch. File changes before submission or during the helper operation clear the review and refuse success. Historical recovery remains allowed after quote expiry; no RPC descriptor is needed.

All 82 preparation/locking/signing/recovery Qt checks passed, including exact approval, changed/oversized candidate refusal, malformed recovery fields, changed validly hashed review, lost response and post-expiry import. The full process probe now routes recovery commands through Qt as well as signing/preparation/locking; ordered reconstruction passed for `upstream-bch-qt-signing-recovery-fixture.patch`. `build/run-bch-qt-signing-recovery-validation-20261010.py` passed both full fixtures in 181.94 seconds, including preparation, wallet locks, signing, recovered-candidate review/import and saved status through the actual Qt job and production helper. Fourteen helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets compilation passed. The terminal verdict is `build/bch-qt-signing-recovery-validation-status-20261010.json`; all source and copied-binary hashes match `build/bch-qt-signing-recovery-provenance-20261010.json`. UI source and wallet binary hashes also match the panel provenance record.

DexEngine exposes guarded recovery review/import. The hidden `--dex-bch-signing-recovery` option attaches a private wrapper after `--dex-bch-preparation`, with the existing private-prototype opt-in. Startup still performs only the five saved-status reads, never recovery review/import. The panel displays original inputs/outputs and the recovered transaction identity, with a separate retention-only checkbox that resets on state/market changes. All 40 QML checks and six targeted engine cases passed; the wallet build passed without launch. The rendered mock panel `build/bch-qt-signing-recovery-panel-20261010.png` was inspected. Evidence: `build/bch-qt-signing-recovery-tests-20261010.log`, `build/bch-qt-signing-recovery-panel-qml-tests-20261010.log`, `build/bch-qt-signing-recovery-engine-tests-20261010.log`, `build/bch-qt-signing-recovery-panel-build-20261010.log` and `build/bch-qt-signing-recovery-panel-provenance-20261010.json`. Full settlement/recovery and real-node verification remain unfinished; mining remains off.


### Public signing/recovery roadmap — deployment verified, 10 October 2026

The public roadmap now describes the verified private signing and recovered-transaction import controls, complete Qt/helper fixtures, 82 preparation/locking/signing/recovery Qt checks and 40 QML checks. It also records the separate older punishment-review snapshot passing all 48 tests. The newer corrected dispatch/cancellation controller run remains pending, and full settlement, protected peer transport and real-node gates remain unfinished; markets stay disabled.

Only the roadmap was deployed with prior-hash guards, a backup and atomic replacement. Deployment record: `build/website-roadmap-bch-signing-recovery-deploy-20261010.json`; backup: `/home/www/Monzero-backups/roadmap-bch-signing-recovery-20261010-153024`. Independently fetched public and local bytes match SHA-256 `e8a666fc048a9077a74c79961186e97fc99f087a56cdb5cc68c7645316d9d519`. Local and public browser checks passed at 1440, 390 and 360 pixels, with HTTP 200, no horizontal overflow and no JavaScript errors; mobile rendering was inspected locally. Evidence: `build/roadmap-bch-signing-recovery-local-check-20261010.log` and `build/roadmap-bch-signing-recovery-public-check-20261010.log`. Mining remains off.


### Reviewed peer refund protection — verified local fixtures, 10 October 2026

Added a separate offline `monzero-bch-refund-protection-review-v1` controller review before accepting the native funder's authenticated refund adaptor. It binds the exact trade, quote, execution admission, agreement, peer identity and domain-separated frame/signature hashes. It verifies the adaptor and identity signature before presenting approval and refuses a different retained receipt or backwards time. Explicit acceptance requires this distinct commitment before delegating to the existing immutable authenticated receipt operation. Saved receipt status is historical and does not claim that private recovery data has been retained or that funding may be broadcast.

The preparation/signing fixture now exercises wrong-role, backwards-time, expiry, changed-signature and wrong-approval refusal with unchanged metadata/snapshots, approved acceptance, saved receipt and repeated acceptance without replacing the receipt. Ordered patch reconstruction passed for `upstream-bch-refund-protection-review.patch`. `build/run-bch-refund-protection-review-validation-20261010.py` passed both full preparation/signing/recovery fixtures in 186.09 seconds, including the new reviewed refund-protection controller assertions. Fourteen helper unit tests, seven subprocess checks, two retained-negotiation fixtures and all-features/all-targets compilation passed. The terminal verdict is `build/bch-refund-protection-review-validation-status-20261010.json`; current source and all four copied-binary hashes independently match `build/bch-refund-protection-review-provenance-20261010.json`. The new review/acceptance remains a controller boundary and is not yet exposed through helper commands or Qt. Helper/Qt refund-protection exchange, private recovery preparation controls, full settlement and real-node verification remain unfinished. Mining remains off.


### Refund-protection helper interface — validation in progress, 10 October 2026

The production helper now accepts `--refund-protection-review-stdio-v1`,
`--refund-protection-submit-stdio-v1` and `--refund-protection-status-stdio-v1`.
The private wrapper has exactly `schema` (`monzero-private-bch-refund-protection-v1`),
`admissionConfigFile` and `peerMessageFile`.
The peer message has exactly `schema` (`monzero-bch-peer-refund-protection-v1`),
`frame` (at most 8192 bytes) and `signature` (64 bytes).
It is read from a bounded owner-only file under the existing private-file lease checks.

Requests use `monzero-bch-refund-protection-{action}-request-v1`, a non-nil `requestId`,
and a distinct `commitment` only for submit.
Every operation restores the pinned quote, original BCH-funder admission and negotiation approval.
Review and submit verify the actual peer adaptor and identity signature.
Submit checks current time again before accepting the exact approved review.
Responses contain review details/hashes and the saved receipt commitment/time only,
with `executable` and `chainObserved` false.
Status requires neither the peer message file nor a current quote; it validates historical receipt state.
No RPC credentials, wallet call, raw secret-bearing refund transaction or broadcast are involved.

Ordered reconstruction passed for `upstream-bch-refund-protection-helper.patch`.
Fifteen helper unit tests and seven subprocess checks passed.
Full authenticated fixtures and all-target compilation are still running; no completed integration verdict is claimed here.
Qt wiring, outgoing peer transport and private recovery preparation controls remain unfinished.


### Refund-protection helper — complete local validation, 10 October 2026

The full validation completed successfully: 15 helper unit tests, seven subprocess checks,
two retained-negotiation fixtures and both full funding/preparation fixtures (204.34 seconds).
The all-features/all-targets release check passed.
All current source and four copied-binary hashes independently match the provenance manifest.
Evidence: `build/bch-refund-protection-helper-validation-status-20261010.json` and
`build/bch-refund-protection-helper-provenance-20261010.json`.
This validates offline helper receipt handling; Qt wiring and complete settlement remain unfinished.


### Litecoin light-wallet proof boundary — implementation, validation pending

Added `swap-machine/src/litecoin_light.rs` as an offline foundation for the approved light-wallet path.
`script_hash` computes the Electrum reversed SHA256 script identifier.
`match_merkle_proof` bounds the transaction, 80-byte header and JSON response before parsing;
it matches caller-selected request ID, transaction ID, block hash and claimed height,
checks branch order/endianness/root and rejects unconsumed position bits,
ambiguous 64-byte non-witness leaves, malformed envelopes and excessive depth.
The initial wire contract requires JSON-RPC 2.0 and the Electrum 1.4 Merkle-result shape.

`MerkleMatch` is a non-serializable mathematical match, not chain or spendability authority.
No Litecoin Scrypt work, difficulty, active-chain membership, header height, confirmations,
witness validity, current unspentness or MWEB acceptance is established here.
The module cannot produce `VerifiedLiveLock` and is not connected to any signing or funding gate.
The seven tests use independently generated Python hashlib vectors and explicitly test witness limitations.
No external wallet server is contacted.
Ordered patch reconstruction passed; compile/runtime validation is in progress.

Implementation still required: authenticated transport and protocol/network negotiation;
Litecoin header validation and persistent chain selection/reorganisation handling;
transaction history and spend discovery without confusing omission with proof of unspentness;
local wallet creation/restoration, ownership, signing and reservations;
then receive/balance UI and complete swap/recovery acceptance through the new backend.
The existing Core RPC path remains the only execution backend.

Protocol references: [ElectrumX Merkle methods](https://electrumx.readthedocs.io/en/latest/protocol-methods.html#blockchain-transaction-get-merkle),
[ElectrumX script hashing](https://electrumx.readthedocs.io/en/latest/protocol-basics.html),
and [Litecoin header identity versus Scrypt work](https://github.com/litecoin-project/litecoin/blob/master/src/primitives/block.cpp).

Existing transport reuse note: `electrum-pool/src/lib.rs::broadcast_all` broadcasts to multiple servers
and the default configuration uses retry/quorum behaviour.
Do not reuse it as swap relay authority without binding exact approved bytes, durable attempt state,
ambiguous-response reconciliation and explicit retransmission policy.
Server quorum does not validate Litecoin consensus or prove current unspentness.


### Litecoin light-wallet proof boundary — verified offline, 10 October 2026

All seven new proof checks and eleven existing Litecoin RPC checks passed.
One real-node RPC fixture remains ignored; no node, mining or public server was started.
The all-features/all-targets release check passed, and current source plus the immutable
test binary match `build/litecoin-light-proof-provenance-20261010.json`.
Terminal evidence: `build/litecoin-light-proof-validation-status-20261010.json`;
test logs: `build/litecoin-light-proof-{proof,rpc}-tests-20261010.log`.
Ordered reconstruction passed for `upstream-litecoin-light-proof.patch`.
This completes only the offline proof-matching component described above.
Authenticated transport, Litecoin header-chain validation, local wallet/signing,
spend discovery, receive/balance UI and full light-backend swap acceptance remain unfinished.
Full-node execution remains required in the current private prototype; no live market is enabled.


### Litecoin light-wallet read session — implementation, validation pending

Added `litecoin_light::session::ReadSession` over a caller-supplied asynchronous stream.
It negotiates exactly Electrum 1.4 and checks the reported genesis against the selected
Litecoin mainnet/testnet/regtest identity and script hashing against SHA256 before transaction queries.
This checks the server's claim, not its honesty or chain consensus.
Production callers still need to construct and authenticate a TLS connection.

The read-only inclusion path requests the specified header, exact transaction and Merkle branch,
then uses the existing offline checker against caller-selected block identity and height.
It never discovers a trusted chain anchor from the same response.
There are no wallet keys, signing, subscriptions, automatic retries or broadcast methods.

Each request has an identity, a bounded newline-delimited response and a whole-request deadline.
Malformed/error responses, wrong headers and proof failures invalidate the session.
The session becomes unusable before the first request await, so cancelling a pending future
also prevents a late response from being reused by the next request.
Six added duplex-stream fixtures cover fragmented complete proof retrieval, request order and parameters,
wrong chain/hash algorithm, unsupported protocol, wrong/duplicate IDs and server errors,
wrong-header refusal without further queries, EOF/oversized lines and timeout/cancellation.
No external server or real node is contacted.
Ordered reconstruction passed for `upstream-litecoin-light-session.patch`; runtime and compile checks are pending.


### Litecoin light-wallet read session — verified local fixtures, 10 October 2026

The session validation passed all thirteen light-wallet checks (six session fixtures plus seven proof checks)
and eleven existing Litecoin RPC checks; the real-node fixture remained ignored.
The all-features/all-targets release check passed.
Current source and the immutable binary match `build/litecoin-light-session-provenance-20261010.json`.
Terminal result: `build/litecoin-light-session-validation-status-20261010.json`;
logs: `build/litecoin-light-session-{proof,rpc}-tests-20261010.log`.
This establishes the bounded read-only protocol flow over local in-memory streams.
It does not establish TLS authentication, server honesty, chain consensus, spendability or a functioning light wallet.
TLS connection setup, independent Litecoin header validation, wallet/signing and user-facing funding remain required.
All markets remain disabled; mining stays off.


### Litecoin Electrum TLS connector — implementation, validation pending

Added `litecoin_light::tls::TlsEndpoint` with an explicit canonical host, port and Litecoin network.
Its public connection path loads system roots through `rustls-native-certs`, requires a nonempty
error-free root load, checks certificates and hostname using Rustls, and negotiates the read-only session.
The platform loader honours its configured certificate environment; this is system-PKI trust, not certificate pinning.
DNS/TCP/TLS/protocol setup has a combined 30-second deadline.
No plaintext fallback, custom certificate verifier, signing, relay or default public server was added.
The connector depends on the already cached/locked `tokio-rustls` 0.26.4 and `rustls-native-certs` 0.8.3.

The new disposable fixture generator creates independent CA, localhost server and expired certificates
without saving CA private keys. Test server keys are owner-only and test-only.
Four explicit local TLS fixtures exercise a complete authenticated inclusion read,
wrong hostname/untrusted CA/expired certificate refusal before RPC, authenticated wrong-chain refusal,
and plaintext/stalled-handshake failure. An ordinary unit test covers endpoint syntax.
The fixtures inject test roots into the same connection routine; system-store discovery itself
and public internet servers are not exercised by these tests.
Source, generator and fixture hashes are recorded in the validation provenance.
Ordered reconstruction passed for `upstream-litecoin-light-tls.patch`; runtime checks are in progress.
Independent Litecoin header-chain validation, wallet/signing and full light-wallet trading remain unfinished.


### Litecoin Electrum TLS connector — verified local fixtures, 10 October 2026

Validation passed: fourteen ordinary light-wallet checks, all four explicit TLS fixtures,
and eleven existing Litecoin RPC checks (29 passes across the three filters).
The actual-node RPC fixture remains unrun; the four TLS fixtures listed as ignored
in the ordinary filter were explicitly run and passed in the separate TLS filter.
The all-features/all-targets release check and ordered reconstruction passed.
Current source, copied binary, fixture generator and certificate/key file hashes
match `build/litecoin-light-tls-provenance-20261010.json`.
Terminal evidence: `build/litecoin-light-tls-validation-status-20261010.json`;
logs: `build/litecoin-light-tls-{proof,tls,rpc}-tests-20261010.log`.

These are local TLS/loopback integration results, not public-server or consensus acceptance.
The public system-root loading entry point has not yet been exercised end to end;
fixtures inject roots into the shared connector.
Litecoin header-chain verification, local wallet/signing, balance screens and full swap acceptance
remain unfinished. No live market or mining was enabled.


### Public Litecoin TLS trust-store path — validation pending

Added child-process fixtures that invoke the public `TlsEndpoint::connect()` method.
Each child has a cleared environment plus an explicit fixture certificate file and required loader path;
no parent-process certificate environment is modified.
The accepted path loads roots, authenticates TLS, negotiates Electrum and fetches a complete inclusion proof.
Unrelated roots must reject the server before RPC, while empty/missing certificate files
must fail before any TCP connection to the waiting listener.
The generator now emits PEM fixture roots alongside the earlier DER files.
Ordered reconstruction passed for `upstream-litecoin-light-system-roots.patch`; runtime validation is pending.

### Pinned Litecoin header-rule source audit

Downloaded the upstream MIT-licensed source at commit `ec1b6489a900d09cf5991e220dce089c77a232a2`
(the peeled `v0.21.5.8` tag) to `build/litecoin-header-source-review-ec1b6489a900`,
with original URLs and SHA256 hashes in `provenance.json`.
The audit confirms that block identity hashing is separate from Scrypt proof-of-work verification.
Difficulty validation must preserve Litecoin's retarget lookback and intermediate rounding behaviour,
its testnet minimum-difficulty exception and regtest no-retarget policy.
Header-context checks also enforce median-past/future time and activation-dependent version requirements.
A valid Merkle root or TLS certificate proves none of these checks.
No header validator or consensus acceptance is implemented by this source audit.
Sources: [difficulty rules](https://github.com/litecoin-project/litecoin/blob/ec1b6489a900d09cf5991e220dce089c77a232a2/src/pow.cpp),
[header context](https://github.com/litecoin-project/litecoin/blob/ec1b6489a900d09cf5991e220dce089c77a232a2/src/validation.cpp),
and [header hashes](https://github.com/litecoin-project/litecoin/blob/ec1b6489a900d09cf5991e220dce089c77a232a2/src/primitives/block.cpp).


### Public Litecoin TLS trust-store path — verified local processes, 10 October 2026

All fourteen ordinary light-wallet checks, six explicit TLS fixtures and eleven existing RPC checks passed
(31 top-level passes; the actual-node RPC fixture remains unrun).
The public-entry-point fixtures launched isolated child processes with explicit PEM stores,
verified a complete accepted proof read and rejection of unrelated, empty and missing stores.
This tests the platform loader's configured-file path; it does not certify every operating system's default trust store.
All-features/all-targets compilation and ordered patch reconstruction passed.
Current source, copied test binary, generator and certificate files match
`build/litecoin-light-system-roots-provenance-20261010.json`.
Terminal result: `build/litecoin-light-system-roots-validation-status-20261010.json`;
logs: `build/litecoin-light-system-roots-{proof,tls,rpc}-tests-20261010.log`.
The pinned upstream header-source hashes were also rechecked.
TLS and protocol evidence still do not establish chain validity or spendable balance.
Independent header-chain validation, local wallet/signing and complete app swaps remain required; mining remains off.


### Litecoin header-sequence validation — implementation, validation pending

Added `litecoin_light::headers::HeaderSequence`, initialized only from the selected
network's pinned Litecoin genesis constants. Appending one exact 80-byte header checks
parent linkage, required difficulty, median of the last eleven timestamps,
the caller-clock future limit, activation-dependent version and Scrypt work
before changing either the retained headers or cumulative work.
Genesis identity follows Core's explicit genesis exception.

The implementation uses fixed Scrypt N=1024/r=1/p=1 and Litecoin's compact-target limits.
Retargeting preserves its first-versus-later interval ancestor distinction and
shift/divide rounding, testnet minimum-difficulty recovery and default regtest rules.
Ten new tests include independently computed Python Scrypt vectors for all three genesis headers,
a fixed synthetic child header, atomic rejection, compact-target edge cases,
Core's four difficulty vectors, the additional retarget ancestor, testnet recovery,
activation boundaries and median/future timestamp boundaries.
The synthetic fixture is static; no nonce search, node or miner runs in these tests.

Dependencies add pinned `scrypt` 0.11.0 and its `salsa20` 0.10.2 dependency.
The existing dependency graph was compared against its pre-edit snapshot and preserved;
unrelated dependency-edge changes introduced during resolution were removed.
Ordered reconstruction passed for `upstream-litecoin-light-headers.patch`.
The locked/offline runtime build and full regression check are in progress.

This validates one in-memory header branch, not best-chain selection or freshness,
checkpoint policy, durable synchronization, block-body/MWEB consensus or current unspentness.
It creates no funding/signing permission and is not yet connected to the read-session synchronization path.
Those requirements, local wallet/signing and complete app swap acceptance remain unfinished.


### Litecoin header-sequence validation — verified offline, 10 October 2026

All ten new header tests passed, including the pinned Core difficulty vectors,
independent Scrypt vectors, timestamp/version boundaries and mutation refusal.
The complete filters passed 24 ordinary light-wallet checks, six explicit TLS fixtures
and eleven existing Litecoin RPC checks (41 top-level passes).
The actual-node RPC fixture remains unrun; no node or miner was launched.
The all-features/all-targets release check and ordered reconstruction passed.
Current source, copied binary, generator and TLS fixture hashes match
`build/litecoin-light-headers-provenance-20261010.json`; the dependency comparison
confirms only the two intended package additions and the swap-machine dependency entry changed.
Terminal evidence: `build/litecoin-light-headers-validation-status-20261010.json`;
logs: `build/litecoin-light-headers-{proof,tls,rpc}-tests-20261010.log`.

This proves the documented single-branch header checks, not synchronization, best-chain selection,
checkpoint/freshness policy, restart persistence, wallet spendability or full atomic swaps.
Those integration and acceptance requirements remain open. Mining and all live markets stay off.


### Litecoin validated header retrieval — implementation, validation pending

`ReadSession::advance` now requests bounded `blockchain.block.headers` batches from
the next local height and requires the sequence's network to match the negotiated session.
The requested count is limited to 1..2016; result count, advertised maximum, exact hex length
and bounded response framing are checked before validating any header.
A zero or short response is only a server report, never evidence of being fully synchronized.

`HeaderSequence::append_batch` validates each header and restores the original length and work
if any header in the batch fails. Mutation is synchronous after the network await,
so a cancelled request cannot install a partial batch.
Invalid server data poisons the session; there is no automatic retry.
New tests cover valid/empty retrieval, next-height request arguments, wrong network/count refusal
before I/O, malformed result fields and exact rollback after a valid first header with a bad suffix.
Ordered reconstruction passed for `upstream-litecoin-light-sync.patch`; runtime checks are in progress.
Fork selection, freshness, checkpoint policy, restart persistence and wallet integration remain unfinished.


### Litecoin validated header retrieval — verified local fixtures, 10 October 2026

All five new batch/retrieval tests passed. Full filters passed 29 ordinary light-wallet checks,
six explicit TLS fixtures and eleven existing Litecoin RPC checks (46 top-level passes).
The real-node RPC fixture remains unrun. All-features/all-targets release compilation
and ordered reconstruction passed. Current source, copied binary and fixture hashes match
`build/litecoin-light-sync-provenance-20261010.json`.
Terminal evidence: `build/litecoin-light-sync-validation-status-20261010.json`;
logs: `build/litecoin-light-sync-{proof,tls,rpc}-tests-20261010.log`.
These are bounded batch extension and local transport checks, not complete chain synchronization
or best-chain/freshness acceptance. Fork selection, persistence, wallet integration and live swaps remain unfinished.
Mining remains off.


### Litecoin branch selection — verified local fixtures, 10 October 2026

`HeaderSequence::fork` creates an isolated validated prefix and recomputes its work.
`adopt` accepts only a same-network candidate with strictly more accumulated work;
equal or lower work leaves the selected branch unchanged.
A successful replacement reports the old tip, new tip and common ancestor.
`contains` lets callers check whether previously observed block evidence remains on this branch.
This is selection among supplied candidates, not proof of the network's current best chain.
Fixed-header fixtures cover a competing branch, tie retention, reorganisation, detached evidence,
prefix work, isolated mutation, ordinary extension and wrong-network refusal.
No miner or node is launched by these tests.
Ordered reconstruction passes with `upstream-litecoin-light-forks.patch`.
Validation passed: 31 ordinary light-wallet tests, six explicit TLS fixtures and eleven RPC tests
(48 top-level passes), plus all-features/all-targets release compilation.
The actual-node RPC fixture remains unrun; the ignored TLS child runs through its parent fixtures.
Terminal evidence is `build/litecoin-light-forks-validation-status-20261010.json`.
Current sources, copied test binary and TLS fixtures match `build/litecoin-light-forks-provenance-20261010.json`.
Test binary SHA-256: `74f48414f886f26fa159dd77780de1a9c2f1d02774084dedffd6b3a64b9d075f`.
Automatic fork discovery, freshness/checkpoint policy, restart persistence, confirmation integration
and wallet spending remain unfinished. Mining remains off and live markets remain disabled.


### Litecoin selected-chain inclusion — verified local fixtures, 10 October 2026

`ReadSession::inclusion_on` checks the session network and selected height before any I/O,
then matches the raw transaction and Merkle branch against the locally validated header.
It does not ask the peer to supply a replacement header. Invalid proofs poison the session.
`MerkleMatch::confirmations_on` computes depth against the supplied selected chain on each call;
missing or orphaned block membership returns `None`, preventing reuse of cached confirmation depth.
The count is relative to this branch and does not establish freshness, witness validity,
transaction consensus validity, unspentness or signing authority.
Fixed-header tests cover successful proof retrieval, increasing depth, reorganisation invalidation,
wrong-network/unknown-height refusal before I/O and bad-proof connection poisoning.
Ordered reconstruction passes with `upstream-litecoin-light-confirmations.patch`.
Validation passed 35 ordinary light-wallet tests, six explicit TLS fixtures and eleven RPC tests
(52 top-level passes), plus all-features/all-targets release compilation.
The actual-node RPC fixture remains unrun; the TLS child runs through its parent fixtures.
Terminal evidence: `build/litecoin-light-confirmations-validation-status-20261010.json`.
Sources, copied test binary and TLS fixture hashes match `build/litecoin-light-confirmations-provenance-20261010.json`.
Test binary SHA-256: `85487d0d744d800b833a09bea6039b8f30a1e4d229869e1e3cbd88be1c62cb10`.
Automatic fork discovery, freshness/checkpoints, restart persistence, wallet discovery/signing,
DEX integration and full release acceptance remain unfinished. Mining stays off; live markets stay disabled.


### Litecoin header restart cache — verified local fixtures, 10 October 2026

`HeaderCache` uses an owned private directory and an exclusive process lease.
Snapshots contain a versioned marker, genesis identity, header count and raw post-genesis headers.
Loading bounds the file against a caller-selected maximum height, checks exact framing,
and replays every header through consensus validation to rebuild work from pinned genesis.
Missing files return `None`; corruption, wrong networks and invalid headers return errors.
Saving streams into an exclusive temporary file, syncs its contents, atomically renames it,
and syncs the directory. A final sync failure is reported even if replacement already occurred.
Symlinks, hard links, nonregular records and loose file permissions are refused.
Tests cover all three genesis identities, restart/work recovery, reorganisation persistence,
old-reader consistency across replacement, abandoned temporary files, resource limits,
corruption, local clock checks, exclusive leases and filesystem boundary failures.
This cache is Unix-specific, contains no wallet keys and provides no anti-rollback or freshness guarantee.
Loading performs blocking Scrypt work; application callers must use a blocking worker.
Ordered reconstruction passes with `upstream-litecoin-light-cache.patch`.
Validation passed 39 ordinary light-wallet tests, six explicit TLS fixtures and eleven RPC tests
(56 top-level passes), plus all-features/all-targets release compilation.
The actual-node RPC fixture remains unrun; the ignored TLS child runs through its parent fixtures.
Terminal evidence: `build/litecoin-light-cache-validation-status-20261010.json`.
Sources, copied test binary and TLS fixtures match `build/litecoin-light-cache-provenance-20261010.json`.
Test binary SHA-256: `b760c15af8db9f7582cb091d2d18122ba1a1e0074fe6f042f887ce4d08a60b64`.
Wallet lifecycle integration, automatic synchronization/fork discovery, freshness/checkpoint policy,
owned-output discovery/signing and DEX/release acceptance remain unfinished. Mining and live markets stay off.


### Litecoin incremental fork synchronization — verified local fixtures, 10 October 2026

`ReadSession::reported_tip` consumes a dedicated connection after the bounded Electrum tip response,
so subsequent subscription notifications cannot collide with download/proof requests.
The returned network/height/hash is explicitly an untrusted peer claim.
`begin_sync` locates a shared ancestor by binary search (at most 32 bounded header requests)
and constructs an isolated validated prefix. `SyncCandidate::advance` downloads at most 2016
headers per call, validates them, and checks the final identity against the originally reported tip.
`finish` refuses incomplete/invalid attempts and selects a complete branch only if it has more work.
The selected chain stays unchanged during download; cancellation or inconsistent responses invalidate the attempt.
Tests cover strict tip parsing, two-step download, shared-chain completion without downloading,
fork replacement, refusal of a weaker branch, changed-tip/empty-response refusal, request limits,
wrong-network refusal and cancellation before selection. No nodes or miners run.
Protocol reference: https://electrumx.readthedocs.io/en/latest/protocol-methods.html#blockchain-headers-subscribe
Ordered reconstruction passes with `upstream-litecoin-light-candidate.patch`.
Validation passed 46 ordinary light-wallet tests, six explicit TLS fixtures and eleven RPC tests
(63 top-level passes), plus all-features/all-targets release compilation.
The actual-node RPC fixture remains unrun; the ignored TLS child runs through its parent fixtures.
Terminal evidence: `build/litecoin-light-candidate-validation-status-20261010.json`.
Current sources, copied binary and TLS fixtures match `build/litecoin-light-candidate-provenance-20261010.json`.
Test binary SHA-256: `d8d28992df5e0df338802ff8030fbd2f81c012585d791d814aecf7e398fdfaa2`.
Reaching one reported tip establishes neither freshness nor the network's best chain.
Application orchestration/cache wiring, independent-peer/freshness/checkpoint policy,
wallet creation/discovery/signing, DEX integration and release acceptance remain unfinished.
Mining remains off; live markets remain disabled.


### Litecoin durable chain-state coordination — verified local fixtures, 10 October 2026

`ChainState` owns the header-cache lease and exposes only a shared selected-chain reference.
Opening replays the stored chain; only a missing cache starts at genesis.
A complete synchronization candidate is checked against the configured height ceiling,
compared by work and written durably before becoming visible through the state handle.
Incomplete, equal-work and out-of-policy candidates do not write a replacement cache.
Any publication error invalidates the handle, including uncertainty after replacement;
reopening revalidates the actual durable record rather than continuing with stale in-memory state.
Tests use production tip parsing and synchronization over local in-memory streams, then actual
cache files, to cover initial publication, reorganisation/reopen, exclusive ownership, incomplete
and out-of-policy refusal, deterministic storage refusal and corrupt-cache rejection.
An injected error after a successful cache write checks uncertain-publication recovery;
it is an API-boundary failure fixture, not an induced operating-system fsync failure.
Ordered reconstruction passes with `upstream-litecoin-light-state.patch`.
The first compile found a test-helper module visibility error; the test-only scope is corrected.
Validation passed 50 ordinary light-wallet tests, six explicit TLS fixtures and eleven RPC tests
(67 top-level passes), plus all-features/all-targets release compilation.
The actual-node RPC fixture remains unrun; the ignored TLS child runs through its parent fixtures.
Terminal evidence: `build/litecoin-light-state-v2-validation-status-20261010.json`.
Current sources, copied binary and TLS fixtures match `build/litecoin-light-state-v2-provenance-20261010.json`.
Test binary SHA-256: `814e359ab9890675660b58e8a5b22ac229d868c9c4806cc650516587b8b57d47`.
The coordinator performs blocking disk/replay work and still needs the app worker/helper lifecycle.
Freshness/checkpoint policy, local wallet creation/discovery/signing and DEX/release acceptance
remain unfinished. Mining remains off and live markets remain disabled.


### Litecoin read-only app helper — verified local fixtures, 10 October 2026

Added the Linux `native-ltc-light` worker executable with strict, bounded JSON stdin
and one JSON response. `--status-stdio-v1` opens and revalidates cached headers offline.
`--sync-stdio-v1` connects to an explicitly configured TLS endpoint, reads a disposable
reported-tip connection, finds the shared ancestor, validates bounded header batches and
publishes a complete stronger candidate through `ChainState` before returning status.
Configuration requires schema `monzero-private-ltc-light-v1`, `network` (`main`, `test` or
`regtest`), a canonical private `cacheDirectory` and `maximumHeight` (at most 10,000,000).
Synchronization additionally requires `endpoint` with explicit `host` and `port`.
The configuration file must be canonical, owned, single-link and mode 0600; it is leased
for the operation. Requests carry a non-nil `requestId` and mode-specific schema
`monzero-ltc-light-{status,sync}-request-v1`. Sync requires `maximumDownloadHeaders`
from 1 through 10,000,000; status refuses that field. The complete candidate must fit the
budget before header download begins. Incomplete candidates are not published or cached;
a failed initial sync must restart its download. There is no automatic retry.
Responses retain `executable=false` and `freshness=unassessed`, distinguish the reported
peer tip from the selected chain and report changes/download counts. They contain no
wallet keys, balances, signing commands, broadcast operation or personal file paths.
Seven production-subprocess fixtures use only loopback TLS to cover offline status,
sync/restart/reorg, same-tip/no-write, weaker branches, budget/empty-data refusal,
untrusted certificates and malformed requests/configuration/cache records.
Ordered reconstruction passes with `upstream-litecoin-light-helper.patch`.
Validation passed all seven production-helper process fixtures and the existing 50 ordinary
light-wallet, six explicit TLS and eleven RPC tests (74 top-level passes), plus all-features/all-targets
release compilation. The actual-node RPC fixture remains unrun; the ignored TLS child runs through parents.
Terminal evidence: `build/litecoin-light-helper-validation-status-20261010.json`.
Sources, both copied binaries, TLS fixtures and Python process-test hashes match
`build/litecoin-light-helper-provenance-20261010.json`.
Helper SHA-256: `6959b97dab8ecf5e18674aed6cbb9e9b0b82b863c0adbd08908e3f325b4d789c`.
The unchanged library-test binary retains SHA-256 `814e359ab9890675660b58e8a5b22ac229d868c9c4806cc650516587b8b57d47`.
The Qt launcher and progress/lifecycle wiring, freshness/checkpoint policy, actual local
wallet creation/discovery/signing, DEX acceptance and platform/release checks remain unfinished.
Mining and live markets stay off.


### Litecoin Qt light-chain launcher — verified local fixtures, 10 October 2026

`LitecoinLightJob` now launches the production read-only helper asynchronously with bounded
stdin/stdout/stderr, a one-hour process deadline, explicit cancellation and executable-digest
checks before and after execution. A private canonical launcher configuration uses schema
`monzero-private-ltc-light-launcher-v1` with `helper`, `helperDigest` and `configFile`.
The referenced helper configuration is pinned for the operation; changed files invalidate results.
Only LANG and explicit SSL_CERT_FILE/SSL_CERT_DIR trust-store overrides pass to the worker.
Status and explicit synchronization remain separate operations. Every new operation clears
previous displayed evidence; failures clear it again. Responses require exact schemas, request
identity, network, bounded integer heights/downloads, hash and work formats, unassessed freshness,
consistent synchronization details and `executable=false`. No wallet/signing authority is added.
The wallet build passes. Seven Qt process-test groups pass, including 19 malformed-response
subcases and real-helper offline status, TLS synchronization, restart, reorganisation, corrupt
cache refusal, cancellation, changed configuration and failed-refresh evidence invalidation.
Evidence: `build/litecoin-light-qt-provenance-20261010.json`,
`build/litecoin-light-qt-tests-final-20261010.log` and `build/litecoin-light-qt-build-20261010.log`.
Rust sources are unchanged from the verified helper snapshot; its prior 74 local checks remain
separate evidence and were not rerun for this Qt-only change. No nodes or mining were launched.
This launcher is compiled into the private wallet build but still needs DexEngine/settings/QML
wiring. Freshness policy, local wallet creation/discovery/signing and complete DEX acceptance
remain unfinished. Live markets remain disabled.


### Litecoin private app light-chain panel — verified, 10 October 2026

DexEngine now owns the light-chain launcher and exposes separate saved-header loading,
explicit synchronization and cancellation methods/status. The hidden `--dex-ltc-light`
option accepts the private launcher configuration only with the existing private-DEX opt-in.
Startup queues offline saved-header loading; it does not contact an Electrum server.
The LTC panel shows stored height and separately labelled server-reported height/downloads,
busy/failure state, explicit load/sync/cancel controls and the unfinished freshness/wallet boundary.
No header result changes trading activation or supplies a spendable wallet balance.
Private configuration attachment is implemented; ordinary wallet onboarding/settings are not.
The wallet build passed, all 41 QML checks passed, and the DEX suite passed 52 tests with
21 external-fixture tests skipped. Those skipped flows are not counted as acceptance.
The first isolated QML invocation lacked its font import path; the corrected run uses
`-import qrc:/fonts` with the registered application resource bundle. Existing harness
isOpenGL/deprecation warnings remain; no wallet/daemon/miner was launched.
The captured light-chain panel was visually inspected. Evidence:
`build/litecoin-light-panel-provenance-20261010.json`,
`build/litecoin-light-panel-qml-final-20261010.log`,
`build/litecoin-light-panel-dex-tests-20261010.log`, and `build/litecoin-light-panel.png`.
Local wallet creation/restoration, owned-output discovery, fee-aware balances, signing,
freshness/checkpoint policy, complete swap/recovery acceptance and release gates remain unfinished.
Mining stays off; every live market remains disabled.


### Litecoin public-account address derivation — verified local fixtures, 10 October 2026

`PublicAccount::from_master` borrows a caller-owned BIP32 root and derives a BIP84 account,
retaining only its public extended key and origin metadata. Mainnet uses m/84'/2'/account';
testnet/regtest use m/84'/1'/account'. Receive/change derivation uses branches 0/1 and
non-hardened address indices. Addresses use the existing Litecoin-specific native SegWit codec.
Invalid root metadata/network kind, hardened address indices and out-of-range accounts are refused.
Script matching is exact derivation correspondence, not proof of unspentness or current signing control.
There is no private-key serializer, seed generator, mnemonic flow, vault or signing API.
The dependency may internally copy private derivation material; best-effort erasure of the
explicit temporary key is not a secure-memory guarantee. The caller remains responsible for its root key.
Address derivation is pure and does not allocate/persist indices; backup and discovery policy remain required.
Independent Python HMAC/OpenSSL/Bech32 vectors use a PUBLIC TEST SEED and cover receiving,
change and multiple accounts on all three networks. The generator first matches the published
BIP84 Bitcoin example, then produces the Litecoin vectors. Regtest and testnet share the standard
test coin path but use distinct address encodings. No real wallet keys are generated or used.
References: https://github.com/bitcoin/bips/blob/master/bip-0084.mediawiki and
https://github.com/satoshilabs/slips/blob/master/slip-0044.md (Litecoin coin type 2; all-testnets type 1).
Ordered reconstruction passes with `upstream-litecoin-light-account.patch`; independent vectors reproduce exactly.
Validation passed 53 ordinary light-wallet tests (including 12 independent address vectors),
six explicit TLS fixtures, eleven RPC tests and seven helper subprocess tests (77 top-level passes),
plus all-features/all-targets release compilation. The actual-node RPC fixture remains unrun.
Terminal evidence: `build/litecoin-light-account-validation-status-20261010.json`.
Current sources, vector file, generator, copied binaries and TLS fixtures match
`build/litecoin-light-account-provenance-20261010.json`.
Test binary SHA-256: `6350f60ef544f32e3e84f27ae851eaaeb680cb2762de18790c9e8a5d38027f7b`.
Helper SHA-256: `5acc6191c5ae8954e5d348a89823e3ee624db01c398762ba52fb73c475760053`.
Encrypted seed lifecycle, backup/restoration UX, durable address allocation/discovery, balances,
local signing, freshness policy and complete DEX/release acceptance remain unfinished.
Mining stays off and live markets remain disabled.


### Litecoin durable address allocation — implementation, 10 October 2026

`AddressBook` binds its private journal to the full account public key/chain code,
master fingerprint, account index and network genesis through a domain-separated digest.
Creation is explicit; reopening requires an existing valid record and cannot silently reset it.
A non-nil request UUID identifies each allocation. Repeating it returns the same branch/index;
reusing it for another branch fails. Receive/change indices advance independently.
The complete bounded record is compared and atomically replaced under the existing directory
lease before an address is returned. Publication errors invalidate the handle until reopen,
including an injected lost result after a successful durable replacement. Out-of-lease changes
are rejected and invalidate the handle. No private keys are written.
The journal primitive adds a bounded compare-and-replace method; existing execution-head behavior
is unchanged. Tests cover retry/restart, branch separation, lease exclusion, account/seed/network
mismatch, malformed state, changed records, pre-publication failure and uncertain publication.
Ordered reconstruction passes with `upstream-litecoin-light-allocation.patch`.
Validation passed under `build/litecoin-light-allocation-v2-validation-status-20261010.json`: 57 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (81 total), plus the all-features/all-targets release check. The actual-node RPC test remains ignored. Source, binary and fixture hashes were rechecked against provenance.
Reopening validates and syncs the existing record and directory before returning an address, reconciling uncertain publication. The failure fixture injects an API error after successful durable publication; it does not simulate an operating-system fsync failure.
The 10,000-entry journal capacity is a resource bound, not a wallet discovery gap policy.
Backup rollback protection, discovery/gap handling, encrypted seed lifecycle, receiving-address UI,
balances, local signing and full DEX/release acceptance remain unfinished. The allocator is not
exposed by the read-only chain helper. Mining stays off and live markets remain disabled.


### Litecoin light-wallet transaction discovery input — 10 October 2026

`ReadSession::reported_history` retrieves script history through the negotiated Electrum session.
The caller sets an entry budget between 1 and 1,000; responses also have a byte limit.
Entries retain transaction IDs and reported block/mempool positions only.
Malformed IDs/heights/fees, duplicate transactions, excess entries and unknown fields fail the request and invalidate the connection.
Local script/budget errors fail before network I/O.
The query discloses the script hash to the configured server.
Empty history does not prove an unused address, and history does not establish completeness, confirmations, unspentness or balances.
Reported transactions still need raw transaction/script checks and locally anchored inclusion verification.
Protocol reference: https://electrumx.readthedocs.io/en/latest/protocol-methods.html#blockchain-scripthash-get-history
Three mock-stream tests cover valid and empty histories, invalid reports and request limits.
Validation passed under `build/litecoin-light-history-validation-status-20261010.json`: 60 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (84 total).
The all-features/all-targets release check and ordered patch reconstruction passed.
The actual-node RPC test remains ignored; this is isolated fixture evidence.
Source, fixture and binary hashes match `build/litecoin-light-history-provenance-20261010.json`.
The ordered patch is `upstream-litecoin-light-history.patch`.
Address scanning with gap policy, transaction reconciliation, encrypted seed storage, signing and full swap acceptance remain unfinished.
This API is not exposed by the private GUI helper; no external wallet/server was contacted.
Mining stays off and live markets remain disabled.


### Litecoin bounded address discovery — 10 October 2026

`ReadSession::scan_branch` derives and queries one BIP84 account branch from index zero.
The account network must match the negotiated session before any query.
The caller supplies the known allocated count; `AddressBook::allocated_count` exposes that count after checking the retained journal.
A reported gap is counted only beyond the allocated range, and any nonempty history resets it.
The caller explicitly chooses a gap of 1..1,000, an address budget of 1..10,000 and a total history-entry budget of 1..10,000.
Results distinguish `ReportedGap` from `AddressBudget`, retaining nonempty address histories with their indices.
History-budget exhaustion or query failure returns an error without a partial successful scan.
Scanning does not allocate addresses, persist discovery state, prove complete history or establish balances.
The server learns the queried scripts; omission can produce a false reported gap.
Tests cover leading empty allocated addresses, gap reset, exact derived script requests, partial address-budget termination, total-entry exhaustion and pre-I/O network/limit rejection.
Validation passed under `build/litecoin-light-scan-validation-status-20261010.json`: 64 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (88 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, binary and fixture hashes were verified against `build/litecoin-light-scan-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-scan.patch`.
Restoration beyond missing allocation metadata needs an explicit extended scan policy.
Seed lifecycle, transaction/script/inclusion reconciliation, balances, signing and full DEX acceptance remain unfinished.
No helper/UI activation, real-node run or mining was performed; live markets remain disabled.


### Litecoin included transaction effects — 10 October 2026

`ReadSession::transaction_on` retrieves a transparent transaction and Merkle branch using a header from the caller's locally validated chain.
Network/height mismatch fails before network I/O; malformed transaction/proof responses invalidate the session.
`IncludedTransaction` retains committed output amounts/scripts, input outpoints, coinbase classification and the inclusion match.
It exposes exact-script receipt matching and spent-outpoint references, without exposing witness data or signing authority.
The constructor independently checks that transaction effects match the proven transaction ID.
Confirmation depth is recomputed against the selected branch, returning none for an orphan or unrelated header.
Existing inclusion-only APIs share the same fetch/verification path and retain their original result type.
A receipt is not a current unspent balance: missing spends, conflicting spends, coinbase maturity, chain freshness and transaction consensus validity still require handling.
Tests cover exact script/value/outpoint mapping, altered-output rejection, unrelated-chain depth and pre-I/O network/height rejection.
Validation passed under `build/litecoin-light-transactions-validation-status-20261010.json`: 67 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (91 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, binary and fixture hashes match `build/litecoin-light-transactions-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-transactions.patch`.
Discovery-to-accounting orchestration, durable transaction state, encrypted keys, signing and full swap/release acceptance remain unfinished.
No real-chain test or helper/UI activation occurred; mining remains off and live markets remain disabled.


### Litecoin observed-set accounting — 10 October 2026

`ObservedWallet::reconcile` recomputes wallet-script receipts minus spends in an explicitly supplied included-transaction set.
Transactions outside the selected header chain are excluded; the snapshot records its tip and height.
Script duplicates do not double-count receipts, and transaction order does not affect subtraction.
Duplicate active transaction identities, conflicting input spends, impossible known-parent references, immature observed coinbase spends and amount overflow fail reconciliation.
Input scripts, transaction count and processed output count are bounded.
Coinbase amounts are separated until depth 100, indicating maturity for a possible next-block spend, not general spendability.
Rule reference: https://github.com/litecoin-project/litecoin/blob/v0.21.5.8/src/consensus/consensus.h
The snapshot is provisional: omitted spends can inflate its amounts, and no history completeness, mempool reconciliation, freshness or consensus-validity claim is made.
Accounting fixtures inject transaction identities against fixed headers to test accounting logic only; they do not claim those synthetic transactions pass Merkle verification.
Separate existing tests cover the production inclusion boundary.
Tests cover spend subtraction, script deduplication, transaction order, removal of an orphaned spend, conflicts, duplicates, overflow and coinbase maturity boundaries.
Validation passed under `build/litecoin-light-accounting-validation-status-20261010.json`: 71 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (95 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, fixture and binary hashes match `build/litecoin-light-accounting-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-accounting.patch`.
Discovery orchestration, durable observations, mempool handling, encrypted keys, signing and full DEX/release acceptance remain unfinished.
Development fixtures did not launch wallet/chain processes; live markets remain disabled.
Separately, at the user's subsequent request, mainnet mining resumed on the original FRJ2nMKx…ENAhsryR address with exactly two threads; daemon RPC confirmed active mining.


### Litecoin combined wallet refresh — 10 October 2026

`ReadSession::refresh_wallet` scans receiving and change branches against a borrowed selected header chain.
Both branches must reach their reported gap; an exhausted address budget returns no snapshot.
Repeated transaction reports are merged, inconsistent positions rejected, and confirmed transactions fetched once through `transaction_on`.
Each confirmed history claim must match an output to the reported script or a spend of a matching output present in the verified observation set.
Effects are bounded across downloads; accounting now bounds total processed inputs as well as outputs.
The result includes both scans, tip-bound provisional accounting and a distinct count of unverified mempool reports.
Mempool spend effects are NOT subtracted; the result is not spendable balance or a funding capability.
Failure invalidates the session and returns no partial snapshot.
Five mock-session fixtures cover empty two-branch refresh, incomplete discovery, conflicting positions, pending-report deduplication and history beyond the selected chain.
A complete nonempty confirmed refresh remains to be tested end-to-end with a matching fixed transaction/header fixture; existing separate tests cover inclusion and accounting.
Validation passed under `build/litecoin-light-refresh-validation-status-20261010.json`: 76 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (100 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, fixture and binary hashes match `build/litecoin-light-refresh-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-refresh.patch`.
Mempool reconciliation, durable observations, freshness/completeness policy, encrypted keys, signing, private helper/UI wiring and full DEX/release acceptance remain unfinished.
Mainnet mining remains separately enabled at the user's request with two threads on the original address.


### Litecoin funded refresh fixture — 10 October 2026

The combined refresh now has a fixed funded fixture using the existing PUBLIC TEST SEED.
An independent Python generator serializes two synthetic transactions and headers, checks fixed-nonce Scrypt work and calculates SHA256d identities.
The generator and tests perform no nonce search, node launch or external network access.
Production header validation, raw transaction parsing, Merkle inclusion, exact wallet script relation and accounting run together through the mock Electrum stream.
The receipt is 100,000 base units; its later spend returns 60,000 units to change.
A refresh against the retained prefix without the spend returns the original receipt.
Further cases reject an invalid Merkle position and a validly included transaction falsely attributed to an unrelated wallet script.
Transaction-fetch deduplication and exact RPC parameters are checked.
These synthetic transactions use invented input history and do not establish full block-body or UTXO consensus validity, nor real-chain swap acceptance.
Fixture generation reproduces the checked-in JSON byte-for-byte.
Validation passed under `build/litecoin-light-funded-validation-status-20261010.json`: 80 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (104 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, fixture, generator and binary hashes match `build/litecoin-light-funded-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-funded.patch`.
Mempool reconciliation, durable wallet state, freshness/completeness policy, encrypted keys, local signing and private helper/UI integration remain unfinished.
The separately authorized XMZ miner remains configured for two threads.


### Litecoin pending transaction accounting — 10 October 2026

Wallet refresh now fetches bounded canonical raw transactions for distinct pending history IDs and checks exact transaction identity.
Pending history must have an output or observed input relation to every claimed wallet script.
Reported mempool spends remove the affected confirmed coins from provisional available amounts; pending receipts are separately counted and never promoted to confirmed amounts.
Pending chains count terminal wallet outputs only, so intermediate change is not double-counted.
New snapshots recalculate reservations from the supplied reports rather than retaining stale pending locks.
Duplicate IDs, conflicting pending spends, conflicts with observed confirmed spends, invalid known-parent output indices, pending coinbases and immature coinbase spends are rejected.
A confirmed child with a parent still reported pending is rejected as inconsistent evidence.
Pending effects and amount arithmetic are bounded; raw bytes prove transaction identity, not signature validity or actual mempool membership.
A malicious peer can fabricate pending spends or omit them; these amounts remain provisional and confer no swap funding/signing authority.
The funded mock refresh verifies 100,000 confirmed units becoming reserved with 60,000 pending change and only one confirmed Merkle request.
Further tests cover wrong raw transaction identity, pending chains, restoration on a fresh report set, conflicts, coinbase rejection and contradictory confirmation state.
Validation passed under `build/litecoin-light-pending-v2-validation-status-20261010.json`: 84 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (108 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, fixture and binary hashes match `build/litecoin-light-pending-v2-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-pending.patch`.
Durable wallet state, freshness/completeness policy, encrypted keys, local signing and private helper/UI integration remain unfinished.
The separately authorized XMZ miner remains configured for two threads.


### Litecoin public-account worker descriptor — 10 October 2026

`PublicAccount::descriptor` exports the account-level BIP32 public key, selected Litecoin network, account index and master fingerprint under a versioned strict schema.
`from_descriptor` imports against a separately selected network and rejects unsupported schemas, private keys, invalid encodings, noncanonical fingerprints, wrong account depth, nonhardened/mismatched account children and key-network mismatch.
Testnet/regtest require the explicit network binding because BIP32 test keys alone cannot distinguish them.
This is watch-only address-linking data intended for private worker configuration, not logs or public URLs.
Import does not authenticate the claimed origin path/fingerprint or prove private-key ownership.
Tests round-trip all three networks and boundary account indices, comparing receive/change addresses, and reject malformed/duplicate/extra fields and invalid key metadata.
Validation passed under `build/litecoin-light-descriptor-validation-status-20261010.json`: 87 light-wallet tests, 6 explicit TLS tests, 11 RPC tests and 7 helper process tests (111 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, fixture and binary hashes match `build/litecoin-light-descriptor-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-descriptor.patch`.
The descriptor is a prerequisite for helper wallet-refresh configuration; that helper mode and GUI balance wiring are not yet implemented.
Encrypted keys, local signing, durable wallet state, freshness/completeness policy and full DEX acceptance remain unfinished.
The separately authorized XMZ miner remains configured for two threads.


### Litecoin private watch-only helper — 10 October 2026

`native-ltc-light` now accepts explicit `--wallet-init-stdio-v1` and `--wallet-refresh-stdio-v1` operations.
The existing private 0600 configuration optionally carries `wallet: {account: <PublicAccountDescriptor>, addressBookDirectory: <canonical private directory>}`.
Initialization opens an explicit account-bound address journal without network access; it cannot reset allocated or incompatible records.
Refresh opens the retained journal, reads receive/change allocation counts under its lease, and queries the configured authenticated TLS peer against the retained header chain.
It does not silently synchronize headers or initialize a missing journal.
The refresh request schema is `monzero-ltc-light-wallet-refresh-request-v1`, with non-nil `requestId` and `walletScan: {gap, addressesPerBranch, historyEntriesPerBranch}`.
Scan limits are checked before network I/O; the whole refresh has a one-hour deadline in addition to per-request deadlines.
The response uses `monzero-ltc-light-wallet-refresh-response-v1` and contains `chain` plus `wallet`, with `executable:false`, `watchOnly:true`, `spendable:false` and `balanceStatus:"provisional"`.
Atomic amounts are decimal strings in `observedConfirmedAtomic`, `immatureAtomic`, `pendingSpentAtomic` and `pendingReceivedAtomic`.
It also reports scan counts, pending transaction count and observed confirmed coin count, without returning public account keys or transaction details.
Errors return no partial stdout snapshot.
Existing status/sync response shapes remain unchanged.
Production subprocess tests use an independently derived PUBLIC TEST SEED descriptor and loopback TLS to verify initialization, permissions, descriptor binding, funded/pending refresh, repeated refresh, scan limits and wrong raw transaction rejection.
Validation passed under `build/litecoin-light-wallet-helper-validation-status-20261010.json`: 87 light-wallet tests, 6 explicit TLS tests, 11 RPC tests, 7 chain-helper process tests and 3 wallet-helper process groups (114 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, fixture, generator and binary hashes match `build/litecoin-light-wallet-helper-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-wallet-helper.patch`.
Qt currently accepts the earlier chain-only configuration; wallet command/configuration integration in Qt/QML remains outstanding.
This watch-only worker does not create private keys, allocate a receiving address, sign, broadcast or enable a DEX market.
Encrypted key lifecycle, receiving UX, durable observations, freshness/completeness policy and full DEX acceptance remain unfinished.
The separately authorized XMZ miner remains configured for two threads.


### Litecoin private watch-only Qt/QML integration — 10 October 2026

The private Litecoin launcher accepts optional wallet descriptor/directory configuration and exposes explicit initialization and wallet refresh through `DexEngine`.
The panel offers Initialize watch-only account and Refresh provisional amounts only for a configured watch-only wallet.
Refresh uses gap 20, at most 500 addresses per branch and 1,000 history entries per branch; exceeding limits returns failure, not a partial balance.
The panel displays observed confirmed amounts after reported pending spends, immature amounts, pending reservations and pending receipts separately.
Decimal formatting uses strings to preserve all 64-bit atomic amounts without floating-point rounding.
Observations are labeled provisional and non-spendable, with network freshness and omitted-history limitations.
No wallet query or initialization is automatic.
All operations clear preceding wallet amounts; failure and cancellation leave no old amounts displayed.
The Qt worker validates exact response fields, schema/identity/network, non-executable/watch-only/non-spendable flags, decimal amount range, scan budgets and count consistency.
The new commands grant no order execution, key creation, signing or broadcast authority.
Validation: application/probe/DEX targets built; 7 existing chain-worker groups and 3 wallet-worker groups passed, including 14 malformed wallet response variants and a valid-then-failed refresh.
The production helper was exercised over loopback TLS through the Qt worker with funded transaction fixtures.
QML passed 42 cases; DEX tests passed 52 with 21 external-fixture skips.
The rendered panel screenshot was inspected for overlap.
Evidence and source/artifact hashes: `build/litecoin-watch-wallet-panel-provenance-20261010.json`.
Receiving-address allocation UX, encrypted seed lifecycle, local signing, durable observations, freshness/completeness policy and full DEX acceptance remain unfinished.
The separately authorized XMZ miner remains configured for two threads.


### Litecoin encrypted seed vault foundation — 10 October 2026

`SecretSeed` owns 32 bytes in a zeroizing, non-Debug/non-Clone/non-serializable type, with explicit OS-entropy generation or caller-owned restoration.
It derives public BIP84 accounts without exposing a public private-key serializer.
`SeedVault` stores a fixed-size 176-byte authenticated record under the existing private directory lease and atomic publication rules.
The versioned format uses Scrypt N=2^17, r=8, p=1 (128 MiB) and XChaCha20-Poly1305 from pinned RustCrypto 0.10.1.
The dependency already existed in the lockfile; this adds its direct use without changing its version.
Fresh 16-byte salt and 24-byte nonce come from the OS RNG on creation.
Version, network genesis, wallet UUID, salt and nonce are authenticated associated data.
Opening requires exact record length/version/network/wallet identity, and synchronizes recovered state before unlock.
Wrong password or ciphertext/header tampering fails; creation cannot replace an existing vault, and changed-on-disk records cannot be unlocked through an old handle.
Passwords are exact nonempty byte strings up to 1,024 bytes; password strength/UX policy is not implemented here.
The implementation uses in-place encryption/decryption and zeroizes owned plaintext and derived-key buffers.
This is not an mlock/secure-memory guarantee: dependency internals may copy secrets, and swap/core-dump hardening remains outstanding.
The caller retains responsibility for seed backup and any password/entropy copies.
Independent Python Scrypt/libsodium PUBLIC TEST vectors reproduce exact ciphertext; tests also cover restart, context mismatch, tampering, randomized encryption and password limits.
Reference: https://docs.rs/chacha20poly1305/0.10.1/chacha20poly1305/
Validation passed under `build/litecoin-light-vault-validation-status-20261010.json`: 91 light-wallet tests, 6 explicit TLS tests, 11 RPC tests, 7 chain-helper tests and 3 wallet-helper groups (118 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, generator, fixture and binary hashes match `build/litecoin-light-vault-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-vault.patch`.
No production seed was created or migrated; this vault is not connected to the helper or GUI.
Backup/recovery ceremony, mnemonic compatibility, password change, account binding, secure unlock UX, signing and full DEX acceptance remain unfinished.
The separately authorized XMZ miner remains configured for two threads.


### Litecoin encrypted vault lifecycle — 10 October 2026

Encrypted backup export authenticates the current retained ciphertext before returning its bytes.
Restore verifies password, exact format, network and wallet identity before publishing; it cannot overwrite different retained ciphertext and can retry identical restoration.
Password change authenticates the old password, generates fresh salt/nonce and atomically replaces the encrypted record under its lease.
Any publication failure invalidates the handle until reopen; tests inject failure both before writing and after a successful durable replacement.
These injected cases exercise ambiguous API outcomes, not actual operating-system fsync failure.
Reopening reconciles durability and accepts whichever password matches the retained authenticated record.
`unlock_for_account` derives the selected public account from the decrypted seed and requires an exact descriptor match before returning key material.
Changing a password does not revoke old backups, which still decrypt with the old password.
The encrypted seed backup does not include address-allocation/discovery metadata; a complete app backup bundle/ceremony remains required.
Tests cover wrong/tampered backup refusal before publication, restore retries, account mismatch, password-change restart, overwrite refusal, old-backup recovery and uncertain publication.
Validation passed under `build/litecoin-light-vault-lifecycle-validation-status-20261010.json`: 94 light-wallet tests, 6 explicit TLS tests, 11 RPC tests, 7 chain-helper tests and 3 wallet-helper groups (121 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, fixture, generator and binary hashes match `build/litecoin-light-vault-lifecycle-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-vault-lifecycle.patch`.
No production seed was created or changed; lifecycle operations are not yet wired to the helper or GUI.
Mnemonic compatibility, complete backup/recovery UX, secure unlock handling, signing and full DEX acceptance remain unfinished.
The separately authorized XMZ miner remains configured for two threads.


### Litecoin encrypted single-account recovery bundle — 10 October 2026

`vault::backup::export` verifies that the unlocked seed derives the address book's public account, then encrypts the seed-vault record, account index and complete allocation journal together.
The distinct versioned bundle format authenticates network/wallet identity and all bundled metadata with fresh salt/nonce and the existing fixed Scrypt/XChaCha20-Poly1305 construction.
Input lengths are bounded before decryption/deserialization.
Restore verifies nested vault authentication and account-bound allocation validity before preflighting both destination journals.
Both private directory leases are held, and conflicting retained records are rejected before either publication.
Publication spans two directories, so it is explicitly retryable rather than claimed atomic across both: the same bundle can finish a restore where only the seed vault was published.
No wallet is returned until both records are durably retained.
Retrying identical restored state is allowed; restoring over newer allocations is rejected.
Tests preserve receive/change counts, original request-to-address mappings and next indices, and reject wrong passwords, tampering, mismatched account metadata and conflicting destinations.
A valid older backup in an empty destination cannot reveal allocations made after export; rescan and backup-age handling remain required.
This single-account bundle excludes active DEX trade/recovery journals, observations and other accounts, and is not a complete application backup.
Validation passed under `build/litecoin-light-wallet-backup-validation-status-20261010.json`: 98 light-wallet tests, 6 explicit TLS tests, 11 RPC tests, 7 chain-helper tests and 3 wallet-helper groups (125 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The actual-node RPC fixture remains ignored.
Source, fixture, generator and binary hashes match `build/litecoin-light-wallet-backup-provenance-20261010.json`.
Ordered reconstruction uses `upstream-litecoin-light-wallet-backup.patch`.
No production keys were backed up or restored; destination-file saving/verification and user recovery ceremony are not yet wired to the helper or GUI.
Secure unlock handling, signing and full DEX acceptance remain unfinished.
The separately authorized XMZ miner remains configured for two threads.


### 2026-10-10 — authenticated Litecoin backup file storage

The light-wallet library now saves an authenticated single-account backup to a fixed file in an existing private directory, durably retains it, and checks the stored bytes by reading them back. Inspection and restore share validation of the encrypted seed vault, account and allocation record. Inspection exposes public recovery metadata and a ciphertext digest, without serializing the seed or password. The complete envelope is bounded to the journal file limit.

Identical save retries are supported; different contents cannot replace an existing backup. New backup generations require a new destination directory. Loading authenticates existing bytes but does not establish durability, freshness or an independent/offline copy. Tests cover restore from the saved file, allocation counts, wrong passwords, tampering, conflicting contents, unsafe permissions, symlinks, missing publication and a retry after an injected error following successful publication. The latter does not simulate an operating-system fsync failure.

Validation: `build/litecoin-light-backup-storage-validation-status-20261010.json` reports passed. There are 101 ordinary light-wallet tests, 6 explicit TLS tests, 11 RPC tests, 7 chain-helper tests and 3 wallet-helper groups (128 total). The actual-node RPC fixture remains ignored. All-features/all-targets release compilation and ordered patch reconstruction passed. All 283 recorded source, fixture and binary hashes were independently checked against `build/litecoin-light-backup-storage-provenance-20261010.json`. Reconstruction includes `upstream-litecoin-light-backup-storage.patch`.

This is library functionality, not yet a helper or GUI backup/restore workflow. The bundle excludes active DEX recovery journals and other accounts; older backups restored into an empty destination cannot reveal subsequent address allocations. Recovery UX, backup-age/rescan handling, secure unlock integration, signing and complete DEX acceptance remain unfinished. No production wallet was backed up or restored by these tests. The separately authorized XMZ miner remains configured for two threads.


### 2026-10-10 — explicit Litecoin receiving-address helper

`native-ltc-light --wallet-receive-stdio-v1 CONFIG` now opens the configured, initialized watch-only address book and durably reserves a receiving address using the request UUID.
An identical UUID returns the same address across process restarts; a new UUID allocates the next receiving index.
The response includes the address, derivation path and current receive/change allocation counts, with `watchOnly: true`, `spendable: false` and `executable: false`.
There is no implicit address-book initialization, network query, signing or market activation.
The caller must retain the request UUID for retries after uncertain results.
This mode rejects refresh/download budgets and uses the existing strict private configuration and journal boundaries.

Validation passed: 101 ordinary light-wallet tests, 6 explicit TLS tests, 11 RPC tests, 7 chain-helper tests and 4 wallet-helper groups (129 total), plus all-features/all-targets release compilation.
The new process test compares the initial address with the independent public fixture and verifies durable retries, new allocation, uninitialized refusal, malformed requests and network mismatch without allocation changes.
The actual-node RPC fixture remains ignored.
All source, fixture and binary hashes in `build/litecoin-light-receive-helper-provenance-20261010.json` were independently verified.
Ordered reconstruction passed with `upstream-litecoin-light-receive-helper.patch`.
The Qt/QML Receive control and key ownership/recovery UX remain unwired; these watch-only addresses are not presented as signing-ready trading wallets.
No real wallet received a new address during the fixture tests.


### 2026-10-10 — Litecoin receiving-address Qt worker

`LitecoinLightJob::receiveWallet(requestId)` and `DexEngine::receiveLitecoinLightWallet(requestId)` now expose the receiving helper to the application.
The caller supplies a canonical non-nil UUID, preserving retry identity instead of silently allocating another address.
The worker validates the watch-only flags, network-specific P2WPKH address shape, configured BIP84 account/receiving path and bounded allocation counts before exposing `wallet-address` state.
Address encoding remains the digest-pinned Rust helper's responsibility; the Qt shape check is not an independent checksum or key-ownership proof.
Failure clears the wallet/address snapshot.

The probe and full GUI executable built successfully.
Five wallet Qt fixture groups passed, including real-helper restart/retry behavior and eight malformed receiving responses; all seven existing chain Qt groups also passed.
Evidence: `build/litecoin-receive-qt-build-20261010.log`, `build/litecoin-receive-qt-tests-20261010.log`, `build/litecoin-receive-chain-qt-tests-20261010.log` and `build/litecoin-receive-qt-provenance-20261010.json`.
The app was compiled but not launched.
The QML Receive control and persistent UI retry identity remain to be connected; secure key ownership/recovery, signing and full trading acceptance remain unfinished.


### 2026-10-10 — watch-only receiving-address panel

The private Litecoin panel now has an explicit Show receiving address action.
The address appears only after a validated worker result, with network and watch-only/spending limitations, and is hidden during loading or after failed validation.
No receiving request runs automatically.
The worker stores a non-secret UUID in account/address-book-scoped application settings before requesting allocation.
A settings lock serializes concurrent token creation; synchronization errors and malformed saved IDs fail without issuing a receiving request.
Normal process restarts reuse the ID and address.
This settings record is not a key backup or a power-loss durability guarantee: deleting or losing it may allocate another address, while the native address book retains earlier allocations.
The current control retrieves the retained address; explicit address rotation remains future work.

The full GUI and probe builds passed, as did six wallet Qt groups, seven chain Qt groups and all 43 QML checks.
The added checks verify restart identity, malformed stored-ID refusal, explicit UI action, busy/failure clearing and pair visibility.
The isolated rendered screenshot `build/litecoin-receive-panel.png` was inspected for layout overlap.
Build/test logs use the `build/litecoin-receive-panel-` prefix; source, binary and screenshot hashes are recorded in `build/litecoin-receive-panel-provenance-20261010.json`.
Only isolated fixture runners were launched, not the wallet app or real Litecoin nodes.
Key ownership/recovery UX, signing and full DEX settlement/recovery acceptance remain unfinished, and live trading remains disabled.


### 2026-10-10 — owned P2WPKH input signing

`SecretSeed::sign_p2wpkh_input` verifies account derivation and the requested previous-output script before returning a SIGHASH_ALL witness, without exporting a private key.
It rejects duplicate/null inputs and a pre-existing target witness.
This is a low-level signing primitive, not spending approval: authenticated previous outputs, fee policy, reservations, coin selection and broadcast remain caller responsibilities.
The two new tests exercise output/amount/outpoint commitments and ownership/input rejection.
Validation `build/litecoin-light-sign-input-v2-validation-status-20261010.json` passed 131 selected runtime checks and all-features/all-targets release compilation.
Ordered patches reconstruct the source; provenance hashes were independently verified.
The actual-node fixture remains ignored and consensus acceptance is not claimed.
No signing GUI/helper or live market was enabled.


### 2026-10-10 — retained spend review

`SpendReview` retains one exact payment, optional derived change and the selected unsigned inputs, calculates the fee with checked arithmetic and rejects fees above an explicit absolute maximum.
It signs the retained transaction without replacing reviewed fields.
Previous-output authentication, unspentness, freshness, user consent and durable reservations remain external requirements.
Validation passed 133 selected runtime checks and all-features/all-targets release compilation.
Provenance hashes and ordered reconstruction were verified; the actual-node fixture remains ignored.
Evidence: `build/litecoin-light-spend-review-validation-status-20261010.json` and matching provenance/logs.


### 2026-10-10 — inclusion-anchored spend review

`ConfirmedSpendReview` extracts input amounts/scripts from inclusion-checked transactions, enforces confirmation and coinbase-maturity requirements, and restricts signing to the saved network/height/tip.
A changed tip requires a new review, including after a chain extension.
This does not establish freshness, complete history, unspentness, reservations or user consent.
The independent funded fixture exercises Merkle matching and review construction; an explicitly injected policy fixture exercises successful matching-seed signing and immature-coinbase rejection.
The chain-change test requires its specific guard error, avoiding a false pass from wrong-key refusal.
Validation `build/litecoin-light-confirmed-review-v3-validation-status-20261010.json` passed 135 selected runtime checks and all-features/all-targets release compilation.
Provenance hashes and ordered patch reconstruction were verified.
The actual-node RPC fixture remains ignored; no consensus or full-swap acceptance is claimed.


### 2026-10-10 — bounded server unspent reports

`ReadSession::reported_unspent` queries exact script hashes with strict field parsing, duplicate detection and an explicit output budget.
Malformed responses invalidate the connection without returning partial results; local limits reject before I/O.
These values are peer claims, not spendability evidence; matching with included transactions and observed spends remains separate.
Validation passed 138 selected checks and all-features/all-targets release compilation.
Source/fixture/binary hashes and ordered patches were verified; the actual-node fixture remains ignored.
Evidence: `build/litecoin-light-unspent-validation-status-20261010.json` and matching provenance/logs.


### 2026-10-10 — inclusion-checked candidate query

`included_unspent_candidates` validates reported output index, amount and script against raw transaction bytes and selected-chain Merkle evidence.
It excludes mempool, insufficiently confirmed and immature coinbase outputs; errors return no partial candidate set.
The returned inclusion evidence does not prove unspentness or a complete/fresh wallet view.
Independent funded fixtures check inclusion, amount/index/script mismatch refusal, depth filtering and mempool exclusion.
Validation passed 141 selected checks and all-features/all-targets release compilation; provenance and ordered patches were verified.
Evidence: `build/litecoin-light-candidates-validation-status-20261010.json` and matching provenance/logs.
Observed-spend reconciliation, durable reservations and approved signing integration remain unfinished.


### 2026-10-10 — observed candidate reconciliation

`retain_observed_candidates` intersects included candidates with a same-tip wallet snapshot, removing candidates absent after observed confirmed/pending spends.
It rejects stale snapshots, duplicates and inconsistent amount/depth/coinbase metadata, and retains only mature candidates.
Incomplete histories and wall-clock freshness remain unresolved; the result grants no spending authority.
Real-inclusion fixtures cover pending-spend exclusion, stale-tip refusal and duplicates.
Validation passed 143 selected checks and all-features/all-targets release compilation; provenance hashes and ordered patches were verified.
Evidence: `build/litecoin-light-reconcile-validation-status-20261010.json` and matching provenance/logs.
Durable reservation, consent and signing orchestration remain unfinished.


### 2026-10-11 — resumed; durable Litecoin input claims verified

Development resumed at the user’s request; mining remains off (daemon status verified inactive, zero threads and zero hashrate).
The interrupted claims work now passes fresh validation.
`ClaimedSpend` binds the operation ID, public account, exact unsigned transaction and previous-output metadata to immutable private journal records.
It preflights every input claim before writing, keeps the shared directory lease through signing, and checks retained records again before signing.
Identical retries survive restart or a deliberately simulated partial publication; conflicting operations cannot claim the same input or retain unrelated claims before an ordinary conflict is detected.
Tests cover lease contention, restart, conflicts, partial publication and tampered-manifest signing refusal.
Partial publication is simulated by retaining one claim; it is not an actual OS fsync-failure test.
All cooperating preparations must use the same store; these records neither lock network coins nor establish unspentness, user consent or safe release.
There is no automatic claim release and no new broadcast path.

`build/litecoin-light-claims-validation-status-20261011.json` reports passed: 117 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and four wallet-helper groups (145 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-claims-provenance-20261011.json` were independently verified.
Reconstruction now includes `upstream-litecoin-light-claims.patch`.
The actual-node RPC fixture remains ignored.
The interrupted BCH controller run still has no successful result and has not been restarted in this resumed checkpoint.
Approved signing orchestration, durable recovery/release policy, broadcasting and full DEX acceptance remain unfinished.


### 2026-10-11 — retained Litecoin signing verified

`ClaimedSpend::approve_and_sign` now requires the commitment to the exact retained operation, account, transaction and previous outputs.
It rechecks input claims, durably records signing intent, signs the reviewed transaction, and retains and reads back the signed bytes before returning them.
The earlier unretained `ClaimedSpend::sign` entry point was replaced.
Tests cover incorrect commitments without publication, deterministic retries, reopening with the same operation/review, conflicting signed bytes, a failed signing attempt followed by retry, and conflicting intent records.
Restart currently requires reconstructing the exact review; an independent journal loader is still needed.
Intent records do not establish human consent, input unspentness or broadcast approval.
There is no new broadcast path, and the app/helper signing UI remains unfinished.

`build/litecoin-light-retained-signing-v2-validation-status-20261011.json` reports passed: 119 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and four wallet-helper groups (147 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-retained-signing-v2-provenance-20261011.json` were independently verified.
The first compile attempt failed on a test fixture type mismatch; the corrected v2 run is the passing evidence.
The patch stack now includes `upstream-litecoin-light-retained-signing.patch`.
Actual-node RPC remains ignored, and the interrupted BCH controller run remains unverified.
Mining was checked inactive with zero threads and zero hashrate.
Journal recovery/release policy, authenticated candidate-to-signing orchestration, user approval UI, broadcasting and full DEX acceptance remain unfinished.


### 2026-10-11 — Litecoin retained transaction recovery verified

`ClaimedSpend::open` reloads the saved transaction and previous outputs under the private journal lease.
It requires an externally supplied operation, account, payment/change policy, maximum fee and exact manifest commitment, validates the reconstructed review, and checks every retained input claim.
The caller no longer needs to reconstruct transaction inputs from memory.
Opening does not create records, unlock keys, release claims or broadcast.
`load_signed` requires the matching signing intent, canonical transaction bytes, exact unsigned transaction content and a valid SIGHASH_ALL signature with the expected derived public key for every input.
These checks establish signatures against retained previous outputs, not current unspentness or broadcast authorization.

Tests reload signed bytes after dropping the seed and original handle; reject changed payment/fee/account/commitment expectations and missing claims; and reject changed outputs, damaged signatures and missing signing intent.
`build/litecoin-light-reopen-validation-status-20261011.json` reports passed: 122 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and four wallet-helper groups (150 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-reopen-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-reopen.patch`.
Actual-node RPC remains ignored; the interrupted BCH controller run remains unverified.
The app/helper still needs review-context persistence, approval and recovery UI, authenticated candidate-to-signing orchestration, broadcast handling and full DEX acceptance.
No mining was started.


### 2026-10-11 — observed Litecoin inputs connected to retained signing

`ConfirmedSpendReview::retain_observed` now consumes an inclusion-checked review, requires every input to remain in the supplied same-tip wallet observations with the expected amount and satisfied maturity, and then retains durable claims.
`ObservedClaimedSpend::approve_and_sign_on` rechecks the selected tip and supplied wallet observations before entering durable signing, including on retries.
Known pending spends prevent both initial retention and subsequent signing; the tests verify that rejection before retention leaves the journal empty and rejection before signing writes no new records.
The wrapper retains its chain anchor in memory; restart still requires a fresh inclusion/observation review to reconstruct this policy wrapper.
The lower-level journal loader remains available for inspecting retained signed bytes.
Snapshot equality does not establish wall-clock freshness or complete server history, and no broadcast or automatic claim release was added.

A policy-only injected inclusion fixture exercises successful signing and observed-pending-spend rejection.
The separate independent header/Merkle fixture exercises retention and changed-tip rejection; its synthetic external funding input is not a consensus-valid chain history.
`build/litecoin-light-observed-signing-validation-status-20261011.json` reports passed: 124 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and four wallet-helper groups (152 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-observed-signing-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-observed-signing.patch`.
Actual-node RPC remains ignored; the interrupted BCH controller run remains unverified.
Automatic coin selection, app/helper approval and recovery wiring, broadcasting and full DEX acceptance remain unfinished.
No mining was started.


### 2026-10-11 — Litecoin payment selection and exact change verified

`ConfirmedSpendReview::select_payment` validates supplied included outputs against account ownership and same-tip observations, excludes absent/pending-spent or insufficiently confirmed/immature outputs, and selects largest first with outpoint tie-breaking.
It constructs an exact payment followed by optional owned change, with the caller's explicit absolute fee and minimum change threshold.
It never silently consumes small change as extra fee.
The bounded greedy selection does not search every subset, estimate network fees, assert relay policy, allocate change addresses, consult other operations' claims, or broadcast.
The caller must durably reserve the supplied change index; later claim retention still rejects conflicting input use.

Tests verify deterministic ordering, exact payment/change/fee, selection through observed retention and two-input signing, exact-match no-change payments, tiny-change refusal, amount overflow, wrong ownership, duplicates, insufficient depth and pending-spend exclusion.
Selection/signing tests use policy-only injected inclusion identities, not consensus-valid funding history.
`build/litecoin-light-selection-validation-status-20261011.json` reports passed: 127 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and four wallet-helper groups (155 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-selection-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-selection.patch`.
Actual-node RPC remains ignored; the interrupted BCH controller run remains unverified.
Fee estimation, durable change-allocation wiring, app/helper approval and recovery, broadcasting and full DEX acceptance remain unfinished.
No mining was started.


### 2026-10-11 — durable change allocation connected to payment preparation

`prepare_payment` validates the account-bound address book and payment selection before allocating change, reserves needed change under the spend operation ID, reconstructs the review with the actual reserved index, and retains observed input claims.
No-change payments do not allocate a change address.
Address allocation and claims are separately durable, not an atomic two-store transaction; a claim failure leaves the change allocation retained for exact retry.
All preparations for a wallet must still use the same claim store.
There is no implicit signing or broadcast approval.

Tests cover a preexisting change index, restart reuse of the same address/transaction/commitment, two-input signing, wrong-book and insufficient-funds rejection before allocation, retained allocation across repeated claim conflicts, and no-change behavior.
The payment fixtures use injected policy inclusions rather than consensus-valid funding history.
`build/litecoin-light-preparation-validation-status-20261011.json` reports passed: 129 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and four wallet-helper groups (157 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-preparation-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-preparation.patch`.
Actual-node RPC remains ignored; the interrupted BCH controller run remains unverified.
Wallet-scan-to-preparation orchestration, fee estimation, app/helper approval and recovery, broadcasting and full DEX acceptance remain unfinished.
No mining was started.


### 2026-10-11 — scanned Litecoin evidence connected to payment preparation

`WalletObservation` retains its account, scanned derivation paths and inclusion-verified transactions alongside provisional accounting.
Its Linux `prepare_payment` method reconstructs candidates only from outputs remaining in that accounting snapshot and feeds the existing selection/change-allocation/claim flow.
Callers no longer supply previous outputs or ownership paths for this path.
The snapshot remains provisional and does not establish complete history or freshness.

The two existing successful funded-refresh tests now also prepare a payment from the observed receiving/change output, reject a different selected tip before allocation, and reopen the stores to verify the same commitment and change count.
They use independent header/Merkle fixture evidence; the synthetic funding history is not consensus-valid and these cases do not sign with the fixture seed.
`build/litecoin-light-scan-prepare-validation-status-20261011.json` reports passed: 129 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and four wallet-helper groups (157 total; existing tests were strengthened).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-scan-prepare-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-scan-prepare.patch`.
Before app exposure, restored/discovered address high-water marks must be reconciled with the address book to avoid selecting a previously used change index.
That reconciliation, fee estimation, app/helper approval and recovery, broadcasting and full DEX acceptance remain unfinished.
Actual-node RPC remains ignored; the interrupted BCH controller run remains unverified.
No mining was started.


### 2026-10-11 — discovered Litecoin addresses retained before change allocation

`AddressBook::retain_discovered` now advances receiving/change allocation counts monotonically in one record replacement, preserves existing request identities and fills newly covered indices with internal allocation IDs.
It retains the existing record format and capacity limits; uncertain publication invalidates the handle until validated reopen.
`WalletObservation::retain_discovered_addresses` checks the account and selected tip before retaining the highest reported-used index on each branch, including pending history.
Scanned payment preparation invokes this reconciliation before reserving change.
Discovery can be durably recorded even if the later payment preparation fails; it is never rolled back to retry a payment.
Peer omissions and rollback of an older valid backup remain unresolved.

The funded scan fixture now asserts that discovered change index zero leads to new change index one, including after reopen.
Two new tests cover monotonic counts, preserved request IDs, capacity rejection, next-index selection and a simulated lost result after actual record publication.
This is an API-level failure simulation, not an OS fsync-fault test.
`build/litecoin-light-discovery-allocation-validation-status-20261011.json` reports passed: 131 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and four wallet-helper groups (159 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-discovery-allocation-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-discovery-allocation.patch`.
Fee estimation, app/helper approval and recovery wiring, broadcasting and full DEX acceptance remain unfinished.
Actual-node RPC remains ignored; the interrupted BCH controller run remains unverified.
No mining was started.


### 2026-10-11 — private Litecoin helper exposes retained payment review

`native-ltc-light --wallet-prepare-stdio-v1 CONFIG` refreshes the configured watch-only account against retained headers, reconciles discovered addresses, selects inputs, reserves change and retains the unsigned review and input claims.
The wallet configuration requires an explicit private `spendDirectory` for this operation.
Its bounded request uses schema `monzero-ltc-light-wallet-prepare-request-v1`, a retained non-nil `requestId`, `walletScan`, and `payment` containing `address`, canonical integer-string `amountAtomic`, `feeAtomic`, `minimumChangeAtomic`, and positive `minimumConfirmations`.
This mode allows up to 2048 request bytes; previous modes retain the 512-byte limit.
Payment addresses currently use the existing network-checked Litecoin SegWit codec.
The response returns payment/change addresses and atomic amounts, exact fee, input count, txid and signing commitment, with `watchOnly:true`, `spendable:false`, `signed:false` and `executable:false`.
There is no key unlocking, signing or broadcast.

The new loopback-TLS helper test prepares from the independent funded fixture, retries in a separate process, verifies unchanged claim records and review, rejects changed payment under the same identity, rejects malformed fees/request modes, and checks that no signed transaction exists.
`build/litecoin-light-prepare-helper-validation-status-20261011.json` reports passed: 131 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and five wallet-helper groups (160 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-prepare-helper-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-prepare-helper.patch`; the Python helper test is maintained directly in the GUI repository.
Qt/QML still needs support for this operation and configuration field, plus retained review-context handling.
Fee estimation, app approval and recovery, signing-helper integration, broadcasting and full DEX acceptance remain unfinished.
Actual-node RPC remains ignored; the interrupted BCH controller run remains unverified.
No mining was started.


### 2026-10-11 — Qt Litecoin payment-preparation worker verified

`LitecoinLightJob::prepareWallet` now validates a caller-retained operation ID, canonical atomic amount strings, bounded scan/confirmation policy and a network-shaped SegWit address before launching the preparation helper.
Configuration accepts the optional explicit `spendDirectory`; availability requires it and a TLS endpoint.
The worker requires an unsigned, watch-only, non-spendable response whose payment address, amount and fee exactly match the request; validates input count, transaction ID, commitment and change shape/threshold; and clears rejected helper results.
`DexEngine::prepareLitecoinLightPayment` exposes the worker call for subsequent QML integration.
Qt address validation is structural; checksum/network decoding and owned-change derivation remain enforced by the pinned Rust helper.
The spend-directory field is excluded from the receiving-request settings identity so enabling preparation does not silently allocate another receiving address.

Full app, Qt probe and DEX test binary builds passed (`build/litecoin-light-qt-prepare-build-20261011.log`).
Eight wallet-worker groups and seven chain-worker groups passed, including real loopback TLS preparation/retry and malformed review responses.
The receiving-request test now adds the spend-directory setting between processes and confirms unchanged allocation.
DEX tests passed: 52 passed, 21 external-fixture-dependent skipped, zero failures.
Source, test, binary and log hashes are recorded and verified in `build/litecoin-light-qt-prepare-provenance-20261011.json`.
These GUI repository changes do not add an upstream Rust patch.
There is no QML payment action yet; request/review persistence and the review screen remain to be connected.
Fee estimation, signing-helper/approval integration, recovery, broadcasting and full DEX acceptance remain unfinished.
No app instance or miner was started by this validation.


### 2026-10-11 — Qt Litecoin preparation request persistence verified

`prepareCurrentWallet` validates the payment and source pins, retains the exact wallet/payment/scan context and a canonical operation ID in the configured private spend directory, then launches the helper with that ID.
`DexEngine::prepareCurrentLitecoinPayment` exposes this path for the forthcoming review UI.
`LitecoinPaymentRequest::retain` follows the existing Qt journal pattern: canonical owned private directory, nonblocking exclusive directory lease, relative no-follow file access, bounded canonical JSON, 0600 single-link records, and file/directory sync before returning an ID.
The filename is derived from the context hash. Identical context reuses its ID; malformed, partial, unsafe or conflicting records fail closed without deletion or a fresh-ID fallback.
Partial direct publication can leave a blocked record; automatic repair/release is not implemented.
This is retry persistence, not a completed multi-payment lifecycle: an identical context intentionally reuses the previous operation and cannot silently start a second payment.

Full app, Qt probe and DEX test binary builds passed (`build/litecoin-light-request-build-20261011.log`).
Nine wallet-worker groups and seven chain-worker groups passed.
The real helper preparation test now uses persisted IDs across processes and verifies exact saved context and rejection after corrupting the record.
Additional storage tests cover a busy directory, unsafe file permissions and a symlinked record.
DEX tests: 52 passed, 21 external-fixture-dependent skipped, zero failures.
Source, test, binary and log hashes are recorded and verified in `build/litecoin-light-request-provenance-20261011.json`.
No OS fsync-fault injection was performed for this Qt store.
QML payment entry/review and persisted-context loading remain unfinished, alongside fee estimation, signing/approval, recovery, broadcasting and full DEX acceptance.
No app instance or miner was started by this validation.


### 2026-10-11 — Litecoin payment entry and unsigned review panel verified

The configured private Litecoin panel now exposes recipient, LTC payment, manual fee, minimum change and input-confirmation fields, and an explicit Prepare payment review action using the persisted-request Qt path.
Decimal-to-atomic conversion uses string operations with eight-decimal precision and the u64 bound, avoiding JavaScript floating-point amount conversion.
The panel explains input reservation and unavailable signing/sending.
A returned review shows network, recipient, exact payment/fee/change, change address, input count and transaction ID; it appears only for the submitted form context and an unsigned watch-only response.
Loading, failed preparation, edited details or a market change hide the review.
`walletPrepareConfigured` controls panel visibility independently of the worker's busy state.

Full app and Qt probe builds passed (`build/litecoin-light-payment-panel-build-20261011.log`).
All 45 isolated QML tests passed, including exact amount conversion/bounds, explicit submission, form binding and clearing states.
Nine wallet-worker and seven chain-worker groups passed; the real-helper test checks the new panel-configuration status flag.
The rendered `build/litecoin-payment-panel.png` was visually inspected without overlapping fields or review text.
Source, test, resource bundle, binary, screenshot and log hashes are recorded and verified in `build/litecoin-light-payment-panel-provenance-20261011.json`.
The application was built but not launched against a user's wallet; UI tests used an isolated mock backend and helper tests used loopback fixtures.
Persisted-context loading into the form, fee estimation, signing/approval and recovery integration, broadcasting and full DEX acceptance remain unfinished.
No mining was started.


### 2026-10-11 — saved Litecoin requests load into the payment form

The private request journal now supports bounded read-only listing under a shared directory lease, with canonical owned/private/single-link record checks, exact context-hash filenames, schema/UUID validation and complete-result failure on malformed records.
Listing scans at most 50,000 directory entries and 1,000 saved request records; only exact matches to the configured wallet context are returned.
Qt additionally validates each payment and scan policy and clears prior reviews before loading the list.
The QML panel provides Find saved requests, a selector, and Load selected request.
Loading restores exact payment, fee, minimum change, confirmations and scan settings without launching preparation.
A separate preparation click reuses the persisted context/operation ID.
This loads request context, not a freshness assessment or permission to sign/send.

Full app, probe and DEX test binary builds passed (`build/litecoin-light-load-request-build-20261011.log`).
All 46 QML tests and 17 Qt worker groups passed, including real process-restart request loading, hash-mismatch rejection, wallet-context filtering, and restoration without an implicit helper action.
DEX tests: 52 passed, 21 external-fixture-dependent skipped, zero failures.
The rendered saved-request selector/form (`build/litecoin-saved-request-panel.png`) was inspected without overlapping controls.
Source, test, binary, resource, screenshot and log hashes are recorded and verified in `build/litecoin-light-load-request-provenance-20261011.json`.
Fee estimation, key-management/signing approval integration, broadcast/recovery lifecycle and full DEX acceptance remain unfinished.
No app instance with real wallets or miner was started by validation.


### 2026-10-11 — bounded Litecoin fee reporting and size estimates verified

`ReadSession::reported_fee_rate` adds the Electrum 1.4 `blockchain.estimatefee` request with explicit target and atomic-rate ceiling.
It preserves unavailable estimates as `None`, rejects malformed/negative/excessive rates, and poisons the session on remote failure while leaving locally invalid requests without I/O.
Original decimal/exponent spellings are converted with integer arithmetic, rounding fractional atomic rates upward.
`ReportedFeeRate::p2wpkh_fee` estimates an upper-bound virtual size using 73-byte signature and 33-byte key witnesses, rounds the absolute fee upward, and enforces a caller maximum.
It assumes all inputs are native P2WPKH; it does not establish ownership, relay policy, freshness, or confirmation guarantees.
Protocol references: https://electrumx.readthedocs.io/en/latest/protocol-methods.html#blockchain-estimatefee and https://github.com/litecoin-project/litecoin/blob/master/test/functional/rpc_fundrawtransaction.py .

Tests cover decimal/exponent precision, u64 bounds, upward rounding, unavailable/invalid RPC results and session behavior, and fee-size bounds against actual two-input signatures.
`build/litecoin-light-fees-v2-validation-status-20261011.json` reports passed: 134 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and five wallet-helper groups (163 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-fees-v2-provenance-20261011.json` were independently verified.
The first compile attempt failed on an empty character literal; the corrected v2 run is the passing evidence.
The patch stack includes `upstream-litecoin-light-fees.patch`.
The helper/UI do not yet expose these estimates; manual payment fees remain in place.

The BCH controller validation interrupted on 2026-10-10 was restarted with a fresh 2026-10-11 runner after confirming no old runner remained and verifying the pinned binary plus current BCH/native-funding source hashes.
Its immutable binary is recorded in `build/bch-current-controllers-status-20261011.json`, with logs in `build/bch-current-controllers-tests-20261011.log` and runner `build/run-bch-current-controllers-20261011.py`.
The process remains active; partial passing test lines are not a completed BCH result.
Signing/approval integration, fee UI, broadcasting/recovery and full DEX acceptance remain unfinished.
No mining was started.


### 2026-10-11 — fee-aware Litecoin payment selection verified

`ConfirmedSpendReview::select_estimated_payment` examines deterministic largest-first input prefixes, computes the conservative native-P2WPKH signed-size fee for each, checks exact no-change payments first, and then checks change-bearing payments against the configured minimum change.
It adds inputs when necessary to cover their own fees and never consumes a small remainder as extra fee.
The fee ceiling is enforced before returning a review.
Both manual and estimated selection share the same inclusion/ownership/maturity/observation eligibility checks.
`WalletObservation::estimate_payment` exposes this as a pure review using retained scan evidence, with no allocation, claims, signing or broadcast.
Like manual selection, it does not exhaustively search arbitrary subsets or prove complete/fresh unspentness.

Three new tests use a mock-session fee report and injected policy inclusions to check extra inputs for fee coverage, exact no-change selection under a cap that would reject a change output, small-change refusal and fee-ceiling rejection.
`build/litecoin-light-estimated-selection-validation-status-20261011.json` reports passed: 137 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and five wallet-helper groups (166 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-estimated-selection-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-estimated-selection.patch`.
The helper/UI still require fee-estimate exposure; manual fees remain in the app.
BCH controller validation (`build/bch-current-controllers-status-20261011.json`) remains active, with no terminal result; its existing process handle was polled rather than restarted.
Signing/approval, fee UI, broadcasting/recovery and full DEX acceptance remain unfinished.
No mining was started.


### 2026-10-11 — helper exposes bounded Litecoin payment fee quotes

`native-ltc-light --wallet-estimate-stdio-v1 CONFIG` accepts a bounded `walletScan` and `feeQuote` with network-checked recipient, canonical atomic amount/minimum-change strings, input confirmations, target blocks, maximum atomic rate per kvB and maximum absolute fee.
It fetches the reported fee rate and uses retained-header wallet scan evidence for fee-aware selection.
The response echoes payment/address, keeps watch-only/non-spendable/prepared-false flags, and returns either `feeEstimate:null` or target, atomic rate, conservative virtual size, exact proposed fee, change and input count.
An unavailable report does not become a zero/default fee or require a wallet history scan.
No spend directory, address allocation, input claims, signing or broadcast are part of this operation; opening the existing address book still reconciles its durability without changing its contents.

The new loopback helper test verifies the absolute quote against the fixture transaction size, both rate and absolute caps, unavailable reports, malformed responses, request-mode isolation and unchanged address-book contents.
`build/litecoin-light-estimate-helper-validation-status-20261011.json` reports passed: 137 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and six wallet-helper groups (167 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Source, fixture and binary hashes in `build/litecoin-light-estimate-helper-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-estimate-helper.patch`; the Python helper test is maintained in the GUI repository.
Qt/QML still needs fee-quote request/response handling and explicit application of an estimate to the form.
BCH controller validation remains active on its original 2026-10-11 process handle, with no terminal result.
Signing/approval, broadcasting/recovery and full DEX acceptance remain unfinished.
No mining was started.


### 2026-10-11 — Litecoin fee quotes connected to Qt and payment panel

The Qt watch-only worker now exposes `wallet-estimate` through `estimateLitecoinPaymentFee`.
It validates request bounds, response identity/payment echoes, unavailable estimates, rate and absolute fee caps, nonzero bounded input counts/virtual size, minimum change and exact ceiling arithmetic without floating-point amount conversion or integer overflow.
The worker supports quoting without a spend directory; the current payment panel remains gated by the existing preparation configuration.
The panel provides target-block, maximum-rate and maximum-total-fee fields, an explicit Estimate fee action, and a separate Use estimate action.
Requesting a quote does not change the entered fee, reserve addresses/inputs, prepare a payment, sign or broadcast.
Editing payment/quote/scan details hides a mismatched quote; loading, errors and market changes also hide it. Unavailable estimates leave manual fee entry available.

Validation: full GUI/probe/DEX-test build passed; 13 wallet-worker groups and seven chain-worker groups passed against the pinned real helper plus strict fake-response boundaries; 47 QML checks passed; DEX tests passed 52 with 21 external-fixture-dependent skips.
Tests cover loopback TLS quotes without a spend directory, unchanged address-book bytes, unavailable/excessive rates, malformed requests/responses, ceiling rounding, overflow rejection, explicit application and unchanged preparation state.
The fee-panel screenshot was inspected. Logs and source/binary/resource/screenshot hashes are recorded in `build/litecoin-light-estimate-ui-provenance-20261011.json` and were independently rehashed.
The prior 167 Rust/helper checks remain the helper evidence; Rust sources were unchanged in this milestone.
BCH controller validation remains running on its original 2026-10-11 process, with no terminal result.
Signing/approval UI, broadcasting/recovery and complete DEX acceptance remain unfinished. Live markets remain disabled.
Mining status was checked: inactive, zero threads, zero hash rate. Mining was not restarted.


### 2026-10-11 — reopen exact Litecoin payments against refreshed observations

`WalletObservation::reopen_payment` now reopens a retained payment using the caller's retained payment/change/fee policy, operation and signing commitment, then matches its exact input order to the refreshed scan evidence.
`ObservedClaimedSpend::revalidate` checks every retained prevout, derivation branch/index, selected-chain inclusion, confirmation depth and coinbase maturity before accepting the current same-tip confirmed/pending accounting.
It preserves the saved unsigned transaction and commitment; it does not rerun selection, allocate addresses, create replacement claims, sign or broadcast.
Existing explicit signing still rechecks the new observation anchor and commitment before using the seed.

Tests cover a process-style reopen after a chain extension, rejection of stale observations, insufficient depth, missing input evidence, changed derivation paths and pending spends, then explicit signing with a public fixture seed and verified reload of the signed transaction.
The funded scan tests also exercise direct reopening with identical transaction/commitment, unchanged address-book bytes, stale-chain rejection and changed fee/change policy rejection.
The policy signing fixture injects inclusion identities; funded scan fixtures separately exercise header/Merkle validation. Neither establishes mainnet consensus acceptance or complete server history.

`build/litecoin-light-reobserved-validation-status-20261011.json` reports passed: 138 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and six wallet-helper groups (168 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
The source, fixture and immutable binary hashes in `build/litecoin-light-reobserved-provenance-20261011.json` were independently rehashed.
The patch stack includes `upstream-litecoin-light-reobserved.patch`.
Helper/UI recovery and signing approval integration, broadcasting/recovery lifecycle and full DEX acceptance remain unfinished; markets remain disabled.
BCH controller validation remains live on its original process, without a terminal result. Mining remains off.


### 2026-10-11 — helper and Qt reopen retained Litecoin payments

`native-ltc-light --wallet-reopen-stdio-v1 CONFIG` accepts the original operation as `requestId`, bounded `walletScan`, original `payment`, and `retainedReview` containing `signingCommitment` and `changeAddress` (null for no change).
The helper checks the saved change allocation with the new read-only `AddressBook::reserved`, refreshes wallet observations and calls the exact-transaction recovery path.
It verifies exact fee and minimum-change policy and returns the original unsigned review fields. It never substitutes inputs, allocates recovery change, recreates missing claims, signs or broadcasts.
The no-change path requires no change allocation. Invalid commitments, policy, missing claims and observed pending spends fail without a partial response.

`LitecoinLightJob::reopenWallet` and `DexEngine::reopenLitecoinPayment` now expose the operation to app code.
The Qt worker binds the helper response to payment, fee, saved signing commitment and change address, clears failed results, and reports `wallet-reopened` only for a valid response.
The payment UI still needs durable review persistence/loading and an explicit reopen action; no signing or broadcast UI was enabled.

Validation: 139 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and seven wallet-helper groups passed (170 total).
All-features/all-targets release compilation and ordered patch reconstruction passed; the stack includes `upstream-litecoin-light-reopen-helper.patch`.
The full GUI/probe/DEX-test build passed, plus 14 wallet-worker and seven chain-worker groups against the new pinned helper, and 52 DEX tests with 21 external-fixture-dependent skips.
Recovery tests cover change/no-change, independent process reopening, byte-identical journals, pending spends, absent claims, request-mode isolation, and rejection of different valid-looking commitments/change addresses by Qt.
Source/fixture/binary hashes in `build/litecoin-light-reopen-helper-provenance-20261011.json` and GUI/test hashes in `build/litecoin-light-reopen-qt-provenance-20261011.json` were independently verified.
The QML source was unchanged in this milestone; the prior 47 UI checks remain its evidence.
BCH controller validation remains running on its original process, without a terminal result. Mining remains off.
Signing/approval integration, broadcasting/recovery lifecycle and full DEX acceptance remain unfinished; markets remain disabled.


### 2026-10-11 — persist and explicitly refresh Litecoin payment reviews in the app

Successful current-wallet preparations now retain the validated full unsigned review in `ltc-ui-review-{contextHash}.json`, associated with the already retained request context and operation identity.
Publication requires the original request to remain byte-identical. The private directory lease, owned 0600 single-link regular files, canonical bounded JSON, exclusive creation, exact retry comparison, file/directory fsync and readback reuse the request journal's fail-closed policy.
Different or partial review records are never silently replaced. These records are review context, not signing consent or authenticated protection against rollback.
Listing attaches a validated review to its matching saved request. Legacy requests without a review remain loadable; listing does not recreate a missing review or infer approval from native claims.

The payment panel loads the request without preparing, signing or sending, then exposes an explicit Refresh saved payment review action when the form still matches its saved payment/scan context.
That action calls the helper-backed reopen worker with the saved operation, payment, commitment and change address. Only a validated `wallet-reopened` result appears as the current unsigned review.
Edits, loading, failures and market changes prevent stale results from appearing as current.

Validation: full app/probe/DEX-test build passed; 15 wallet-worker groups and seven chain-worker groups passed against the pinned reopen helper; 48 QML checks passed; DEX tests passed 52 with 21 external-fixture-dependent skips.
The new tests cover process restarts, exact review retries, conflicting result refusal, invalid permissions/symlinks/corruption, changed payment bindings, lost original requests, legacy review absence, explicit UI refresh and stale-form handling.
The reopened-review screenshot was inspected. Evidence hashes in `build/litecoin-light-review-ui-provenance-20261011.json` were independently rehashed.
Rust sources were unchanged; the previous 170 Rust/helper checks remain their evidence.
BCH controller validation remains live on its original process without a terminal result. Mining was checked inactive with zero threads and zero hash rate.
Key-management/signing approval UI, broadcasting/recovery lifecycle and full DEX acceptance remain unfinished; live markets remain disabled.


### 2026-10-11 — bind explicit Litecoin signing to the encrypted vault

`ObservedClaimedSpend::approve_with_vault_on` now provides a vault-backed signing operation for an already retained/revalidated payment.
It checks the selected chain and same-tip observed inputs, exact signing commitment and retained claim bytes before attempting password-based key unlocking.
The vault then authenticates the password and binds its seed to the retained public account before any signing intent is published.
The unlocked seed stays within the call and uses the existing zeroizing ownership policy; existing limitations on dependency-internal copies and secure memory remain.
The existing exact-transaction signing path rechecks claims and durably retains intent and signed bytes before returning. Neither this API nor possession of a password obtains human consent or authorizes broadcast.

The new public-fixture policy test rejects pending spends, wrong commitments, passwords, account vaults and changed manifests without adding signing intent or signed records.
It then drops the initial seed/vault/preparation handles, reopens the vault and claimed review, explicitly signs, checks deterministic retry and verifies signed-record recovery after dropping the reopened handles.
Its injected inclusion identities test policy, not consensus-valid funding. No user wallet keys or funds were used.

Validation passed: 140 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups and seven wallet-helper groups (171 total), plus all-features/all-targets release compilation and ordered patch reconstruction.
Source, fixture and immutable binary hashes in `build/litecoin-light-vault-signing-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-vault-signing.patch`.
The helper binary hash remains identical to the previous milestone because this new library signing operation is not yet exposed by the helper.
Helper credential handling, wallet setup/backup and signing-approval UI, broadcasting/recovery lifecycle and full DEX acceptance remain unfinished. Live markets remain disabled.
BCH controller validation remains live on its original process, without a terminal result. Mining remains off.


### 2026-10-11 — explicit vault-backed Litecoin helper signing

`native-ltc-light --wallet-sign-stdio-v1 CONFIG` now accepts the original operation, bounded scan, payment and retained review, plus `signingApproval` containing `approvedCommitment` and a password supplied through standard input.
The configured wallet must explicitly name a vault directory and nonnil wallet ID. Approval must match the retained commitment; the existing reopen path authenticates the exact payment and refreshed observed inputs before vault unlocking.
Signing uses the account-bound vault method and durably retains intent/signed bytes. The response reports signed=true, watchOnly=false and broadcast=false; it returns no password, seed or raw transaction. Live-market executable remains false.
Other helper modes reject signing approval. Owned stdin and decoded-password buffers are zeroized, with a bounded 1,024-byte password and 8,192-byte signing request. Parser/dependency-internal copies and secure-memory limitations remain; no stronger erasure guarantee is claimed.

A new independent PUBLIC 32-byte-seed fixture combines the existing encrypted-vault format with independent account derivation, fixed-nonce headers and synthetic funding history.
The fixture generator preserves the previous generators' default vectors, which were compared unchanged, and performs no node/network access or nonce search during reproduction.
Two real-helper loopback TLS groups verify BIP143 ECDSA signatures independently with Python cryptography, exact retained transaction/txid, durable deterministic retries across helper processes, absent broadcast calls, and rejection of missing/mismatched approval, wrong/oversized passwords, wrong-account vaults, missing vault configuration, pending spends and missing claims.
The history remains synthetic, not consensus-valid funding; no user keys or funds were used.

Validation passed: 140 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups, seven wallet-helper groups and two signing-helper groups (173 total), plus all-features/all-targets release compilation and ordered patch reconstruction.
All 15 wallet-worker and seven chain-worker groups passed against the new pinned helper; Qt/QML source was unchanged.
Source/fixture/binary provenance in `build/litecoin-light-sign-helper-provenance-20261011.json` and worker evidence in `build/litecoin-light-sign-helper-qt-provenance-20261011.json` were independently verified.
The patch stack includes `upstream-litecoin-light-sign-helper.patch`; signing tests/fixtures/generators are retained in the GUI tools directory.
Qt credential transport and explicit signing UI, wallet setup/backup, signed-result recovery/broadcast lifecycle and full DEX acceptance remain unfinished. Markets remain disabled.
BCH controller validation remains live on its original process without a terminal result. Mining remains off.


### 2026-10-11 — explicit Litecoin signing in the Qt worker and payment panel

Private wallet configuration may now include a validated vault directory and wallet ID.
`signReviewedLitecoinPayment(commitment,password)` requires the same worker's successful `wallet-reopened` state and its exact retained signing commitment; it cannot sign directly from startup or substitute a caller-supplied payment.
The worker uses the retained operation/payment/scan/review, sends the credential only in bounded helper stdin, and verifies every returned unsigned-review field against the current review before accepting signed=true/watchOnly=false/broadcast=false.
Credentials never enter worker status or request/review files. Worker-owned UTF-8/input/output buffers are best-effort erased, but Qt/QML/QProcess may keep copies; no secure-memory guarantee is claimed.
Vault metadata is excluded from public request/receiving identity derivation, so adding a vault preserves existing saved requests, reviews and receiving-address retry identity.

The panel requires a visible refreshed unsigned review, matching saved request/form, password and explicit consent before Sign reviewed payment is enabled.
It clears the password/consent on submission, changed review/form, loading and errors. A successful result displays Signed payment saved locally. Not broadcast.
The approval checkbox uses the standard control for a visible checkmark in software rendering. Approval and signed-receipt screenshots were inspected.

The real-helper Qt test signs only PUBLIC encrypted fixture keys, verifies journal output and deterministic retries, rejects a wrong password without new signing records, and checks vault-addition identity stability.
Fake-helper boundaries test quotes, backslashes, newlines, embedded null and Unicode password transport; changed commitments/txids/fees, broadcast claims, unsigned results and extra credential fields are rejected.
The new test caught and fixed Qt 5 null truncation in the probe by using explicit-length UTF-8 conversion.
Validation passed: full GUI/probe/DEX-test builds; three signing-worker, 15 wallet-worker and seven chain-worker groups (25 total); 49 QML checks; 52 DEX tests with 21 external-fixture-dependent skips.
Sources, binaries, logs, resources and screenshots are recorded and independently rehashed in `build/litecoin-light-sign-ui-provenance-20261011.json`.
Rust/helper sources were unchanged; prior 173-check validation remains their evidence.
Wallet setup/backup UI, signed-result recovery and broadcast lifecycle, chain acceptance and full DEX integration remain unfinished. Live markets remain disabled.
BCH controller validation remains live on its original process without a terminal result. Mining remains off; no user keys or funds were used.


### 2026-10-11 — offline recovery of signed Litecoin payments

The helper now supports `wallet-signed`: it reopens the original retained operation and verifies its manifest, input claims, signing intent, exact transaction bytes and signatures without a vault, password or network endpoint. It does not sign again or broadcast. Local header/address-book configuration is still required; current unspentness, broadcast and confirmation status are not assessed.

The Qt worker binds the recovered receipt to every field of the saved unsigned review and rejects malformed or inconsistent results. The payment panel exposes an explicit Recover signed payment action for a loaded saved review. Its receipt states that broadcast and confirmation status have not been checked; it never presents recovery as proof that a transaction was not broadcast. Edited forms, loading and failures hide stale receipts.

Validation passed: 174 Rust/helper checks; all-features/all-targets release compilation; ordered patch reconstruction; full GUI/probe/DEX-test builds; four signing/recovery, 15 wallet and seven chain worker groups (26 total); 50 QML checks; and 52 DEX tests with 21 external-fixture-dependent skips. Real-helper and Qt recovery succeeded with endpoint/vault configuration removed. Tests reject changed signed bytes, intents, claims and review fields; only PUBLIC fixture keys and synthetic funding were used. The recovered-receipt screenshot was inspected.

Evidence is retained in `build/litecoin-light-signed-recovery-provenance-20261011.json` and `build/litecoin-light-signed-recovery-ui-provenance-20261011.json`; sources, fixtures, binaries and UI evidence were independently rehashed. The engine patch stack includes `upstream-litecoin-light-signed-recovery.patch`.

Wallet setup/backup UI, explicit broadcast and reconciliation, actual chain acceptance and full DEX integration remain unfinished. Live markets remain disabled. The original BCH controller validation remains running without a terminal result. Mining remains off.


### 2026-10-11 — explicit journaled Litecoin submission library

`TlsEndpoint::broadcast_retained` now accepts a retained signed payment and its expected review commitment.
It verifies the original claims, signing intent and signatures, binds the exact signed-byte hash to the configured host, port and network, and durably retains endpoint-specific submission intent before connecting.
Only a matching canonical txid response from `blockchain.transaction.broadcast` permits a durable server-acknowledgement record.
The session is consumed; cancellation, timeout, malformed replies and connection failures never trigger automatic retries or release input claims.
An explicit retry submits the same signed bytes.

`ClaimedSpend::broadcast_status` checks this endpoint's local records without network access or unlocking the vault.
Its states are NoLocalAttempt, OutcomeUnknown and ServerAcknowledged.
NoLocalAttempt does not prove no other submission occurred; ServerAcknowledged is only a historical server report, not current mempool membership, consensus acceptance or confirmation.
Corrupt/conflicting records, orphan acknowledgements and changed signed bytes fail validation.
The caller still must obtain send consent and assess current chain/fee policy.
The helper and GUI do not expose submission yet; no user transaction was sent.

Five new in-memory transport/journal tests cover exact wire bytes, explicit retries, restart recovery, endpoint isolation, invalid approval/network/intent, malformed replies, cancellation after the write, preserved claims and corrupt retained evidence.
Validation passed: 145 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups, seven wallet-helper groups and three signing-helper groups (179 total).
All-features/all-targets release compilation and ordered patch reconstruction also passed.
The submission tests use PUBLIC synthetic funding; actual Litecoin consensus acceptance remains unproven.
Qt/QML source was unchanged, so its previous validation is retained rather than counted as a new run.
Source/fixture/binary hashes were independently verified in `build/litecoin-light-broadcast-provenance-20261011.json`; logs, runner and patch evidence are hashed in `build/litecoin-light-broadcast-evidence-20261011.json`.

Protocol reference: https://electrumx.readthedocs.io/en/latest/protocol-methods.html#blockchain.transaction.broadcast
Remaining work includes helper/GUI send consent and status, lost-acknowledgement chain reconciliation, wallet setup/backup, actual chain acceptance and full DEX settlement integration.
Live markets remain disabled. BCH validation is still running in its original process without a final result. Mining remains off.


### 2026-10-11 — explicit Litecoin helper sending and actual node acceptance

The private helper now exposes `--wallet-broadcast-stdio-v1` and offline `--wallet-broadcast-status-stdio-v1`.
Sending requires the original operation/payment/retained review, scan limits and a separate `broadcastApproval` containing `approvedCommitment`, `approvedTxid`, `host` and `port`.
The helper verifies saved signatures before contacting the endpoint, refreshes the original input evidence against the selected chain, enforces the unchanged payment/fee policy, and submits only the retained signed bytes.
Signing credentials and send approval are mutually exclusive; other modes reject send approval.
The helper neither unlocks a vault nor signs during submission.
There is no automatic retry. A send error may follow successful transmission and requires status/reconciliation.

A durable commitment-specific submission marker precedes endpoint intent and prevents signing again after an attempted send, including partial intent publication.
This avoids a later signing retry returning the old not-broadcast receipt.
Offline status requires the configured endpoint identity but makes no connection and needs no vault.
It verifies saved records and reports no-local-attempt, outcome-unknown or server-acknowledged, with the exact endpoint and confirmationStatus=unassessed.
The first state is not evidence that no other endpoint received the transaction; an acknowledgement remains a historical server report.
Corrupt/missing submission evidence is rejected rather than repaired into a success claim.

Validation passed: 146 ordinary light-wallet tests, six TLS tests, eleven RPC tests, seven chain-helper groups, seven wallet-helper groups and seven signing/recovery/submission-helper groups (184 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
Four signing/recovery, 15 wallet and seven chain Qt worker groups (26 total) passed against the new pinned helper.
Qt/QML source and its previous build artifacts were independently checked unchanged; no new UI test run is claimed.

`test_litecoin_light_chain.py` additionally ran two complete PUBLIC-wallet scenarios against isolated Litecoin Core 0.21.5.8 regtest nodes, with peer connections disabled and MWEB activation deferred.
Each generated a real 441-block header branch and confirmed funding, synchronized and validated it through the helper's TLS path, prepared and signed a payment, and submitted those exact bytes to the actual node.
The node accepted the payment into its mempool and included it in a block.
One scenario returned the matching acknowledgement; the other deliberately closed TLS after node acceptance.
After restarting the helper, the latter correctly remained outcome-unknown even though independent node RPC verified confirmation.
Both helper receipts kept confirmationStatus=unassessed; this is actual transparent-chain transaction acceptance evidence, not yet helper-driven confirmation reconciliation or full DEX settlement acceptance.
Both disposable nodes were stopped and removed, and no user wallet, public-network funds or XMZ mining was used.

Evidence: `build/litecoin-light-broadcast-helper-provenance-20261011.json`, `build/litecoin-light-broadcast-helper-evidence-20261011.json`, and `build/litecoin-light-broadcast-helper-chain-{ack,lost-ack}-20261011.json`.
Source, fixture, binary and actual-chain evidence hashes were independently verified.
Reproduce the node fixture with `python3 tools/dex-engine-port/test_litecoin_light_chain.py HELPER TLS_FIXTURE LITECOIND REPORT`, adding `--drop-ack` for the lost-reply case.
The ordered engine patch stack now includes `upstream-litecoin-light-broadcast-helper.patch`.

GUI send consent/status controls, chain-based lost-acknowledgement reconciliation, wallet setup/backup and full DEX settlement integration remain unfinished.
Live markets remain disabled. The original BCH controller process remains live without a terminal result. Mining remains off.


### 2026-10-11 — explicit Litecoin sending in Qt and the payment panel

The Qt worker exposes offline saved-send-status checks and a separate send operation.
A send requires the same worker's validated status, the exact saved transaction ID and review commitment, and the configured endpoint displayed in that status.
The worker retains the original payment and scan policy rather than accepting replacement payment fields at submission.
It strictly validates the complete returned payment, endpoint, result state and unassessed confirmation status.
Malformed replies, wrong identities/endpoints, failures and cancellation clear the visible result; they never trigger a send retry.
Acknowledged transactions cannot be sent again through this worker state. Unknown outcomes require another explicit status check and consent.

The panel adds Check saved send status and Send signed payment with a separate unchecked consent box.
Its review shows recipient, amount, fee, change/address, input count, transaction ID, network and server.
Consent clears when the displayed context changes, while loading and on submission.
Loading or checking a saved request cannot send it.
The display distinguishes no recorded attempt at this endpoint, unknown send outcome and historical server acknowledgement; all keep confirmation unassessed.
A failed asynchronous send prominently directs the user to check saved status because transmission may have occurred.
The unsigned review now explains the sign-then-check-send-status flow. Approval and acknowledgement screenshots were inspected.

Validation passed: full app/probe/DEX-test builds; eight signing/recovery/send worker groups, 15 wallet-worker groups and seven chain-worker groups (30 total); 52 QML checks; and 52 DEX tests with 21 external-fixture-dependent skips.
The old QML assertion that sending was unavailable was updated for the new flow.
The actual-chain fixture gained `--qt-probe PROBE`, exercising the new worker through the pinned helper and TLS bridge into isolated Litecoin Core 0.21.5.8 regtest.
Both matching-acknowledgement and deliberately lost-reply scenarios passed: the node accepted and confirmed the exact signed payment, while restarted worker recovery reported server-acknowledged or outcome-unknown respectively.
The initial fixture attempts were rejected because their JSON configuration was not canonical; the fixture now uses the app's existing compact, sorted format. No production validation was relaxed.
The successful evidence is `build/litecoin-light-broadcast-ui-chain-{ack,lost-ack}-v2-20261011.json`.
Both disposable nodes stopped cleanly. These checks use PUBLIC fixture funds only.

`build/litecoin-light-broadcast-ui-provenance-20261011.json` records independently rehashed source, binary, test, resource and screenshot evidence.
Rust/helper sources and fixtures remain identical to the previous 184-check validation.
Chain-based confirmation/lost-reply reconciliation, wallet setup/backup and full DEX settlement integration remain unfinished. Live markets remain disabled.
BCH controller validation continues in its original live process without a terminal result. Mining remains off.


### 2026-10-11 — Litecoin payment inclusion and reorganization reconciliation

`ClaimedSpend::observe_payment_on` verifies retained signatures and queries only the original payment script's bounded history.
It fetches and compares the exact signed bytes for a matching report, then validates a Merkle branch against the selected Scrypt-validated header when the server reports a block position.
The result distinguishes NotReported, ReportedPending and Included, retaining selected height/tip and inclusion depth.
An observation cannot be reused through its typed accessor after the selected chain changes.
A missing report is not proof of rejection, pending membership is only a peer report, and header inclusion does not claim full transaction/UTXO consensus validation or global-chain freshness.
No observation signs, sends or releases input claims.

The helper adds `--wallet-reconcile-stdio-v1`, requiring the original payment/review and operation, explicit maximumDownloadHeaders (1..10,000,000) and maximumHistoryEntries (1..1,000).
This explicit chain-check operation first synchronizes the header cache and requires the reported tip to match the selected tip, then observes the retained payment without a vault.
It rejects signing/send approval and wallet-scan fields.
The receipt carries separate local broadcast status and confirmationStatus=not-reported, reported-pending or included.
`paymentObservation` records selectedHeight, selectedTip, height/blockHash (null when not included), confirmations and observedAtUnix.
The helper does not persist a proof as permanent confirmation; each chain check recomputes it, while offline send-status inspection remains confirmation-unassessed.

Validation passed: 150 ordinary light-wallet tests, six explicit TLS tests, eleven RPC tests, seven chain-helper groups, seven wallet-helper groups and nine signing/recovery/send/reconciliation groups (190 total).
All-features/all-targets release compilation and ordered patch reconstruction passed.
All 30 existing Qt worker groups passed against the new pinned helper. Qt/QML code and build artifacts were checked unchanged.
Four new transport tests cover exact witness bytes, inclusion/height/branch verification, pending/absent reports and invalidation of observations after branch changes.
Helper tests verify explicit budgets, forbidden approvals, no-vault observation, unchanged payment records and rejection of altered bytes or impossible heights.

Two actual Litecoin Core 0.21.5.8 regtest scenarios, with matching or deliberately lost send acknowledgements, passed the sequence not-reported at selected height 441, reported-pending at 441, included with depth 1 at 442, and not-reported after a longer replacement branch reached 443 without the payment.
The stale Merkle proof from the removed block was rejected on the replacement chain.
The fixture stopped its own node before removing its disposable mempool file to construct that branch; original payment/claim records remained unchanged throughout reconciliation.
Both fixture nodes stopped cleanly. No user funds or XMZ mining were involved.
Successful reports are `build/litecoin-light-reconcile-chain-ack-20261011.json` and `build/litecoin-light-reconcile-chain-lost-ack-v2-20261011.json`.
Run the existing actual-node fixture with `--reconcile --reorg`, optionally adding `--drop-ack`.

Sources, fixtures and binaries were independently rehashed in `build/litecoin-light-reconcile-provenance-20261011.json`; additional evidence is in `build/litecoin-light-reconcile-evidence-20261011.json`.
The new patch is named `upstream-litecoin-light-payment-reconcile.patch` to distinguish it from earlier candidate reconciliation.
A patch-name collision during auditing was corrected by reconstructing the earlier candidate patch from its hash-verified 2026-10-10 source snapshot; the full ordered stack again reproduces every indexed engine source.

GUI presentation of dated chain observations, wallet setup/backup and full DEX settlement integration remain unfinished. Live markets remain disabled.
BCH controller validation remains in its original live process without a terminal result. Mining remains off.


### 2026-10-11 — Litecoin payment chain observations in Qt and QML

The private light-wallet payment panel now offers an explicit **Check payment on chain** action for a loaded saved review.
It reuses the original operation, payment and full saved review, with bounded header/history requests.
Loading a request or checking offline send status does not initiate a chain query.
No vault password, signing approval or send approval is supplied.

The Qt worker validates the response schema, exact saved payment, configured endpoint, selected height/tip, block position and confirmation-depth arithmetic.
The observation time must be integral and within the current request interval (one second of response clock tolerance).
Included-at-tip results must match the selected tip hash; pending/absent results require null block position and zero confirmations.
Malformed, stale, mismatched and cancelled results clear the wallet and chain display.
A chain check does not enable signing or sending.

The panel shows UTC check time, selected tip/height, transaction ID, server and either verified selected-header inclusion/depth, an untrusted pending report or no report.
Local send acknowledgement remains separate.
A new check hides the previous result; a changed branch replaces it.
The display states that a missing report does not establish failed/nonexistent sending, and that the server view may be incomplete or out of date.
This is header/Merkle inclusion, not full UTXO validation or global-chain freshness.

Validation: application, Qt probe and DEX test targets built successfully.
All 31 Qt worker groups passed (9 signing/recovery/send/reconciliation, 15 wallet and 7 chain), including strict receipt mutations and cancellation.
QML: 53 passed. DEX: 52 passed, 21 external-fixture skips, zero failures.
The new QML test checks explicit invocation, original context, UTC time, changed payment context, pending/absent states, loading/failure clearing and disabled sign/send actions.
The captured panel was visually inspected; the offscreen software capture has pixelated text but no observed clipping.

Both actual Litecoin Core 0.21.5.8 scenarios now exercise reconciliation through Qt: matching and lost send acknowledgement.
Each passed unreported (441), pending (441), included with depth one (442), then unreported on a longer replacement branch (443).
Qt rejected the removed block's stale proof and cleared both result objects; saved payment records remained unchanged.
Both owned fixture nodes stopped. No user wallet or public funds were used.
Run test_litecoin_light_chain.py with --qt-probe PROBE --reconcile --reorg, optionally --drop-ack.
Reports: build/litecoin-light-reconcile-ui-node-ack-20261011.json and build/litecoin-light-reconcile-ui-node-lost-ack-20261011.json.
Source, binary and result hashes: build/litecoin-light-reconcile-ui-provenance-20261011.json.
The existing 190-check Rust/helper evidence remains applicable to unchanged Rust sources, independently rehashed here.

Wallet setup/backup UX and full light-wallet DEX settlement integration remain unfinished; live markets remain disabled.
The original BCH controller validation remains running without a terminal result.
Mining remains off. The multicoin integration goal remains active.


### 2026-10-11 — Offline Litecoin encrypted backup helper

The helper now accepts explicit wallet-backup-save and wallet-backup-verify operations.
Each request uses its matching monzero-ltc-light-<mode>-request-v1 schema, a nonnil requestId,
and backupApproval containing an absolute canonical private directory and password.
Passwords use the existing bounded, zeroizing stdin transport; request size is limited to 8192 bytes.
Signing, broadcast, payment, quote, retained-review, scan and header/history-budget fields are forbidden.
Other helper modes reject backupApproval.
No network connection or header-cache access is required.

Save opens the configured address book and vault, authenticates their account binding, exports the existing encrypted seed/allocation bundle,
then publishes litecoin-wallet.backup through private durable storage with authenticated read-back.
A new export never overwrites different existing ciphertext; use a new directory for a new backup generation.
If output is lost after saving, explicitly verify the existing destination.
Verify authenticates the saved bundle against configured network, wallet identity and public account without requiring live vault/address-book files.
Neither operation restores a wallet, signs, sends or enables trading.

Responses contain schema, requestId, executable=false and wallet.
The wallet object has watchOnly=true, spendable=false, backupVerified=true, backupSaved, backupDigest,
walletId, receivingAllocated, changeAllocated, scope=seed-and-address-allocations,
allocationFreshness=unassessed and activeSwapRecovery=false.
No seed, password, account xpub or bundle bytes appear in stdout.
Allocation counts describe the saved snapshot; verification does not establish freshness, an independent/offline copy,
or recovery of payment claims, signed transactions or active swap state.

Validation passed: 150 ordinary light-wallet checks, six explicit TLS checks, eleven RPC checks,
seven chain-helper groups, seven wallet-helper groups, nine signing/recovery/send/reconciliation groups and three backup-helper groups (193 total).
All-feature/all-target release compilation and ordered patch reconstruction passed.
All 31 existing Qt groups passed with the new helper; Qt/QML source and build artifacts are unchanged from the prior milestone.
PUBLIC fixture tests independently decrypt the saved bundle using Python Scrypt/libsodium and compare its encrypted vault and address records.
They also verify missing live-wallet/cache independence, old allocation counts, password/identity/account/corruption rejection,
required explicit approval, private/canonical destinations and preservation of existing backups and wallet records.
An initial negative test accidentally reused the fixture wallet ID; the corrected mismatch test and final validation passed.
The initial failed status is retained as historical evidence, not reported as a successful run.

Helper SHA-256: c6fdd5e09185fdd04832b90389cc4559a77018c32d9066a2e05ab14ddaead5a6.
Final provenance: build/litecoin-light-backup-helper-provenance-v2-20261011.json.
Final status: build/litecoin-light-backup-helper-final-status-20261011.json.
Additional evidence: build/litecoin-light-backup-helper-evidence-20261011.json.
Reproducible helper delta: upstream-litecoin-light-backup-helper.patch, appended to the ordered verifier.

Backup controls, wallet creation/restoration UI and full light-wallet DEX settlement integration remain unfinished.
Live markets remain disabled. The original BCH controller test process is still running without a terminal result.
Mining remains off and the multicoin integration goal remains active.


### 2026-10-11 — Litecoin backup controls in Qt and QML

The private light-wallet panel now exposes separate Save encrypted backup and Verify existing backup controls.
An existing absolute private folder and wallet/backup password are required.
The panel describes the fixed filename, refusal to overwrite a different backup generation,
seed/allocation scope and exclusion of payment records and active swap recovery.

LitecoinLightJob::backupWallet validates the destination and password bounds, then uses the existing escaped stdin credential path.
The worker requires the exact four-field backup response, configured wallet identity, valid digest, bounded allocation counts,
correct save/verify mode and explicit limited-scope/unassessed-freshness flags.
Only a successful result exposes the locally retained destination. Failure/cancellation clears wallet, chain and destination receipts.
Backup checking does not enable signing or sending and does not require an endpoint.
Worker-owned credential buffers are erased best-effort; Qt/QML/QProcess copies are not a secure-memory guarantee.

Passwords are masked and cleared on submission, directory changes, worker-status changes and market/visibility changes.
The receipt is bound to the submitted/current destination and reports wallet identity, digest and saved allocation counts.
Busy/failed operations hide prior receipts; uncertain outcomes instruct the user to verify the destination before another save.
The panel explicitly avoids claiming current allocation completeness or an independent offline copy.

Validation passed: app, Qt probe and DEX targets built; 34 Qt groups passed (three backup plus the existing 31).
The new worker tests exercise real no-endpoint saving, verification after removing the live fixture vault, overwrite refusal,
wrong-password failure, exact receipt identity/scope/counts/mode, escaped stdin passwords, cancellation and destination preflight.
QML: 54 passed with zero warnings in the final run.
DEX: 52 passed, 21 external-fixture skips, zero failures.
The new QML case verifies explicit actions, password clearing, destination changes, busy/error handling and receipt wording.
The backup panel screenshot was inspected; software-capture text is pixelated but labels, controls and receipt are readable without observed clipping.

The run also exposed an existing payment-observation date warning during object clearing.
The display now checks for a finite in-range timestamp before formatting it; the final QML run has no date warning.
No Rust/helper changes were needed: the prior 193-check helper sources, fixtures and binaries were independently rehashed unchanged.
Evidence and current file hashes are in build/litecoin-light-backup-ui-provenance-20261011.json.

Wallet creation/restoration UI, complete recovery coverage for active trading state and full light-wallet DEX settlement integration remain unfinished.
Live markets remain disabled. BCH controller validation remains in its original running process with no terminal result.
Mining remains off; the multicoin goal remains active.


### 2026-10-11 — Explicit Litecoin restore helper and restored-key chain acceptance

The helper now accepts --wallet-restore-stdio-v1 with schema monzero-ltc-light-wallet-restore-request-v1,
a nonnil requestId and restoreApproval {directory, password, approvedDigest}.
The digest must be canonical lowercase SHA-256 and match the authenticated encrypted backup.
Backup source, network, wallet identity and configured public account are validated before destination publication.
Other approvals, payment/scan requests and header/history budgets are forbidden.
Other helper modes reject restoreApproval; passwords retain bounded zeroizing stdin handling.

Restore uses the existing two-directory preflight and durable publication implementation.
It does not overwrite conflicting ciphertext or allocation records; an identical retry can complete a vault-only partial publication.
The helper authenticates the restored vault against the configured account and rechecks restored allocation counts before returning success.
It requires existing private configured vault/address-book directories but no network connection or header cache.
No account discovery, configuration creation, automatic activation, signing, sending or active-trade restoration is implied.

The four-field response contains schema, requestId, executable=false and wallet.
Wallet fields are watchOnly=true, spendable=false, restored=true, backupDigest, walletId,
receivingAllocated, changeAllocated, scope=seed-and-address-allocations,
allocationFreshness=unassessed and activeSwapRecovery=false.
A valid old backup cannot establish allocations made after export; it cannot replace a conflicting newer local address record.

Validation passed: 196 Rust/helper checks (150 ordinary, 6 TLS, 11 RPC, 7 chain-helper, 7 wallet-helper,
9 signing/recovery/send/reconciliation, 3 backup and 3 restore groups).
All-feature/all-target release compilation and ordered patch reconstruction passed.
All 34 existing Qt worker groups passed against the new helper. Qt/QML sources and binaries remain unchanged.
Restore fixtures cover exact byte recovery, idempotent retries, continued non-reused address allocation,
vault-only partial publication, conflict preservation, digest/password/account mismatches and forbidden request fields.

The actual-node fixture now supports --restore-before-sign.
Both matching-acknowledgement and lost-acknowledgement scenarios passed using an isolated Litecoin Core 0.21.5.8 node.
After preparing a payment, the fixture saves/verifies a backup, restores it into new directories,
compares the exact vault/allocation bytes, then removes its original fixture key and allocation files.
The restored keys sign the prepared payment; the actual node accepts it into the mempool and includes it in a block.
Qt send/observation then passes unreported at 441, pending at 441, included at 442, and unreported after replacement branch 443;
the removed block's stale proof is rejected.
Original spend/claim records are deliberately retained and byte-checked: this does not prove recovery of lost active-swap state.
Both owned nodes stopped. No public funds or user wallet keys were used.

Helper SHA-256: 5ffed36c3d14ebeccc54486764f2a6b22129dd8136773db6b9384d794cd1f6e1.
Provenance: build/litecoin-light-restore-helper-provenance-20261011.json.
Node reports: build/litecoin-light-restore-helper-node-ack-20261011.json and build/litecoin-light-restore-helper-node-lost-ack-20261011.json.
Additional evidence: build/litecoin-light-restore-helper-evidence-20261011.json.
The ordered patch stack now includes upstream-litecoin-light-restore-helper.patch.

Restore approval UI, wallet creation/configuration UX, complete active-state recovery and full light-wallet DEX settlement remain unfinished.
Live markets remain disabled; the original BCH controller process is still running without a terminal result.
Mining remains off and the multicoin goal remains active.


### 2026-10-11 — Litecoin restore approval in Qt and QML

The private light-wallet backup panel now exposes Restore verified backup only after an explicit successful verification.
The worker requires that verification in the same session, the exact verified backup digest and source directory,
and a newly supplied bounded password over the existing escaped stdin credential path.
Configured vault and address-record destination folders are displayed before restoration.
The user must separately check the restore consent box; changing status, backup context, destinations or market clears consent.
Passwords are cleared on submission and existing status/context changes.

LitecoinLightJob::restoreWallet refuses direct unverified invocation or approval for a different digest/directory.
Its response validator binds the entire restored receipt to the prior verified backup, including wallet identity,
allocation counts, digest and limited recovery scope.
Successful restoration does not enable another restore, signing or sending.
Failure/cancellation clears receipt data; the UI asks the user to preserve any recovered records, verify the same backup again
and explicitly retry without resetting destination folders.
The success receipt describes restored seed/address records and explicitly leaves payment and active swap recovery incomplete.

Validation passed: application, Qt probe and DEX targets built.
All 37 Qt groups passed: three restore, three backup, nine signing/recovery/send/reconciliation, fifteen wallet and seven chain.
New tests cover real offline restoration into fresh directories, exact retries, conflict preservation,
same-worker verification/approval, full-receipt mismatches, password transport and cancellation.
QML: 55 passed with no warnings. DEX: 52 passed, 21 external-fixture skips, no failures.
The QML restore test checks explicit consent, both displayed destinations, changed-destination invalidation,
password clearing, unknown outcomes, immediate refusal and successful limited-scope presentation.
The approval panel screenshot was inspected and shows the masked password, verified digest, destination folders and consent without observed clipping.

The isolated actual-node fixture now accepts --qt-restore with --qt-probe and --restore-before-sign.
Both acknowledgement and lost-acknowledgement scenarios passed using Qt verification/restoration,
then helper signing with restored keys and Qt sending/reconciliation.
Original fixture key/allocation files were removed before signing; retained payment/claim records were deliberately preserved.
The real node accepted and confirmed the payment; Qt removed inclusion after the replacement branch and rejected the stale proof.
Both owned Litecoin nodes stopped. This is not proof of recovering lost active-swap records.

Node reports: build/litecoin-light-restore-ui-node-ack-20261011.json and build/litecoin-light-restore-ui-node-lost-ack-20261011.json.
Current source/binary/test hashes: build/litecoin-light-restore-ui-provenance-20261011.json.
The unchanged 196-check Rust/helper sources, fixtures and binaries were independently rehashed.

New-wallet creation/configuration UX, complete active-state recovery and full light-wallet DEX settlement integration remain unfinished.
Live markets remain disabled. The original BCH controller process remains running without a terminal result.
Mining stays off; the multicoin goal remains active.


### 2026-10-11 — Durable new Litecoin wallet setup library

vault::setup::initialize now provides the storage foundation for explicit new-wallet creation.
It accepts a setup directory, distinct vault/address/backup directories, selected network, wallet UUID,
hardened account index and bounded password. All directories must already be canonical, private and owned.

Before publishing a vault, setup durably retains litecoin-wallet-setup.json containing fixed context and an encrypted generated seed.
No plaintext seed or password is serialized. The intent is an additional encrypted seed copy; password changes cannot revoke it or older backups.
A retry authenticates and reuses this exact encrypted seed. Changed identity, network, index or paths are rejected.
Fresh setup refuses to adopt existing vault, address or backup records.
The setup directory holds its process lease through the operation.

Setup publishes the encrypted vault and identical empty address book, then saves and authenticates the initial encrypted backup.
If that backup was already published before an interruption, it authenticates and reuses the existing backup rather than generating a replacement.
Account identity and empty allocation counts must match. Only then is litecoin-wallet-ready.json retained and a CreatedWallet returned,
containing public account and authenticated backup information.
Errors may leave partial records; identical retries complete publication without replacing different records or keys.
Repeating setup after later address allocations fails without resetting those allocations.
This API does not activate the wallet, connect to peers, sign, send or enable trading, and it does not claim an independent/offline backup copy.

Validation passed: 199 Rust/helper checks (153 ordinary light-wallet, 6 explicit TLS, 11 RPC,
7 chain-helper, 7 wallet-helper, 9 signing/recovery/send/reconciliation, 3 backup and 3 restore groups).
All-feature/all-target release compilation and ordered patch reconstruction passed.
All 37 existing Qt worker groups passed against the rebuilt helper; Qt/QML source and build artifacts remain unchanged.

Three new library tests cover interruption after each of intent, vault, address and backup publication.
Each resumed setup keeps the original encrypted seed, maintains a stable completion record and backup digest,
and its backup restores to the same account and first receiving derivation path.
Tests also cover wrong credentials, changed network/identity/index/destinations, corrupted intent,
existing-wallet refusal, private/distinct-directory preflight and preservation of newer allocations.

Machine SHA-256: 70c0432072f2018360f008c6e17fb8b708a1f8323d9eb14134d42124d46301e5.
Rebuilt helper SHA-256: c38eae0f9e14bed44f5d0a4284e24f5ae7da852f795cc77ad6da4c3ba2eab280.
Provenance: build/litecoin-light-setup-provenance-20261011.json.
Additional evidence: build/litecoin-light-setup-evidence-20261011.json.
The ordered patch stack now includes upstream-litecoin-light-setup.patch.

Creation-helper/configuration publication and app setup controls remain unfinished; this milestone adds library behavior only.
Complete active-state recovery and full light-wallet DEX settlement integration also remain unfinished.
Live markets remain disabled. The original BCH controller process is still running without a terminal result.
Mining remains off and the multicoin goal remains active.


### 2026-10-11 — Litecoin creation helper and private configuration publication

The helper now accepts --wallet-create-stdio-v1 against a separately approved private setup configuration.
The setup schema is monzero-private-ltc-setup-v1 with network, setupDirectory, vaultDirectory,
addressBookDirectory, backupDirectory, spendDirectory, cacheDirectory, walletId, accountIndex,
maximumHeight and optional endpoint {host,port}.
The existing private config-file ownership, size, no-symlink and lease checks are reused.
All six storage directories must be existing, canonical, private and distinct.
Endpoint validation is offline; creation does not connect to the configured server.

The stdin request uses monzero-ltc-light-wallet-create-request-v1, a nonnil requestId and
createApproval {approvedConfigDigest,password}. The canonical SHA-256 must match the exact setup configuration bytes.
Other signing/send/backup/restore approvals, payments, quotes, scans and chain/history budgets are rejected.
Passwords use bounded zeroizing input handling and are absent from configuration and response output.

setup::configuration::initialize_configured durably binds the complete setup/storage/chain options in litecoin-wallet-settings.json.
It refuses changed options and does not adopt a standalone prior setup under new settings.
It then uses the retained encrypted-seed setup workflow, authenticates the initial backup and publishes litecoin-wallet.json
with the standard monzero-private-ltc-light-v1 schema.
Publication is private, durable, read back and non-overwriting; an identical retry can complete a missing configuration.
The public account descriptor is retained in that private configuration, not printed in the helper receipt.
Conflicting configurations and later address allocations are preserved, not reset.
Errors may leave durable partial setup records; they must be preserved.

The four-field response contains schema, requestId, executable=false and wallet.
Wallet reports created=true, backupVerified=true, configurationFile/configurationDigest, backupDigest,
walletId, zero initial receiving/change allocations, scope=seed-and-address-allocations,
allocationFreshness=unassessed, activeSwapRecovery=false, watchOnly=true and spendable=false.
This is setup/configuration evidence, not activation or trading authorization.

Validation passed: 202 Rust/helper checks (153 ordinary light-wallet, 6 TLS, 11 RPC, 7 chain-helper,
7 wallet-helper, 9 signing/recovery/send/reconciliation, 3 backup, 3 restore and 3 creation groups).
All-feature/all-target release compilation and ordered patch reconstruction passed.
All 37 existing Qt worker groups passed with the new helper; Qt/QML sources and binaries were checked unchanged.
Creation tests exercise the generated configuration with receiving/backup verification, exact key/configuration retries,
missing-configuration completion, conflicting files/settings, wrong passwords and pre-key rejection of invalid approvals/settings.

The actual isolated-node fixture now supports --create-wallet.
Both matching-acknowledgement and lost-acknowledgement scenarios passed using newly generated disposable regtest wallets.
The fixture independently decrypts its own generated test vault with Python Scrypt/libsodium and derives the public account/address;
neither the random seed nor account xpub is put in the result report.
The created configuration is preserved; a separate runtime copy selects the loopback TLS peer.
The node funds the new receiving address, then the fixture backs up and restores through Qt into new directories,
removes its original test key/address files, signs with restored keys and submits through Qt.
The real node accepts/confirms the payment. Qt observes pending/included states, removes inclusion on the replacement branch
and rejects the removed block's stale proof. Payment/claim records are deliberately preserved throughout.
Both owned nodes stopped; no user wallet or public funds were involved.

Helper SHA-256: 239a61c05d776a505601ea86a52fa41726f408103b2b61c36b4db776c193373f.
Provenance: build/litecoin-light-create-helper-provenance-20261011.json.
Node reports: build/litecoin-light-create-helper-node-ack-20261011.json and build/litecoin-light-create-helper-node-lost-ack-20261011.json.
Additional evidence: build/litecoin-light-create-helper-evidence-20261011.json.
The ordered stack now includes upstream-litecoin-light-create-helper.patch.

App creation/configuration controls, complete active-trading-state recovery and full light-wallet DEX settlement integration remain unfinished.
Live markets remain disabled. The original BCH controller test is still running without a terminal result.
Mining stays off and the multicoin goal remains active.


### 2026-10-11 — Litecoin wallet creation and explicit opening in Qt

The private Litecoin setup profile now has app controls to review the network, wallet identity,
account, storage folders and optional TLS endpoint; confirm a password; and explicitly create or
resume the wallet. The helper verifies its initial encrypted seed/address backup before reporting
creation. The app validates the generated configuration, retained setup settings and completion
receipt against the reviewed setup. Creation does not automatically open the wallet or contact a peer.

A separate Find saved wallet action reviews an existing generated configuration without creating keys
or claiming that backup contents were rechecked. Open wallet requires the displayed configuration
digest. A fresh app session can reopen that same wallet and reuse its receiving address. The original
setup, launcher and generated configuration remain checked for changes. Secrets and creation consent
clear on submission and context changes. Conflicts or interrupted creation preserve existing records.

Validation: app/probe builds passed; 41 Qt wallet test groups and 56 QML tests passed;
the DEX suite passed 52 tests with 21 external-fixture-dependent skips. Both actual isolated Litecoin
regtest runs passed with creation/opening through Qt, funding the new address, backup/restore through
Qt, signing with restored keys, submission, confirmation, replacement-branch reconciliation and stale
proof rejection. One run deliberately dropped the broadcast acknowledgement after node acceptance.
Both owned test nodes stopped. All 300 pinned Rust source hashes and both node-report evidence maps
were rechecked unchanged; the prior 202 Rust/helper checks remain the applicable helper evidence.

Evidence: build/litecoin-light-create-ui-provenance-20261011.json and
build/litecoin-light-create-ui-node-{ack,lost-ack}-20261011.json.
The creation-panel screenshot is an offscreen software-rendered test artifact, not a full desktop
layout acceptance result.

Profile provisioning still requires a private setup configuration; a complete public onboarding wizard,
active-trading-state backup/recovery and full light-wallet DEX settlement remain unfinished.
Live markets remain disabled. The original BCH controller validation is still running, not passed.
Mining was verified inactive with zero threads. The multicoin development goal remains active.


### 2026-10-11 — Dogecoin confirmed-unspent actual-node acceptance

Rebuilt the swap-core test binary from the current pinned sources and reused the hash-verified
swap-machine observer binary. Three Dogecoin primitive tests and two observer tests passed.
The explicit actual-node fixtures then passed twice against isolated Dogecoin Core 1.14.9:
once with the engine refund signer and once with an independent Python fixture signer.
Both used the production Dogecoin RPC observer.

The observer accepted the exact confirmed funding output, rejected it after a pending refund
spent it, and continued to reject it after that refund confirmed. Refund output observation
survived node restart. Invalidating its block revoked confirmation/unspent evidence; mining the
same saved refund bytes on a replacement branch restored valid observation and left the old
block's proof rejected. Premature refund and mismatched transaction locktime were rejected;
the confirmed recipient received 9.99 DOGE with a 0.01 DOGE fee. Both owned nodes stopped.

Reports: build/dogecoin-unspent-20261011-{engine-node,independent-node}.json.
Sources/binaries: build/dogecoin-unspent-20261011-provenance.json.
Runner and terminal status: build/run-dogecoin-unspent-20261011.py and
build/dogecoin-unspent-20261011-status.json. Source and artifact hashes were checked unchanged.
The standard unit runs intentionally skip the node adapters; those adapters were invoked by
the explicit node fixtures. This proves primitives and observation, not a DOGE/XMZ swap.
The legacy funding-signature/transaction-identity problem still requires a complete two-party
protocol solution before dependent spends or live trading can be enabled.


### 2026-10-11 — isolated BCHN consensus lab prepared

Downloaded the official BCHN v29.0.0 Linux release matching reviewed commit
89a591f7c5b1fd110c0819377ad8f2647d656800. The archive SHA-256 was checked against
GitHub release-asset metadata before extraction. This is an isolated compatibility fixture,
not a recommendation to deploy that version on a public network.

The executable started on disposable regtest storage with peer discovery/listening disabled,
reported the expected regtest genesis and zero network peers, then shut down cleanly.
No swap, covenant acceptance or mainnet synchronization was attempted. The next step is
submitting production-generated BCH covenant redemption and timeout transactions to this node,
including maturity and reorganisation rejection. Existing BCH mock tests do not prove that result.

Executable: build/bchn-29.0.0-lab/bitcoin-cash-node-29.0.0/bin/bitcoind.
Startup and binary/archive hashes: build/bchn-isolated-startup-20261011.json.
Preparation script: build/prepare-bchn-lab-20261011.py.
Archive SHA-256: 6125d1cbecc1db476f2b6b7b91da5acde92d2311b8e738124e3db64ca84b33e1.
Release: https://github.com/bitcoin-cash-node/bitcoin-cash-node/releases/tag/v29.0.0.
The ongoing BCH controller validation and continuous LTC/XMZ settlement run remain separate
unfinished checks. Mining remains off.


### 2026-10-11 — actual BCHN covenant acceptance and continuous LTC success

Added bch_consensus_probe.rs, an owned-test adapter linking the production swap-core covenant
and spend implementations. It constructs the pinned public reference covenant and generates
redemption/timeout bytes against actual funded parent transactions. It has no wallet secrets,
network calls or live execution authority. test_bch_consensus.py creates an isolated BCHN
regtest wallet, funds P2SH32 outputs and submits those exact generated spends.

Both branches passed actual BCHN v29.0.0 consensus/mempool acceptance. Premature CSV timeout
was rejected. Altered output amounts and destinations were rejected for both branches.
Each valid spend paid exactly 99,000 atoms from a 100,000-atom lock (1,000-atom fee), survived
node restart, lost confirmation on block invalidation and reconfirmed the exact retained bytes
on a replacement branch. The fixture explicitly reloads its wallet after restart and advances
the mock timestamp to avoid recreating the exact invalidated regtest block. All owned BCHN
processes stopped. All 17 existing BCH core tests also passed.

Applicable current result: build/bch-consensus-node-v5-20261011.json.
Probe/library provenance: build/bch-consensus-probe-provenance-20261011.json.
Build artifacts/runner: build/bch-consensus-probe-artifacts-20261011.jsonl and
build/build-bch-consensus-probe-20261011.py. Core log: build/bch-consensus-core-tests-20261011.log.
Initial runs found fixture wallet routing/reloading and deterministic replacement-block issues;
those failure logs are retained. The earlier v4 result predates the strengthened negative cases;
v5 is the result bound to the current probe and Python source. No production Rust source changed.
This validates the reference covenant primitives, not authenticated two-party execution, two-stage
recovery completion, adaptor-secret release policy or full BCH/XMZ settlement.

Separately, build/litecoin-resident-settlement-v2-20261011-success.json now records a completed
continuous-app success path through final native payout. The same engines handled approved
startup, private negotiation/recovery exchange, reservation, funding, redemption, recovered-wallet
restoration/reopening, payment preparation/submission and confirmation/reorganisation checks.
The pinned source/binary snapshot was rechecked unchanged. Its recoveryPathsVerified and
executable fields remain false. The paired cancellation/refund run is still live, as is the
original long BCH controller validation. Mining remains off and no market is activated.


### 2026-10-11 — BCH two-stage recovery on an actual node

Added bch_recovery_consensus_probe.rs and test_bch_recovery_consensus.py. The adapter
uses the production RecoveryPlan and covenant spend methods with explicit public fixture
scalars and disposable node-wallet payout/refund destinations. Three independently funded
paths passed on isolated BCHN v29.0.0: immediate success, cancellation then signed refund,
and cancellation then mature punishment when the refund is withheld.

Success pays 99,000 atoms from a 100,000-atom lock. Cancellation moves 99,000 atoms into
the production recovery covenant; refund and punishment pay 97,000 atoms after the second
2,000-atom fee. Node wallet accounting confirms the exact selected recipient amount and zero
for the other recipient. The original lock is deliberately older than both configured delays
before cancellation; punishment is nevertheless rejected until its independent five-block
cancellation-relative delay matures. Premature two-block cancellation is also rejected.

Every final path survives a node restart, loses confirmation when its block is invalidated,
and reconfirms identical bytes on a replacement branch. Success/refund signatures extracted
from the node-observed transaction bytes recover the expected fixture adaptor scalar through
the production verification/recovery methods. No private scalar is included in the report.
All owned BCHN processes stopped. Current adapter, dependency and node-test evidence hashes
were verified. Production Rust sources were unchanged.

Report: build/bch-recovery-consensus-node-20261011.json.
Probe provenance: build/bch-recovery-consensus-probe-provenance-20261011.json.
Build script: build/build-bch-recovery-consensus-probe-20261011.py.
This demonstrates BCH recovery primitives with known fixture keys, not authenticated two-party
negotiation, cross-curve binding, safe release policy, a full BCH/XMZ swap or live activation.
The long BCH controller validation remains separate and incomplete. Mining remains off.


### 2026-10-11 — continuous LTC/XMZ success and refund acceptance passed

The v2 acceptance runner finished both scenarios successfully. Both reports set
residentAppSettlementVerified and fullCrossChainSettlement to true: the same two Qt engines
remain alive from approved startup through final native payout/refund, with existing exact
amount/fee, private peer exchange, retry, wallet restoration/reopening and indexed-chain checks.
The refund scenario cancels from the native-funder app and also rejects premature punishment.
Fresh app setup/restart verification completes after each resident app exits.

Reports: build/litecoin-resident-settlement-v2-20261011-{success,refund}.json.
Terminal status: build/litecoin-resident-settlement-v2-20261011-status.json (passed).
All 300 source hashes and pinned binary/Qt/test-driver hashes were rechecked against
build/litecoin-resident-settlement-v2-20261011-provenance.json after both runs.
The release-daemon attempt remains a retained startup failure; these passing runs use the
recorded checklist-tests daemon/wallet-RPC binaries.

This closes the continuous success/refund gate within this isolated node-backed fixture.
It does not prove every recovery path: both reports retain recoveryPathsVerified=false and
executable=false. Mature native-funder punishment, additional interruption/ambiguous-reply and
MWEB boundaries, light-wallet DEX settlement, public onboarding and release acceptance remain
open. The long BCH controller validation is still live; mining remains off.


### 2026-10-11 — production BCH observation verified against BCHN

Added bch_observation_consensus_probe.rs, a read-only adapter for the production BchRpc.
The two-stage consensus fixture now optionally invokes it with credentials for its owned
loopback node through stdin. Deployment context pins the node subversion, regtest checkpoint
and fixture wallet anchor. No observer operation signs or broadcasts.

The complete success/refund/punishment fixture passed again with 45 observer calls.
Both indexed and explicit-block transaction observations agree with the node. Shallow depth,
wrong checkpoint and wrong expected subversion reject. Confirmed-unspent evidence disappears
immediately when a spend enters the mempool, while historical parent confirmation remains.
Cancellation output observation allows a safe early refund, rejects the near-punishment refund
window, and recognizes punishment maturity only after the cancellation-relative delay.
Restart re-observation succeeds; invalidated/replaced block hints reject and the replacement
branch is freshly verified. This uses actual node data, not mocked RPC responses.

Current combined report: build/bch-recovery-observation-node-20261011.json.
Observer/library hashes: build/bch-observation-consensus-probe-provenance-20261011.json.
Compilation artifacts: build/bch-observation-consensus-artifacts-20261011.jsonl.
The earlier recovery-only report predates the optional observer extension; this combined report
binds the current Python fixture. Production source hashes remain unchanged. All fixture BCHN
processes stopped. Authenticated full BCH/XMZ execution and complete app settlement remain open.

The mature native-funder LTC punishment run has started under
build/run-litecoin-resident-punishment-20261011.py; follow its matching status JSON and provenance.
No punishment result or result from the original long BCH controller run is claimed yet.
Mining remains off.


### 2026-10-11 — BCH wallet funding against BCHN; LTC punishment accepted

Added bch_funding_consensus_probe.rs and test_bch_funding_consensus.py. The production
BchRpc preparation path selected ordinary confirmed wallet inputs, preserved the 100,000-atom
principal, returned exact parent bytes and a 235-atom fee, and left the existing wallet-lock
list and mempool unchanged. The fixture separately excludes its mining coinbase outputs.
Wrong pinned anchor script, wrong checkpoint and a one-atom fee ceiling rejected without
locking or broadcasting. The actual node then signed the unsigned candidate as an explicit
fixture action; production FundingPreview/SignedFunding verification accepted it and rejected
changed bytes. Exact funding passed node acceptance, restart and replacement-branch confirmation.

Report: build/bch-funding-consensus-node-20261011.json.
Probe/library provenance: build/bch-funding-consensus-probe-provenance-20261011.json.
The signing and relay RPCs in this test are fixture actions, not the production admitted executor.
Executor-only locking/signing/relay and their durable approvals remain a separate actual-node
integration requirement. The adapter does not expose those private mutation methods.

The resident LTC native-funder punishment fixture also completed successfully:
build/litecoin-resident-punishment-20261011-punishment.json and its matching status/provenance.
It verifies native-funder cancellation, premature-punishment rejection, mature exact-byte
punishment, explicit retry, reorganisation rejection and fresh-app confirmation after restart.
The recipient received 99,980,000 Litecoin atoms after two 10,000-atom fees. No native payout
or refund was executed in this branch; fullCrossChainSettlement, recoveryPathsVerified and
executable remain false, as intended for this unilateral recovery outcome.
All pinned source and artifact hashes were rechecked; the owned fixtures stopped.

The long original BCH controller test remains active. Live markets remain disabled and mining
off. Remaining integration includes full authenticated BCH/XMZ execution and app recovery
controls, broader Litecoin interruption/MWEB acceptance, and light-wallet atomic settlement.


### 2026-10-11 — BCH peer refund-protection app controls

Added BchRefundProtectionJob and the prototype BCH panel using the existing private
admission/helper architecture. The hidden --dex-bch-refund-protection option accepts a
private canonical wrapper with schema monzero-private-bch-refund-protection-v1,
admissionConfigFile and peerMessageFile. Admission must be for the BCH funder.
Startup reads saved status only; review and acceptance require explicit user actions.

The worker pins configuration, signed quote and helper executable, binds the review to
trade, peer identity, quote, agreement, message and signature hashes, and requires the
exact reviewed commitment. It rechecks private message bytes before and after acceptance.
BCH operations share the existing serial startup/operation guard. The QML panel displays
the public review and requires separate consent, cleared when status or market changes.
An uncertain acceptance cannot automatically retry; saved-status checks work even when
the peer message has been removed. Saving this message neither funds the trade nor proves
that recovery is ready. Live markets remain disabled.

The app and DEX test targets build. Worker tests: 21 passed, including changed identity
and message with recomputed review checksums, malformed replies, lost-reply reconciliation,
and bounded private-file reads. QML: 57 passed. Admission regression: 19 passed, with its
external real-helper fixture skipped. Logs: build/bch-refund-worker-tests-20261011.log,
build/bch-refund-panel-qml-20261011.log and
build/bch-refund-admission-regression-20261011.log. These worker tests use a fixture helper;
full real-helper Qt acceptance and authenticated BCH/XMZ settlement remain outstanding.
The original long BCH controller test is still running; no terminal result is claimed.
Mining remains off.

The broader DEX regression completed: 52 passed, 0 failed, 21 external-fixture skips
(build/bch-refund-dex-regression-20261011.log). Source, binary and validation-log hashes
are recorded in build/bch-refund-ui-provenance-20261011.json.


### 2026-10-11 — Refund Qt worker real-helper prerequisite check

Added a realHelperRequiresSavedNegotiation case using the independent signed-quote
fixture and the pinned native Rust admission/refund helper. A BCH-funder wrapper
configures successfully, but its status check without saved negotiation returns unknown;
review and acceptance stay unavailable, no receipt appears, and neither metadata nor
snapshot directories are created. All 22 refund-worker cases pass with this fixture.
The same helper also passes all 20 admission Qt cases, including successful signed-quote
review, so the negative result is paired with a working real-helper positive control.

Evidence: build/bch-refund-real-qt-tests-20261011.log,
build/bch-refund-real-admission-tests-20261011.log and
build/bch-refund-real-qt-provenance-20261011.json. The helper hash matches its archived
build provenance. The prior UI provenance records the earlier test binary; this new
manifest records the expanded test source and binary. Successful authenticated receipt
acceptance through Qt still requires the retained-negotiation integration fixture.
Mining remains off; the original long BCH controller process is still live.


### 2026-10-11 — Retained refund Qt acceptance integration started

Added dex_bch_refund_protection_probe and test_bch_refund_qt_retained.py. The adapter
uses the existing Rust funding fixture hook without modifying production Rust sources.
It copies the fixture metadata and snapshots into a private temporary directory, extracts
the previously authenticated message from the copied receipt, removes that copied receipt,
and drives explicit status/review/acceptance through BchRefundProtectionJob and the pinned
real helper. A fresh Qt process must then recover the identical receipt without the peer
message. The original Rust fixture checks that its own state remains byte-for-byte intact.

Probe compilation and Python syntax validation passed. The integration process is running;
no acceptance result is claimed yet. Follow build/bch-refund-qt-retained-20261011.log and
its result JSON; starting hashes and process identifiers are in
build/bch-refund-qt-retained-provenance-20261011.json. The report's existence alone does not
prove the enclosing Rust fixture passed: its terminal exit and unchanged-state checks must
also succeed. This does not test full BCH/XMZ settlement or node relay. Mining remains off.


### 2026-10-11 — Retained refund Qt acceptance passed

The retained fixture completed successfully (1 passed, no failures; 80.04 seconds).
The production Qt worker accepted the authenticated peer message through the pinned
Rust helper, rejecting wrong approval and repeat submission along the way. After removing
the copied peer-message file, a fresh Qt process recovered the same receipt without
changing its bytes. The enclosing Rust fixture also checked that its original metadata
and snapshots were unchanged. The copied private test state was cleaned up.

Result: build/bch-refund-qt-retained-20261011.json. Terminal output:
build/bch-refund-qt-retained-20261011.log. All starting source and executable hashes were
rechecked and the result/log hashes added to
build/bch-refund-qt-retained-provenance-20261011.json. This closes the successful
real-helper Qt receipt-acceptance check; it does not prove full BCH/XMZ chain settlement.

Reproduce using tools/dex-engine-port/test_bch_refund_qt_retained.py with --helper
(native-bch-admission), --machine-tests (the swap-machine test binary), --qt-probe
(dex_bch_refund_protection_probe) and a new --report path. The adapter reuses the
existing retained funding fixture hook and independently pins its Qt probe and helper.
No production Rust source changes or public-chain funds are required.


### 2026-10-11 — Next BCH integration boundary identified

Inspection of native-bch-admission and funding/recovery.rs confirms that the app helper
can accept peer refund protection but cannot yet request retention of the private recovery
bundle. The engine already exposes retain_bch_recovery and retained_bch_recovery; funding
review requires that retained protection. The missing app/helper operation must therefore
bridge this existing engine prerequisite, not weaken funding's recovery requirement.

Required interface: separately reviewed public funding/cancel/refund/punishment IDs and
a context commitment; explicit retention approval; historical saved status after restart
and expiry; no refund bytes or private scalar in helper output. Exact retry must preserve
the original record and finish an interrupted pin publication without choosing new funding.
Validation must cover missing signed funding/peer protection, changed approval, orphan
snapshot publication, saved-state tampering, and unchanged relay/signing call counts.
The current Rust source snapshot remains unchanged while its original controller run is live.

The real Qt retained fixture has also been extended to corrupt one identity-signature byte
before review and require refusal without any metadata changes or receipt. This v2 run
is active; follow build/bch-refund-qt-retained-v2-20261011.log and its matching provenance.
The earlier successful acceptance/restart report remains valid for its recorded source hashes.

The v2 fixture completed successfully: 1 passed, 0 failed. The real helper rejected the
altered signature through Qt without a receipt or metadata changes; the restored authentic
message was then accepted, and fresh-process recovery returned the identical receipt.
Starting hashes were rechecked and the v2 provenance updated with terminal log/result hashes.


### 2026-10-11 — Recovery retention implementation in isolated validation

Created build/bch-recovery-retention-work-20261011 from the current engine sources, leaving
the original long controller run unchanged. Added review_bch_recovery and
retain_reviewed_bch_recovery around the existing engine retention method. The public review
binds trade, quote, admission, signed funding digest, peer receipt and funding/cancel/refund/
punishment transaction IDs. It excludes preparation time so approval survives dispatch delay.
No private refund bytes are returned. The existing exact retry and orphan-publication rules
remain in retain_bch_recovery.

The isolated native helper adds --recovery-retention-{review,submit,status}-stdio-v1 using
the private admission configuration and the existing file-lease/quote/admission checks.
Status returns only a saved commitment, timestamp and transaction IDs; review is read-only;
submit requires the exact review commitment. No RPC credentials or broadcast methods enter
this interface. Fixture assertions cover stable approval, wrong approval without writes,
expiry refusal, exact transaction IDs and retry without changing retained records.

Validation is active under build/run-bch-recovery-retention-20261011.py, status
build/bch-recovery-retention-status-20261011.json. It builds the helper and test binary,
checks all features/targets for swap-machine, then runs the retained fixture. No pass or
main-worktree integration is claimed yet. Draft diff: build/bch-recovery-retention-draft-20261011.patch.
Qt controls and dedicated subprocess coverage for the new helper modes remain to implement.


### 2026-10-11 — Recovery retention subprocess fixture added

Added tools/dex-engine-port/test_bch_recovery_retention_helper.py using the existing
retained funding fixture hook. On copied private state it checks null initial status,
read-only review, wrong/unexpected approval without writes, explicit retention, exact
public response fields, unchanged retry/restart, orphan snapshot publication and damaged
snapshot rejection. The original fixture checks its own stores remain untouched.

Python syntax validation passed. The isolated Rust helper is still compiling; the dependent
subprocess runner waits for that specific build/fixture process to finish successfully.
Follow build/bch-recovery-retention-status-20261011.json and
build/bch-recovery-retention-subprocess-status-20261011.json. No subprocess pass is claimed
until both the result report and enclosing test exit succeed. Qt controls and integration
of the isolated draft into the primary engine remain outstanding. Mining remains off.


### 2026-10-11 — Recovery retention Qt worker checks passed

Added BchRecoveryRetentionJob with the existing bounded private-helper process architecture.
It pins admission/quote/helper sources, requires a saved-status check before review, binds
review to trade and quote, verifies the exact public field set and review digest, and
requires an unchanged approved review on submit. Saved transaction IDs must match the
reviewed IDs; timestamps must be within the admitted quote interval and not in the future.
Uncertain replies clear displayed approval and disable automatic retry; explicit saved-status
checks remain available. The worker never receives private refund bytes.

All 9 worker checks passed: explicit acceptance/restart, self-consistent wrong trade/quote
reviews, unexpected private-data fields, lost replies, mismatched saved IDs and future
timestamps. Log: build/bch-recovery-retention-qt-tests-20261011.log. Source/binary hashes:
build/bch-recovery-retention-qt-provenance-20261011.json. These tests use a fixture helper.
App wiring, visible controls and real-helper Qt acceptance are not yet implemented.
The isolated Rust machine build and dependent real-helper subprocess fixture remain active.


### 2026-10-11 — Recovery retention app controls wired

Connected BchRecoveryRetentionJob to DexEngine with shared BCH operation exclusion and
queued startup saved-status checks. Added the hidden prototype-only
--dex-bch-recovery-retention option, taking the existing private admission configuration.
The BCH panel shows funding/cancellation/refund/punishment IDs and signed quote, with
explicit status, review and separate retention consent. Status or market changes clear
consent. Saved recovery does not indicate relay, chain maturity or live trading availability.

The wallet builds and all 58 QML checks pass, including the new panel's status/review/consent
sequence, market switching, uncertain results and immediate command refusal. Logs:
build/bch-recovery-retention-ui-build-20261011.log and
build/bch-recovery-retention-qml-20261011.log; corresponding source/binary evidence is in
build/bch-recovery-retention-ui-provenance-20261011.json. The broader DEX regression is
running. Isolated Rust compilation and all-feature/all-target checking passed; the
retained-state engine test and dependent helper subprocess run remain active.
The new Rust helper has not yet been integrated into the primary engine source tree.

The isolated retained-state engine fixture subsequently completed: 1 passed, 0 failed
(82.36 seconds). The helper subprocess fixture is now running against that compiled helper;
its process and terminal result are tracked separately.


### 2026-10-11 — Recovery retention real-helper subprocess checks passed

The isolated helper fixture passed (1 test, 122.23 seconds). It verified absent initial
status, read-only public review, wrong/unexpected approval without writes, first retention,
exact retry, fresh-process status, orphan metadata-pin publication and damaged snapshot
rejection. The enclosing fixture also confirmed its original stores stayed unchanged.
Evidence: build/bch-recovery-retention-subprocess-20261011.json/log and matching provenance.
The v1 driver was archived before extending it with optional Qt coverage.

Added dex_bch_recovery_retention_probe using the production Qt worker. The extended fixture
now drives first retention through Qt and reads the identical saved bundle in a fresh Qt
process before continuing the helper retry/orphan/tamper checks. The probe builds; this
Qt integration run is active under build/run-bch-recovery-retention-qt-integration-20261011.py.
No Qt integration pass is claimed yet. The earlier broader DEX regression also completed:
52 passed, 0 failed, 21 external-fixture skips (bch-recovery-retention-dex-tests-20261011.log).


### 2026-10-11 — Recovery retention ordered patch prepared

Added upstream-bch-recovery-retention.patch and verified that the existing ordered patch
sequence plus this patch exactly reproduces the isolated engine source. The initial audit
found only AGENT.md/CLAUDE.md symlinks converted to regular files by the copy operation;
restoring their symlink form resolved the audit. Evidence:
build/bch-recovery-retention-isolated-patch-audit-v2-20261011.log.

Archived the 123 BCH/native-funding-RPC source files selected by the original live controller
runner under build/bch-controller-source-snapshot-20261011, verifying every hash against
that runner's manifest. Its older full manifest also includes Litecoin light-wallet files
changed in later milestones, so this archive deliberately proves only the original runner's
BCH/controller scope. No full old-worktree hash match is claimed. The new patch has not yet
been added to the primary engine or normal verifier sequence; Qt integration remains active.


### 2026-10-11 — Recovery retention integrated into primary engine

The real Qt integration fixture passed (1 test, 137.01 seconds). First retention used
BchRecoveryRetentionJob and the production Rust helper; a fresh Qt process recovered the
identical saved bundle. The same fixture then passed helper retry, orphan publication and
tampered-snapshot checks. All source/probe/helper hashes were rechecked; evidence is in
build/bch-recovery-retention-qt-integration-20261011.json/log and matching provenance.

Applied upstream-bch-recovery-retention.patch to the primary engine after a clean apply
check, and verified that all source hashes match the successfully validated isolated copy.
Added it to verify-upstream-patches.sh; the normal ordered-patch audit passes
(build/bch-recovery-retention-main-patch-audit-20261011.log). Existing uncommitted changes
and the real Git index were preserved. Primary-path all-feature/all-target checking and
helper rebuilding are active under build/run-bch-recovery-retention-main-build-20261011.py.

The old long controller process still runs its previously copied test binary. Its result
will describe the archived BCH/controller source snapshot, not the newly integrated
recovery-retention source. Full authenticated BCH/XMZ chain execution and public onboarding
remain unfinished. Mining stays off.


### 2026-10-11 — Recovery panel visual verification

Extended the recovery-retention QML fixture to display full 64-character funding, quote,
cancellation, refund and punishment identifiers and capture the reviewed panel. All 58
QML checks pass again (build/bch-recovery-retention-panel-qml-20261011.log). Inspected
build/bch-recovery-retention-panel-20261011.png: the identifiers and explicit save control
are visible without clipping in the test viewport. This is fixture presentation evidence,
not a funded swap screenshot.

The next recovery interface gap is cancellation: relay_bch_cancel already binds the
reviewed transaction, saved recovery and deployment, checks confirmed unspent funding
and maturity before writing intent, and refuses retransmission after an uncertain reply.
The private app helper does not expose its review/status/relay/observation flow yet.
No cancellation UI or relay integration is claimed by the retention milestone.

Primary-engine all-feature/all-target checking and helper rebuilding subsequently passed.
The completed runner reverified all source hashes against the tested isolated implementation.
Binary: build/bch-recovery-retention-main-helper-20261011; SHA256
07689dd5e55deb5e4e2dd55c22acd9d6fe2cbbd6f3e359c662d6f25139217121.
Build receipt: build/bch-recovery-retention-main-build-status-20261011.json.


### 2026-10-11 — Offline cancellation review boundary added

Added review_bch_cancel_relay_for_deployment and saved_bch_cancel_relay_review. Existing
RPC-object review delegates to the explicit deployment method. The helper can now obtain
an exact cancellation review from the pinned deployment without reading wallet credentials
or contacting a node. Historical saved review validates retained relay state; cancellation
review remains available after quote expiry because it is recovery, not new funding.
Neither method establishes maturity or grants relay/refund-release permission.

Extended the retained fixture to compare old/new review commitments, reject a wrong
deployment and pre-preparation time, allow historical recovery review, and check unchanged
metadata/snapshots. The engine patch sequence passes with
upstream-bch-cancel-offline-review.patch (build/bch-cancel-offline-patch-audit-20261011.log).
Compilation, all-feature/all-target checking and the retained fixture are running under
build/run-bch-cancel-offline-20261011.py. No new test pass is claimed yet. Cancellation
helper commands and Qt controls remain to implement. Mining stays off.


### 2026-10-11 — Cancellation helper draft and offline fixture

Staged build/native-bch-cancel-helper-draft-20261011.rs outside the active compilation
snapshot. It adds separate review/status/submit/observe requests, private cancellation
configuration with pinned deployment, and a private submission wrapper using the existing
RPC descriptor. Review/status require no credentials or RPC. Submission checks exact
approval and deployment, then delegates maturity, intent and no-automatic-retry rules to
relay_bch_cancel. Observation delegates exact confirmed-cancellation verification and
returns public block/tip/confirmation fields independently from saved relay history.
This draft is not compiled or applied yet.

Added test_bch_cancel_helper.py as a retained funding-fixture hook. It is designed to compare
the cancellation ID with saved recovery, independently verify the public review digest,
check offline saved status, reject changed deployment/unexpected approval, and verify no
metadata/snapshot writes on failed requests. Python syntax checks pass; no helper result
is claimed until the staged implementation is built and this fixture runs. Positive
relay/chain observation and Qt controls remain separate required integration work.
The offline engine-review build continues without source changes or restart.

Cancellation offline-review validation update: the release test binary compiled in
3m 11s and the all-feature/all-target release check passed in 13.97s. The retained
fixture is now running under the same runner (build/bch-cancel-offline-status-20261011.json).
No terminal fixture result is claimed yet; the cancellation helper draft remains staged.


### 2026-10-11 — Cancellation helper applied; validation running

Offline cancellation review validation passed: release compilation, all-feature/all-target
checking and the retained fixture (1 passed, 101.46 seconds). All recorded source hashes
were rechecked before applying the staged helper. Added upstream-bch-cancel-helper.patch
to the normal ordered sequence; the patch audit passes.

The helper now exposes --cancel-{review,status,submit,observe}-stdio-v1. Offline review
and status use the private cancellation wrapper and pinned deployment. Explicit submit
uses a separate private RPC wrapper and delegates maturity/intent/uncertain-result handling
to the engine. Observation returns exact confirmed transaction and block/tip evidence,
separate from saved relay acknowledgement. No private refund bytes are exposed.

Compilation, all-feature/all-target checking and the offline subprocess fixture are active
under build/run-bch-cancel-helper-20261011.py; status:
build/bch-cancel-helper-build-status-20261011.json. No helper validation pass is claimed
yet. Positive cancellation dispatch/observation and Qt controls remain unverified and
unimplemented respectively. Mining remains off; live markets stay disabled.


### 2026-10-11 — Cancellation relay helper fixture staged

The new helper compiled successfully; its offline subprocess fixture is active. Prepared
build/bch-cancel-helper-relay-fixture-20261011.rs and the corresponding parent fixture
draft. The new ignored test will call the real helper against the existing owned loopback
RPC server for acknowledged and lost-reply cancellation cases. It checks wrong approval
and immature funding before any intent, one relay request only, saved acknowledged/attempted
state across helper processes, refusal to relay again after uncertainty, exact confirmation,
invalid confirmation rejection and unchanged private snapshots.

The drafts remain outside the active source snapshot and have not been compiled or run.
They will be applied after the current helper fixture has a terminal result. These are
loopback RPC integration checks, not actual BCHN chain acceptance or full BCH/XMZ settlement.
No cancellation GUI controls are enabled. Mining remains off.


### 2026-10-11 — Cancellation offline helper passed; relay fixture applied

The real-helper offline fixture passed (1 test, 133.39 seconds), including review/status,
independent review-digest checking, retained recovery transaction-ID agreement, rejection
of changed deployment/unexpected approval, and unchanged saved stores. Compilation and
all-feature/all-target checking passed. Helper SHA256:
2dee8aefda26f5f35742cfc51598b1a261f4fa9b6f3ec36e87d178e4c62f34ef.
Evidence: build/bch-cancel-helper-build-status-20261011.json and
build/bch-cancel-helper-subprocess-20261011.json/log.

Applied the staged relay fixture as funding_helper/cancellation.rs with a separate ignored
test entry. It invokes the real helper for acknowledged and lost-reply cases against the
owned loopback server, preserving existing fixture paths. Added
upstream-bch-cancel-helper-relay-tests.patch; the normal ordered-patch audit passes.
Compilation/checking and the new fixture are running under
build/run-bch-cancel-relay-20261011.py (bch-cancel-relay-status-20261011.json).
No positive helper relay/observation pass is claimed until this completes. Actual BCHN
executor acceptance and the cancellation Qt flow remain open. Mining stays off.


### 2026-10-11 — Qt cancellation protocol checks passed

Added BchCancellationProtocol.h using the existing admission validation primitives. The
public review validates both funder roles, exact field counts and digest, trade/quote/
deployment/network binding, principal-minus-fee arithmetic, quoted delay/confirmation
policy, valid transaction identifiers and preparation chronology. Historical review does
not incorrectly require an unexpired funding quote. Relay acknowledgement remains distinct
from fresh chain confirmation; observation requires the reviewed transaction, enough
confirmations and an observation timestamp within the request interval.

All 14 checks passed, including mutations with recomputed digests so trade, amount and
policy checks are exercised independently of checksum failure. Test/log hashes:
build/bch-cancel-protocol-provenance-20261011.json; log:
build/bch-cancel-protocol-tests-20261011.log. These validators are not yet connected to a
cancellation process worker or visible app controls. The real-helper relay fixture is
still compiling. Mining remains off.


### 2026-10-11 — Cancellation Qt worker checks passed

Added BchCancellationJob using the existing funding-worker process/file-pinning design
and BchCancellationProtocol validators. Offline saved status is required before review;
submission requires the exact review, a separately pinned RPC descriptor and explicit
approval. An attempted submission or previously observed cancellation disables new
submission controls. Saved relay acknowledgement and fresh chain confirmation remain
separate. Failed/stale observations clear visible confirmation evidence.

The worker supports both cancellation roles and keeps the RPC descriptor separate from
offline review/status. The helper alone opens wallet credentials and enforces current
maturity before dispatch. All 9 worker cases and 14 protocol cases pass (logs:
build/bch-cancel-worker-tests-20261011.log and bch-cancel-protocol-final-tests-20261011.log).
Worker cases cover explicit consent, acknowledgement without confirmation, stale proof,
lost reply, fresh-process attempted status and malformed helper responses. Source/binary
evidence: build/bch-cancel-worker-provenance-20261011.json.

These worker tests use a fixture helper. The real-helper relay fixture remains active;
Qt/real-helper integration, engine wiring and visible cancellation controls remain open.
Mining stays off.


### 2026-10-11 — Cancellation helper relay passed; app controls connected

The real helper relay/observation fixture passed (1 test, 206.15 seconds), covering wrong
approval, immature funding without intent, exactly one acknowledged or lost-reply relay,
fresh-process saved state, no retransmission after uncertainty, exact cancellation
confirmation and rejection of invalid confirmation data. Source hashes were rechecked.
This uses the owned loopback RPC fixture, not BCHN chain consensus acceptance.
Evidence: build/bch-cancel-relay-tests-20261011.log and status/sources files.

Connected BchCancellationJob to DexEngine, the shared BCH operation guard and read-only
startup status queue. Hidden prototype options --dex-bch-cancellation and
--dex-bch-cancellation-submit accept offline or submission configurations respectively.
The BCH panel separates saved status, exact transaction/fee review, explicit submission
consent and chain observation. Market/status changes clear consent; acknowledgement
is not confirmation, and confirmation is not proof that a refund is spendable.

The app builds and all 59 QML checks pass. Inspected the panel screenshot with full-length
transaction IDs and formatted BCH amounts (build/bch-cancellation-panel-20261011.png).
The broader DEX regression is running. Source/build/test evidence:
build/bch-cancel-ui-provenance-20261011.json. Real-helper Qt integration, native-funder
helper coverage and actual BCHN executor acceptance remain open. Mining stays off.

The broader DEX regression completed: 52 passed, 0 failed, 21 external-fixture skips
(build/bch-cancel-dex-tests-20261011.log).


### 2026-10-11 — Cancellation Qt/real-helper fixture started

Added dex_bch_cancellation_probe around the production Qt worker. The retained cancellation
fixture accepts an independently digest-pinned Qt probe while its private configuration
continues to pin the real Rust helper. The adapter performs status/review before approved
submission and returns only worker-validated results. The fixture expects Qt to reject a
second submission even after acknowledgement, matching the actual app policy; the direct
helper path retains its existing idempotency check.

The probe builds and the ordered-patch audit passes with upstream-bch-cancel-qt-fixture.patch.
The combined fixture is compiling under build/run-bch-cancel-qt-20261011.py; starting Qt
source/binary hashes are in build/bch-cancel-qt-provenance-20261011.json. No combined pass
is claimed yet. This run uses owned loopback RPC, not actual BCHN chain execution. Mining
remains off.


### 2026-10-11 — Public roadmap updated for BCH recovery controls

Published the verified private-recovery retention milestone, cancellation controls, direct
helper relay checks and 59 QML checks to https://monzero.org/roadmap/. The page explicitly
keeps combined cancellation Qt/helper acceptance and full BCH/XMZ settlement unfinished.
Mobile/desktop rendering passed at 360, 390 and 1440 pixels without horizontal overflow.
The previous remote/public hash was checked, a backup retained, the page replaced
atomically and the public response verified against the new local hash.
Deployment receipt: build/website-roadmap-bch-recovery-deploy-20261011.json.
Published SHA256: d90676070ff2277c82560541efc109ae86b0a8a9fe9d5e84f954be4ea2309db6. Only the roadmap was changed.

### 2026-10-11 — BCH controller snapshot completed; cancellation interoperability fix

The previously launched BCH controller snapshot finished: 50 passed, 0 failed,
8 optional tests ignored (16342.79 seconds). This is evidence for the archived
controller snapshot and copied binary, not the newer cancellation or recovery-retention
work. Evidence: build/bch-current-controllers-status-20261011.json,
build/bch-current-controllers-tests-20261011.log and
build/bch-controller-source-snapshot-20261011/source-hashes.json.

The first combined Qt cancellation/real-helper fixture failed during review. Inspection
identified a wire-format mismatch: rust-bitcoin serializes a human-readable OutPoint
as `txid:vout`, while the Qt validator and synthetic test fixture expected an object.
The validator now requires the canonical string, a nonzero lowercase transaction ID
and a canonical unsigned 32-bit output index; the display extracts the transaction ID.
Regression cases reject object, missing-index, leading-zero, negative, overflowing
and multiple-separator encodings. Protocol tests: 20 passed; worker tests: 9 passed.
The wallet and cancellation probe rebuilt successfully. The failed report is retained;
a separate v2 combined integration run is pending. Mining remains inactive (zero threads).
Evidence: build/bch-cancel-outpoint-{protocol,worker}-20261011.log,
build/bch-cancel-outpoint-provenance-20261011.json,
build/bch-cancel-qt-v2-status-20261011.json.

### 2026-10-11 — cancellation Qt / real-helper acceptance passed

After fixing the canonical outpoint encoding, the combined production Qt worker / real
helper fixture passed (1 test, 290.19 seconds). It verifies exact approval, refusal before
maturity, one relay only, acknowledgement and lost-reply persistence, fresh-process
saved status, no retransmission after uncertain dispatch, exact confirmation and
rejection of invalid confirmation. Private snapshots remain unchanged. All 59 QML
checks also pass. Engine source and Qt/probe hashes were rechecked after completion.
Evidence: build/bch-cancel-qt-v2-tests-20261011.log,
build/bch-cancel-qt-v2-status-20261011.json,
build/bch-cancel-outpoint-provenance-20261011.json.
This fixture uses an owned loopback RPC server, not a full BCHN/XMZ settlement.
Native-funder cancellation helper coverage and the remaining BCH app recovery flows
remain unfinished. The refund-release helper candidate is isolated under
build/bch-refund-release-work-20261011 and is not yet integrated.

### 2026-10-11 — isolated refund-release helper candidate

Added offline public review and saved-status APIs that bind the deployment and refund
safety margin, plus explicit helper review/status/submit/observe commands. Submission
uses the existing engine refund-window checks and durable one-shot relay; public review
retains the explicit native-spend-share disclosure and exposes no private refund bytes.
The helper builds and the all-features/all-targets check passes. The retained fixture
passed (1 test, 140.90 seconds): exact refund transaction ID, saved offline review,
approval checksum, incorrect deployment, unexpected approval and unsafe margins,
margin changes alter approval, and no metadata/private-snapshot writes.
Evidence: build/bch-refund-release-{status,subprocess-status,subprocess-provenance}-20261011.json.
Candidate: build/bch-refund-release-candidate-20261011.patch; isolated source:
build/bch-refund-release-work-20261011. This is not integrated or enabled in the app.
Actual refund helper relay/observation, changed-margin reconciliation after dispatch,
and Qt acceptance still require validation before integration.

### 2026-10-11 — refund worker and relay validation in progress

Added BchRefundReleaseProtocol and BchRefundReleaseJob using the existing pinned,
bounded helper-process lifecycle. Review validates the signed quote's refund destination,
principal less both fees, canonical funding/recovery outpoints, original configured
safety margin and explicit native-spend-share disclosure. Saved acknowledgement is
not chain confirmation. Submit requires a prior saved-status check and exact approval;
uncertain submission disables retries, including after restoring attempted state.
Protocol tests: 12 passed. Worker tests: 9 passed, including stale observation and
malformed response rejection. Reports: build/bch-refund-{protocol-final,worker}-tests-20261011.log.
Source/probe manifest: build/bch-refund-worker-provenance-20261011.json.

The isolated engine adds a real-helper relay fixture for acknowledged/lost replies,
spent-output refusal, exact confirmation, changed-margin rejection after dispatch and
no duplicate relay. Runner: build/run-bch-refund-relay-20261011.py.
The Qt offline real-helper fixture is running separately via
build/run-bch-refund-release-qt-20261011.py. Neither pending run is counted as passed.
The worker is not yet wired into DexEngine or exposed in the app UI. Mining remains off.

### 2026-10-11 — refund offline Qt acceptance and prototype controls

The production refund worker / real-helper offline fixture passed (1 test, 131.73
seconds). It verifies the exact retained refund ID and approval, read-only saved status,
invalid deployment and unsafe-margin rejection with no private-store writes.
Evidence: build/bch-refund-release-qt-20261011.json and .log;
worker/probe hashes rechecked in build/bch-refund-worker-provenance-20261011.json.

Wired BchRefundReleaseJob into DexEngine's shared BCH operation guard and queued
startup saved-status checks. Hidden prototype options --dex-bch-refund-release and
--dex-bch-refund-release-submit attach the private configurations. The BCH panel
shows exact transaction, amount, fee, destination and margin, with separate consent
explaining that refund publication lets the peer recover XMZ. Observed BCH confirmation
never claims XMZ recovery is complete. Unknown results clear stale evidence; consent
resets on status or market change. All 60 QML checks pass; the panel screenshot was
inspected. The wallet and DEX test binary built successfully.

The first relay fixture failed because its adapter used --cancel instead of --refund;
that failed report is retained. The corrected v2 run and dependent Qt relay run are
pending; neither is claimed as passed. The refund helper remains in the isolated
engine copy pending those checks and ordered-patch integration. Live markets remain
disabled and mining remains off.

Refund UI regression result: 52 DEX checks passed, 21 external-fixture checks skipped.
Evidence: build/bch-refund-ui-dex-tests-20261011.log; current UI/build hashes verified
in build/bch-refund-ui-provenance-20261011.json.

#### Private BCH refund configuration (prototype)

The optional wallet flags `--dex-bch-refund-release CONFIG` and
`--dex-bch-refund-release-submit CONFIG` are mutually exclusive and require the DEX
prototype. Startup reads saved status only. Review, submission and chain observation
are separate user actions. The helper candidate must be explicitly pinned by the
existing admission configuration; older helpers do not implement these commands.

The private review configuration has exactly four fields:

- `schema`: `monzero-private-bch-refund-v1`.
- `admissionConfigFile`: canonical absolute path to the existing BCH-funder admission.
- `deploymentCommitment`: the original BCH wallet deployment digest.
- `safetyBlocks`: positive integer margin; confirmations plus margin must be less than
  the signed punishment delay. Changing this value changes the review commitment.

The private submission configuration has exactly three fields:
`schema: monzero-private-bch-refund-submission-v1`, `refundConfigFile` (the review
configuration's absolute path) and `rpcFile` (the existing private BCH funding RPC
configuration). Files retain the existing owner-only configuration requirements.
Offline review/status never open RPC credentials. Submit rechecks the exact confirmed
cancellation and unspent recovery output before retaining intent and relaying.

Helper modes are `--refund-{review,status,submit,observe}-stdio-v1`. Requests remain
bounded to 512 bytes with action-specific schema and request UUID; only submission
accepts the exact review commitment. Responses contain public review, relay history
and optional current observation. Acknowledgement is not confirmation. Once intent
exists, an uncertain result cannot be retransmitted automatically. Status/observation
reject a changed configured safety margin instead of displaying a different approval.
Publishing the refund reveals the native share to the counterparty; the UI requires
separate explicit consent and never treats BCH confirmation as completed XMZ recovery.

### 2026-10-11 — roadmap verification corrections published

Published the completed 50-test historical BCH controller snapshot, cancellation Qt/helper
acceptance, offline refund Qt/helper checks and 60 QML checks. Refund relay acceptance
and full BCH/XMZ execution remain explicitly unfinished. Rendering at 360, 390 and
1440 pixels passed without horizontal overflow. The previous remote/public hash was
checked, a backup retained and the final public hash verified. Roadmap only.
Receipt: build/website-roadmap-bch-refund-deploy-20261011.json.
Public SHA256: 3444e19078a39364fdd84b38ae89a5fd6251e38e637dcb665e6d4491bb00e9ae.

### 2026-10-11 — refund helper relay acceptance and engine integration

The corrected direct-helper fixture passed (1 test, 245.94 seconds). It verifies exact
approval; spent recovery output and invalid confirmation refusal before intent; one
relay; acknowledged and lost-reply persistence; no retransmission after uncertainty;
changed-margin status, observation and submission rejection; exact confirmation and
unchanged private snapshots. This is an owned loopback RPC fixture, not full BCHN/XMZ
settlement. The first adapter-mode failure remains recorded separately.

Integrated the five tested Rust source files through
upstream-bch-refund-release-helper.patch and added it to the ordered-patch verifier.
The verifier passes; before/after source hashes were checked against the isolated tested
candidate. Main-worktree helper rebuild/all-features/all-targets check and the combined
Qt relay fixture are running. Evidence: build/bch-refund-relay-v2-tests-20261011.log,
build/bch-refund-relay-provenance-20261011.json,
build/bch-refund-main-patch-audit-20261011.log,
build/bch-refund-main-status-20261011.json and
build/bch-refund-relay-qt-status-20261011.json.

### 2026-10-11 — main refund build verified; real BCHN executor acceptance started

The main-engine refund helper builds and the all-features/all-targets check passes.
Source hashes rechecked; helper provenance is recorded in
build/bch-refund-main-provenance-20261011.json. The combined Qt refund relay fixture
is still running and is not yet counted as passed.

Started an isolated test of production BCH wallet preparation, input locking, signing,
private recovery retention and funding relay against an owned BCHN 29 regtest wallet.
The fixture uses authenticated negotiation and the production admitted execution
controller, rather than fixture-side signing/broadcasting. It restores saved controller
state and checks exact transaction bytes; the node driver will independently verify
amounts, fees, confirmation and node restart. This is funding acceptance, not a complete
BCH/XMZ settlement. Source: build/bch-node-executor-work-20261011; driver:
tools/dex-engine-port/test_bch_node_executor.py; runners/status:
build/build-bch-node-executor-20261011.py and build/run-bch-node-executor-20261011.py.
Compilation and node acceptance are pending. The existing mining daemon is untouched.

### 2026-10-11 — complete refund Qt/helper relay fixture passed

The same retained-state relay fixture passed through the production Qt worker and real
helper (1 test, 310.35 seconds). It verifies exact approval, refusal of spent recovery
outputs and invalid confirmation before submission, acknowledged and lost replies,
fresh-process historical status, disabled repeat submission, changed-margin rejection,
exact fresh confirmation and unchanged private snapshots. The Qt worker intentionally
disables a second send even after acknowledgement; the direct helper only returns the
saved acknowledgement without another RPC. Evidence:
build/bch-refund-relay-qt-20261011.log and build/bch-refund-relay-provenance-20261011.json.
Source, helper, test binary and Qt probe hashes were checked after the run.

This completes the prototype refund controls' loopback acceptance. It does not prove
full BCHN/XMZ settlement or expose a live market. The independent real-BCHN funding
executor test is still compiling. Mining remains off.

### 2026-10-11 — production BCH funding accepted by actual BCHN

The owned BCHN regtest fixture passed. Production AdmittedExecution performs wallet
preparation, input locking, reviewed signing, authenticated peer refund protection,
private recovery retention and reviewed funding relay. Invalid signing and relay
approvals are refused. Restoring the controller preserves the exact unsigned/signed
transaction and acknowledgement; an acknowledged retry returns the saved result.
BCHN accepted the transaction, and the independent driver checked 100,000 atoms at
the exact lock script with a 235-atom funding fee. Three confirmations and exact raw
transaction bytes survive node restart. The temporary node has stopped.

The Rust fixture passed in 48.57 seconds; all-features/all-targets check also passed.
Source and executable hashes were rechecked. The test-only engine changes are now
integrated through upstream-bch-node-funding-executor-test.patch, with the ordered
patch audit passing. Evidence: build/bch-node-executor-20261011.json,
build/bch-node-executor-tests-20261011.log,
build/bch-node-executor-provenance-20261011.json and
build/bch-node-executor-patch-audit-20261011.log.

This closes production BCH wallet lock/sign/relay node acceptance, not full two-chain
settlement. Authenticated handoff and native funding against both chains, protected
transport, remaining settlement controls and broader interruption recovery remain.
The roadmap now reports this milestone and completed Qt refund relay acceptance.
Mobile/desktop checks passed; the prior remote/public hash was checked, backup saved
and the new public response verified. Receipt:
build/website-roadmap-bch-node-executor-deploy-20261011.json.
Public SHA256: 341c08ed5da44c659d7f04a4c4050606c2bb59bb7d39fa178ba1ccdaeaa488f5.
Live markets remain disabled. User mining remains off.

### 2026-10-11 — authenticated real-node funding handoff fixture in progress

Extended the owned-node executor fixture in an isolated source copy to authenticate
and retain the BCH funding message on the native-funder side, reject the wrong peer
signature without writes, restore that receipt, and require fresh chain evidence.
The driver coordinates actual block generation: mempool funding must be refused,
three confirmations must permit the native funding-window check, and depth 15 with
the signed 20-block cancellation delay and 5-block margin must be refused. Read-only
checks must leave metadata and private snapshots unchanged. No XMZ payment is made
by this fixture; actual two-chain native preparation/relay remains a subsequent gate.

Source: build/bch-node-handoff-work-20261011; driver:
tools/dex-engine-port/test_bch_node_handoff.py. Build/run:
build/build-bch-node-handoff-20261011.py and build/run-bch-node-handoff-20261011.py.
Both status files still report compilation/waiting; no passed result is claimed.
The original passing node-executor fixture and artifacts are preserved unchanged.

### 2026-10-11 — real-node handoff and native funding-window checks passed

The authenticated handoff fixture passed (1 test, 103.37 seconds). Against owned BCHN,
the native-funder controller rejects the wrong identity signature without writes,
restores the exact receipt, refuses mempool funding, accepts three confirmations and
refuses depth 15 under the signed 20-block cancellation delay plus a five-block margin.
Observation leaves metadata/private snapshots unchanged. Node restart and exact
funding amounts/fees also pass. Integrated upstream-bch-node-handoff-test.patch; the
ordered-patch audit passes. Source/artifact hashes rechecked. Evidence:
build/bch-node-handoff-20261011.json, build/bch-node-handoff-tests-20261011.log,
build/bch-node-handoff-driver-provenance-20261011.json and
build/bch-node-handoff-patch-audit-20261011.log.

A separate two-chain funding fixture is now compiling in
build/bch-paired-funding-work-20261011. Its quote binds an actual isolated XMZ genesis,
source wallet and payout/refund destinations. It prepares, explicitly approves and
relays retained XMZ funding, authenticates its proof back to the BCH side, refuses
unconfirmed native payment and requires fresh confirmation. Drivers:
tools/dex-engine-port/test_bch_paired_funding.py and bch_native_funding_fixture.py.
Build/run: build/build-bch-paired-funding-20261011.py and
build/run-bch-paired-funding-20261011.py. This pending run is not a pass and does not
claim complete settlement. Only disposable offline regtest/fakechain services are used;
user mining remains off.

### 2026-10-11 — paired funding exposed an observer-context error

The first actual two-chain funding fixture failed after 586.96 seconds with
`native transfer is not incoming`. BCH funding and native submission reached their
checkpoints, but the observer incorrectly used the source wallet's outgoing-transfer
record. No complete pass is claimed. Failure evidence is preserved in
build/bch-paired-funding-tests-20261011.log and
build/bch-paired-funding-driver-20261011.log; the original isolated source/driver remain.

The v2 candidate supplies a dedicated authenticated observer wallet RPC and restores
only the negotiated shared view key through NativeWalletSession::restore_view. The
source wallet retains exclusive funding responsibility. After generating confirmation
blocks, the driver refreshes the observer before the production funding verifier reads
its incoming-transfer record. The incoming-transfer guard remains unchanged. The
owned-fixture timeout is 1800 seconds, matching the existing paired-chain harness and
allowing for the observed cryptographic verification cost; the original run was an
explicit verifier failure, not a timeout. The v2 build/run is pending:
build/build-bch-paired-funding-v2-20261011.py,
build/run-bch-paired-funding-v2-20261011.py and
build/bch-paired-funding-v2-driver-provenance-20261011.json.
No live market or user mining is enabled.

### 2026-10-11 — paired acceptance candidates and roadmap reconciliation

The v2 funding test remains in progress with a dedicated incoming-payment observer.
Review found a separate driver accounting error: the native-fee variable was reused
for BCH accounting. The v3 driver preserves separate fees and will rerun the same
pinned Rust binary after v2 finishes. Neither pending driver is recorded as passed.

An isolated successor adds actual BCH redemption and native claim-key recovery:
approved retained redemption, exact 99,000-atom owned recipient output and
1,000-atom fee, unconfirmed-spend rejection and recovered negotiated key equality.
It does not execute the final native payout. Build/run files are
build/build-bch-paired-redemption-20261011.py and
build/run-bch-paired-redemption-20261011.py; execution depends on passing funding
acceptance. Main engine sources are unchanged by these pending candidates.

The website roadmap now includes the already-passed BCHN authenticated handoff
and removes stale statements about the Litecoin continuous fixtures and archived
BCH controller rerun. Desktop/mobile rendering passed, and the deployed page hash
was verified against the local file. Receipt:
build/website-roadmap-bch-handoff-deploy-20261011.json.
User mining remains stopped (active=false, threads=0, speed=0).

### 2026-10-11 — actual BCH/XMZ funding Rust acceptance passed

The v2 Rust fixture passed (1 test, 599.14 seconds) using owned BCHN regtest and
XMZ fakechain services. It exercised production BCH funding, authenticated handoff,
actual native wallet preparation and approved relay, proof receipt, unconfirmed
payment refusal and incoming-payment confirmation from a dedicated shared view
wallet. All-feature/all-target compilation also passed. The three test-source
changes were integrated via upstream-bch-paired-funding-test.patch; the complete
ordered-patch audit passed. Production verification guards were unchanged.

The enclosing v2 Python driver completed the controller and node checks but failed
while serializing its report: its native fee variable had been overwritten by a BCH
Decimal. The partial JSON is failure evidence, not a completed acceptance report.
The corrected v3 driver is rerunning the identical pinned Rust binary with separate
fee variables. Evidence: build/bch-paired-funding-v2-tests-20261011.log,
build/bch-paired-funding-v2-driver-20261011.log,
build/bch-paired-funding-integration-20261011.json and
build/bch-paired-funding-patch-audit-20261011.log.

The isolated redemption/claim-key successor compiled successfully, including all
features and targets. Its node run remains dependent on v3 funding acceptance.
Neither funding alone nor claim-key recovery establishes final native payout or
complete BCH/XMZ settlement. Live markets remain disabled and user mining is off.

### 2026-10-11 — full native payout acceptance candidate

A further isolated successor extends the paired success path through a dedicated
recovered-wallet RPC service, production restore and explicit reopen, exact saved
payout preparation, distinct relay approval, uncertain-confirmation rejection and
fresh two-chain payout evidence. Its independent driver checks the original source
balance debit plus funding fee, actual BCH recipient output, exact native principal,
returned reserve and an empty recovered wallet. Keys remain outside public reports.

Candidate source: build/bch-paired-settlement-work-20261011.
Build/run: build/build-bch-paired-settlement-20261011.py and
build/run-bch-paired-settlement-20261011.py. Execution waits for successful earlier
funding and redemption candidates. This is pending acceptance, not a pass or a
release-readiness claim. It targets the production controller on owned isolated
chains; complete resident-app integration and recovery coverage remain separate.

### 2026-10-11 — market status descriptions reconciled

MarketRegistry now describes verified isolated LTC app success/refund fixtures and
BCH node-backed funding/handoff, while retaining the unfinished settlement/recovery
and live-trading gates. The wallet rebuilt, and the existing multicoin preview/gate
and wallet-presentation gating checks passed. Evidence:
build/dex-market-status-provenance-20261011.json and
build/dex-market-status-tests-20261011.log. The progress document now begins with
a current per-market summary instead of a stale 6 October heading.

The full native-payout candidate compiled successfully, including all features and
all targets. Its actual-node acceptance remains queued behind the earlier paired
funding and redemption tests. No settlement pass is inferred from compilation.

### 2026-10-11 — complete paired funding acceptance report passed

The corrected v3 driver passed using the unchanged pinned v2 Rust test binary
(1 Rust test, 597.34 seconds). Both-chain production funding, authenticated proof,
actual incoming native payment confirmation, exact saved funding bytes and BCHN
restart checks completed. The report separately records 100,000 BCH atoms locked,
a 235-atom BCH funding fee, 200,000,000,000 native atoms funded and a
1,386,240,000-atom native funding fee. No full settlement is claimed.

Evidence: build/bch-paired-funding-v3-20261011.json and
build/bch-paired-funding-v3-driver-provenance-20261011.json. All pinned driver,
binary and Rust source hashes were rechecked, including equality with the
integrated main engine sources. Earlier failed drivers and their logs remain.
The redemption/claim-key successor has now started; the full native-payout
successor compiled and remains dependent on that result.

The paired-funding milestone is now published on the website roadmap after
desktop/mobile rendering checks. Public and local page hashes match; receipt:
build/website-roadmap-bch-paired-funding-deploy-20261011.json.
The public text leaves BCH redemption, final native payout and complete
settlement unfinished while their isolated candidates run.

### 2026-10-11 — native-funder funding receipt controls candidate

An isolated API/helper candidate adds read-only review of the authenticated BCH
funding handoff, acceptance bound to its exact review commitment and historical
saved-receipt status. The review binds the quote/admission, authenticated message,
exact funding outpoint, contract script, principal and funding fee. It has no RPC
configuration and provides neither chain confirmation nor native payment consent.
The helper preserves private-file ownership, lease, size and executable pin checks.

Candidate source: build/bch-funding-receipt-v2-work-20261011. The actual-node
fixture checks wrong role/signature/approval, expired review, unchanged records
on refusal, exact retry and restored receipt identity. Its real-helper subprocess
fixture uses a separate copy of the pre-receipt metadata with explicit admission
consent, verifies approved acceptance and saved status without the peer file, and
checks that private snapshots remain unchanged. Helper protocol unit checks and
all-feature/all-target compilation are also scheduled.

This work is not integrated or reported passing yet. Build/run evidence will be
build/bch-funding-receipt-v2-status-20261011.json and
build/bch-funding-receipt-v2-run-status-20261011.json. Driver:
tools/dex-engine-port/test_bch_funding_receipt_helper.py.
The earlier paired redemption and full native-payout candidates remain separate.

The initial receipt candidate now compiles for all features/targets and passes all
16 helper unit tests. Its API/helper sources are byte-identical to those in the
expanded subprocess-fixture snapshot; their hashes were rechecked. Evidence:
build/bch-funding-receipt-unit-provenance-20261011.json and
build/bch-funding-receipt-helper-unit-20261011.log. The expanded actual-helper
fixture remains pending, and these changes are not integrated into the app yet.

### 2026-10-11 — receipt validator and corrected subprocess candidate

BchFundingReceiptProtocol now has separate exact-message approval and historical
status entry points. It validates quote/role/context, canonical outpoints, exact
principal, positive capped fee, the current P2SH32 script shape and the public
review digest. The new Qt protocol target passed all 18 checks, including missing
message bindings, changed transaction ID, wrong role, altered amounts, malformed
indexes, wrong script/network and extra private fields. Worker and UI integration
remain pending. Evidence: build/bch-funding-receipt-qt-protocol-provenance-20261011.json.

Review caught a fixture-only envelope assertion expecting eight fields instead of
the actual seven. The v2 acceptance runner was stopped while waiting for its build,
before any test child started; its compiled source/binaries remain preserved. The
v3 snapshot changes only that assertion. It compiled for all features/targets, and
the actual-node/helper run has started:
build/bch-funding-receipt-v3-run-status-20261011.json. The production API/helper
code is unchanged from the candidate whose 16 unit tests passed.

### 2026-10-11 — BCH redemption and native claim recovery passed

The paired redemption fixture passed (1 Rust test, 1416.14 seconds). Both deposits
were confirmed before production success release, receipt and approved retained
BCH redemption. BCHN accepted the exact 99,000-atom recipient output and
1,000-atom redemption fee. Recovery rejected an unconfirmed redemption, then
recovered the negotiated native spend/view keys from the confirmed spend without
writing keys to the public report. The recipient output and exact transaction bytes
survived BCHN restart. Source, driver and binary hashes were rechecked.
Evidence: build/bch-paired-redemption-20261011.json and
build/bch-paired-redemption-driver-provenance-20261011.json. This does not execute
the final native payout. That separate successor has now started.

### 2026-10-11 — funding receipt API/helper integrated; Qt interoperability pending

The actual-node/real-helper receipt fixture passed (1 Rust test, 155.70 seconds),
including exact consent, wrong signature/approval refusal, initial acceptance,
idempotent helper retry and fresh-process status after deleting the peer file.
The validated API/helper and fixture are integrated through
upstream-bch-funding-receipt-helper.patch; the ordered-patch audit passed.
Evidence: build/bch-funding-receipt-v3-20261011.json and
build/bch-funding-receipt-integration-20261011.json.

BchFundingReceiptJob now provides status-first review, exact approval, stale-source
rejection, retained public receipt details and no automatic retry after uncertainty.
A separate private peer-file reader supports the bounded funding envelope while
the existing configuration limit remains unchanged. The worker passed 21 checks
with one external-fixture test skipped; the protocol validator passed 18 checks.
A test-only Qt probe has built, and the combined real-worker/real-helper fixture
is pending in build/bch-funding-receipt-qt-work-20261011. Main app controls and
combined interoperability are not yet claimed complete. Evidence:
build/bch-funding-receipt-qt-provenance-20261011.json.

The wallet rebuilt with BchFundingReceiptJob included. The combined Qt/helper
fixture compiled for all features/targets and has now started; its result remains
pending. The public website roadmap now includes the verified paired redemption
and native claim-key recovery milestone. Desktop/mobile rendering passed and the
public page hash matches the local file; receipt:
build/website-roadmap-bch-paired-redemption-deploy-20261011.json.
Final native payout and complete BCH/XMZ settlement remain explicitly unfinished.

### 2026-10-11 — private BCH peer funding receipt controls

With `MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE`,
`--dex-bch-funding-receipt CONFIG` attaches the native-funder receipt controls.
CONFIG is an absolute, canonical, owner-only private JSON file containing exactly:

```json
{
  "schema": "monzero-private-bch-funding-receipt-v1",
  "admissionConfigFile": "/absolute/path/native-funder-admission.json",
  "peerMessageFile": "/absolute/path/peer-funding-message.json"
}
```

The admission must already be explicitly approved for the native-funder role.
The peer file is an owner-only regular file, without symlinks or additional hard
links, containing exactly schema `monzero-bch-peer-funding-receipt-v1`, `frame`
(a byte array) and `signature` (64 bytes). It is bounded to 6,004,096 bytes;
configuration limits are unchanged. Use the authenticated funding message
produced by the peer; never substitute an unsigned transaction summary.

Startup checks saved status only. Review validates and displays the exact public
message bindings; accepting requires explicit consent. After an uncertain result,
check saved status instead of retrying acceptance automatically. Historical status
works after removal of the peer file. Receipt acceptance neither confirms the BCH
transaction nor authorizes native funding. Other BCH operations cannot overlap.

Real Qt/helper acceptance passed, alongside 18 protocol checks, 21 worker checks
(one external-fixture skip), 61 QML checks and 52 general DEX checks (21 external
fixture skips). Evidence: `build/bch-funding-receipt-ui-provenance-20261011.json`
and `build/bch-funding-receipt-qt-20261011.json`. Live trading remains disabled.

### 2026-10-11 — private native deposit preparation contract

The integrated Rust helper and `BchNativePreparationJob` support preparation of
the XMZ side of a BCH/XMZ trade. With
`MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE`, use
`--dex-bch-native-preparation CONFIG` to attach the controls. Startup reads saved
status only; preparation still requires explicit review and consent. These are isolated
BCH regtest/XMZ fakechain interfaces, not a public deposit service.

The preparation wrapper contains exactly these nine fields; replace the
placeholder wallet and deployment commitment with the fixture's validated values:

```json
{
  "schema": "monzero-private-bch-native-preparation-v1",
  "admissionConfigFile": "/absolute/path/native-funder-admission.json",
  "bchRpcFile": "/absolute/path/bch-rpc.json",
  "nativeRpcFile": "/absolute/path/native-rpc.json",
  "deploymentCommitment": "<64 lowercase hexadecimal characters>",
  "wallet": "<source XMZ wallet address for the configured network>",
  "account": 0,
  "priority": 1,
  "safetyBlocks": 10
}
```

Use canonical absolute paths and owner-only regular files without symlinks or
additional hard links. The admission must be explicitly approved for the
`native-funder` role, and its authenticated BCH funding receipt must already be
retained. The helper verifies the wallet's network/checksum. Priority is 1–4;
the positive safety margin must fit the quote's cancellation window together
with its confirmation requirement. The deployment commitment binds the BCH
observer used for the trade; it is not an arbitrary identifier.

`bchRpcFile` uses the existing `monzero-private-bch-funding-rpc-v1` descriptor
with `port`, `wallet`, `anchor`, `anchorScript`, `cookieFile`, `checkpointHeight`,
`checkpoint` and `subversion`. `nativeRpcFile` contains exactly:

```json
{
  "schema": "monzero-private-bch-native-rpc-v1",
  "daemonPort": 28081,
  "walletPort": 28083,
  "loginFile": "/absolute/path/native-wallet-login"
}
```

The ports must be distinct and nonzero; both endpoints are on loopback. The
private login file contains `username:password`, optionally followed by one
newline. Submission checks that the daemon is offline fakechain with no peer
connections. The caller owns the disposable nodes and exclusive source wallet;
all configurations sharing that wallet must share the native RPC descriptor so
its file lease serializes construction.

The helper takes `--native-preparation-status-stdio-v1`,
`--native-preparation-review-stdio-v1` or
`--native-preparation-submit-stdio-v1`, followed by the wrapper path. Each stdin
request contains schema `monzero-bch-native-preparation-<action>-request-v1`
and a non-nil UUID `requestId`. Only submission includes `commitment`, exactly
matching the approved review. Requests are bounded to 512 bytes.

The Qt worker checks saved status first. Review binds the source wallet/account,
priority, destination, exact amount, fee cap, safety margin and trade/observer
commitments. Submission rechecks consent before reading RPC credentials, then
checks the live BCH lock and source wallet before constructing the transaction
with `do_not_relay`. Preparation does not broadcast the XMZ transaction.

Review and saved status work without RPC descriptors or credentials. A saved
`prepared` result exposes the transaction ID, fee and weight; an `attempted`
result records an uncertain construction without inventing a successful payment.
Neither state permits replacement preparation. After a lost reply, check saved
status; do not automatically retry. Private transaction metadata stays in the
engine store. Broadcast requires a separate approval path, which is not yet
exposed in the app.

The direct-helper paired-chain fixture passed in 657.19 seconds and then verified
relay and confirmation through the controller. Offline Qt/helper interoperability,
18 helper unit checks, 16 protocol checks and 9 worker checks also passed. The
actual Qt preparation submission subsequently passed in 671.28 seconds, including
offline saved status and refusal of repeat preparation. All 18 helper unit tests
also passed for that freshly compiled snapshot. Longer controller acceptance
runs remain pending. Evidence: `build/bch-native-preparation-paired-qt-provenance-20261011.json`,
`build/bch-native-preparation-paired-provenance-20261011.json`,
`build/bch-native-preparation-qt-interop-provenance-20261011.json` and
`build/bch-native-preparation-integration-20261011.json`.

### 2026-10-11 — private native funding broadcast configuration

`--dex-bch-native-relay CONFIG` attaches the native broadcast worker under
`MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE`. It uses the
same nine-field preparation wrapper documented above. Startup checks saved relay
status after preparation status; it does not broadcast. The BCH/XMZ panel offers
saved-status checks, exact-payment review and a separate consent checkbox before
broadcast. Consent resets when the review/status or selected market changes.

The helper operations are `--native-relay-review-stdio-v1`,
`--native-relay-submit-stdio-v1` and `--native-relay-status-stdio-v1`. Each takes
the wrapper path and a bounded stdin request with schema
`monzero-bch-native-relay-<action>-request-v1` and a non-nil UUID `requestId`.
Only submission includes the separately approved broadcast `commitment`.
Preparation approval is not broadcast approval.

Review and status include the saved preparation and public payment terms for
independent Qt validation. Submission checks exact consent before opening
credentials, then rechecks the BCH lock and source wallet through the production
relay. It broadcasts only the retained transaction. An `attempted` state is
uncertain; `acknowledged` means the wallet accepted dispatch, not that the chain
confirmed payment. Either retained state blocks another helper submission.
Status remains available without RPC descriptors or credentials. Never discard
retained attempt records to retry a payment after an ambiguous result.

Direct-helper paired acceptance passed in 774.81 seconds; actual Qt worker/helper
paired acceptance passed in 844.16 seconds. Both include offline saved status,
repeat refusal and subsequent controller verification of native confirmation.
See `build/bch-native-relay-paired-provenance-20261011.json` and
`build/bch-native-relay-paired-qt-provenance-20261011.json`. These isolated funding
fixtures do not establish complete resident-app settlement or recovery coverage.

### Private BCH-funder XMZ payment-proof receipt

`--dex-bch-native-proof CONFIG` attaches the proof-receipt worker under
`MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE`. It requires a
proof-context-capable helper pinned by the admission configuration. The current
proof helper/context candidates are still undergoing real-helper acceptance;
this interface does not enable a live market or establish a complete GUI swap.

The canonical, owner-only mode-0600 wrapper has exactly three fields:

```json
{"admissionConfigFile":"/absolute/private/admission.json","peerMessageFile":"/absolute/private/proof.json","schema":"monzero-private-bch-native-proof-v1"}
```

The admission configuration must select `bch-funder` and the retained trade. The
private peer envelope has schema `monzero-bch-peer-native-proof-v1`, a byte-array
`frame` (1–8192 bytes), and a 64-byte `signature`. The helper opens this bounded
private file only for review/submission and authenticates its existing signed
message. The GUI worker never opens the envelope or exposes the transaction key.
Protected delivery of that file remains separate unfinished work.

Startup asks `--native-proof-context-stdio-v1` to recover public funding context
from the local authenticated journal, then calls `--native-proof-status-stdio-v1`.
Neither step requires the peer file or authorizes receipt. The panel separately
requests `--native-proof-review-stdio-v1`; `--native-proof-submit-stdio-v1` requires
the exact displayed review commitment and explicit consent. Each command accepts
a bounded request with schema `monzero-bch-native-proof-<action>-request-v1` and a
non-nil UUID `requestId`; only submit includes `commitment`.

A lost submission reply blocks another submission. Read saved status to recover
the outcome; a fresh app can recover retained receipt history without the peer
file. Historical receipt can follow quote expiry. Receipt is not XMZ confirmation
and grants no BCH redemption, claim-wallet or payout authority. Those require
separate chain verification and settlement controls.

### Fresh native payment observation (acceptance pending)

The observation helper candidate accepts `--native-observation-stdio-v1` with
schema `monzero-bch-native-observation-request-v1` and a non-nil UUID `requestId`.
No approval field is accepted. Its mode-0600 canonical wrapper is:

```json
{"admissionConfigFile":"/absolute/private/admission.json","nativeRpcFile":"/absolute/private/observer.json","schema":"monzero-private-bch-native-observation-v1"}
```

It requires a BCH-funder admission and an already retained authenticated native
proof. The existing private native RPC descriptor supplies distinct loopback
fakechain daemon/wallet ports and a private login-file path. No credentials or
transaction keys are returned. A successful response contains public proof
review plus transaction, destination, amount, genesis, tip, heights, depth and
observation time; it does not save confirmation or authorize settlement.

The engine's observation worker additionally requires public context recovered
by its saved-proof worker for the same trade. Checks are explicit, never part of
startup. Starting a check clears old chain evidence; failure leaves it unknown.
Rechecking proof status also clears the previous observation. All BCH operations
share one operation guard. The real-node observation fixture remains pending.

`--dex-bch-native-observation CONFIG` attaches the private observation panel under
the same prototype opt-in. Also attach the matching `--dex-bch-native-proof`
configuration and recover its saved receipt first. The observation option does
not schedule an on-chain check at startup. The panel shows exact amount,
transaction, confirmations, heights, tip and check time; it clears details while
checking and after failure. Full resident-app settlement remains unverified.

#### Private BCH settlement review panel

`--dex-bch-success-release-review CONFIG` attaches an offline settlement review
under the existing `MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE`
opt-in. The wrapper uses schema `monzero-private-bch-success-release-review-v1`,
`admissionConfigFile`, `deploymentCommitment` and positive `safetyBlocks`.
The same trade/quote must have a validated saved native proof before review.
Review is explicit and read-only; it does not observe chains, sign, construct an
adaptor or release funds. Proof refresh invalidates the displayed review.
The public release-review helper is integrated after real Qt/helper paired acceptance.
Private preparation and saved-status helpers retain their separate acceptance gates.

The DEX engine also owns the private success-preparation worker. It requires the
same trade and quote in the saved proof, release review and preparation config.
`submitBchSuccessPreparation(commitment)` validates the exact displayed review
before calling the helper. It is never queued at startup and shares the BCH
operation guard. Proof/review refresh clears preparation evidence. An attempted
request with an unknown result may have written the private journal; it is not
proof that preparation did not occur. A subsequent explicit request must repeat
consent and fresh chain checks. `--dex-bch-success-preparation CONFIG` attaches the separate approval controls
beside the release review under the same private prototype opt-in. Its six-field
wrapper uses schema `monzero-private-bch-success-preparation-v1`,
`admissionConfigFile`, `deploymentCommitment`, `safetyBlocks`, `bchRpcFile` and
`nativeRpcFile`. Configure the matching release review and native proof as well.
The UI resets consent on review/status changes and shows uncertain attempts
without automatic retries. Helper paired acceptance remains pending.

`requestBchSuccessPreparationCheck()` reads saved preparation after restoring the
same trade's native proof context. It does not require a fresh release review or
RPC descriptors and never submits preparation. The panel offers this check even
when only the preparation configuration is attached. Historical results always
have `chainObserved=false`; retained identity signatures do not establish peer
release or chain settlement. Submission still requires exact approval and the
original RPC descriptors. Missing or changed descriptors do not authorize a
fallback backend. The status helper remains a candidate pending real acceptance.


## Offline local-custody acceptance (shared light wallets)

Run `python3 tools/dex-engine-port/test_light_wallet_local_custody.py HELPER REPORT`
with the release `native-ltc-light` helper and a new report path. Requires Linux
and bubblewrap; there is no unsandboxed fallback. This exercises LTC/BTC/BCH/DOGE
regtest creation, backup verification and restoration in disposable directories
with networking disabled. Checks also cover repeat creation, wrong password,
wrong-chain requests, restored receive addresses and owner-only file permissions.
It starts no nodes, mining or servers and touches no existing wallet. It does not
prove online-query privacy or complete DEX recovery. Reports exclude passwords,
wallet files, keys and addresses; temporary wallets are deleted on exit.
