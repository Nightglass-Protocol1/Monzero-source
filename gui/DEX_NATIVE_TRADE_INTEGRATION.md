# Native BTC/XMZ trade integration boundary

Current source inspection: `PrivateQuoteSession` and `SignedQuoteAcceptor`
consume `SettlementTerms::Terms`. Its V7 format requires a positive protocol fee
and an XMZ fee lock/refund commitment pair. It is not the approved zero-app-fee
native engine milestone. Do not connect that acceptor to the adapted engine by
zeroing those fields, retaining signatures or weakening the V7 decoder.

The next integration needs a separately versioned native BTC/XMZ quote type and
signature domain. Preserve V7 recovery interpretation. New quote acceptance
must not imply engine funding authorization merely because signatures verify.

Required bindings before engine entry:

- Both participant identities and explicit BTC-funder/XMZ-funder roles.
- Quote ID, swap ID, expiry and a durable single-use acceptance record.
- Both genesis identities and deployment policies, BTC satoshis and XMZ atoms.
- Zero application fee, with separate explicit caps for native chain fees.
- Payout/refund destinations, confirmation policies and Bitcoin relative
  cancellation/punishment windows selected by the engine configuration.
- A second authenticated negotiation transcript binding the actual engine keys,
  cross-curve proofs and transaction templates to the accepted quote. Initial
  commercial terms cannot attest templates that have not yet been negotiated.

The GUI must display these terms and obtain approval for the exact immutable
commitment. Native code must verify the commitment and fresh chain evidence
before each irreversible operation; a QML boolean is not signing authority.
Persist prepared actions before broadcast and reconcile uncertain outcomes.

Existing `SwapWalletSession` coordinates exclusive native wallet access but has
no trade driver. `DexEngine` attachment is presentation only; signing, publishing,
validation and matching remain disabled. The private startup config does not
open a wallet. These boundaries must not be bypassed to make the page look live.

Acceptance requires two disposable participants exchanging the new quotes,
rejecting altered/expired/replayed or wrong-role terms, and completing settlement
and refund through the same GUI-connected driver. The standalone engine fixture
uses fixed terms and does not provide this evidence. Current restart tests also
retain Bob's encrypted signature in the parent; durable peer-message recovery
remains unverified.

## Initial commercial-term codec

`NativeTradeQuote` now provides a separate canonical JSON encoding and
domain-separated commitment for zero-app-fee BTC/XMZ commercial terms. All
numeric values are decimal strings, with positive uint64 amounts, Bitcoin supply
bounds, explicit participant funding roles and bounded expiry (at most one hour
from validation). Decoder output changes only after full canonical validation;
unknown fields, duplicate JSON keys, non-string numbers and trailing data fail.

This is not signature acceptance or trade authority. Destinations are currently
only syntax/length bounded; chain-specific decoding and pinned network policy
are required before acceptance. Participant key encodings likewise need actual
signature verification. `verifySignatures` now checks both participant signatures
under distinct BTC-funder/XMZ-funder domains, requires caller-pinned identities
and leaves output unchanged on failure. It uses the existing OfferIdentity
Ed25519 framing with the new role domains; these signatures cannot be reused
as legacy quote signatures. Success is fresh agreement on commercial terms,
not acceptance into a replay-protected trade. The old V7 path remains unchanged.

`NativeQuoteReplay::reserve` now separately reserves mutually signed terms on
Linux. It requires canonical private owner-controlled storage, locks the directory,
creates the quote record exclusively, syncs its signed artifact, links the swap
ID to that record and syncs the directory before reporting success. Every reused
quote or swap ID is rejected. Partial files remain blocked; this API never deletes
or resets reservations and never authorizes funding. Other platforms fail closed.
Trusted storage and stable ancestors are mandatory; replacing the directory would
discard replay protection.

`recover` now reads historical signed terms without modifying records. It bounds
reads, rejects noncanonical or malformed records, checks private owner-controlled
regular files and the matching quote/swap hardlink pair, and verifies both pinned
participant signatures at the recorded reservation time. Quote expiry does not
prevent this historical read. It does not rearm reservation, establish current
chain state or authorize an action; the recorded local time is trusted journal
metadata, not an independently signed timestamp or proof that a previous caller
received success. Missing/incomplete pairs fail closed and output stays unchanged.

The regression test checks invalid signatures leave no records, repeated IDs
fail even with freshly signed changed terms, independent IDs can reserve, and
a fresh process rejects an already recorded quote. Eight independent processes
released from a pipe barrier also race the same reservation: exactly one succeeds
and only the two expected record names remain. A pre-existing truncated record
rejects retry without altering that record. The full test passed ten consecutive
runs after these additions. This is process-concurrency and partial-record
evidence, not power-loss fault injection. Storage-exhaustion policy and actual
interruption at each filesystem operation remain unverified.
No negotiated transcript, GUI approval or engine execution is connected yet.

## Full application verification

The full `monzero-wallet-gui` target compiled and linked with the new quote and
replay sources. `test_gui_private_startup.py` then passed in a network/home-isolated
namespace against the real wallet-RPC and native helper: authenticated provisioning,
no wallet open and credential cleanup on exit. This proves startup compatibility,
not a GUI-driven trade or public release readiness. No installed package was
replaced and no real wallet was opened. A Claude read-only review of these new
components was requested separately; it is not independent audit sign-off.

The recovery regression now also rejects changed signed amounts, duplicate JSON
keys, oversized records and truncated records without changing caller output or
allowing a new reservation. Normal CMake/CTest and a direct build with
`-Wall -Wextra -Werror -fsanitize=address,undefined` passed after these additions.
The sanitizer run used `ASAN_OPTIONS=detect_leaks=0`; it is not leak-check evidence.
These tests deliberately modify disposable signed-record fixtures only.

Claude's read-only review completed. It found no false-success authorization path
under the documented trusted-storage assumptions and highlighted uncertain final
directory sync, nonblocking contention, network/address policy and cross-chain
timing as integration concerns. A Linux test-only linker wrapper now forces the
final directory fsync to fail: reservation returns false, historical signed terms
can still be read, and replay remains rejected. CMake/CTest passed with this
injection enabled. It simulates a syscall error, not power-loss durability.

The quote uint64 XMZ range is an encoding bound, not a funds/supply assertion.
Core currently defines a nonzero final subsidy, so its `MONEY_SUPPLY` constant
must not casually be treated as Bitcoin-style hard-cap policy. Actual available
funds and deployment limits must be checked by the driver. Likewise BTC block
windows and XMZ confirmations cannot be compared as equal time units: engine
policy must account for both chain clocks and revalidate before funding. Neither
gap is solved by valid commercial-term signatures. AI review is not an audit.

`verifyForDeployment` adds an explicit caller-provisioned policy check on top
of mutual signature verification: both genesis/network identifiers, exact engine
confirmation and cancel/punish settings, and nonzero maximum principal/native-fee
limits must match. Tests independently change each policy field and verify
rejection without changing output. No production policy values are selected or
derived from peer terms. This is not yet connected to startup or the driver;
plain signature reservation remains historical storage, not deployment approval.
Address decoding and justification of the selected cross-chain timing policy
remain required before funding.

The adapted Rust engine now exposes `QuoteDestinations::parse`, using its existing
Bitcoin and native XMZ codecs for all four destinations. Expected networks are
explicit arguments from trusted deployment configuration. Checks include bounded
input, checksum/encoding validity and canonical round-trip; some Bitcoin networks
share legacy prefixes, so genesis checks remain separate. This does not establish
address ownership, output spendability, signatures or funding authority.
All 50 engine library tests passed, including malformed destinations in every
position and wrong-network/noncanonical cases. Twenty-one-patch reconstruction
passed. The decoder is not yet connected to the GUI quote or native helper pipe.

The native helper now has a separate `--quote-destinations-stdio-v1` mode with
explicit `--bitcoin-network` and `--xmz-network` arguments. It performs no RPC
and loads no session credentials. Bounded canonical requests carry all four
destinations, a request UUID and quote commitment; successful replies echo those
bindings with `executable: false`. The commitment is caller correlation, not
independent verification of the signed quote. Invalid input emits no stdout and
only a generic diagnostic. Deadline is ten seconds, input/output limits 2048 bytes.
`test_quote_destinations.py` passed against the rebuilt helper's actual process
pipes, including invalid addresses in every position, wrong networks, duplicate
and unknown fields, oversized input and generic failure diagnostics. All 50
engine library tests, affected-package all-features/all-targets compilation and
twenty-two-patch reconstruction passed. GUI invocation remains unwired.

`NativeDestinationJob` now provides the asynchronous Qt caller with trusted
helper digest/metadata checks, sanitized environment, bounded stdout/stderr,
deadline/cancellation and exact canonical response matching against the request
UUID, quote commitment and all four destinations. Each job is single-use; output
remains non-executable. CMake/CTest passed, and the optional real-helper test
passed for valid destinations, wrong Bitcoin network and cancellation.
The initial integration test correctly rejected the Cargo build artifact's
multiple links/group-writable mode; the fixture now makes a private 0500 copy.
No launch checks were weakened. This caller is not yet attached to a GUI approval
or trade coordinator; deployment-policy/signature checks remain caller obligations.

`NativeQuoteCoordinator` now combines mutual signatures, trusted deployment
policy, the native address job, review of immutable terms and explicit approval
of their commitment. It rechecks freshness/policy after the helper and again
before durable reservation. No reservation is written while merely reviewing;
wrong commitments are rejected. Reserved means commercial terms recorded, not
permission to fund. There is still no engine transcript, funding driver or GUI
attachment. Helper/network policy and identity pins must come from native startup.

The DexEngine bridge now allows a native coordinator attachment and exposes only
its ready review and commitment-bound commercial approval to QML. The DEX page
shows full exact terms as plain text, requires a separate review acknowledgement,
resets it when the quote changes, and labels approval as sending no funds.
The panel is absent without a ready attached coordinator. Resource-backed QML
tests passed for visibility, acknowledgement, exact commitment forwarding and
reset; the full GUI target rebuilt successfully. Startup does not yet construct
this coordinator or provision authenticated incoming native quotes. Thus this
is presentation wiring, not an available live trading workflow.

Post-wiring regression: all 33 `dex_tests` checks passed. The real-helper native
quote test also exercises a review that expires after reaching Ready and a
coordinator cancelled during checking; neither accepts approval or adds replay
records. These checks passed alongside the valid approval/replay cases. Startup
provisioning remains pending; no wallet or production service was changed.

## Opt-in startup quote loading

`--dex-private-quote /absolute/private/config.json` now constructs and attaches
one coordinator, only with the existing private DEX runtime opt-in. It does not
start wallet-RPC, open a wallet, approve terms or enable trading. The source file
must be canonical compact JSON, owner-controlled mode 0600, single-link regular
storage, at most 16384 bytes. Configuration is trusted local input, never a peer
download: it supplies helper/digest, private replay directory, both participant
pins, exact deployment policy, signed terms and both signatures. The loader
validates policy/signatures and the helper before launching address validation.

Schema: `monzero-private-native-quote-v1`; exact top-level fields are `schema`,
`helper`, `helperDigest`, `directory`, `btcPeer`, `xmzPeer`, `policy`, `terms`,
`btcSignature`, `xmzSignature`. Pins/signatures are lowercase hex. Policy fields
match `DeploymentPolicy`, with all numbers encoded as canonical decimal strings.
No default identities, chain policies or automatic approvals are provided.
The test generates disposable signed configuration rather than providing a
reusable live configuration. Full GUI startup with this new option still needs
isolated end-to-end verification; normal backend startup evidence predates it.

Follow-up verification now passed: the full GUI was rebuilt and run in the
home/network-isolated startup fixture. Independently generated disposable Ed25519
signatures (PyNaCl using the exact domain framing) reached the native coordinator's
Ready state and exposed a nonempty review through DexEngine. Missing runtime
opt-in was rejected. A newly signed quote with an invalid BTC destination failed
the native destination check and the smoke-mode readiness assertion. The private
replay directory remained empty throughout; no approval or funding occurred.
The real wallet-RPC provisioning/credential-cleanup regression also passed.
This verifies startup through review readiness, not an interactive user approval
or engine negotiation/settlement through the GUI. No real wallets were exposed.

