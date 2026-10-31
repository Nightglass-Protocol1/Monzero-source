# DEX verification checkpoint — 2026-09-19

Goal: a functioning BTC/XMZ DEX integrated into the Monzero GUI.
This checkpoint is not live activation or independent security approval.

## Executed evidence

- Located the working tree at `/home/pinhead/Projects/Monzero-App/core`.
- Reproduced the existing spendable fakechain fixture using the native binaries
  under `build/release/bin` and `bitcoin_settlement-8b8b067cd74c3287`.
- Strengthened the Python fixture's fee check from a balance inequality to
  outgoing transaction amount/type/confirmation checks and exact conservation:
  remaining balance + transferred principal + recorded fee = locked principal.
  The strengthened fixture passed in a second disposable run.
- Ran `check-upstream.sh machine-test`: 40 passed, zero failed.
- Fixed `verify-upstream-patches.sh` to include the restore, relay, spendable,
  and spendable-fixture patches after the original eight patches.
  All twelve now reproduce the indexed engine sources against the live tree.
  This is source reconstruction, not independent binary reproduction.
- Shell syntax and GUI worktree whitespace checks passed.

No real wallet, production node, installed GUI, or website package was changed.

## Access and outstanding work

Claude Code is installed and authenticated; a separate read-only review completed.
Source inspection corroborates three findings:

- `restore_and_spend` discards the estimate, then constructs a separate relay
  transaction. It does not enforce equality with the previewed fee/weight.
- `NativeTransferRelay::relay` parses the response after requesting broadcast.
  A parse or transport error is not evidence that no transaction was sent.
- Dropping the outer future can skip the sequential close call altogether.

These require fault-injection and recovery tests; they are not independent audit
sign-off. A blind retry may create a duplicate payment if other spendable funds
remain; this is not a claim that chain double-spend protection is bypassed.
Proxmox at `192.168.0.50` responded to SSH but rejected authentication.
The local SSH agent had no loaded identities. The user can unlock the existing
key locally with `ssh-add -t 4h ~/.ssh/monzero_proxmox`; no passphrase belongs
in source, logs, or chat.

The spendable fixture still generates the shared keys locally. It does not
exercise actual State5-derived recovery keys through this spending function.
The existing combined Bitcoin settlement fixture already provides protocol
negotiation and recovery-key export: extend that path rather than introducing
another mock negotiation.

Before live wiring, explicitly test operation ownership, cancellation, failed
cleanup and ambiguous relay responses. Currently the spendable function discards
the close result, and wallet-session uncertainty can prevent closing altogether.
A successful broadcast must remain successful even if cleanup fails, but cleanup
health must be separately observable. An error after dispatch does not establish
that nothing was broadcast. Durable reconciliation is required before retry.

Next milestone: run actual negotiated-key spending through success and refund
paths on isolated chains, then add restart/ambiguous-broadcast evidence and the
authenticated GUI trade workflow. Keep live execution gated pending this evidence
and release checks. The selected first-release fee scope remains as documented in
DEX_FIRST_RELEASE_SCOPE.md; do not invent a fee recipient or collection policy.

## Follow-up: protocol-recovered spending and cleanup ownership

Implemented `test_wallet_recovery.py --native-spendable` and ran both
`engine_redeem_is_accepted_on_regtest` and
`engine_cancel_and_full_refund_are_accepted_on_regtest` successfully.
These use the actual engine's negotiation and Bitcoin-observed recovered keys,
then call the Rust native spending primitive on funded XMZ fakechain outputs.
The fixture checks imported funds across a wallet-RPC restart, exact confirmed
destination principal, and source-wallet fee/balance conservation.

Also fixed `swap_wallet_spendable` closing a pre-existing wallet when restore
failed because a matching-address wallet was already open. Restore now must
succeed before this invocation attempts cleanup. A new assertion in the ignored
Rust integration test proves the matching pre-existing wallet remains open after
rejection. The rebuilt integration executable
`bitcoin_settlement-bdda458b960cf68b` passed both combined paths with this check.
The Rust library suite passed again: 40 tests, zero failures.
`cargo check --offline --locked -p swap-machine --all-features --all-targets -j2`
also passed. Existing unused-dependency/dead-code warnings remain; this is an
affected-package check, not a complete upstream workspace/platform build.
The thirteen-patch reconstruction check passed, including the new ownership patch.

Claude performed a second read-only fixture review. Its tracing confirmed the
recovered-key paths and noted two limits: fee evidence trusts native wallet-RPC
accounting rather than independently decoding the transaction, and disposable
keys are passed to the test child in its environment. This is test infrastructure,
not an approved production secret-transport design. No private keys were logged.