## Rust signature boundary verification

The engine's `native_quote_signatures::verify` now verifies both role-separated
Ed25519 signatures with explicit caller-pinned identities and strict verification.
It requires canonical JSON and the expected schema/identity fields, and returns
the same domain-separated byte commitment used by the GUI. Fixed disposable
PyNaCl/libsodium vectors check both signatures and the resulting commitment.
This is signature verification only: the test payload is deliberately not a
complete commercial quote, and passing does not authorize negotiation or funding.
Engine-side commercial policy, freshness, replay and transcript binding remain
separate integration requirements.

Revalidation passed: all 51 swap-machine library tests; offline locked
`cargo check -p swap-machine --all-features --all-targets`; and reconstruction
of all indexed engine sources from the ordered 23-patch series.
Compilation still reports unused-dependency/dead-code warnings.
No real-wallet operation, deployment or change to the live-trading gate occurred.

The subsequent `native_trade_quote::VerifiedQuote` boundary now accepts only a
complete 23-field signed commercial quote. Its private storage exposes immutable
terms, the signature-bound commitment and independently decoded destinations.
It rejects unexpected/missing/non-string fields, noncanonical or overflowing
amounts, unsupported application fees, invalid identifiers, expired quotes,
incompatible network labels and out-of-range confirmation/timelock settings.
Address-network configuration is caller-supplied. Mainnet and fakechain share XMZ
address encoding; this check does not substitute for an exact deployment/genesis
policy or an observed chain identity check.

All 53 library tests passed, including newly signed malformed commercial quotes
and expiry boundaries. The ordered 24-patch series reproduced indexed sources.
This type is not yet consumed by Alice/Bob construction: deployment policy,
durable approval and transcript binding must precede that integration. No GUI
funding path or live trading was enabled by adding it.

Engine `VerifiedQuote::check_deployment` now independently checks all twelve
deployment-policy fields: four exact chain identities/labels, four exact timing
settings, and four nonzero principal/native-fee caps. It rechecks expiry at use.
Tests mutate every policy field separately, cover exact and larger caps, expiry,
and distinguish XMZ fakechain from mainnet despite their shared address encoding.
All 54 library tests passed, and the 25-patch source reconstruction passed.
The policy check is still a callable boundary, not yet an enforced step in the
Alice/Bob constructors or a live-chain attestation. Native quote transport and
negotiation binding remain necessary before funding can be enabled.

The new `signed_quote_drives_engine_negotiation` regression independently verifies
the same signed quote for both roles, checks local policy, and passes its amounts
and BTC payout/refund destinations into the actual Alice/Bob state machines.
It completes their cryptographic message exchange with in-memory Bitcoin wallets
and constructs the unsigned lock. Deliberately non-default amounts (450000 sats,
150000000000 XMZ atoms) survive negotiation: the lock amount and both roles' XMZ
funding expectations match. All 55 library tests, all-feature/all-target checking,
and the 26-patch reconstruction passed.

This is an in-process negotiation test, not two independent public peers or a
funded GUI swap. Punishment destination, remaining-refund timeout, amnesty and
individual path fees still use explicit fixture settings outside quote v1.
Those settings must be bound in the negotiated transcript before production
funding; v1 commercial approval alone cannot authorize them. XMZ payout/refund
execution and durable approval are not exercised by this test. Nothing broadcasts.

## Authenticated engine negotiation transcript

`native_negotiation::Transcript` now wraps all five typed engine setup messages
without changing the legacy engine wire/state formats. Each bounded canonical
frame binds the commercial quote commitment, swap UUID, expected role/sequence,
previous-frame digest and complete typed message. Strict Ed25519 verification
uses the funder identities from the verified quote, with a separate negotiation
signature domain. Message0's inner swap ID must also match. Each participant
consumes its own and its peer's frames and obtains the same final commitment.

The real in-process negotiation test now sends every message through this
wrapper before the engine receives it. It rejects wrong-role signatures, missing
signatures, unframed signatures, changed quote/swap/previous/schema context,
duplicate JSON fields, unexpected nested fields and same-session replay.
Failures do not advance the cursor. All 55 library tests and the 27-patch source
reconstruction passed. The transcript carries protocol view-key shares: it must
not be logged, exposed in an order book or sent over unauthenticated plaintext
transport.

A read-only Claude review corroborated the separation between quote identities
and fresh engine keys, and identified economic policy gaps. This was scoped AI
assistance, not an independent security sign-off. The new wrapper addresses
identity/session binding when used; it does not establish GUI funding authority,
aggregate fee limits, approval of punishment/amnesty settings, live-chain
observations or engine-proof validity. The engine still validates its own proofs
and transaction signatures after transcript verification. On engine rejection,
the driver must abort, not continue merely because the authenticated frame passed.
The cursor is currently in memory only; crash recovery, durable transcript/state
pairing, peer acknowledgement, network transport and GUI orchestration remain
unimplemented for this wrapper. No live trading was enabled.

## Quote-price presentation and requested price bands

The commercial review now displays exact BTC/XMZ principal amounts, the derived
BTC-per-XMZ offer rate and separate network-fee limits. Calculation uses integer
arithmetic with Monzero's 11-decimal atomic units; it does not read demo prices.
Rates longer than twelve decimal places are marked approximate, and smaller
positive rates are displayed as less than 0.000000000001 rather than zero.
Signed principal amounts remain authoritative. Native tests cover ordinary,
repeating, tiny and maximum-size cases; the resource-backed QML review test
checks the visible summary and clearing stale amounts when the review changes.

The user additionally requires a moving price band for protocol trades.
This is not implemented by the display calculation. The initial reference
BTC/XMZ price, allowed percentage deviation and trusted pricing source remain
unconfirmed; examples such as 100 satoshis and ten percent are not defaults.
Before live activation, define authenticated/fresh reference observations,
bounded updates and manipulation handling, bind the pricing policy to offers
and pre-funding acceptance, and test both band edges with exact arithmetic.
Changing a reference must not change already committed swap amounts or prevent
refund/recovery. A price band constrains our protocol, not external markets or
the economic value of XMZ. No price-band enforcement is claimed yet.

Post-presentation verification passed: full GUI build, native quote tests with
the real destination helper, four resource-backed QML tests, and the isolated
full-GUI startup fixture. The startup fixture reached signed-quote review,
rejected missing opt-in and invalid addresses, opened no wallet and wrote no
quote reservations. The installed application and website were not replaced.

`native_price_band::contains` now implements the exact inclusive band comparison
for positive BTC-satoshi/XMZ-atom ratios and explicit deviations below 100 percent
in basis points. No reference price or band default is supplied. Big-integer
cross multiplication prevents overflow and avoids rate rounding; tests cover
both boundaries, one satoshi outside, fractional rates, zero-width bands,
invalid ratios and maximum uint64 values. All 58 engine library tests and the
28-patch reconstruction passed. `num-bigint` is pinned to the existing lockfile
version 0.4.6; its cached source and num-integer were downloaded through Cargo.
This arithmetic helper does not authenticate a price, enforce freshness, select
market observations or admit trades. Those integrations and user-selected policy
remain outstanding; the display and live trading gates are unchanged.

## Negotiated Bob-state quote preflight

`bob::State2::check_native_quote` now checks fresh deployment policy against
the negotiated lock principal, XMZ principal, BTC payout/refund addresses,
cancel/punish timelocks and XMZ confirmations. It accepts only the initial
zero-amnesty/full-refund profile. Each complete Bitcoin path (lock plus redeem,
lock plus cancel/refund, lock plus cancel/punish) must fit the signed BTC fee
budget; early refund is also bounded. Arithmetic uses uint128 sums to reject
oversized fees without overflow. Spend-path fees cannot consume the principal.

The authenticated negotiation test calls this preflight before its lock-state
transition. Tests reject changed amounts, destination, timeouts, expired quotes,
nonzero amnesty, huge fees and a budget one satoshi below the required total.
The destination mutation initially revealed both test wallets shared the default
seed; Alice now has a distinct disposable seed and distinct addresses are asserted.
After correction, all 58 library tests and the 29-patch reconstruction passed.
This is not yet an unavoidable production funding gate: durable approval,
authenticated state association, observed chain identity, pricing policy and
approval of the punishment destination still require driver integration.
Existing recovery operations are not conditioned on quote expiry or price bands.

Alice's negotiated `State3` now has the corresponding `check_native_quote`
preflight, including consistency between its stored BTC principal and lock output,
XMZ principal, payout/refund addresses, timing and complete Bitcoin path budgets.
The initial profile requires explicit zero amnesty and disabled withhold publishing.
The authenticated negotiation test invokes both preflights and exercises Alice's
principal/profile mutations, expired quotes and exact/insufficient fee caps.
All 58 library tests and the 30-patch reconstruction passed. These callable checks
still need durable driver integration; Alice's native XMZ funding transaction,
its network fee and final payout/refund destinations are not verified by this
Bitcoin-state preflight. No funding, signing or broadcast entry point was enabled.

## Current-engine disposable settlement/recovery recheck

Rebuilt the `bitcoin_settlement` integration executable and native helper with
the locked current engine, then ran `test_wallet_recovery.py --native-spendable`
separately with `engine_redeem_is_accepted_on_regtest` and
`engine_cancel_and_full_refund_are_accepted_on_regtest`. Both completed with exit
status zero. Each used owned Bitcoin regtest containers and an offline XMZ
fakechain with verified genesis, ephemeral loopback RPC and disposable wallets.
Both checked native key restoration, non-relayed fee estimates, protocol-key
recovery, idempotent import, RPC restart, actual recovered-key spending with
exact fee accounting, and read-only reconciliation with a deliberately missing
receipt. The real native pipe response also passed the Qt recovery receiver.

Artifact SHA-256 identities for this run:

- `bitcoin_settlement-012688f7ed48c10c`:
  `9c874db2bddbdb0adcff756b059f6ef17988bafd84755a3c6785111e6bddb84b`
- `native-fee-preview`:
  `9d954b4743507dae9623fb80e78437ad90a69ca3334e58f81af77c2fec128f52`

This revalidates the existing separate-process settlement/recovery fixture after
the engine additions. That fixture still uses its own explicit trade parameters;
it does not route GUI approval through the new authenticated quote transcript
and preflights. The in-process signed-quote test and this chain-backed test remain
distinct evidence, not proof of one complete GUI-to-chain live trading workflow.
No real wallets, installed GUI, public nodes or website were changed.

## GUI reservation byte decoder

`VerifiedQuote::from_gui_reservation` decodes the five-field canonical reservation
schema emitted by `NativeQuoteReplay`. It bounds input to 16384 bytes, rejects
unknown/duplicate fields and noncanonical timestamps/signatures, checks both
participant signatures with caller-pinned keys, validates commercial terms and
native address networks at the recorded acceptance time, and requires the quote
to remain fresh now. Tests cover changed terms, wrong identities/signatures,
future/invalid timestamps, expiry and oversized/ambiguous records. All 59 library
tests and the 31-patch source reconstruction passed.

This is a byte-decoding boundary, not a file loader or proof of user approval.
The driver must still verify trusted filesystem metadata, the quote/swap hard-link
reservation pair, expected IDs and single-use ownership, then check deployment
policy. A record can exist after an unsuccessful final directory fsync; readable
bytes alone cannot establish that the GUI approval completed durably. This API
must not be used to deny historical refund/recovery when a quote expires. The
tests construct the matching schema in Rust; actual Qt-written-record ingestion
and the full approval-to-negotiation workflow remain to be exercised.

Actual Qt-record interoperability is now tested separately. The rebuilt
`dex_native_quote_tests --export-reservation-fixture` creates disposable identities,
signs bounded terms with libsodium and writes a real `NativeQuoteReplay` reservation
in a temporary directory. It exports only the public record, test identity pins
and expected commitment. Rust's ignored `qt_written_reservation_interoperates`
test invokes that explicitly selected executable using
`MONZERO_TEST_QT_QUOTE_DRIVER`, decodes the exact Qt-written bytes, checks the
commitment/amounts, and rejects expiry, swapped pins and a wrong Bitcoin network.
The explicit interoperability run passed; the normal library run passed 59 tests
with that one environment-dependent test ignored. All-feature/all-target checking,
the Qt native tests and the 32-patch reconstruction also passed.
This exercises the real reservation writer, not an interactive GUI approval or
the engine's filesystem ownership/durability checks. Test identity pins are trusted
fixture outputs, not a pattern for accepting production peer identities from an
untrusted reservation. No private keys or real wallet data are exported.

## Read-only native reservation storage

Linux `native_quote_reservation::read_record_bytes` now reads the quote/swap pair
through descriptor-relative, no-follow, nonblocking opens under a shared lock on
the private owner-controlled directory. It requires one regular private inode
with exactly two links, bounded stable contents, unchanged path identities and
matching quote/swap IDs in the bytes. It rejects incomplete pairs, extra links,
symlinks, copied substitutes, public permissions, oversized files, renamed IDs
and a busy GUI writer lock. Tests passed (60 regular library tests, one explicit
Qt interop test ignored by default), as did the 33-patch source reconstruction.

The return value is raw bytes, not a verified or authorized trade. Feed it through
`from_gui_reservation` with trusted peer pins and native network configuration,
then check the expected commitment and deployment policy. The directory and
ancestors must remain exclusively controlled. This reader performs no repair,
deletion, reservation consumption or funding; it cannot distinguish an earlier
failed final fsync from an acknowledged successful GUI approval. A live driver
still needs a durable, single-use handoff coupled to authenticated engine state.
Non-Linux platforms explicitly fail closed pending a native storage implementation.

The explicit Qt interoperability test now supplies its own private disposable
directory to the Qt writer. After Qt exits, Rust opens the actual quote/swap
hard-link pair with `read_record_bytes`, compares it byte-for-byte with Qt's
export, and revalidates signatures and commitment using `from_gui_reservation`.
This joined file handoff passed. The Qt regression suite, 60 regular Rust tests
(one separately exercised interop test ignored by default), all-feature/all-target
checking and 34-patch reconstruction also passed. The directory is owned and
removed by the test; no production record is consumed. This closes the previous
test separation between the real Qt writer and native storage reader, but not
the durable approval acknowledgement or negotiation/funding lifecycle.

## Visible commercial-approval outcome

The GUI now exposes `nativeQuoteStatus` from the native coordinator and displays
checking, saved, failed and cancelled outcomes outside the disappearing review
panel. Saved explicitly means commercial terms only: no trade started or funds
sent. Failure warns that a partial reservation can remain and must not be deleted
to force a retry. All statuses retain `executable: false`, including Reserved.
Five resource-backed QML tests, real-helper coordinator/status tests, the full
GUI build and isolated full-GUI startup checks passed. Real wallets were not
opened. This fixes previously missing outcome feedback; it does not add trade
execution authority or mark the durable engine handoff complete.

## Combined reservation validation

`native_quote_reservation::read_verified_quote` joins private file-pair reading,
signature/network/freshness validation, caller-expected commitment matching and
trusted deployment-policy checks in one read-only entry point.
The actual Qt-written reservation interoperability test now exercises this path,
including wrong commitment, wrong peer pin, expiry, insufficient principal cap
and wrong genesis rejection, followed by successful rereading of unchanged bytes.
Its policy is explicitly the Qt fixture policy, not copied from incoming terms.

All 61 library tests passed with the Qt interoperability fixture explicitly
enabled; all-feature/all-target checking passed with existing warnings.
The 35 ordered patches reproduce the indexed engine sources.
This function is not wired to a production trade-start command and does not
acknowledge successful GUI approval, consume a reservation or authorize funding.
Durable single-use handoff and the full GUI-to-settlement lifecycle remain open.

## Qt reservation drives authenticated negotiation

The explicitly enabled `qt_reserved_quote_drives_engine_negotiation` test now
sends disposable signed commercial terms to the Qt test driver's
`--reserve-signed-fixture` mode over stdin.
The driver uses the real `NativeQuoteReplay::reserve` writer and returns success;
both Rust participants independently reload that pair with
`read_verified_quote` before constructing their engine states.
The shared negotiation test then exercises all five authenticated messages,
matching transcript commitments, both native quote preflights and rejection of
altered economic parameters, using the exact reserved wallet destinations and
non-default principal amounts.
Only public fixture terms, pins and signatures cross this test bridge.

All 62 library tests passed with both Qt integration tests explicitly enabled.
The rebuilt Qt test suite passed with the native helper; all-feature/all-target
checking passed with existing warnings, and all 36 patches reproduce the sources.
This connects Qt reservation storage to real in-memory cryptographic negotiation,
not interactive GUI approval, public peer transport or blockchain funding.
The separate funded settlement/recovery fixture has not yet been joined to this
path; production trade-start acknowledgement and durable state ownership remain
required before enabling live trading.

## Variable principals in the funded separate-peer fixture

The separate Alice fixture no longer hard-codes its Bitcoin and XMZ principals
or reports a hard-coded XMZ amount after negotiation.
Initialization supplies canonical positive amounts; both participants check the
negotiated amounts before the Bitcoin lock is signed and broadcast.
Alice returns its actual funding amount, and checkpoint restart must preserve
that amount as well as the funding keys.
The funded fixture now uses 450,000 regtest satoshis and 150,000,000,000 fakechain
XMZ atoms to detect residual dependencies on the former constants.

Both combined `test_wallet_recovery.py --native-spendable` runs passed with
`engine_redeem_is_accepted_on_regtest` and
`engine_cancel_and_full_refund_are_accepted_on_regtest` respectively.
This includes separate-Alice restart, Bitcoin settlement/refund, native spending
with protocol-recovered keys, exact fee accounting, missing-receipt read-only
reconciliation and the Qt recovery receiver.
All-feature/all-target checking and the 37-patch reconstruction passed.
The tested settlement binary SHA-256 is
`81a758282691232a48d878ac73c03820badfbddfa8c8ad32083bb3a29a979dd4`;
the native helper SHA-256 is
`152ed0f938d470d2b4298e0443c5c47592230df91d1d68198afeb967c133effc`.

This removes a prerequisite obstacle to supplying GUI-selected terms.
The funded separate-process fixture still uses fixture initialization rather
than the signed quote/Qt reservation/authenticated transcript path.
It does not establish a complete GUI-to-funded-trade workflow or production
authorization, and all live gates remain unchanged.

## Integration review: lock addresses versus final proceeds

A local read-only Claude review and direct inspection of
`test_wallet_recovery.py` identified the next integration boundary.
This is AI-assisted development review, not independent security sign-off.
The combined fixture does verify real fakechain funding despite the older
Bitcoin test file's standalone/mock-oriented header.
Do not interpret that header as evidence that the combined run has no native
funding verification.

Three addresses must remain distinct in the design:

- The protocol-derived shared XMZ lock address, funded before settlement.
- The buyer's approved final XMZ payout address, used after successful redemption.
- The seller's approved XMZ refund address, used after cancellation/recovery.

Reject the review's suggestion to replace the shared lock address with a quoted
payout address: doing so bypasses the locking mechanism.
Quote destinations must instead bind the final recovered-funds spend.
Likewise, `read_verified_quote` validates terms but does not prove successful
GUI approval or acquire durable execution ownership.

The current Python fixture creates the final destination after engine
negotiation and its native-spendable branch transfers only `amount * 9 // 10`.
It verifies the exact requested partial payment and fee accounting, leaving a
residual balance; it does not prove full quoted proceeds or residual recovery.
Both settlement and refund use this generic spending-primitive test.
They do not yet assert distinct quote-bound buyer and seller destinations.

The next funded integration must therefore:

1. Create distinct disposable payout/refund wallets before quote signing and
   before starting negotiation; pass only their public addresses to the peers.
2. Obtain an explicit successful fixture approval acknowledgement, separately
   from validating the Qt reservation; retain production single-use ownership as
   a separate requirement rather than treating fixture acknowledgement as it.
3. Load the exact signed quote on both sides, wrap the separate-process exchange
   in authenticated transcripts and check both negotiated states before funding.
4. Continue funding the protocol-derived shared lock address, verifying actual
   chain identities, lock amounts and confirmation policy.
5. Construct the final native spend from the approved role-specific destination
   and fee/amount semantics, not free-form environment overrides; reject altered
   destinations and fees before preparation or relay.
6. Confirm the destination's on-chain receipt and account explicitly for all
   remaining funds on both redemption and refund paths, including restart tests.

Gross locked XMZ versus guaranteed net proceeds remains an explicit user choice.
The existing 90-percent partial-spend convention must not become live pricing
or settlement policy by accident.
No production gates or economic defaults changed during this review.

### Real destinations before engine startup

`test_wallet_recovery.py` now creates distinct disposable payout and refund
wallets before starting the settlement engine.
Their public addresses are available to the engine environment as
`MONZERO_TEST_XMZ_PAYOUT` and `MONZERO_TEST_XMZ_REFUND` for the upcoming quote
integration; the current engine does not yet consume or authenticate them.
The protocol-derived shared lock address must differ from both destinations.
The driver selects payout for redemption and refund for cancellation, verifies
the selected wallet's exact partial receipt, and confirms the other wallet's
balance remains zero.

Both combined funded native-spendable runs passed after this startup reorder,
including separate-peer recovery, missing-receipt reconciliation and Qt recovery
response decoding.
Python compilation and whitespace checks passed.
This supplies real role-specific destinations early enough for quote signing;
it does not yet bind them to signatures or replace the partial-spend convention.

### Signed quote preflights in the funded fixture

With `MONZERO_TEST_QT_QUOTE_DRIVER` explicitly set, the funded engine fixture now
builds a fully signed quote using its actual Bitcoin payout/refund addresses and
the two pre-created fakechain destination addresses.
Disposable fixture keys sign the terms; policy and pins remain fixed test
configuration, and the quote uses the real current time and a bounded expiry.
The Qt reservation driver must exit successfully before the fixture continues.
This is a test-process acknowledgement, not interactive GUI authorization or
durable production execution ownership.

Before calling `lock_btc`, Bob reloads the Qt reservation and runs his native
quote preflight; the separate Alice process independently reloads the same
reservation and checks its negotiated state.
The parent also rejects a changed expected commitment.
Only after both preflights does it emit the fixture check marker and proceed to
the Bitcoin lock.
The Python driver requires that marker when quote integration is requested,
checks both destinations against its wallets and selects the final partial-spend
destination from those checked terms.
Without the explicit Qt driver, standalone legacy fixture runs remain possible
but are not evidence for quote integration.

Both combined funded redemption and cancellation/refund runs passed with the Qt
driver enabled, including final destination balances, native spending with
protocol-recovered keys, restart reconciliation and the Qt recovery decoder.
The 62 library tests (including both Qt interoperability tests), real-helper Qt
tests, all-feature/all-target checking and 38-patch reconstruction passed.
Compilation still emits warnings.

This joins signed commercial quote preflights to funded separate-process tests.
It does not yet authenticate their five-message socket exchange with
`native_negotiation::Transcript`, persist the quote alongside resumed state,
eliminate the partial-spend convention, bind all remaining economic parameters,
or implement production approval/ownership and automatic GUI execution.
The final native spend is still orchestrated by the trusted Python fixture,
not a production quote-bound spending entry point.
Live gates remain unchanged.

### Authenticated negotiation across the funded fixture's process boundary

The quote-enabled separate Alice/Bob fixture now sends all five engine setup
messages through `native_negotiation::Transcript` on both processes.
Each side consumes its own signed outgoing frames and verifies incoming frames;
the final transcript commitments must match before returning negotiated state.
The adapter uses only the disposable fixture identity keys.
Each incoming frame is also tested with a damaged signature: rejection must not
advance the cursor, and the original signature must subsequently succeed.
Message shares remain inside the private fixture socket, never public logs.