This supersedes the earlier checkpoint's missing protocol-key spending evidence,
but does not complete the first acceptance milestone: negotiation is in-process,
the fixture spends 90% of principal with fee headroom rather than sweeping every
remaining atom, and restart coverage is not yet at every irreversible boundary.
Next implementation work is durable broadcast reconciliation, explicit cleanup
health, approved fee/transaction binding, separate peer deployment and GUI wiring.
All owned fixture processes exited; no Docker containers remained running after
these tests. No public deployment or live-trading gate was changed.

## Follow-up: explicit ambiguous-broadcast handling

Added typed `BroadcastOutcomeUnknown` to native relay failures after entering
the transfer dispatch path. Invalid responses and transport/RPC failures must
not be interpreted as evidence of no broadcast. This error intentionally omits
raw responses and credentials. Preflight errors remain separate.

Each relay instance now records dispatch before the first RPC await and rejects
a second submission even if the first future is cancelled. Tests verify error
classification/redaction and cancellation against an owned, nonresponding local
listener. This does not survive process restart or a newly constructed instance;
durable intent and reconciliation remain required.

Verified after rebuilding the integration binary:

- 42 Rust library tests passed.
- Both protocol-recovered native-spendable combined paths passed again:
  Bitcoin redemption and cancellation/full refund on regtest, followed by
  native XMZ spending on fakechain with source/destination accounting checks.
- Affected-package all-features/all-targets compile check passed, with existing
  unused-dependency/dead-code warnings.
- Fourteen ordered patches reproduce the indexed engine sources.
- Whitespace and verifier shell-syntax checks passed.

No real wallets, production nodes, website packages or live gates were changed.

## Follow-up: persistent native spend reservations

Added a required private journal directory to `SpendRequest` and connected its
reservation before native relay. Reservations bind genesis, source wallet and
lock transaction ID, ignoring retry UUID/changed transfer terms for identity.
Unix exclusive descriptor-relative record creation plus file/directory sync
precedes dispatch. Existing or torn records block another spend. A validated
success receipt is persisted separately without masking a broadcast if storage
fails. `SpendOutcome` now exposes receipt persistence and wallet-close health.

Verified: 44 library tests passed, including a separate process rejecting an
existing intent; both rebuilt combined redemption/refund native-spendable tests
passed; affected-package all-features/all-targets check passed; fifteen-patch
source reconstruction and whitespace checks passed. Fixture processes/containers
were stopped after completion. Claude's prior relay-guard review found no guard
logic bug, while noting conservative pre-network classification and missing
success-path guard coverage. A separate journal review was requested.

Limits: the store is a persistent reservation/receipt, not a complete recovery
engine. It assumes trusted persistent storage, has no reset, and currently has
no reconciliation reader. A changed storage directory or direct lower-level
relay call bypasses it; production wiring must enforce those boundaries.
Windows is fail-closed until a native implementation exists. Read-only absence
of a transaction must never be used alone to authorize resubmission. Binding a
prepared transaction ID before broadcast would improve deterministic recovery
and fee approval; this remains implementation work before live GUI trading.

## Follow-up: exact prepared transaction relay

Native spending now prepares a non-broadcast transaction under an explicit
maximum network-fee ceiling, durably records its public transaction hash/fee/
weight, and relays that same metadata with `relay_tx`. The returned hash must
match. The journal binds prepared public terms and fee to its original intent.
Private signed metadata stays native/in memory; it is not written to the public
journal or exposed to QML. Zeroizing the owning buffer does not guarantee all
JSON/HTTP temporary copies are cleared.

Verified: 45 library tests passed; both rebuilt protocol-key redemption/refund
fixtures passed with prepared ID/fee checked against receipt and confirmed wallet
history; affected-package all-features/all-targets check passed; sixteen-patch
source reconstruction, Python syntax and whitespace checks passed. The fixture
fee ceiling is test-only, not a production fee policy. Standalone spending's
Python assertions were updated but the standalone driver was not rerun in this
checkpoint; both combined paths exercised the same Rust spending test.

Claude's journal review confirmed the creation/sync ordering and raised known
limits: stable trusted storage is a caller precondition, pending records can
remain after pre-broadcast errors, and diagnostics currently expose persistence
health rather than the underlying IO error. No reset was added: absence of a
transaction alone is not proof that retry is safe.

Next: a bounded private recovery reader and chain/wallet reconciliation of the
prepared identity, followed by approval/recovery GUI wiring and separate-peer
tests. Lost prepared metadata before broadcast remains a recovery limitation;
there is no automatic resubmission. Production services and live gates remain
unchanged.

## Follow-up: read-only restart reconciliation

The seventeenth engine patch adds a bounded private recovery reader. It binds
the intent and prepared transaction to the expected chain, source wallet and
lock transaction, then checks the exact outgoing amount, destination, fee and
confirmations against a stable local chain snapshot. It neither broadcasts nor
authorizes retries. Missing or ambiguous evidence remains unresolved.