Both combined native-spendable redemption and cancellation/refund runs passed
with Qt reservations and authenticated negotiation enabled, including the
pre-funding quote checks and existing on-chain/restart recovery assertions.
Both standalone Bitcoin-only settlement tests also passed with the quote mode
explicitly unset, exercising the separate non-quote fixture branch.
All-feature/all-target checking, Python compilation, whitespace checking and
the 39-patch reconstruction passed; compiler warnings remain.

The transcript cursor/completion is still in memory only.
The existing resumed Alice checkpoint does not yet persist its quote or completed
transcript association, and post-negotiation fixture commands rely on the
private socket rather than the negotiation signature wrapper.
This is not a production peer transport, durable approval handoff or complete
full-proceeds settlement driver.

### Checkpoint association survives Alice restart

The fixture's completed association now contains both the signed quote
commitment and the completed negotiation transcript commitment.
Alice saves that association together with its state, swap ID and checkpoint
sequence, and returns it on resume.
The surviving parent requires exact equality with its expected association as
well as unchanged funding keys and amount before issuing further commands.
A later quote preflight also rejects a quote that differs from the completed
negotiation association, even if its engine-visible economic fields match.
Historical recovery does not reapply quote freshness checks.

The targeted association test passed for serialized round-trip, missing or
unexpected association, substituted quote/transcript hashes and unknown fields.
Both quote-enabled combined funded redemption/refund runs passed across Alice
restart, as did both standalone Bitcoin settlement tests.
All-feature/all-target checking and the 40-patch reconstruction passed with
compiler warnings still present.
The negative association test exercises the resume comparison/decoder, not
on-disk corruption or power-loss fault injection.

This is an association check within the owned test fixture, not a cryptographic
seal of the whole checkpoint or a production persistent swap database.
The parent still retains its expected association in memory; a simultaneous
parent restart, durable single-use ownership, retained signed terms and complete
state reconstruction remain to be implemented and tested.

### Retained signed terms and expiry-safe recovery inspection

`RecordedQuote` now provides read-only historical term validation using the
retained peer pins, original deployment policy and independently expected quote
commitment.
It shares strict reservation/signature decoding with `VerifiedQuote`, but checks
the terms at recorded acceptance rather than requiring an unexpired new-trade
quote today.
It exposes no inner `VerifiedQuote`; a compile-fail test confirms it cannot be
passed to `Transcript::new`.
It is not proof of user approval or authority to spend: `acceptedAt` remains
unsigned local metadata, and retained state/ownership still require validation.

The fixture association now retains the exact Qt reservation bytes alongside
the quote and transcript commitments.
Alice revalidates those signed bytes and their swap ID when loading its
checkpoint, and the surviving parent compares the complete association.
The historical reader does not reserve records, repair storage or rearm trading.

All 62 library tests passed, including actual Qt record recovery inspection after
expiry and continued rejection by the fresh-quote path.
Tests reject changed destinations, commitments, policy and invalid acceptance
times, and cover extreme current timestamps without arithmetic overflow.
The compile-fail type-boundary test, fixture association test, both combined
funded redemption/refund runs, all-feature/all-target compilation and 41-patch
reconstruction passed; compiler warnings remain.
The funded runs verify checkpoint record retention/revalidation during real
fixture restarts; expiry is exercised by the parser/Qt interoperability tests,
not by waiting for the funded fixture's wall-clock quote expiry.

Full parent-process restart, retained production policy/identity configuration,
single-use execution ownership and full-proceeds settlement remain unfinished.

### Retained quote checks in pending-funding reconstruction

Bob `State3::check_recorded_quote` compares restored principal amounts, Bitcoin
payout/refund addresses, timing, supported refund profile and complete Bitcoin
fee paths with authenticated historical terms, without imposing new-quote expiry.
`PendingFunding::check_recorded_quote` additionally binds swap ID and chain policy.
These checks do not authenticate all serialized engine state or authorize funds;
the existing fresh observations of both chains remain required for resumption.

The funded fixture now saves the complete quote association in a private,
synced companion record and syncs the containing directory.
Its recovery subprocess opens that record without following a final symlink,
checks owner/private mode/link count/size, compares a domain-separated fingerprint
supplied independently by the test parent, revalidates historical signatures and
checks the decoded pending state against the signed terms before querying chains.
Quote-required mode is explicit; it does not infer approval from record presence.

Both combined funded redemption/refund runs passed, including the unavailable-RPC
recovery refusal and successful fresh-process reconstruction.
They also reject independently decoded state with altered XMZ principal, refund
address or Bitcoin cancellation fee.
All 62 library tests, all-feature/all-target compilation, whitespace checks and
42-patch reconstruction passed; compiler warnings remain.

The fingerprint anchor is still passed by the surviving test parent.
This therefore does not yet prove a simultaneous parent/application restart,
production durable authorization, journal ownership or complete checkpoint
authentication.
The native pending-funding codec remains compatible and its low-level resume
method does not automatically require a quote; a production DEX driver must
enforce the quote-aware path and ownership checks.

### Quote-aware final native recovery payment

The fixture previously permitted a 10,000,000,000-atom native spending fee budget
while its signed quote specified 1,000,000,000 atoms.
Quote-enabled spending now obtains the budget from retained signed terms instead
of the standalone fixture default.
`swap_wallet_spendable_for_quote` validates the caller-selected signed payout or
refund destination, chain identity, positive fee ceiling and conservative
payment-plus-fee budget before wallet restoration, journal writes or RPC work.
The underlying prepared-transaction path continues enforcing that fee ceiling.

The fixture passes a pre-funding association fingerprint to the spending process,
which reloads the private retained record and revalidates its signatures.
Negative requests for a different destination, excess fee budget and a payment
that consumes the fee reserve fail with quote-limit errors even with unusable
RPC ports, demonstrating rejection before RPC setup.
Both combined redemption/refund runs passed using the signed 1-billion-atom cap,
including the final native transfer and existing restart reconciliation checks.
All 62 library tests, all-feature/all-target compilation, Python compilation,
whitespace checking and 43-patch reconstruction passed; warnings remain.

This is a conservative partial-recovery payment check, not full-proceeds
settlement, an XMZ application fee, or aggregate accounting for every native
transaction in a swap.
The trusted driver must still derive the payout/refund role from verified
settlement outcome and bind source wallet, recovered keys and lock identity to
engine state; the role enum does not itself prove an outcome.
The standalone low-level primitive remains available for non-quote tests, and
production GUI execution and ownership gates remain closed.

### Price reference freshness guard

`native_price_band::contains_fresh` adds explicit age validation before exact
price-ratio comparison for new trades.
Future observations, stale references and zero maximum age are errors;
the maximum-age boundary is inclusive and subtraction is overflow-safe.
No reference price, deviation, clock source or maximum-age default is selected.
The reference struct is caller-supplied data, not authenticated oracle evidence.
Production reference authentication, update policy and admission integration
remain unfinished; this helper must not gate recovery of existing swaps.

All five price-band tests passed, including freshness boundaries, invalid
policy inputs and extreme timestamps.
All-feature/all-target compilation and 44-patch reconstruction passed;
existing compiler warnings remain.

### Signed-quote price-band validation

`VerifiedQuote::check_price_band` rechecks deployment policy and quote expiry,
then evaluates the signed BTC/XMZ principal ratio against an explicit fresh
reference and deviation policy.
It takes no separate trade amounts that could disagree with the signed quote.
Re-signed boundary cases exercise both inclusive edges and one-satoshi violations;
stale/future references and expired quotes are rejected.
This method is not added to historical recovery or refund paths.
It remains a validation API, not mandatory production admission: reference
authentication, policy agreement and the production execution driver are pending.
No market reference or deviation default has been chosen.

### Post-price-band interoperability verification

The complete engine library suite was rerun with the explicit local Qt fixture
driver and `--include-ignored`: 64 passed, zero failed or ignored.
This includes actual Qt reservation decoding and the Qt-reserved quote's
authenticated engine negotiation, not just hand-constructed Rust records.
The existing Qt native-quote executable also passed against the real native
helper: destination validation, wrong-network/cancellation rejection, exact
single-use quote reservation, expiry and signature/replay checks.

Inspection of `NativeQuoteCoordinator::approve` confirms that success still
means commercial-term reservation only; it neither starts an engine process
nor grants funding authority.
These test results do not prove interactive GUI execution, mandatory price-band
admission, public liquidity, or live settlement readiness.
The next integration boundary remains durable, single-use execution ownership
following explicit trade-start consent, with restart-safe recovery.

### Single-use execution-attempt claim primitive

`native_execution_claim::ExecutionClaim::reserve` consumes a fresh verified quote,
rechecks trusted deployment policy and the exact caller-approved commitment,
and exclusively creates a private per-swap intent through a checked directory
descriptor.
It syncs the intent and containing directory before returning a non-cloneable
handle; creation/write/sync failures never return a handle.
The filename depends on swap identity rather than changeable price, quote ID or
destination, so repricing the same swap cannot obtain a second reservation.
Dropping a handle does not remove the intent.
An existing, malformed or interrupted intent requires recovery investigation,
not deletion or a new execution attempt.

Tests cover wrong consent commitment, expiry, non-private directory, successful
reservation, changed terms, a fresh-process reopen, and an empty interrupted
record that remains untouched.
The implementation is Linux-only; other platforms explicitly reject reservation.

This is not yet a complete execution driver or funding authority.
It does not prove a human consent interaction, authenticate retained state,
persist signatures/policy for full application restart, enforce price policy,
or resume a interrupted swap.
The trusted driver must supply a persistent private namespace, separately obtain
trade-start consent, enforce admission and chain checks, and journal protocol
progress before this can be connected to production GUI execution.
No GUI funding gate or real wallet was changed.

### Execution claim exercised before funded fixture locks

The quote-enabled two-party fixture now obtains an execution claim after both
quote preflights and before constructing/signing/broadcasting the Bitcoin lock.
It verifies the claim's quote commitment and rejects a duplicate attempt before
funding, retaining the original handle for the fixture's execution lifetime.
Consent is explicitly supplied by the disposable fixture, not inferred from
production GUI reservation files.

Both combined funded redemption and cancellation/refund runs completed with the
claim enabled, actual Bitcoin regtest/XMZ fakechain transactions, authenticated
peer negotiation, signed native destination/fee limits, restart reconciliation
and selected-wallet receipt checks.
All-feature/all-target compilation and 47-patch reconstruction passed.
These runs still exercise partial native proceeds, not full-proceeds settlement.
This fixture connection does not implement production trade-start consent,
durable recovery ownership or GUI execution; live gates remain closed.

### Recovery source-output binding and additional AI review

A read-only review using the user's installed Claude CLI identified that the
lock transaction ID was used for import/journal identity but not checked against
the inputs selected by wallet RPC.
Inspection of the local wallet RPC implementation confirmed it exposes incoming
output key images and prepared transaction input key images.
Recovery now uses `prepare_from_lock`: every selected input must map uniquely to
an available, unlocked, unfrozen output of the intended lock and account, and
their amounts must cover the payment plus fee.
Prepared metadata retains the checked lock ID, which the spend journal compares
before recording preparation.
Unrelated deposits may exist but cannot be selected by this recovery path.
A mismatch fails before relay; it does not freeze outputs or retry automatically.
This still trusts the dedicated local wallet RPC and exclusive caller ownership,
not an independently decoded transaction proof.

The review also noted primary-account wallet identification and public low-level
spending APIs.
Those are documented trusted-caller interfaces, not demonstrated external
exploits: selected-account source binding is now checked during recovery, while
mandatory quote-aware production dispatch remains a separate unfinished task.
This AI-assisted review is not an independent security audit or release sign-off.

The fixture now explicitly asserts and labels its residual balance and partial
payment rather than describing that payment as final proceeds.
Full-proceeds settlement still requires defined gross/net fee semantics and a
durable plan for all remaining funds; the public quote policy has not changed.

Validation: all 66 engine library tests passed with Qt interoperability enabled;
all-feature/all-target compilation, Python compilation, whitespace checks and
48-patch reconstruction passed.
Both combined funded redemption and cancellation/refund fixtures completed with
source-input binding, fee/destination checks and restart reconciliation enabled.
Compiler warnings remain, and no production gate or real wallet was changed.

### Adversarial input evidence through the RPC preparation path

A bounded, owned loopback RPC fixture now exercises `prepare_from_lock`,
including wallet authentication challenge handling, chain/wallet/balance reads,
incoming output inspection and non-relaying transfer preparation.
Missing, empty, duplicate, unrelated and mixed-lock selected input evidence is
rejected without returning a prepared transfer or issuing `relay_tx`.
The fixture asserts that every construction request has `do_not_relay=true`.
It uses no real daemon, wallet, funds or external server.
This supplements, rather than replaces, the real funded success/refund tests and
the direct input-validator tests.
The mock models a Digest challenge but is not a Digest-authentication security
test or independent transaction-signature verification.

### Read-only GUI recovery request

The DEX page now offers a read-only recorded-payment check through
`DexEngine::requestNativeRecoveryCheck()`.
The invokable takes no transaction hashes, paths or wallet identities from QML.
A native coordinator must first pin a validated journal-derived target with
`SwapWalletSession::pinRecoveryTarget` while the intended wallet is open.
That target is immutable for the session and tied to the verified wallet address;
unconfigured, busy, stopped or different-wallet states cannot request a check.
The existing helper-digest, journal-directory and operation-ownership checks
remain in force.
The UI explicitly distinguishes confirmed payment evidence from recovery of
all swap funds, and neither success nor the button authorizes another payment.

All 33 C++ DEX tests and five QML presentation tests passed.
Production journal loading must still supply the pinned target; this change
does not invent one from a public offer or make trade execution available.

The full GUI build and isolated private-startup test also passed: authenticated
disposable RPC startup, no wallet opened, credential cleanup, signed quote review
and missing-opt-in/bad-address rejection.
No installed GUI, real wallet, website package or live node was replaced.

### Consolidated isolated settlement runner

`tools/dex-engine-port/verify_native_settlement.py` now reproduces patch
verification, all 67 engine library tests including Qt interoperability,
settlement-test rebuild, and sequential funded redemption/refund fixtures.
Both executable paths are selected from Cargo build artifacts with exact source,
target-kind and test-profile checks, rather than a hard-coded hash or guessed
helper path.
Missing/ambiguous/wrong-source artifact-selection tests passed.
The runner removes inherited `MONZERO_TEST_*` variables before supplying its
explicit owned fixture configuration.
The final runner completed successfully end to end; Python compilation and
whitespace checks also passed.

Native GUI startup remains intentionally fresh-service-only and opens no wallet.
Historical wallet opening/journal selection requires a distinct explicit recovery
flow, not an implicit startup side effect.
The runner neither implements that flow nor certifies production readiness.

The runner now removes inherited Cargo/Rust overrides and compiler wrappers,
old fixture variables and selected subprocess injection settings before launch.
It pins Docker to `unix:///var/run/docker.sock` and rejects a missing socket,
instead of inheriting a potentially remote Docker context.
This does not change saved Docker configuration or claim a fully hermetic build;
the local checkout, prerequisite binaries, host and dependency cache remain trusted.
Artifact-selection and environment-pinning unit tests passed.
The hardened runner also completed end to end: patch verification, all 67 engine
tests and both funded settlement/recovery fixtures passed with the pinned local
Docker environment.

### Roadmap milestone 1: non-executable full-proceeds accounting

Added `native_funding_rpc::full_proceeds::preview` as a separate experiment.
It constructs a single native `transfer` with `do_not_relay=true`, explicit
fee subtraction from its sole destination, and no requested signing artifacts.
The response must consume all and only the available intended lock inputs;
recipient atoms plus the actual native fee must equal the supplied gross lock
principal exactly, within the fee ceiling.
An incomplete, frozen, locked, ambiguous or wrong-account lock fails closed.
No `sweep_all`, multi-transaction fallback, output freezing or relay is available.
Its result is not a `PreparedTransfer` and cannot be passed to the journal or relay.

This does not reinterpret signed `xmzAtoms`, choose the public gross/net policy,
or replace the existing partial recovery payment.
It trusts dedicated native wallet-RPC accounting, not an independently decoded
transaction proof; exclusive wallet ownership remains a caller requirement.
Coin selection that includes unrelated deposits is rejected rather than repaired.
The caller must treat the result as transient construction evidence, not a final
fee commitment, confirmation, settlement result or authorization to spend.

The owned native-spending fixture now constructs this preview before the existing
partial payout, rejects an incorrect principal and an insufficient fee ceiling,
then completes its original payment from the same unspent funds.
Both locally generated-key and protocol-recovered-key fixtures supply the exact
lock principal separately from the partial payment amount.
Three new unit tests cover construction flags, exact input coverage, multi-input
locks, duplicate/foreign/missing inputs, frozen/locked/wrong-account evidence,
overflow, destination amounts, residuals, fee limits and artifact rejection.

Claude CLI performed two read-only code reviews, including checking the local
wallet's fee-subtraction and response semantics.
Its useful findings reinforced the single-transaction boundary and explicit
all-input coverage requirement.
Its suggested broader changes were not automatically applied: actual net-of-fee
execution still needs approved quote semantics and durable journal integration.
This is AI-assisted review, not independent security sign-off.

Validation for this increment: all 70 engine library tests passed, including
Qt interoperability; all-feature/all-target compilation of `swap-machine`
passed with existing warnings.
The standalone owned-wallet fixture and the consolidated funded redeem and
cancel/refund fixtures passed with the full-proceeds preview assertions enabled.
Both combined paths also retained successful partial payment and restart
reconciliation checks.
The 50-patch reconstruction, Python syntax and whitespace checks passed.
The new patch was generated from a fresh reconstructed clone, including its
new source file, and verified against the live engine tree.
Acceptance output: `build/full-proceeds-preview-acceptance.log` in the core tree.
No real wallet, installed GUI, public package, node or live-trading setting changed.

### Roadmap milestone 2: retained execution commercial context

`ExecutionClaim::reserve_recoverable` now writes a v2 execution-attempt record
containing the complete canonical signed reservation, the original acceptance
timestamp, execution request timestamp, commitment and deployment-policy snapshot.
It checks the reservation against caller-pinned identities and the exact reviewed
commitment before using the existing durable single-use reservation mechanism.
The filename remains `swap-<uuid>.execution`, so a legacy v1 record, an incomplete
write or a previous v2 attempt still blocks a new attempt in the same namespace.
No automatic migration, repair, deletion or reopening of an execution claim exists.

`RecordedExecution::load` is read-only and returns `RecordedQuote`, not a fresh
`VerifiedQuote` or `ExecutionClaim`.
It revalidates both role signatures, exact historical policy, commitment, swap
identity and timestamp ordering against trusted caller-retained context.
An expired quote remains inspectable after restart if it was valid at acceptance
and the recorded execution request time.
Neither local timestamp proves that explicit user consent actually occurred.
The reader requires a bounded canonical record in a private owned directory,
rejects aliases and unsafe permissions, and checks file stability while reading.
The policy passed to recovery must be the retained policy for that swap, not a
replacement policy for admission of new trades.

The funded fixture now reserves using v2 and checks historical reload before
funding, while retaining duplicate-execution rejection.
A separate-process unit fixture verifies loading after quote expiry, unchanged
file bytes and refusal of another execution attempt.
Negative cases cover legacy and torn records, signature/identity/policy/timestamp
changes, duplicate JSON keys, oversized files, hardlinks, symlinks and permissions.

This is retained commercial context, not complete application restart recovery.
Production integration must still persist and authenticate the corresponding
protocol checkpoint and wallet/journal association, establish exclusive recovery
ownership, and explicitly connect the GUI's recovery flow.
The record does not reveal whether funding or broadcast happened and never
permits an automatic retry.
No wallet keys, credentials or signed transaction metadata are stored in it.

Validation: all 71 engine library tests passed, including Qt interoperability and
the separate-process historical-record test.
Both funded redeem and cancel/refund fixtures passed with v2 execution records,
as did all-feature/all-target `swap-machine` compilation (existing warnings remain).
The historical-quote compile-fail documentation test also passed.
Its first invocation exposed an unconfigured `rustdoc` path, so the acceptance
runner now pins the local `RUSTDOC`, checks its availability and runs documentation
tests explicitly; both runner unit tests passed after this change.
The 51-patch reconstruction, Python compilation and whitespace checks passed.
Funded acceptance output is in `build/recoverable-execution-context-acceptance.log`.

Claude's focused read-only review found no concrete bug in the reviewed change,
while noting unsigned local timestamps, dependence on trusted context and the
limited file scope of its review.
Those limits remain explicit; this is not independent audit or release approval.
No live wallet, GUI installation, public download or node was changed.

### Roadmap milestone 2: exclusive recovery ownership and checkpoint binding

The live `ExecutionClaim` now retains an exclusive nonblocking file lock for its
lifetime, in addition to the existing permanent create-once attempt record.
Dropping a handle releases process ownership only, never the durable reservation.
New recoverable attempts use schema v3 to mark this coordination protocol.
V2 records remain readable for historical inspection but cannot yield a recovery
lease: their original writers did not retain an ownership lock.
V1 and damaged records still block creation of another attempt.

`RecoveryLease::acquire` uses the same checked per-attempt inode and holds its
exclusive lock until the guard is dropped or its process exits.
It validates historical signatures, policy and caller pins before returning.
These are cooperative Linux process locks on private trusted storage, not a
distributed lock or protection against hostile code replacing files as the owner.
No file is removed, rewritten or automatically migrated during acquisition.

`RecoveryLease::resume_pending` binds checkpoint economics to the retained signed
quote before the existing fresh checks of both chains and the cancellation window.
The caller must retain ownership for subsequent protocol operations and still
authenticate checkpoint storage; the guard is not a funding/retry permit.
Low-level primitives remain trusted-caller interfaces.

The funded fixture explicitly relinquishes the original claim before its child
opens the checkpoint, acquires recovery ownership and revalidates the checkpoint
against the retained attempt.
After the child exits, the parent reacquires ownership before continuing.
The child rejects an altered XMZ principal through the guarded resume method,
including in the unavailable-RPC scenario, before the normal fresh-chain check.
This is a controlled fixture handoff, not proof of complete GUI-process restart.

The separate-process test now checks exclusion while the original claim is live,
exclusion between recovery handles, and acquisition after a test owner is killed.
Attempt bytes remain unchanged and a fresh execution attempt remains prohibited.
Full production checkpoint persistence/authentication, state-transition journaling
and GUI recovery integration remain unfinished.

Validation: the final combined version passed all 71 engine library tests,
the historical-quote compile-fail documentation test, both funded redeem/refund
fixtures, and all-feature/all-target `swap-machine` compilation.
The two acceptance-runner unit tests and 53-patch reconstruction also passed.
Existing compiler warnings remain; no Windows or production validation is implied.
Logs: `build/owned-checkpoint-resume-acceptance.log` and
`build/owned-checkpoint-compile.log` in the core checkout.

Claude performed read-only design and implementation reviews.
It confirmed the need to separate permanent attempt reservation from process
ownership and flagged the continuing trusted-caller status of low-level methods.
The guarded checkpoint-resume wrapper is now used in the funded child fixture;
it is not yet the complete production coordinator or an independent audit.
No real wallets, installed GUI, live nodes or public packages were changed.

### Guarded pending-checkpoint storage (patch 54)

The live execution claim can now save one private Bob pending checkpoint under
its retained directory descriptor.
The envelope includes the quote commitment and complete encoded pending state.
Storage uses exclusive creation, then file and directory synchronization;
only successful completion returns the full-envelope SHA-256 digest.
A recovery lease requires that independently retained digest before decoding,
then rechecks the recorded signed terms and supplied chain/lock policy.
The signed funded fixture now uses this API instead of its raw checkpoint file.