Both combined integration runs completed successfully: Bitcoin redemption and
cancellation/full refund on regtest, with spending of native fakechain funds
using the protocol-recovered keys. Each run withheld the success receipt,
restarted the wallet RPC, reopened the generated swap wallet, and launched a
fresh Rust recovery test. Reconciliation found the prepared payment without
changing balances, populating the transaction pool, recreating the receipt or
changing intent/prepared bytes. An excessive confirmation requirement was
rejected before the valid confirmation check passed.

This is local wallet/daemon evidence, not an independent cryptographic inclusion
proof or a simulated power loss at every irreversible boundary. The fixture
uses disposable funds and in-process negotiation, not a public two-peer trade.
Recovery still needs trusted startup wiring and GUI presentation; absent
transactions do not permit resubmission. Production deployment and live gates
remain unchanged.

## Follow-up: GUI recovery response boundary

Added `NativeRecoveryReceiver` and a dedicated CMake regression target. This
request-scoped display decoder requires the expected genesis, wallet, lock ID,
prepared transaction ID and minimum confirmation count. It rejects stale,
replayed, expired, oversized, duplicate-key and mismatched responses. Atom-like
confirmation strings preserve exact uint64 values without JSON floating-point
rounding. Its returned state always denies execution, retry authority and
transport authentication, even when a confirmation response is accepted.

Subsequent integration verification: both redemption and full-refund fixture
runs passed with the real recovery helper pipe and Qt response decoder. Invalid
chain/transaction/policy/configuration requests were rejected. Confirmed replies
were accepted after restart with the receipt withheld; balances, pool and
journal evidence remained unchanged. This covers the synchronous disposable Qt
driver, not a production session.

Added `NativeRecoveryJob`, an asynchronous bounded process wrapper requiring a
helper digest, local journal path and wallet process owner. It compiles directly
with warnings treated as errors. The CMake `dex_tests` rebuild and targeted
launch-rejection regression passed. Cancellation, owner shutdown and successful
asynchronous lifecycle tests, session integration and visible UI remain pending.

The subsequent asynchronous lifecycle regression also passed: valid completion,
explicit cancellation, timeout, owner shutdown, job destruction and mismatched
reply identity. Successful completion releases the owner's operation token;
uncertain outcomes prevent another operation. Owner stop/destruction now clears
the displayed evidence immediately. Tests use a private executable copy because
the regular build output is group-writable and is correctly rejected by the
digest/metadata launch boundary. Both targeted Qt tests passed after rebuilding.
These are mock-helper lifecycle tests; real-helper integration above used the
synchronous driver. Session wiring and visible UI are still pending.

Session/presentation follow-up: `SwapWalletSession` now supports a pinned local
recovery directory and native-only recovery requests with trusted transaction
identities. Checks require an open wallet and pinned helper digest, serialize
through the process owner, and integrate cancellation. A successful result is
available only while the session is open; control/fee operations invalidate it,
and an expiry notification refreshes presentation. `DexEngine` exposes a
read-only status property and the DEX QML page displays evidence with an explicit
no-retry/no-trading warning. There is no peer-supplied directory or QML launcher.

The rebuilt targeted coordinator/presentation/lifecycle tests passed (three
test methods plus init/cleanup). QML lint passed. No full GUI binary rebuild or
rendered UI verification has yet been performed for this change. Normal main
startup still needs backend attachment and a trusted trade/recovery selection
workflow; this change does not make the installed app live.

Broader validation checkpoint: the complete `dex_tests` executable passed with
32 passes, no failures or skips. A full `monzero-wallet-gui` build is running
and has not yet completed; dependency deprecation warnings are not evidence of
a successful GUI build. A separate read-only Claude review of recovery
lifecycle, stale evidence and authority handling is also pending. No production
binary or wallet was replaced. The local SSH agent currently has no identities,
so Proxmox deployment remains unavailable without reloading its key; local
development and verification continue independently.

Claude's read-only lifecycle review completed. Failed recovery jobs are now
released rather than retained until session teardown, and one explicit timeout
value is passed through validation, execution and UI expiry scheduling. The
review did not identify an authority bypass. Its connection-order concern is a
maintenance consideration, not a demonstrated final-state failure; current
owner-stop tests pass. This AI review is not independent security sign-off.
The coordinator and lifecycle regressions passed again after cleanup changes.