This is a Linux storage primitive, not complete durable application recovery.
Secret checkpoint contents remain plaintext in private local files.
Zeroizing serialization buffers does not scrub all decoded protocol objects.
The caller must independently protect and persist the digest and recovery
context; the API cannot establish their trust or latest-state provenance.
A failed or interrupted write can leave an occupied checkpoint slot without a
returned digest, and a crash after return but before caller persistence remains
unhandled by a durable application manifest.
Do not delete or overwrite such records, derive a replacement trusted digest
from them, or start another trade as an automatic recovery action.
Directory-descriptor anchoring prevents redirection but does not ensure that a
renamed directory remains discoverable at the original recovery path.

Unit coverage includes exclusive creation, digest mismatch, altered/empty and
oversized files, file/directory permissions, symlink/hardlink rejection and
directory-path replacement.
These are not power-loss or injected filesystem-failure tests.
Claude supplied read-only design and implementation reviews, not an independent
security audit.
Its crash-window and secret-lifecycle concerns remain explicit follow-up work;
its claimed successful creation inside an already unlinked directory was
checked locally and rejected by Linux with ENOENT.

Validation passed: 73 engine library tests, the historical-quote compile-fail
documentation test, both isolated funded redemption and cancellation/refund
fixtures, all-feature/all-target compilation, two runner unit tests and
54-patch source reconstruction.
Acceptance evidence: `build/guarded-checkpoint-acceptance.log` in the core checkout.
Compiler warnings remain; rustfmt was unavailable, so no formatting-tool pass
is claimed.
The fixtures still pay partial XMZ proceeds; production readiness, installed
GUI behavior and public deployment are not established by these results.
No real wallets, live nodes or public packages were changed in this step.

### Preparation before checkpoint persistence (patch 55)

`ExecutionClaim::prepare_pending` validates and encodes the secret checkpoint
without filesystem writes and returns an opaque `PreparedCheckpoint` borrowing
that claim.
The digest is available before persistence, so a future application coordinator
can durably retain trusted metadata first and then consume the same preparation
through `persist`.
The immutable bytes stay in a zeroizing buffer; neither secret serialization nor
Debug/Clone is exposed by this type.
The original claim cannot be dropped while its preparation is still used.
The compatibility `save_pending` delegates to these operations, but does not
establish metadata-before-checkpoint ordering.

This is not a completed durable manifest or crash-safe retry protocol.
The digest describes intended bytes, not proof of successful persistence.
A metadata record may outlive a missing or torn checkpoint, and an execution
claim cannot be reconstructed after a crash to repeat its write.
Recovery must fail closed and reconcile; it must not remove the occupied slot
or create another execution attempt.
The funded fixture retains the digest in its parent process, not a production
durable trust store.
Its pending checkpoint is still created after chain funding, so this does not
provide pre-funding durability for all earlier negotiation/signing states.
Those states, both parties' recovery data, secret protection and interrupted
write handling remain requirements for the application coordinator.

Claude reviewed the design, emphasizing exact-preparation metadata binding,
early bounds checks, legacy-claim rejection and the absence of retry authority.
The lifetime-bound implementation prevents selecting another claim at persist
time, and checks the prepared digest again before writing.

Validation passed: 73 engine library tests, both compile-fail documentation
tests, both funded redemption and cancellation/refund fixtures, all-feature/
all-target compilation, two runner unit tests and 55-patch reconstruction.
The funded tests check no write on preparation/drop, stable content identity,
matching persisted digest and rejection of a second write.
Evidence: `build/prepared-checkpoint-acceptance.log` in the core checkout.
Claude's implementation review reported no new functional defect in the split;
this is AI review, not independent security sign-off.
Prepared secret buffers should be held only as long as necessary; zeroizing
on ordinary drop is not process-memory protection or crash-time scrubbing.
Existing warnings remain and rustfmt was unavailable.
Partial-payout and production-recovery limitations remain unchanged.

### Durable local checkpoint pins (patch 56)

`CheckpointPinStore` opens an explicitly trusted, canonical, private and owned
directory; it must not be selected from peer input.
The caller must provision that directory durably, including its parent entry.
The combined `persist_with_pin_store` operation requires a different directory
inode from checkpoint storage, exclusively creates and synchronizes a fixed
domain/swap/quote-commitment/digest record, then writes the secret checkpoint.
It never updates an existing pin or checkpoint.
The pin describes intended content, not proof that the checkpoint was written.

`RecoveryLease::load_pinned_pending` reloads the pin from that trusted directory,
checks its exact format, swap and quote identity, then uses the existing complete
checkpoint digest check before decoding and validating the checkpoint.
Missing, torn or mismatched data produces an error, never fallback or overwrite.

| Retained pin | Checkpoint | Recovery result |
| --- | --- | --- |
| Missing or malformed | Any | Reject; never adopt a hash from checkpoint bytes |
| Valid for another swap/quote | Any | Reject identity mismatch |
| Valid | Missing, torn or digest mismatch | Reject; no automatic repair or new attempt |
| Valid | Matching private record | Decode and check terms; fresh chain reconciliation still required |

The funded recovery child obtains its digest only from the pin store and asserts
that the old parent-supplied digest environment variable is absent.
The parent still supplies fixture paths, lock/policy and historical quote context;
this is not whole-application cold-start recovery or a production trust-root
discovery mechanism.
Separate directories are not protection against malicious same-user code,
coordinated replacement of both stores, rollback or loss of the storage device.
Secret checkpoint encryption/key management and pre-funding state persistence
remain unfinished, as do torn-write recovery and full-proceeds settlement.

Claude's design review highlighted the pin-only failure window and mixing the
legacy direct-persistence API with the combined operation.
Either can leave retained records after an error; an error is not proof that
nothing was written, and callers must not retry or erase records automatically.
The code performs the directory inode comparison inside the checkpoint module;
Rust child-module access permits the parent-private fields and compilation
checks this, contrary to the review's accessor concern.
Returning a digest remains supported; the funded child's explicit environment
assertion and disk-load path establish that it does not receive that digest
from the parent.
This review is AI assistance, not an independent security audit.

Validation passed: 74 engine library tests, two compile-fail documentation
tests, funded redemption and cancellation/refund fixtures, all-feature/all-target
compilation, two runner unit tests and 56-patch reconstruction.
The pin-store unit test covers reopen, wrong swap/quote, truncation/trailing
bytes, duplicate/missing records, same-directory rejection and public directory
permissions; it does not simulate power failure or injected fsync errors.
Evidence: `build/durable-checkpoint-pin-acceptance.log` in the core checkout.
Existing compiler warnings remain; rustfmt is unavailable locally.
No live wallets, nodes, packages or website deployment were changed.

### Bob recovery checkpoint before Bitcoin funding (patch 57)

Inspection confirms `bob::State2::lock_btc` constructs `State3` and the lock
transaction without signing or broadcasting it.
The signed funded fixture now durably provisions its pin directory and saves
that State3 before calling wallet signing or submitting the lock transaction.
`prepare_bitcoin_lock` checks the recorded signed economics, then uses a distinct
envelope domain and filename through the existing preparation/pin-store writer.
The later pending checkpoint remains separate and cannot overwrite this record.

`RecoveryLease::load_bitcoin_lock` checks private storage, retained pin identity,
the full checkpoint hash, envelope commitment, expected lock txid and signed
economic terms before returning the historical state.
It does not attest chain identity/confirmation, reserve wallet inputs, authorize
another lock, or decide whether an earlier broadcast succeeded.
The caller must reconcile the exact lock against its configured chain.
No signed lock transaction or broadcast receipt is stored by this API.

The recovery child compares the signed refund constructed from the pre-lock
state with the one constructed from the later pending state.
The cancellation fixture now constructs its actual Bitcoin refund from the
pre-funding checkpoint, after the existing cancellation-maturity checks.
This exercises retained recovery material, not only checkpoint parsing.
Alice's pre-funding state, earlier negotiation state, signing/broadcast journaling,
secret protection and complete application restart remain unfinished.
The fixture still retains other parent-process context and only pays partial
XMZ proceeds; this is not a live-readiness result.

Claude was asked to review this step but reached its session usage limit before
returning findings. No Claude review or independent audit is claimed here.

Validation passed: 74 engine library tests, both compile-fail documentation
tests, funded redemption and cancellation/refund, all-feature/all-target
compilation, two runner unit tests and 57-patch reconstruction.
The funded refund was constructed from the pre-lock checkpoint and accepted
on disposable Bitcoin regtest; the wrong-lock and duplicate-save checks also ran.
Evidence: `build/pre-funding-bitcoin-checkpoint-acceptance.log` in the core checkout.
Existing warnings remain and rustfmt was unavailable.
No real wallets, installed GUI, live nodes or public releases were changed.

### Alice historical economics and pre-funding restart (patch 58)

Alice's fixture checkpoint loader previously checked the retained quote and
swap association without comparing the restored economic state against it.
`alice::State3::check_recorded_quote` now checks principal, destinations, timing,
refund profile and aggregate Bitcoin-path fee bounds against historical terms.
It shares the same private economic validator as fresh `check_native_quote`;
only fresh admission calls the deployment/freshness check.
Historical validation is not new funding or broadcast authority.
The fixture loader calls this check before acknowledging resumed state.

The funded fixture now asks Alice to synchronize her negotiated-state checkpoint,
kills the child and restarts it before Bitcoin signing/broadcast and before XMZ
funding. It compares the restored public funding keys and quote/transcript
association, then repeats fresh quote preflight before proceeding.
Later post-funding restart tests remain in place.
Economic mutation assertions cover BTC/XMZ principals, amnesty/refund profile,
cancel timing, excessive cancel fees and withhold mode.

This does not replace Alice's test-only checkpoint storage with production
guarded storage. The parent still retains her public funding tuple, association,
paths and protocol sequence; it never obtains her serialized private state.
Whole-application cold start, independent trust-root discovery, encrypted secret
storage and Alice process-ownership persistence remain unfinished.
Both parties still need durable signing/broadcast outcome handling.
Claude was retried but returned its session-limit error, with no review findings.

Validation passed: 74 library tests including the extended economic-mutation
assertions, two compile-fail documentation tests, both funded redemption and
cancellation/refund, all-feature/all-target compilation, two runner unit tests
and 58-patch source reconstruction.
Evidence: `build/alice-pre-funding-recovery-acceptance.log` in the core checkout.
Existing warnings remain; rustfmt was unavailable.
No production wallet, node, GUI installation or published package was changed.

### Guarded Alice negotiated-state storage (patch 59)

The engine now provides `prepare_alice_negotiated` and
`RecoveryLease::load_alice_negotiated` with a distinct Alice envelope and filename.
Preparation validates historical economic terms and uses the existing
zeroizing serialization buffer, write-once file and pin-first persistence.
Loading validates the private pin store, full-file digest, envelope commitment,
expected Bitcoin lock txid and historical economic terms.
Neither method authorizes funding, signing or broadcasting.

The signed Alice child creates its own execution/pin directories and reserves
an attempt before acknowledging initial quote preflight.
It retains its execution claim while alive and writes the secret negotiated
state once, before either swap asset is funded.
On restart, it uses parent-retained public commitment/association pins and lock
identity, acquires the existing recovery lease, and loads guarded state.
The lease remains held for subsequent fixture operations.
Signed restart metadata holds only swap/sequence/association, not private State3.
An invalid guarded load cannot fall back to legacy JSON state.
Unsigned fixtures explicitly use the legacy format and reject signed associations.
Initial child launch clears inherited restart-mode/context variables.