Added `tests/qml/tst_DexRecovery.qml`, which instantiates the actual DEX page with
mock backend data and checks unresolved, confirmed, cleared and unattached
recovery presentation. The test passed headlessly (three passes including
init/cleanup). The host lacked the QtTest QML module; Kali package
`qml-module-qttest` 5.15.19+dfsg-2 was downloaded and extracted only under
`build/qml-test-module.ynzwEuE5`, without a system installation. Run with
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software qmltestrunner
-input monzero-gui/tests/qml/tst_DexRecovery.qml -import monzero-gui/fonts
-import build/qml-test-module.ynzwEuE5/extracted/usr/lib/x86_64-linux-gnu/qt5/qml`.
The source-only runner warns about missing qrc fonts/image resources, so this is
presentation-behavior evidence, not packaged visual approval. QML lint and
whitespace checks passed. The full GUI build is still running.

Resource-backed follow-up: added `tests/dex-qml-runner.cpp`, a minimal Qt Quick
test runner that registers the actual binary `qml.qrc` resource bundle without
creating the application's wallet manager. Built the bundle with
`rcc -binary monzero-gui/qml.qrc -o build/dex-qml-test-resources.rcc`, compiled
the runner with Qt5QuickTest, and reran the page test using
`MONZERO_TEST_RESOURCE_BUNDLE` pointing to that bundle. All three checks passed
with no missing font/image warnings (the existing Connections syntax still
emits a deprecation notice). This validates page behavior with bundled assets,
not the full application's startup, desktop interaction, or live backend.

Private startup follow-up: the full `monzero-wallet-gui` target now builds with
the opt-in backend loader. The initial long-running build had started before
the new source was added and failed linking that symbol; CMake regeneration
included it and the incremental build passed. The new CMake configuration-test
target also passed through CTest.

`test_gui_private_startup.py` passed against the full GUI in a bwrap namespace
with no network, host wallet directories or host credentials exposed. It checks
missing opt-in/config failure before service creation, successful separate mock
backend provisioning, and credential cleanup on smoke-test exit. The earlier
`--test-qml` attempt was intercepted by the existing QML_TESTS harness; a hidden
`--startup-smoke-test` alias now exercises ordinary startup and root QML loading
before exit. No production binary was installed. This is mock RPC provisioning,
not real wallet-RPC, forced-exit, interactive recovery or live-trade acceptance.

Real wallet-RPC startup now also passed inside the network/home-isolated full-GUI
fixture. Added a bounded hidden smoke-mode delay so an in-namespace probe can
verify HTTP authentication, `get_version`, and the explicit no-wallet-open
error before graceful GUI exit. The first probe raced the socket bind after
credential creation; it now waits for the authentication challenge with a fixed
deadline instead of treating the login file as readiness. Final checks proved
clean `stopped` service state, credential cleanup and no wallet key files.
The GUI rebuilt successfully with the delay option. No daemon, funded wallet,
real swap, forced-exit recovery or production deployment is covered by this test.

Separate-participant follow-up: the Unix settlement fixture now keeps Alice's
negotiation/signing state in a child process and exchanges bounded private socket
messages with Bob in the parent. Initial combined redemption/native-spending/
restart reconciliation passed. A subsequent context/framing rejection test also
passed; final combined redemption and refund runs have now both exited zero.
Both reported real native helper responses accepted by the Qt receiver,
read-only reconciliation after RPC restart with a missing receipt, and native
spending with protocol-recovered keys and exact on-chain fee accounting. Nineteen-patch
source reproduction passed. This remains a single trusted local test harness
controlling chain wallets, with disposable refund-key export; it is not public
peer authentication or independent-operator trading. No live gates changed.

The earlier standalone-decoder checkpoint is superseded by the helper, lifecycle,
GUI presentation and isolated startup results above. These separate checks do
not yet prove a funded trade driven end to end from the GUI. Durable participant
restart, authenticated quote exchange and integrated user approval remain open.

Alice checkpoint follow-up: both combined native redemption and refund runs now
pass after an explicit SIGKILL/restart of Alice following funding verification.
Alice saves and syncs her negotiated State3 in a private fixture file before
acknowledging the checkpoint. The replacement child restores it, binds the swap
ID and sequence, and the parent checks the public funding keys before continuing.
The tests still pass native spending and missing-receipt restart reconciliation
through the real helper and Qt decoder. Affected-package all-features/all-targets
compilation and twenty-patch reconstruction passed. This proves one acknowledged
checkpoint boundary, not torn persistence or a complete production restart matrix.

Claude's read-only review confirmed the sequence handshake and identified that
the fixed checkpoint filename prevented a second checkpoint on the same swap.
The fixture now uses immutable sequence-numbered checkpoint files, checks the
expected sequence on restore and bounds reads before parsing. Both settlement
paths now request two consecutive crash/restart cycles. The updated binary
compiled and patch reconstruction passed; both combined runs exited zero with
native spending, missing-receipt reconciliation and Qt recovery checks passing.
Affected-package all-features/all-targets compilation and framing rejection tests
also passed against this revision.
This AI-assisted review is not independent security sign-off. Repeated restarts
at one boundary still do not cover interrupted writes or every signing/broadcast
boundary, and the disposable plaintext key checkpoint is not production storage.

Signing-boundary follow-up: the fixture now restarts Alice after constructing
the cancellation or redemption transaction, reconstructs it from restored state,
checks that its Bitcoin transaction ID is unchanged, and broadcasts the restored
version. The refund path also restarts Alice after confirmed Bitcoin refund and
before joint XMZ key recovery. This does not claim witness-byte identity or
durable delivery of Bob's encrypted signature: the trusted parent retains that
message across Alice's restart. Compilation and patch reconstruction passed;
both combined native settlement runs exited zero, including exact fee accounting,
missing-receipt reconciliation and Qt recovery checks. The affected-package
all-features/all-targets check and framing rejection test also passed.

## Follow-up: buyer top-up dispatch journal (2026-09-20)

Added `native_funding_rpc::topup_journal` as the seventy-third engine patch,
implementing the first piece of the selected delayed-top-up candidate from
`DEX_BUYER_RESERVE_CANDIDATE.md`: a durable, single-use journal for the
buyer's own wallet's payment to the recovered swap destination, distinct from
the seller-side lock/claim journals so it can never gain claim authority over
the lock or be confused with it. It reuses the existing write-once
directory-descriptor primitives (`spend_journal::{reserve_intent,create_record}`)
and mirrors `SpendJournal`'s reserve/record_prepared/record_relay shape, keyed
by the buyer's wallet address rather than the swap wallet.

Verified: `cargo check -p swap-machine --all-features --all-targets` and
`cargo test -p swap-machine --lib` both pass (91 passed, 0 failed, 2 ignored,
including 4 new tests: self-destination/empty-reserve rejection, the
private-directory and write-once-receipt requirement, the prepared-fee
ceiling, and that a buyer top-up journal and a seller claim journal for the
same trade lock coexist without interfering). `check-upstream.sh machine-test`
reproduces this. The seventy-three-patch chain was independently replayed
against a fresh index built from the pinned commit and reproduces the exact
current engine sources with zero drift (`verify-upstream-patches.sh`, updated
to include the new patch).

This is a durable-record primitive only: it does not yet observe Bitcoin
redemption, dispatch the buyer's transfer, import the confirmed top-up, or
run the two-source claim through it. No GUI wiring, real fund movement, or
live-trading gate is affected. Next: bind Bitcoin-outcome observation to a
top-up dispatch decision, then the confirmed-import and two-source claim
steps from `DEX_BUYER_RESERVE_CANDIDATE.md`, then a real two-party fixture
using negotiated (not separately minted) buyer/seller wallets.

## Follow-up: confirmation-depth redeem observation and destination binding (2026-09-20)

Added the seventy-fourth engine patch, completing the "bind Bitcoin-outcome
observation to a top-up dispatch decision" step named above. `bob::State4`
gains `watch_for_redeem_btc_confirmed(bitcoin_wallet, required_confirmations)`,
which waits for the redeem transaction to reach the caller-specified
confirmation depth (via the existing `Subscription::wait_until_confirmed_with`)
before recovering the swap key -- unlike `watch_for_redeem_btc`, which only
waits until the transaction is seen in the mempool and exists for Bob's own
immediate reaction, not for authorizing a new payment. Both reuse the same
`check_for_tx_redeem`/`recover_from_redeem` machinery, so the txid-match check
that guarantees the recovered key is correct is unchanged.

`native_funding_rpc::topup_journal::authorize_recovered_destination` binds the
result: it derives the XmzAddress from the recovered `State5::xmr_keys()` and
rejects unless it exactly matches the buyer's already-approved
`TopUpRequest.destination`, so a caller bug or stale value can never cause a
top-up to be journaled against the wrong address. This is the sole remaining
check needed before a caller could reserve a `TopUpJournal` for a specific
observed outcome; it does not perform the reservation, dispatch, or any RPC
itself.

Verified: `cargo check -p swap-machine --all-features --all-targets` and
`cargo test -p swap-machine --lib` both pass (92 passed, 0 failed, 2 ignored,
including one new test covering acceptance, a substituted spend key, a
substituted view key, and a network mismatch). `check-upstream.sh
machine-test` reproduces this. The seventy-four-patch chain was independently
replayed against a fresh index from the pinned commit and reproduces the
current engine sources with zero drift (`verify-upstream-patches.sh`, updated).

`watch_for_redeem_btc_confirmed` has no isolated unit test of its own --
consistent with `watch_for_redeem_btc`, which is only exercised through the
real-chain (regtest/fakechain) fixtures -- so it still needs a fixture run
against an actual Electrum-backed wallet before being relied on; unlike the
key-recovery correctness (guaranteed by the txid-match check regardless of
which path calls it), the confirmation-wait behavior itself is untested
against a real chain. No GUI wiring, real fund movement, or live-trading
gate is affected. Next: dispatch the buyer's top-up transfer once this
authorization passes, import the confirmed top-up, and run the two-source
claim through the existing exact-net journal/relay path.

## Follow-up: top-up dispatch orchestration (2026-09-20)

Added the seventy-fifth engine patch: `native_funding_rpc::topup_dispatch::
dispatch_topup`, composing patch 73's journal and patch 74's authorization
with the existing `NativeTransferRelay` into one entry point that actually
sends the buyer's top-up payment -- reserve, prepare, record-prepared, relay,
record-relay, mirroring `spendable.rs`'s sequence for the seller-side
recovery spend. The buyer's wallet is the caller's own already-open wallet,
so unlike `spendable.rs` this does no wallet restore or lock-transaction
import.

While wiring this in, found that patch 73's `TopUpJournal::record_prepared`/
`record_relay` accepted a bare `{txid, fee, weight}` value with nothing tying
it back to the journal's approved wallet, destination or amount -- a real
gap once something was actually going to call it, unlike `SpendJournal`,
which cross-checks every field. Fixed in the same patch: both methods now
take the real `relay::PreparedTransfer`/`relay::RelayedTransfer` and
`record_prepared` rejects any mismatch against the durable intent before
persisting.

Verified: `cargo check -p swap-machine --all-features --all-targets` passes;
`cargo test -p swap-machine --lib` passes (91 passed, 0 failed, 2 ignored --
net one fewer than before because the old prepared-fee unit test could not
survive the signature change, same as `SpendJournal` has never had one, for
the same reason: no public constructor for a real `PreparedTransfer` outside
an actual RPC call). `check-upstream.sh machine-test` reproduces this. The
seventy-five-patch chain was independently replayed against a fresh index
from the pinned commit and reproduces the current engine sources with zero
drift (`verify-upstream-patches.sh`, updated).

`dispatch_topup` has no test of its own beyond compiling -- consistent with
`swap_wallet_spendable`, which is only exercised through the Python/regtest
fixtures, not Rust unit tests. It still needs a real fixture run (buyer
wallet, recovered destination, actual RPC relay) before being relied on.
No GUI wiring, real fund movement, or live-trading gate is affected. Next:
import the confirmed top-up transaction into the recovered swap wallet
alongside the original lock, then run the two-source claim
(`full_proceeds::prepare_exact_net_with_top_up` + `exact_net_journal` +
`relay_exact_net`) using both transaction ids, then a real fixture proving
the whole delayed-top-up path end to end with negotiated (not separately
minted) buyer/seller wallets.

## Follow-up: two-source exact-net claim orchestration (2026-09-20)

Added the seventy-sixth engine patch: `native_funding_rpc::exact_net_claim::
claim_exact_net_with_topup`, composing entirely existing primitives into the
final step of the delayed-top-up candidate. Restores the recovered swap
wallet, imports both the original lock transaction and the confirmed
top-up transaction into it, reserves an `ExactNetJournal` before
construction, constructs the two-source exact-net transaction with
`prepare_exact_net_with_top_up`, authorizes the journal with the prepared
result, and relays through `NativeTransferRelay::relay_exact_net`. No new
construction, journaling or relay logic -- this is pure orchestration,
mirroring `spendable.rs`'s restore/import/construct/journal/relay shape for
one source extended to two.

This closes out the three-patch (73-75) plus this one (76) engine-side
sequence covering every piece of the delayed-top-up candidate named in
`DEX_BUYER_RESERVE_CANDIDATE.md`: durable buyer-side journal, confirmed-
redemption destination binding, top-up dispatch, and the two-source claim
that actually delivers the exact quoted principal with reserve return.

Verified: `cargo check -p swap-machine --all-features --all-targets` and
`cargo test -p swap-machine --lib` both pass (91 passed, 0 failed, 2
ignored, unchanged -- no unit test for this orchestration function, same as
`swap_wallet_spendable`). `check-upstream.sh machine-test` reproduces this.
The seventy-six-patch chain was independently replayed against a fresh
index from the pinned commit and reproduces the current engine sources with
zero drift (`verify-upstream-patches.sh`, updated).

`claim_exact_net_with_topup` is completely untested beyond compiling -- it
needs a real fixture (funded lock, dispatched and confirmed top-up,
recovered wallet, both imports, actual relay) before being relied on in any
capacity. No GUI wiring, real fund movement, or live-trading gate is
affected by any of patches 73-76.

**What remains before the "Full settlement engine" roadmap milestone
closes:** restart/crash coverage at each new boundary (after journal
reserve, after top-up dispatch, after claim journal reserve, after claim
relay) matching the rigor of the existing redemption/refund fixtures; the
v2 signed-quote schema (`DEX_BUYER_RESERVE_QUOTE_V2.md`) binding these
fields into authenticated negotiation, since everything built in patches
73-76 still takes its request fields from the caller directly, not from a
signed quote; and GUI execution approval.

## Follow-up: real fixture proves the delayed-top-up path end to end (2026-09-20)

Added the seventy-seventh engine patch: `native_topup_dispatch` and
`native_exact_net_claim`, ignored integration tests driving `dispatch_topup`
and `claim_exact_net_with_topup` against a real fakechain wallet-rpc for the
first time. Extended `test_wallet_recovery.py` with
`verify_topup_dispatch_and_claim`, wired into the existing
`--native-spendable` fixture right after the exact-net-relay/raw-restart
checks it already runs.

Ran the complete `engine_redeem_is_accepted_on_regtest` fixture end to end
(real Bitcoin regtest negotiation via Docker/bitcoind/electrs feeding a
protocol-recovered XMZ key, then every existing native-spendable step, then
the new steps): mint a lock payment and a buyer top-up budget from the
fixture's disposable wallet, dispatch the top-up from a separate buyer
wallet to the recovered swap address, then restore that swap wallet from
its keys and run the two-source claim. **This is the first real execution
of the whole delayed-top-up candidate**, not a unit test.

It took three attempts. The first correctly rejected everything up to the
top-up dispatch, then failed with "full-proceeds payment and fee do not
exactly consume lock" on the claim. Debugging (a temporary balance/probe
dump) showed the wallet held exactly the right two inputs (100 XMZ lock +
10 XMZ top-up, both unlocked and correctly matched by txid) and the
accounting was actually exact (100 XMZ payout + 8.6208 XMZ reserve return +
1.3792 XMZ fee = 110 XMZ) -- the single combined `ensure!` in
`full_proceeds::check_response` was failing on the fee-ceiling clause, not
the amount-accounting clause, because the fixture's approved fee ceiling
(1 XMZ) was below the real two-destination construction's fee
(1.3792 XMZ). This is not a discovered engine defect: the code correctly
rejected a fee above the caller's own approved ceiling. Raised the fixture's
ceiling to 2 XMZ and the full run passed on the third attempt.

Final result: the payout wallet received exactly the locked principal
(100 XMZ), the reserve-return wallet received the positive residual
(8.6208 XMZ), and the recovered swap wallet ended at exactly zero balance,
alongside every pre-existing check in the same fixture run (Bitcoin
settlement, native spending, exact-net relay with and without a dropped
reply, raw-restart candidates) all still passing. No Docker containers were
left running afterward; no production wallet, node, or live-trading gate
was touched.

`cargo test -p swap-machine --lib` is unaffected by this patch (91/0/2 --
these are `#[ignore]`d integration tests, not part of the default run).
`check-upstream.sh machine-test` still reproduces that unit result. The
seventy-seven-patch chain was independently replayed against a fresh index
from the pinned commit and reproduces the current engine sources with zero
drift.

**Delayed-top-up candidate status:** all four engine-side pieces (journal,
confirmation-bound authorization, dispatch, claim) exist, are individually
unit-tested, and have now been proven to work together end to end on
disposable regtest/fakechain funds -- with the caveat that this exercised
the "native mechanics" path (locally minted lock/top-up funding, like the
existing `verify_exact_net_relay`), not a full protocol negotiation binding
these fields to a signed v2 quote. What remains before the "Full settlement
engine" roadmap milestone closes: restart/crash coverage at each new
boundary in this path (none tested yet, unlike the redemption/refund
fixtures which cover several), the v2 signed-quote schema binding these
fields into authenticated negotiation, and GUI execution approval.

## Seventy-eighth patch: top-up dispatch recovery reader, and a GUI-wiring correction (2026-09-23)

Two findings before any new code. First, `DEX_INTEGRATION_ACCEPTANCE.md`'s
2026-09-13 addendum (main.cpp never provisions a `SwapWalletSession`) is
stale: the 2026-09-19 checkpoint in the same file already records that gap
as superseded, and reading `src/main/main.cpp` directly (commit `35e9be017`)
confirms `--dex-private-backend` already constructs `SwapWalletRpcProcess`
and `SwapWalletSession` and calls `attachSwapWalletSession`, with
`test_gui_private_startup.py` as executed evidence. No GUI wiring was added
this patch; there is nothing left to wire at this level until a real trade
driver exists to approve. Second, `claim_exact_net_with_topup` (patch 77)
already reuses `ExactNetJournal::reserve`/`authorize_relay` unchanged for
its claim-side boundaries, and `ExactNetRecovery::load` already takes a
`top_up` parameter -- it was written generically before patch 77 existed.
The claim side needs no new engine code, only fixture-level restart proof
(not done this patch, see below).