The funded paths exercise duplicate checkpoint rejection, recovery exclusion
while the live claim is held, exclusion of another recovery lease, and wrong
lock-id rejection on every signed Alice restart.
Private data remains plaintext; this is not encryption or same-user attack
resistance. The caller must select each party's own namespace: execution claims
are not cryptographic proof of actor identity.
Parent-provided context, explicit GUI start consent, durable signing/broadcast
outcome records, whole-app discovery and encrypted secret storage remain open.
Claude remains session-limited (reported reset 20:40 Europe/London); no Claude
review is claimed for this step.

Validation passed: 74 library tests, two compile-fail documentation tests,
both funded redemption and cancellation/refund with guarded Alice restarts,
all-feature/all-target compilation, two runner unit tests and 59-patch source
reconstruction.
Evidence: `build/guarded-alice-checkpoint-acceptance.log` in the core checkout.
Existing compiler warnings remain; rustfmt was unavailable locally.
Tests still pay partial XMZ proceeds and do not prove full live settlement.
No production wallets, nodes, GUI installation or public packages were changed.

### Exact Bitcoin lock submission intent (patch 60)

`prepare_bitcoin_submission` first reloads Bob's pinned pre-lock checkpoint
under the live execution claim rather than trusting a newly supplied State3.
It checks historical economics, exact txid and all non-witness fields against
the retained PSBT's unsigned transaction.
The first-release storage path requires native witness inputs: empty scriptSig
and a present witness for every input.
It serializes the complete consensus transaction, including witnesses, into a
distinct private envelope and uses the existing pin-first, write-once storage.
The signed fixture saves it before `sendrawtransaction`.

`load_bitcoin_submission` runs under recovery ownership, reloads the pre-lock
state, verifies the submission pin/envelope and repeats identity/structure checks.
It returns recorded transaction bytes, not an execution permit or RPC outcome.
The recovery child loads it after restart; the parent compares the complete
transaction with the transaction returned by the fixture's Electrum service
after lock broadcast/mining and fresh funding verification.

This is submission intent only: witness presence is not script/signature
verification, and storage is not evidence of network acceptance.
The structural unit test deliberately uses a synthetic witness to make that
boundary explicit; actual signatures are exercised by funded regtest submission.
No API here sends, rebuilds, replaces or retries a transaction.
Durable outcome receipts, lost-ack fault injection, reconciliation policy and
whole-application recovery remain separate work; absence of a receipt must never
be interpreted as proof of rejection or permission to create another lock.
Claude remains unavailable under the previously reported session limit; no
Claude review is claimed for this step.

Validation passed: 75 engine library tests, two compile-fail documentation tests,
funded redemption and cancellation/refund with restored submission comparisons,
all-feature/all-target compilation, two runner unit tests and 60-patch source
reconstruction.
The funded paths reject changed-output and missing-witness preparations, reject
duplicate intent writes, reload intent in the child and compare the complete
retained transaction with the observed lock after recovery handoff.
Evidence: `build/bitcoin-submission-intent-acceptance.log` and
`build/bitcoin-submission-final-compile.log` in the core checkout.
Existing warnings remain and rustfmt was unavailable.
Partial XMZ payouts and live gating are unchanged; no production resources were
modified in this step.

### Read-only Bitcoin submission observation (patch 61)

`RecoveryLease::reconcile_bitcoin_submission` reloads the guarded submission,
then queries a read-only observer using the expected genesis from retained signed
terms. It samples chain identity/tip before and after transaction lookup and
rejects mismatched samples or a wrong chain.
An observed transaction must match all retained bytes, including witnesses;
matching txid alone is not enough.
`ExactTransactionObserved` and `NotObserved` are deliberately limited results:
neither authorizes broadcast/retry or establishes confirmations, unspent outputs,
finality, settlement or current redeem eligibility.
RPC failures propagate without being converted into absence.
The observations rely on the configured node; matching sampled tips is not a
cryptographic inclusion proof or a guarantee against all intervening changes.

The funded recovery child performs this observation before pending-state resume,
including the run where the separate native-chain observer is unavailable.
The disposable Bitcoin harness enables txindex; the fixture RPC adapter uses
getblockhash/getbestblockhash/getrawtransaction only and treats every RPC error,
including missing-index/not-found errors, as an error.
Non-observation behavior is tested with a controlled observer, not inferred
from RPC text or HTTP failures.

The new unit test covers exact presence, absence, wrong genesis before lookup,
changed tip, different transaction fields, different witnesses with the same
txid and a failed lookup. It supplies no broadcast capability.
This is not yet durable outcome storage or lost-acknowledgement fault injection.
Full-proceeds settlement and GUI live execution remain unfinished.
Claude is still within its previously reported usage-limit window; no new
Claude review is claimed.

Validation passed: 76 engine library tests, two compile-fail documentation tests,
both funded redemption and cancellation/refund with read-only submission lookup,
all-feature/all-target compilation, two runner unit tests and 61-patch source
reconstruction.
Evidence: `build/bitcoin-submission-observation-acceptance.log` and
`build/bitcoin-reconciliation-final-compile.log` in the core checkout.
Existing warnings remain; rustfmt was unavailable.
The txindex setting affects disposable test containers only; no live node,
production wallet, GUI installation or public package was changed.

### Lost Bitcoin acknowledgement fixture (patch 62)

The signed funded fixtures now submit the retained lock once through a test
boundary that confirms real node acceptance internally, then deliberately returns
an error instead of a success acknowledgement to the caller.
The caller checks that the injected failure occurred, does not rebuild or retry
the lock, and lets the accepted transaction be mined.
The existing separately restarted recovery child then loads the retained intent
and performs read-only observation on the expected chain before pending recovery.
Both the redemption and cancellation/refund paths use this mode.

This injects a client-visible lost acknowledgement, not a TCP disconnect or
process kill during the RPC. The fault controller knows the node accepted it;
production code cannot infer acceptance from an error.
The test parent and its other retained context remain alive.
Durable outcome evidence, whole-app restart and production retry policy remain
unfinished; this test introduces no live broadcast/retry mechanism.
Claude remains unavailable within its previously reported session-limit window.

Validation passed: both funded trade outcomes with the injected acknowledgement
failure, 76 library tests, two compile-fail documentation tests, all-feature/
all-target compilation, two runner unit tests and 62-patch reconstruction.
Evidence: `build/bitcoin-lost-ack-acceptance.log` and
`build/bitcoin-lost-ack-compile.log` in the core checkout.
Existing warnings remain; rustfmt was unavailable. Tests still use partial XMZ
payouts and do not establish live readiness. No production resources changed.

### Durable historical Bitcoin observations (patch 63)

`record_bitcoin_observation` reloads guarded submission data under recovery
ownership, performs fresh read-only observation, then exclusively writes a pin
and record using the existing file/directory synchronization order.
The record binds its observation ID, txid, wtxid, sampled genesis/tip and result;
the envelope and pin bind the retained signed trade commitment and swap.
RPC failures do not produce records. Storage errors can leave occupied files
and never authorize deletion, replacement or a new transaction.

`load_bitcoin_observation` checks the stored hash, domain, observation identity,
transaction identity including witnesses, and expected genesis.
It deliberately returns `HistoricalBitcoinObservation` without querying a node.
The historical result does not prove current presence, spendability, confirmation
or finality. Caller-supplied UUIDs are audit identities, not chronological ordering
or a latest-state selection mechanism. No broadcast receipt is implied.

The first funded child saves history before its deliberate native-RPC failure.
The next child loads that prior record from disk, then performs and records its
own fresh observation. Both round-trip the record and reject duplicate writes
and a nil observation identity.
Private storage and caller-retained context remain trust assumptions; encrypted
secrets, observation discovery, whole-app recovery, full payouts and GUI execution
are not supplied by this audit-history primitive.
Claude's reported session-limit reset had not arrived; no new review is claimed.

Validation passed: 76 library tests, two compile-fail documentation tests,
both funded trade outcomes with cross-child history reload and duplicate-ID
rejection, all-feature/all-target compilation, two runner unit tests and
63-patch reconstruction.
Evidence: `build/bitcoin-observation-history-acceptance.log` and
`build/bitcoin-observation-history-compile.log` in the core checkout.
Existing warnings remain; rustfmt was unavailable. No production wallet, node,
GUI installation or public package was changed; live activation remains gated.

### Exact-net reserve-return construction experiment (patch 64)

The user confirmed that quoted XMZ is the exact recipient quantity, with native
network fees separately funded. Historical signed terms are not reinterpreted.
`full_proceeds::preview_exact_net` now constructs two explicit destinations:
the unchanged experimental principal and a reserve-return output from which
the native fee alone is deducted. It validates exact lock consumption and
principal plus return plus fee conservation, with no exported signing metadata
or relay authority. Reserve ownership is supplied by the trusted caller, not
selected by this function.

The funded spending fixture invokes this construction before its existing
partial transfer. The reserve-return address is public fixture-only data and
receives no funds. This is not a signed exact-net trade or a completed reserve
return. The experiment requires distinct destinations and a reserve greater
than the approved fee ceiling. Common-address refund handling, signed reserve
ownership, durable two-output preparation and actual full settlement remain.
See `DEX_FULL_PROCEEDS_ACCEPTANCE.md` for the accounting decision and boundaries.

Validation passed: 77 library tests, two compile-fail documentation tests,
both funded outcomes including the exact-net construction, all-feature/all-target
compilation, two runner unit tests and 64-patch reconstruction.
Evidence: `build/exact-net-preview-acceptance.log` and
`build/exact-net-preview-compile.log` in the core checkout.
Existing warnings remain; rustfmt was unavailable. Claude's session limit
prevented a second review. No production wallets, packages or live gates changed.

### Explicit delayed-top-up source binding (patch 65)

`preview_exact_net_with_top_up` accepts a distinct top-up transaction identity
and requires the original lock to contain the exact principal, with the top-up
containing the exact reserve. It reuses the exact-net output accounting and
requires all and only the two sources' available key images to be selected.
The existing single-lock preview and relay restrictions remain unchanged.
No transaction metadata or relay object is returned.

Input-binding unit tests reject missing/repeated identities, substituted amounts,
spent/frozen/locked outputs, omitted selected inputs and extra selected inputs.
This does not prove who funded the top-up or that Bitcoin redemption is stable.
Those remain trusted caller obligations pending signed context and durable
funding orchestration. Funded two-source RPC construction is still untested;
the existing funded regression fixtures exercise the single-source preview and
partial transfer, not a completed delayed top-up.

Validation passed: 78 library tests, two compile-fail tests, both existing funded
outcomes, all-feature/all-target compilation, two runner tests and 65-patch
reconstruction. Evidence: `build/top-up-preview-acceptance.log` and
`build/top-up-preview-compile.log`. Existing warnings remain and rustfmt is
unavailable. No live activation or production wallet operation occurred.

### Real two-source construction fixture (patch 66)

The native fixture now imports two separately confirmed payments into a fresh
disposable wallet and calls `preview_exact_net_with_top_up`. Their values are
100,000,000,000 principal atoms and 10,000,000,000 reserve atoms.
Both are funded from the test mint wallet: this is not authenticated buyer
funding or execution of the proposed post-redemption top-up protocol.
It checks exact principal preservation, reserve-plus-fee accounting, wrong
chain/source/amount rejection and unchanged balance, height and empty pool.

The first run correctly rejected the fixture's 1,000,000,000-atom ceiling:
the real two-input fee was 1,379,200,000 atoms. The fixture now approves
2,000,000,000 atoms and explicitly retains rejection at 1,000,000,000 and one
atom. No production fee ceiling was increased. Evidence of that diagnosis is
`build/top-up-fixture-accounting.log`. Source count affects the fee budget;
do not reuse a one-input estimate for top-up claims.

No new claim transaction is broadcast or signed metadata exported.
Actual top-up dispatch, authenticated funding ownership, durable two-source
settlement/recovery, onboarding and cancellation policy remain unfinished.

Final validation passed: 78 library tests, two compile-fail tests, both funded
outcomes with the new disposable two-source construction check, all-feature/
all-target compilation, two runner tests and 66-patch reconstruction.
Evidence: `build/top-up-fixture-final-acceptance.log` and
`build/top-up-fixture-final-compile.log`. Earlier failed logs are retained to
document the insufficient fixture ceiling. Existing warnings remain; rustfmt
was unavailable. Claude's session reset had not arrived. No production resource
or live-trading gate changed.