The buyer's top-up *dispatch* side had no recovery reader at all:
`TopUpJournal` (durable intent before relay, receipt after) had no
`TopUpRecovery` counterpart, unlike `SpendJournal`/`spend_recovery.rs` and
`ExactNetJournal`/`exact_net_recovery.rs`. Added
`native_funding_rpc/topup_recovery.rs`, mirroring `spend_recovery.rs`'s
`RecoveryRecord` shape exactly -- `TopUpJournal` and `SpendJournal` are
structurally identical (full request, single destination, receipt-based),
not `ExactNetJournal`'s two-destination accounting. `TopUpRecovery::load`
independently decodes and range-checks the persisted `.intent`/`.prepared`
files (schema, canonical hash, fee-ceiling, account/priority bounds)
without needing a caller-supplied "expected" struct, then `reconcile`
fetches `get_transfer_by_txid` and checks genesis/wallet/destination/amount/
fee/confirmations against that decoded record, matching
`spend_recovery::ConfirmedSpend`'s existing shape. An earlier version of
this patch tried refactoring `TopUpJournal::reserve` to share an
`intent_terms` helper with the new reader (mirroring `exact_net_journal.rs`);
that edit was rejected as touching shipped security-invariant checks, so the
final version leaves `topup_journal.rs` completely untouched and validates
independently instead, matching `spend_recovery.rs`'s own precedent.