### Private prepared two-source transaction (patch 67)

`prepare_exact_net_with_top_up` constructs with relay disabled and returns
`PreparedExactNet`, which retains signed metadata in a private zeroizing string.
It exposes only validated accounting and transaction identity, not metadata,
serialization or relay. Preparation verifies both source totals/input sets,
unchanged exact principal, returned reserve, fee ceiling and stable chain/wallet.
Public accounting includes both transaction sources and both destinations.
It is not a signed quote, ownership proof or persisted execution intent.

The native two-source fixture prepares and drops the object and checks its
public terms. The Python driver checks unchanged balances and an empty pool.
Unit tests reject malformed metadata and substituted principal and ensure the
private metadata is absent from public accounting. Durable intent-before-relay,
top-up dispatch, restored private transaction state and actual settlement remain.

Validation passed: 79 library tests, two compile-fail tests, both funded outcomes
with private two-source preparation and no balance/pool change, all-feature/
all-target compilation and 67-patch reconstruction. Evidence:
`build/prepared-exact-net-acceptance.log` and
`build/prepared-exact-net-compile.log`. Existing warnings remain; rustfmt was
unavailable. Production resources and live-trading gates remain unchanged.

### Deterministic exact-net post-construction checks (patch 68)

Both exact-net preview and preparation now call `revalidate_exact_net` through
a private `ConstructionContext` implementation backed by the native RPC.
Tests inject absent/replaced wallets, changed genesis/tip/height and RPC failures,
and check wallet-before-snapshot ordering and early termination on wallet errors.
Public callers still cannot inject an observation implementation.
The validation order and fail-closed behavior are unchanged.

These tests address a concrete Claude review follow-up, but do not simulate the
entire async construction sequence or establish atomicity between RPC calls.
Exclusive wallet-service ownership remains the caller's obligation. A matching
pair of observations is not a proof against arbitrary intervening reorgs or
changes after return; later relay must independently validate its requirements.

Validation passed: 81 library tests, two compile-fail tests, both funded regression
outcomes including two-source preparation, all-feature/all-target compilation,
two runner tests and 68-patch reconstruction. Evidence:
`build/exact-net-context-acceptance.log`, `build/exact-net-context-compile.log`.
Claude's narrow follow-up review is recorded in
`build/claude-exact-net-context-review.log`; it is not an independent audit.
Existing warnings and unavailable rustfmt remain. No live resources changed.

### Write-once exact-net journal (patch 69)

`ExactNetJournal` persists an intent before construction and validates the native
prepared object's source identities, destinations, principal, total, account,
priority and fee ceiling against that intent before writing preparation evidence.
It reuses an extracted `reserve_intent` helper with the original v1 lock identity,
so another spending mode or top-up identity cannot reserve that original lock
again in the same retained directory. Existing v1 record semantics are unchanged.
Files are exclusively created relative to a checked private directory descriptor
and both file and directory are synchronized. Torn/occupied records are never
overwritten. No signed metadata, receipt, relay permission or retry API is added.

The funded fixture reserves before preparation, persists the actual public
transaction record, rejects duplicate records and reservations (including changed
top-up identity), and rejects a different principal before creating a prepared
file. Python checks persisted source/destination/accounting fields and private
file modes. A unit test covers cross-mode reservation collisions in both directions.
The existing v1 cross-process/torn-record tests exercise the shared helper too.

Two-source journal reload/reconciliation, private transaction persistence,
authenticated top-up funding, wallet-file identity, whole-app recovery and relay
remain unimplemented. The reservation must be retained in the same caller-owned
directory; this is not protection from hostile same-user code deleting records.

Claude reviewed the initial implementation and identified fast-test coverage
gaps rather than a concrete invariant defect. Added unit tests now cover each
reserved field's mismatch/absence, malformed top-up identities before writing,
private-directory enforcement and a truncated exact-net intent blocking retry.
Prepared-record duplicate rejection remains covered by the real fixture; the
shared exclusive-create primitive also has existing unit coverage.
Review: `build/claude-exact-net-journal-review.log`. Its comments about ignored
tests not running do not describe the consolidated runner, which invokes them.

Final validation passed: 85 library tests, two compile-fail tests, both funded
outcomes with real prepared-journal persistence and duplicate/mismatch rejection,
all-feature/all-target compilation, two runner tests and 69-patch reconstruction.
Evidence: `build/exact-net-journal-final-acceptance.log` and
`build/exact-net-journal-final-compile.log`. Existing warnings and unavailable
rustfmt remain. No production wallets, nodes, packages or live gates changed.

### Historical exact-net record reload (patch 70)

`ExactNetRecovery::load` reuses the bounded, descriptor-relative private record
reader and requires exact intent equality with caller-supplied retained terms.
It rejects unknown prepared fields, incorrect schema, changed intent-bound fields,
invalid transaction identities, noncanonical amounts, excessive fees and any
failure of principal plus reserve return plus fee conservation.
Only immutable public preparation terms are exposed. No RPC or write occurs.

The fakechain driver launches a fresh native process to reload the records after
the preparing process exits. It checks the recorded transaction identity, rejects
changed expected principal, and verifies unchanged files, balance and pool.
Unit tests cover field mismatch, schema and extra-field rejection, amount
format/overflow/conservation and hash constraints. Existing legacy recovery uses
the extracted reader with unchanged decoding and reconciliation semantics.

This is historical preparation, not evidence of submission, confirmation or
settlement. The expected terms must come from trusted retained context, not these
files. No signed metadata is recovered, and no relay or retry path is enabled.
The exact-net two-destination chain reconciliation still needs implementation.

Validation passed: 87 library tests, two compile-fail tests, both funded outcomes
with fresh-process historical reload, all-feature/all-target compilation, two
runner tests and 70-patch reconstruction. Evidence:
`build/exact-net-reload-acceptance.log`, `build/exact-net-reload-compile.log`.
Claude review and triage are recorded in `DEX_PREPARED_EXACT_NET_REVIEW.md`.
Existing warnings and unavailable rustfmt remain. No live resources changed.

### Read-only exact-net reconciliation (patch 71)

`ExactNetRecovery::reconcile` queries the exact recorded transaction on the
dedicated trusted wallet RPC and checks wallet identity and stable chain context.
Its verifier matches transaction identity, total paid amount, fee, account,
unlock time, both distinct destinations and their individual amounts, and exact
confirmation arithmetic at the sampled tip. Destination order may differ.
Errors, absent/pending transfers, insufficient depth and changed context remain
unresolved. No signing, broadcasting, wallet lifecycle operation, refresh or
journal write is performed, and the result supplies no retry authorization.

Synthetic unit evidence exercises valid/reordered destinations and rejection of
changed values, duplicated destinations, wrong identity/type/account, invalid
heights/depth and chain mismatch. It also covers a fee exactly at the approved
ceiling during record loading. A fresh real-fixture process verifies the
unbroadcast prepared transaction cannot be confirmed while files/balances remain
unchanged. Positive real-chain confirmation of this exact two-output transaction
is not yet covered because controlled exact-net relay is still absent.
This trusts local RPC evidence; it is not an independent cryptographic proof of
all source outputs, nor a complete protocol top-up or whole-app recovery test.

Validation passed: 88 library tests, two compile-fail tests, both funded outcomes
with real unbroadcast-reconciliation rejection, all-feature/all-target compilation,
two runner tests and 71-patch reconstruction. Evidence:
`build/exact-net-reconcile-acceptance.log`, `build/exact-net-reconcile-compile.log`.
Claude review: `build/claude-exact-net-reconcile-review.log`, with scope corrections
in `DEX_PREPARED_EXACT_NET_REVIEW.md`. Existing warnings and unavailable rustfmt
remain. Production resources and live-trading gates were unchanged.

### Journal-backed exact-net relay (patch 72)

`ExactNetJournal::authorize_relay` consumes a private prepared transaction only
after its matching public accounting record is durably written.
The resulting opaque capability is required by `NativeTransferRelay::relay_exact_net`.
Relay rechecks wallet, chain context and both available funding sources before
dispatching the exact prepared metadata once. Ambiguous RPC outcomes remain
unresolved rather than authorizing another payment.

The disposable fakechain fixture confirms 100 XMZ at the payout destination,
unused reserve at a separate return destination, exact fee accounting and zero
source residual. A fresh process reconciles both outputs without a receipt.
This uses separately generated test wallets and does not yet prove negotiated
buyer funding or full protocol-driven settlement. The existing two-party
recovery payment remains partial.

Claude supplied a read-only review, not an independent security audit.
Pre-dispatch crash/cancellation can leave a reserved but unbroadcast transaction
without a retry capability; reconciliation alone cannot fix that case.
Exact-net lost-acknowledgement fault injection also remains outstanding.
These are release blockers, not reasons to bypass one-shot safeguards.
No live wallets, deployment or GUI trading gates were changed.

Final validation passed: 88 library tests, three compile-fail tests, both funded
outcomes including the isolated exact-net relay, all-feature/all-target compile,
two runner tests and 72-patch reconstruction. Rustfmt remains unavailable;
existing compiler warnings remain.
Evidence: `build/exact-net-relay-final-acceptance.log` and
`build/exact-net-relay-final-compile.log`.
Review: `build/claude-exact-net-relay-review.log`.

#### Prepared broadcast acknowledgement validation

Both prepared relay paths now use the same `classify_prepared_broadcast`
validator after marking dispatch. Its test rejects RPC failures, absent or
malformed hashes and different transaction IDs as redacted
`BroadcastOutcomeUnknown`; a matching acknowledgement preserves the prepared
transaction's fee and weight. This is response-classification coverage, not
an end-to-end dropped-reply fault test or durable pre-dispatch recovery.
Those gaps remain open. The change is included in maintained patch 72.

Validation passed: 89 library tests, three compile-fail tests, both isolated
funded outcomes, all-feature/all-target compilation and two runner tests.
The acceptance runner also verified ordered patch reconstruction.
Evidence: `build/prepared-broadcast-acceptance.log` and
`build/prepared-broadcast-compile.log`.
Claude's read-only review is in `build/claude-prepared-broadcast-review.log`;
it is not an independent security audit. Rustfmt remains unavailable.
Live-trading gates and production resources remain unchanged.

#### Real exact-net lost acknowledgement fixture

The acceptance runner now performs an additional disposable exact-net payment
through a loopback-only Digest-forwarding proxy in each funded scenario.
The proxy waits for the real wallet RPC to acknowledge `relay_tx`, sends HTTP
headers and drops the promised reply body. The native test must return the
redacted `BroadcastOutcomeUnknown` and reject re-reservation of the same lock.
The proxy records exactly one accepted relay, whose transaction ID must equal
the validated prepared journal record.

After mining, a fresh native process confirms that original payment without
rewriting the journal or requiring a receipt. Recipient and reserve-return
wallets independently confirm the expected amounts and the source has zero
residual balance. No replacement transaction is constructed by recovery.
This closes the real lost-reply test gap for the isolated payment primitive,
not for negotiated buyer funding or whole-application recovery.
Pre-dispatch interruption remains unresolved.

Validation passed: 89 library tests, three compile-fail tests, both funded
scenarios with normal and lost-reply exact-net payments, all-feature/all-target
compilation, two runner tests and ordered 72-patch reconstruction.
Evidence: `build/exact-net-lost-reply-acceptance.log` and
`build/exact-net-lost-reply-compile.log`.
Claude reviewed the fixture in `build/claude-exact-net-lost-reply-review.log`.
Its phrase "already-negotiated" does not describe this separate fixture:
these sources are locally funded disposable wallets, not protocol top-ups.
The proxy's initial hash length check is followed by equality with the validated
journal hash and on-chain confirmation, so that check alone cannot pass the test.
This remains an AI-assisted review, not an independent audit.
Real wallets, public deployment and live-trading gates were unchanged.