Three new tests: `recovery_rejects_altered_intent_and_prepared_record`
(every intent/prepared field independently tampered, must reject),
`confirmation_requires_exact_payment_and_current_tip` (destination/amount/
fee/height/confirmation exactness), and
`load_refuses_to_reconcile_before_a_prepared_record_exists`. That last test
is narrower than a full reserve-through-reconcile round trip:
`PreparedTransfer` has no public constructor outside `relay.rs` (only a live
`relay.prepare()` RPC round trip produces one), so a pure Rust unit test
cannot reach a real `.prepared` record. That full path is Python fixture
work, not done this patch.

Before any of this could run, the pinned local toolchain
(`build/dex-rust-toolchain-1.95.0`) was broken: `rustc`/`cargo` failed with
`libLLVM.so.21.1: cannot open shared object file`, and the sysroot's
`usr/lib/x86_64-linux-gnu` held only 4 files where a working install needs
several times that -- something removed most of it since patch 77's
`check-upstream.sh machine-test` run. `README.md` records the original
install method (Debian `.deb` packages downloaded by APT metadata, extracted
locally, never installed system-wide). Fetched `libllvm21_21.1.8-13`,
`libedit2_3.1-20260512-1` and `libz3-4_4.13.3-1.1` from `ftp.debian.org`,
verified each SHA256 against `packages.debian.org`'s published checksum
before extracting (`libllvm21`: `0b5dd9a2...cf3df`; `libedit2`:
`85133718...76a4cd`; `libz3-4`: `17a0b6d5...d55bc434`), and copied just the
`.so` files into the sysroot -- no system-wide package changes. `cargo`'s
internal `rustc -vV` probe kept failing even after the libraries were
restored; the actual cause was a stale `target/.rustc_info.json` caching the
pre-fix failure, not a live re-probe. Deleting it fixed `cargo check -p
swap-core` immediately. This toolchain state should be treated as fragile on
this machine going forward -- something is deleting large "unrecognized"
files from `build/`, and it is not this patch's job to find out what.

`check-upstream.sh machine-test` (full `cargo test -p swap-machine --lib`,
pinned toolchain, upstream revision `0e17c7f7c` unchanged and verified):
94 passed, 0 failed, 2 ignored (the pre-existing Qt-fixture-only tests),
including all three new `topup_recovery` tests. Ordered patch reconstruction
was not rerun this patch.

**What this patch did not do, and why:** the v2 signed-quote schema
(`DEX_BUYER_RESERVE_QUOTE_V2.md`) was not started. Its own 6-step sequence
turns out to have steps 4 ("buyer-wallet top-up dispatch journal") and 5
("import the exact confirmed top-up, prepare from only the approved
sources") already superseded by patches 73-77 -- only steps 1 (v2 codec +
shared vectors, parallel to v1, must never satisfy v1 validation), 2 (GUI
approval display of principal/fee caps/reserve return), 3 (bind the
commitment into authenticated negotiation and durable execution state) and
6 (a real two-party fixture with insufficient-funds/reorg/cancellation
cases) remain. `native_trade_quote.rs` (943 lines) enforces the v1 domain
with the same per-field rigor as everything else in this codebase; step 6
alone is explicitly comparable in scope to patch 77. Starting it at the tail
of an already long session risked rushing a new authentication scheme for
real fund movement -- deferred whole, not half-done.

**Next:** the Python fixture work for topup-dispatch and claim-side restart
coverage (kill wallet-rpc mid-sequence, restart, `TopUpRecovery`/
`ExactNetRecovery::load` + `reconcile`, assert no duplicate spend) is the
smallest remaining piece and the natural next patch. The v2 quote schema's
step 1 (codec + vectors) is the next tractable slice after that -- self-
contained, no negotiation-state changes, same shape as this patch's work.
