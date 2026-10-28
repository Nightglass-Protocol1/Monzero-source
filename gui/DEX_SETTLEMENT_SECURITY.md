# Settlement-enforced XMZ fee: design requirements

Status: requirements and commitment encoding only. No settlement protocol has
been selected, proven, implemented or enabled. Do not interpret a valid terms
payload or signature as authorization to transfer funds.

## Required outcomes

- Successful protocol trades deliver the agreed principals and XMZ protocol fee.
- Failed, cancelled and refunded trades generate no protocol revenue. Already
  incurred native chain fees are distinct and may not be recoverable.
- An XMZ buyer can start with zero XMZ: the acquired principal funds the fee.
- External-to-external trades require separately funded XMZ fee settlement.
- A modified client must not be able to complete an accepted protocol settlement
  while omitting the agreed fee. This does not govern independent exchanges.

## Implemented commitment boundary

`OfferIdentity` supports encrypted export/import of its Ed25519 signing seed.
The versioned binary header pins Argon2id v1.3 to three passes and 64 MiB, with
a fresh 16-byte salt and 24-byte XChaCha20-Poly1305 nonce on every export. The
header is authenticated as associated data. Import checks the exact header and
length before running the fixed-cost derivation; wrong passwords and modified
blobs fail without producing an identity. Temporary seed and derived-key buffers
are wiped on exit. The passphrase is caller-owned and must be cleared by its
owner; the current identity's in-memory secret is not protected against host
compromise or swapping. Strong passphrases remain necessary.

The encryption API returns encrypted bytes. `OfferIdentityStore` adds explicit
CreateNew/OpenExisting modes, an exclusive lifetime lock, owner-only atomic
creation, bounded reads, and refusal to overwrite, regenerate, or repair an
existing identity. It requires a private existing parent directory, rejects
symlink targets and shared permissions, and checks Unix ownership. Its trusted
directory assumption does not defend against another process with the same OS
identity or a privileged attacker. Native Windows ACL behavior is not qualified.
Restart tests restore the same public key and signatures; wrong passwords,
corruption, concurrent opens and unsafe permissions fail closed. A service still
needs password acquisition, outgoing sequence persistence, and startup recovery
integration. No wallet spend key is involved. KDF parameter choices
follow the [libsodium password-hashing API](https://doc.libsodium.org/password_hashing/default_phf).

`SettlementTerms::build` constructs a V6 byte-length-prefixed payload and SHA-256
commitment with a fixed field order. It binds the protocol and three deployment
identifiers; assets and genesis identifiers; atomic principal, protocol-fee and
native-fee ceilings; success and refund recipients; per-chain anchor/refund heights
and confirmation depths; separate lock/refund commitments for both principals and
the XMZ fee leg; each UTXO leg's distinct redeem, cancel and punish transaction
commitments plus its lock-relative cancel and cancel-relative punish BIP68 block
windows; each party's adaptor secp256k1 and Monzero/Ed25519 swap keys;
each party's exact DLEQ proof commitment; both signing public keys; nonce; quote expiry; an indicative
wall-clock deadline; and the success-only XMZ fee policy. No floating-point
arithmetic is used. Address strings are bounded but NOT chain-validated. Key length
checks do NOT prove key ownership. Genesis syntax checks do NOT verify networks.
The nonce must come from secure random generation.
Accepted maker identity/nonce pairs are now recorded in a persistent replay
database and bound to the canonical commitment and expiry.
V6 additionally requires the exact opaque fill-reservation commitment, preventing
an accepted settlement quote from being moved to another crossing or quantity.

`SignedOffer` now provides a separate canonical V1 envelope for market discovery.
It accepts only the supported BTC/XMZ, LTC/XMZ and BCH/XMZ markets, lowercase
buy/sell direction, canonical positive atomic integers, a lowercase UUID-shaped
offer identifier, explicit protocol/network domains and a lifetime no longer than
24 hours. The maker signs the length-prefixed payload with Ed25519, and verification
returns opaque evidence only after the deployment domain, lifetime and signature
all match. Mutation, cross-network replay, impostor keys, expiry and ambiguous
numeric encodings are covered by regression tests. The evidence always reports
`executable=false`: it proves authorship of advertised terms, not funds, liquidity,
settlement validity or permission to broadcast.

`RelayEnvelope` supplies a separate transport-authentication boundary around an
offer or cancellation commitment. Its canonical length-prefixed V1 payload binds
the exact 32-byte artifact commitment, message type, protocol/network deployment,
one-hour maximum lifetime and nonzero sender sequence to an Ed25519 identity.
Opaque evidence is emitted only after signature, domain and current-lifetime checks,
and always reports `executable=false`. `RelayReplayStore` re-verifies that evidence
against immutable deployment configuration before atomically recording it in a
locked, owner-only file. A new sender must start at sequence one and may then advance
only by one; an exact retry is idempotent, while gaps, stale sequences and conflicting
same-sequence commitments fail closed across restarts. The store provides neither
network transport nor order-book insertion, liquidity proof or broadcast authority.
`RelayIngress::Coordinator` bridges those two local stores without weakening either
proof boundary. It re-verifies the outer relay evidence and inner maker evidence,
requires an exact artifact commitment and message-type match, persists the relay
sequence first, then invokes the idempotent offer-book operation. Consequently an
exact retry can complete a write interrupted between stores. Invalid substitutions
do not consume a sequence; a valid but premature cancellation can be retried after
its referenced offer arrives. The returned opaque evidence commits both inputs and
still reports `executable=false`.

Relay replay storage version 4 persists the signed envelope, pending inner artifact,
and whether it is pending
order-book application. Later sequences from that sender are blocked until the
coordinator has applied the book mutation and durably cleared that flag. This
also holds across restart and prevents a newer message from discarding recovery
of an unfinished cancellation. Earlier file versions are rejected because they lack
the signed recovery envelope or application status. Stored envelopes are reverified
against the configured deployment and record commitment on load, including canonical
field encodings. An exact pending envelope can be retried after its
relay lifetime ends: its signature is rechecked at its signed creation time, its
commitment must match the durable pending record, and the inner artifact is still
validated at the current time. Completion removes this exception. This allows a
pending cancellation to recover without admitting new expired relay messages.
The coordinator reconstructs both signed artifacts from the store and independently
re-verifies the inner offer or cancellation during recovery. Inner payloads are
bounded to 4,096 serialized JSON bytes on preparation and load, and completed
records must have empty payloads. Corrupted inner signatures or commitments leave
the operation pending without changing the order book. Expired inner offers
and permanently invalid pending messages require a recovery policy before
enabling transport; there is currently no automatic skip or reset.

`RelayWireCodec` provides a deterministic binary representation for that opaque
relay evidence. It uses fixed network-byte-order integers, bounded length-prefixed
text, fixed-size artifact/key/signature fields, an explicit magic/version and an
exact body length, with a hard 1,024-byte frame ceiling. Decode rejects every
truncated prefix, appended data, inconsistent or oversized lengths, signature
mutation, expiry and deployment mismatch. It then re-encodes the verified evidence
and requires byte equality, eliminating alternate wire encodings. This is framing
only and opens no socket.

The connection-local `RelayWireCodec::StreamDecoder` handles fragmented and
coalesced frames, with at most one 1,024-byte partial frame retained and a
16,384-byte limit per input chunk. It validates declared lengths before collecting
the body and verifies lifetime when the complete frame arrives. Malformed input,
oversized chunks, or truncated end-of-stream permanently invalidate that decoder;
the transport must close the connection rather than attempt resynchronization.
Each feed returns its authenticated wrappers only if that entire feed succeeds.
Previously returned batches are not rolled back. Wrappers still require replay
and inner-artifact validation through ingress. A future socket owner must enforce
read-size limits, idle deadlines, connection quotas, and message-rate limits;
the decoder does not provide those transport policies.

`RelayIngress::Coordinator` also encodes and accepts complete market messages:
canonical compact JSON version 1 containing the lowercase-hex relay wire frame
and the signed offer or cancellation artifact. Each message is limited to 8,192
bytes before JSON parsing. The receiver rejects alternate JSON encodings,
unexpected fields, noncanonical hex, invalid signatures, deployment mismatches,
and wrapper/inner commitment mismatches before preparing replay state. It then
uses the existing crash-recoverable ingress operation. Encoding alone does not
mutate the order book. These complete messages require a message-oriented
transport or separate length framing; the wrapper-only stream decoder above
must not be used to parse these JSON messages.

`RelayPeerConnection` is an opt-in TCP handler for those complete JSON messages,
prefixed with a four-byte unsigned big-endian length. It attaches to a supplied
socket; it does not listen or discover peers. It bounds socket buffering and
partial messages, rejects invalid lengths before reading a body, caps outgoing
queued bytes at 65,536, and closes on incomplete-message timeout (10 seconds),
truncation, failed ingress, or more than 64 complete messages per one-second
window. Processing yields after eight messages per callback. Completed messages
are applied individually through durable ingress; earlier accepted messages are
not rolled back if a later message fails. Loopback tests exercise actual TCP
fragmentation and coalesced offer/cancellation retries.

This transport provides no encryption, peer discovery, global connection/admission
limits, idle-connection eviction, or source-address privacy. Only public signed
market artifacts belong on it. Per-connection rate limits are not Sybil
protection. Socket and coordinator lifetime/thread constraints apply, and public
deployment remains disabled pending transport integration and adversarial tests.

`RelayPeerServer` supplies an explicitly started listener around the connection
handler. Its caller must select the bind address and port; construction alone
does not expose a listener. Admission is capped at eight active connections and
two per source address, treating IPv4-mapped IPv6 addresses as their IPv4
counterparts. Disconnection and invalid framing release slots. Admitted sockets
are closed after 60 seconds without an accepted market message; partial bytes do
not refresh that idle timer. Stopping closes the listener and its admitted
sockets without deleting market state. These limits bound local connection
resources, not distributed Sybil attacks. No GUI startup path enables this
listener, and no public deployment has been performed.

`OfferBookStore` is the first local consumer of the opaque signed-offer evidence. It holds an
exclusive process lock, atomically replaces an owner-only versioned file, accepts
only evidence that re-verifies against immutable protocol/network configuration,
and re-verifies every signature and evidence commitment on restart. Exact duplicate
insertion is idempotent, while reuse of an offer identifier for different content
fails closed. Active queries exclude expired offers and explicit pruning is atomic.
Corruption, unexpected fields, duplicate persisted identifiers and concurrent
writers fail closed. Canonical cancellation evidence now binds the exact offer ID
and commitment, maker identity, deployment domain and cancellation time. The store
accepts it only from the original maker, within the signed offer lifetime; it
retains and re-verifies the evidence after restart and excludes the cancelled offer
from active results at and after the signed cancellation time; historical validation
before that time remains reproducible. Impostor, substituted, future and late cancellation tests fail
closed. The store is not a relay and does not prove liquidity. Replacement remains
a new signed offer with a fresh identifier; no implicit mutation is accepted.

`OfferMatcher` re-verifies both signed offers at the matching time and accepts only
an active buy and sell for the same market, from distinct maker identities, where
the buy limit meets or exceeds the sell ask. Its domain-separated commitment binds
both offer commitments and identifiers, each original capacity, the maximum common
base-atomic quantity, the shared validity deadline, and the inclusive ask-to-limit
price interval. The validating order book exposes this operation only
for entries that are still active and not cancelled. It does not infer maker/taker
priority from caller-controlled timestamps and does not select an execution price;
those terms require a later authenticated quote. It also does not reserve remaining
quantity, prevent multiple peers from proposing the same crossing, or authorize
execution, and therefore always returns `executable=false`.

`OfferFillReservationStore` now supplies the missing local quantity boundary. A
reservation embeds and re-verifies both signed offers and their crossing, binds a
canonical reservation identifier, exact base-atomic amount and expiry into an
opaque commitment, and is capped at 15 minutes and the shared offer deadline. It
checks the current validating order book before reservation and again before
commit, so cancellation or expiry prevents advancement. Aggregate reserved and
committed quantities are checked independently against both original offer
capacities with overflow-safe arithmetic, including after restart. Exact retries
are idempotent; conflicts, overfill, corruption and concurrent writers fail closed.
Released or naturally expired uncommitted holds free capacity. Committed fills do
not: commitment requires an opaque mutually authorized V6 quote (both participant
signatures verified) whose reservation field, two
participant identities, external base amount and XMZ principal all match the
reserved crossing. The XMZ principal must satisfy the exact rational ask/limit
interval, compared with 128-bit integer products. For indivisible atomic amounts
this rounds the minimum up and the maximum down; an interval containing no whole
atomic amount cannot be committed. Reservation, identity, quantity and price
substitutions fail closed. No
maker-only quote may commit a reservation, including on the idempotent retry path.
Existing committed history is retained for recovery rather than released by this
authorization check. Fill storage requires a private owning directory and, when
present, a private regular file owned by the process user on Unix. Symlinked
files/directories and shared permissions fail closed; saving checks the location
again and requires atomic replacement with successful private permissions. These
checks do not protect against a hostile process with the same user authority. The
history is never automatically chmodded, deleted or reset by a failed check.
`recoverCommitted` is a read-only lookup requiring an opaque
recovered mutual quote and exact quote/reservation commitments. It can identify
an existing committed fill after expiry, but cannot create a commitment, renew a
quote, release capacity, sign or broadcast. It does not restore deleted history
or protect against a local attacker rewriting store files. No
generic hash can label a committed fill settled or refunded. Settlement now requires
opaque outcome evidence produced by the journal only after it revalidates the exact
accepted quote, current non-reorged external redeem confirmation, and the matching
XMZ delivery-plus-fee transaction, its currently confirmed durable tracker entry,
and fee-earned journal event. The outcome binds
the swap, quote and fill reservation commitments; exact retries are idempotent and
settled quantity stays consumed. The two typed XMZ refund proofs are implemented:
the principal and fee legs require distinct signed intent commitments and confirmed
outgoing wallet transactions bound to exact recipients, amounts, fee ceilings,
refund heights/deadline, network genesis and active blocks. Neither proof alone can
release capacity. The journal requires both of them, distinct transaction IDs,
the same accepted quote, and a currently confirmed, non-reorged external
`pay_refund`; successful settlement and refund paths are mutually exclusive. The
fill ledger itself invokes fresh Bitcoin and XMZ tracker validation, requires each
tracked block hash to equal its typed evidence, and only then
persists `refunded`, making the quantity reusable. The version-2 ledger migrates version-1
records in memory, is owner-only, atomically replaced and always non-executable.

Fresh quote acceptance and post-acceptance recovery now use distinct validation
entry points. `SettlementTerms::build` continues to reject a quote at or after its
expiry. `SettlementTerms::rebuildAccepted` revalidates every structural, amount,
deployment, key, commitment, fee and deadline invariant without reapplying quote
freshness, and reproduces the byte-identical V6 payload. Recovery and confirmed
success evidence use that path because expiry must stop new acceptance, not strand
an already funded swap. Their journal transitions still require the exact earlier
`quote_accepted` commitment; rebuilding terms alone never proves acceptance or
authorizes execution.

Cross-curve verification now produces an opaque typed evidence object. A journal
commitment exists only when the V6 terms and proof are bound, the pinned sandbox
response is authenticated, cryptographic verification succeeds, and execution is
still disabled. `SettlementJournal::appendDleqVerification` accepts only that
object and only when the swap's preceding `quote_accepted` artifact is the exact
same V6 settlement commitment. Maker and taker verification events are distinct
and cannot be appended twice. No failed or merely well-formed verifier response
can advance this journal boundary.

This remains a draft negotiation encoding, not proof that the committed scripts or
transactions are correct. Do not broadcast from it. Before broadcast is integrated,
verified chain adapters must construct and decode the committed lock/refund objects,
enforce a reviewed per-leg timing relationship, bind exact success/failure outputs,
and persist accepted terms plus recovery material before each funding action.

## Threats that the eventual protocol must address

1. Fee omission/substitution: demonstrate that principal redemption cannot bypass
   fee settlement, even using a modified client and direct node access.
2. Early fee redemption: a fee recipient must not collect merely because one
   party exposed a secret before the other principal transfer completed.
3. Partial execution: a common hash or two independent swaps alone does not prove
   three-chain all-or-nothing settlement. Explicitly analyze every redemption order.
4. Reorgs and timing: define bounded chain assumptions and staggered per-leg
   recovery windows. One wall-clock deadline is not sufficient for execution.
5. Restart/replay: durable reservation and recovery records must reject duplicate
   quotes and reconcile confirmed transactions after every crash boundary.
6. Counterparty disappearance: participants must recover without the coordinator
   or fee recipient cooperating. Specify network/liveness assumptions honestly.

## Next acceptance milestone

Choose and analyze a BTC/XMZ protocol compatible with the actual Monzero chain.
Model adversarial redemption/refund orderings before handling test funds. Verify
fee omission, premature fee redemption, restart and refund cases on isolated
chains. External-coin pairs introduce a third settlement chain and need separate
analysis. Standard routing through an external liquidity protocol does not by
itself meet the XMZ fee requirement. Independent review is required before live
execution; all activation guards remain in place.

## Implemented reservation safety boundary

The local XMZ reservation store uses an exclusive process lock and atomic file
replacement. Amounts are canonical unsigned decimal strings with checked 64-bit
arithmetic. Reserved and committed amounts remain unavailable to competing offers;
only reserved records may be released, and committed records require a settled or
refunded terminal outcome plus lowercase 32-byte transaction evidence. Tests cover
overspend, corruption, concurrent access, terminal-state idempotence, owner-only
file permissions and abrupt process termination before destructors run.

This provides durable local accounting only. A transaction hash is stored as
evidence but is not yet queried, confirmed, reorg-tracked or cryptographically bound
to the accepted terms. The store therefore cannot authorize execution and cannot
replace settlement recovery or persistent quote-nonce replay protection.

## Implemented signed-quote acceptance boundary

`SignedQuoteAcceptor` rebuilds and validates canonical terms, verifies the maker's
Ed25519 signature in an explicit network domain, and atomically records a hashed
maker/nonce identifier before reporting acceptance. Quote lifetimes are capped at
seven days and store size is bounded. Exact retries are idempotent; reuse of the
same maker nonce with a different commitment or expiry fails closed, including
after restart. Corrupt stores and concurrent writers also fail closed.

Acceptance still returns `executable=false`. The network-domain label is supplied
by immutable acceptor configuration together with the expected protocol and all
three deployment identifiers; quotes that disagree fail before signature checking.
The quote does not yet prove liquidity, address ownership, lock construction,
confirmations, settlement, fee payment or refund safety.

## Implemented confirmation safety boundary

`ConfirmationTracker` durably records chain transaction observations using exact
unsigned decimal heights. Confirmed evidence is accepted only when confirmations
equal `nodeHeight - blockHeight + 1` and a canonical transaction and block hash are
present. Required confirmation policy is immutable after tracking begins. A block
replacement, confirmation rollback or disappearance after mining creates a sticky
reorg state, including across restart. That state cannot report confirmation even
if a later observation reaches the original threshold.

The tracker consumes normalized evidence; chain-specific adapters do not yet obtain
and verify all transaction fields from BTC and XMZ RPC. It therefore remains a
non-executable state machine, not proof that the transaction pays the committed
recipient and amount.

## Implemented Bitcoin evidence boundary

`BitcoinLockPsbtRpcVerifier` binds an unsigned lock PSBT to the exact current V6
settlement terms rather than accepting caller-supplied amounts, keys, fee ceilings
or commitments as authority. It accepts only a BTC regtest leg, derives the COMIT
P2WSH program from the ordered signed keys, verifies the signed lock commitment,
checks the pinned genesis and synchronized node identity before and after strict
PSBT/prevout/fee validation, and requires both identity snapshots to name the same
pinned best block. Successful validation returns opaque, domain-separated prepared
evidence with `executable=false`. The settlement journal accepts that evidence as
`<leg>_lock_prepared` only beneath the exact accepted quote commitment; mutable
maps and arbitrary hashes cannot create the transition. This is preparation
evidence only and supplies no signing or broadcast authority.

`BitcoinTransactionEvidence` validates the Bitcoin Core 31 `getrawtransaction`
object together with the corresponding `getblockheader` object. It binds the
expected txid and destination, aggregates matching outputs in satoshis, rejects
fractional satoshis and duplicate output indexes, and requires the transaction and
header confirmation counts to agree with the independently supplied node height.
If `in_active_chain` is present it must be true. Mempool observations cannot carry
block evidence. All results remain `executable=false`.

The isolated lab now has a deliberately created BTC regtest fixture. Its expected
destination and amount plus its signed transaction were atomically persisted before
broadcast; its txid was derived from those signed bytes. After confirmation the
signed bytes were removed from the owner-only manifest. The C++ live probe reads
that manifest, independently queries `getrawtransaction` and `getblockheader`, runs
this validator, and feeds the normalized result into `ConfirmationTracker`. The
probe passes against Bitcoin Core 31.1. Rerunning fixture creation was verified not
to generate blocks or create a second payment.

## Implemented Monzero evidence boundary

`MonzeroTransactionEvidence` validates incoming or pool transfer entries from the
pre13 wallet RPC against synchronized, trusted fakechain node information. It binds
the expected txid, wallet-owned destination and exact atomic total; rejects outgoing,
mixed-state, double-spend-marked or inconsistent entries; and requires a mined txid
to appear as either the miner transaction or a regular transaction in the claimed
trusted, non-orphaned block. It explicitly converts daemon chain height (a block
count) to the tip height consumed by `ConfirmationTracker`, avoiding an off-by-one
confirmation error. Confirmed but locked outputs do not report spendable. All
results remain `executable=false`.

Because Qt's JSON representation stores RPC numbers as doubles, this boundary
fails closed above the largest exactly representable JSON integer. Production-sized
amount support requires a raw numeric-token parser or an RPC representation that
returns atomic amounts as strings; silently accepting rounded amounts is forbidden.

The isolated lab now has a deliberately created XMZ fixture whose destination,
amount, txid and transaction metadata were atomically saved before `relay_tx`.
After the separate receiver wallet observed the confirmed payment, the metadata was
removed from the owner-only manifest. The combined C++ live probe validates its
wallet and block evidence, records its confirmations, verifies it is unlocked and
reserves part of its balance without enabling execution.

Recovery testing found that fakechain data is deliberately deleted at daemon start
unless `--keep-fakechain` is supplied. The lab service now requires that flag and
explicitly flushes generated chain state through `/save_bc`. A controlled VM reboot
then preserved BTC height 104 and XMZ daemon height 134; both fixture scripts were
idempotent, generated no new blocks, and the combined live probe passed afterward.
Wallet sets made inconsistent by the earlier ephemeral-chain restart were retained
in named owner-only recovery archives rather than overwritten. The corrected lab
state is captured in the `dex-persistent-fixtures-20260908` Proxmox snapshot.

The hardened wallet service also supplies an owner-only shared-ringdb directory in
the recovery tree. This preserves `ProtectHome=true` while avoiding the prior
permission failure at the default home-directory path; the relocated LMDB was
created successfully and no new ringdb permission errors appeared after restart.

## Implemented settlement journal boundary

`SettlementJournal` provides an atomically committed, logically append-only,
hash-chained record of settlement artifact commitments. It deliberately stores no
private keys, raw signatures or raw transactions. It rejects malformed history,
ambiguous duplicate operations and concurrent writers. Each swap must begin with
its accepted quote commitment; each typed pay/receive/XMZ-fee artifact then follows
prepared, signed, broadcast and confirmed ordering. It deliberately rejects generic
settled/refunded terminal events: opaque protocol-specific outcome evidence is
available only after the compound typed chain proofs and durable current-state
trackers succeed. The chain detects record
damage and internal mutation; because an unkeyed file can still be truncated and
rebuilt by a local attacker, callers must checkpoint its head in separately
protected state before this becomes a live-execution control.

## Implemented Bitcoin unsigned-template boundary

`BitcoinSettlementTemplate` constructs a domain-separated commitment over the
ordered, unsigned version-2 transaction template: locktime, every input outpoint
and sequence, and every output index, exact satoshi value and script bytes. Lock
validation requires one shared P2WSH settlement output plus at most one change
output and checks an independently obtained PSBT fee against the signed ceiling.
Spend validation requires the exact committed parent outpoint and CSV/final
sequence, one committed recipient output, a positive fee derived from the parent
amount, and the signed fee ceiling. Coinbase, populated script signatures,
witnesses, duplicate outpoints/indexes, fractional satoshis and ambiguous outputs
fail closed. Results always remain `executable=false`.

Before a Bitcoin lock PSBT can pass the RPC evidence boundary, the verifier also
requires Bitcoin Core's conservative `estimatesmartfee` response and the decoded
unsigned transaction's `vsize`. The actual fee must fit both the signed absolute
ceiling and a bounded network-estimate ceiling (two-times headroom by default).
Malformed rates, RPC error arrays, estimates outside the requested confirmation
horizon, arithmetic overflow, and absent virtual-size evidence fail closed. This
is validation only: the result remains explicitly non-executable.

This boundary expects the decoded transaction and lock-PSBT fee to come from the
allowlisted local Bitcoin Core RPC adapter. It does not validate scripts' redeem
conditions, signatures, adaptor proofs, PSBT UTXO provenance, or protocol timing;
those remain mandatory before signing can be connected.

`BitcoinPsbtEvidence` additionally joins Bitcoin Core 31 `decodepsbt` data to a
separate `gettxout` observation for every funding input. All inputs must be
unspent, non-coinbase, sufficiently confirmed at one pinned best-block hash, and
their exact values and scripts must match the PSBT's witness UTXOs. Input and
output totals must reproduce the reported fee and signed ceiling. Modifiable
PSBTs, missing UTXOs, pre-existing signatures/preimages, non-witness inputs and
unknown/proprietary maps fail closed for the initial BTC/XMZ scope. This still
returns `executable=false`.

`BitcoinPsbtRpcVerifier` now performs that complete read-only call sequence. It
strictly decodes canonical PSBT base64, pins `getbestblockhash`, calls `decodepsbt`,
queries every input through `gettxout`, reruns the tip query, and rejects a missing
input or any tip change before invoking the evidence validator. The general RPC
envelope parser supports the scalar and null results these methods require while
the existing object-only wrapper remains strict for existing consumers.
The RPC client also enforces its own fixed read-only method set and disables proxy
use, preventing caller-supplied allowlists from enabling mutations and preventing
loopback node/wallet authorization headers from leaking to a configured proxy.

`BitcoinSpendPsbtEvidence` applies the same fail-closed provenance model to a
refund or redeem PSBT. It requires exactly one unsigned input and one output,
matches the witness UTXO to an independently observed live P2WSH settlement
parent, pins the best block and minimum confirmations, enforces the committed
sequence and recipient, and reconciles the derived fee with the PSBT report and
signed ceiling. It derives the protocol witness script from the V6-bound ordered
keys and computes the native SegWit-v0 BIP143 `SIGHASH_ALL` digest over the exact
validated transaction and parent amount. The calculator matches Bitcoin's
published test vector and rejects non-empty scriptSig/witness data. It does not
yet authorize signing, and always returns `executable=false`.

`BitcoinEcdsaSignatureVerifier` independently verifies a supplied signature
against a 32-byte digest and compressed secp256k1 key. It requires byte-canonical
DER encoding, in-range scalars, Bitcoin's low-S policy and an explicit trailing
`SIGHASH_ALL` byte. The published BIP143 signature vector passes; digest mutation,
wrong sighash types, malformed DER, uncompressed keys and the mathematically valid
high-S twin fail. This verifies ordinary completed ECDSA signatures only; it is
not an adaptor-signature verifier and has no signing or execution authority.

`CryptoVerifierProtocol` defines the separate adaptor/DLEQ verification boundary
without importing private-key operations into the GUI. Its implementation ID pins
COMIT commit `dc6ba84b`, `ecdsa_fun` 0.10.0, `sigma_fun` 0.7.0 and `bincode` 1.3.3.
Requests bind operation, fresh 32-byte nonce and length-prefixed binary inputs;
responses must echo that commitment and use exact, fail-closed result semantics.
The pinned Rust prototype now verifies both cryptographic objects, including point
and Ed25519 subgroup checks, fixed/strict deserialization and transcript proof
verification. Deterministic internal vectors and a cross-language envelope test
pass. The parser deliberately reports `responseAuthenticated=false`: independent
`CryptoVerifierClient` now supplies hash-pinned, direct child-process authentication
and a minimal Bubblewrap filesystem with all namespaces unshared, no network,
empty environment, no capabilities, bounded I/O/time and pre/post binary hashing.
Unsafe permissions and wrong hashes fail closed. The pinned `ecdsa_fun` package's
published DLC fixtures supply an external valid adaptor vector and a wrong-proof
negative vector. Independent reproduction and an externally sourced cross-curve
vector remain required, so even an authenticated positive response cannot yet
advance settlement.

`BitcoinSpendPsbtRpcVerifier` supplies the corresponding read-only node boundary.
It rejects a PSBT that names anything except the signed expected parent before
querying, pins `getbestblockhash` across `decodepsbt` and `gettxout`, treats a null
UTXO as spent, and requires the same conservative fee-estimate policy as a lock.
Only the fixed read-only RPC allowlist is available and every outcome remains
`executable=false`.

`BitcoinSharedOutputScript` verifies the exact shared-output construction used by
the pinned COMIT reference: Miniscript `c:and_v(v:pk(A),pk_k(B))`, serialized as
`PUSH33 A CHECKSIGVERIFY PUSH33 B CHECKSIG`. Both compressed keys are decoded as
real, distinct secp256k1 curve points with OpenSSL. The witness script must match
the ordered keys byte for byte, and its SHA-256 must equal the P2WSH witness
program. This protocol uses pre-signed transaction sequences for cancel/punish
timing rather than claiming a hashlock branch exists. The chain-specific ordered
keys are mandatory fields in signed V6 settlement terms for UTXO-chain
legs and forbidden on XMZ legs. Full curve/script verification remains at this
chain-specific boundary, and every result stays `executable=false`.

The lock and spend PSBT evidence boundaries now require those V6-bound ordered
keys as inputs and independently derive the expected witness program. Consequently
a transaction commitment, address label or syntactically valid P2WSH output cannot
substitute a different pair of spending keys. Key order is significant and is
tested on both funding and recovery paths.

`BitcoinRecoveryPsbtEvidence` maps each V6 UTXO branch to its exact signed child
and parent template: redeem and cancel spend the lock, while refund and punish
spend the cancel output. It enforces the signed cancel or punish relative sequence
(and final sequence for redeem/refund), reuses the strict unsigned-PSBT,
live-prevout, fee, script and BIP143 validator, and returns `executable=false`.
Thus a caller cannot validate one recovery branch under another branch's policy.
The read-only recovery RPC orchestrator pins one best-block hash across PSBT
decoding, verbose retrieval of the exact parent transaction and lookup of its
unspent output. It reconstructs the unsigned-template commitment from normal
signed on-chain transaction fields, binds the full transaction through its exact
txid, rejects a changing tip or spent parent, and applies the conservative fee
policy only after branch validation. Before any of those calls it also requires
`getblockchaininfo` to report synchronized regtest and block zero to equal the
genesis hash in the signed V6 terms; a different local deployment fails closed.

A successful recovery RPC snapshot now yields an opaque prepared-evidence object
whose domain-separated commitment binds the V6 terms, leg, branch, deployment,
child and parent templates, BIP143 sighash, pinned best block, actual fee and
network-policy ceiling, as well as the ordered contract keys and derived witness
script. Recovery validation also compares those keys byte-for-byte with the signed
V6 terms instead of trusting equivalent caller input.
`SettlementJournal::appendBitcoinRecoveryPrepared`
accepts only this object beneath a byte-identical `quote_accepted` commitment and
records the precise `<leg>_<branch>_prepared` transition idempotently. This does
not represent signature approval or broadcast authorization.

`BitcoinSignedRecoveryEvidence` validates the next local artifact boundary. The
canonical raw SegWit transaction is parsed locally with minimal CompactSize
encodings, bounded fields and no trailing bytes. The parser derives `txid` and
`wtxid` independently, and the transaction must reproduce the exact prepared
unsigned-template commitment, retain an empty `scriptSig`, and contain exactly
the COMIT three-item witness: a strict DER/low-S `SIGHASH_ALL` signature for key A, the corresponding
signature for key B, and the exact ordered shared witness script. Swapped
signatures, extra stack elements and output mutation fail closed. Its opaque
domain-separated evidence can append only the matching
`<leg>_<branch>_signed` journal transition after the exact prepared commitment.
This stage does not query mempool/chain acceptance or authorize broadcast; those
remain separate future gates, and its result is always `executable=false`.

`BitcoinRecoveryMempoolVerifier` adds a non-mutating pre-broadcast policy gate for
that exact signed artifact. It first matches a SHA-256 commitment to the raw bytes,
pins `getbestblockhash`, calls `testmempoolaccept` with a one-transaction package,
and requires Bitcoin Core's returned txid, wtxid, vsize and base fee to agree with
the locally derived identity and prepared fee before rechecking the same tip.
Policy rejection, malformed or substituted response fields and a tip change all
fail closed. The opaque result is deliberately not a `broadcast` journal event,
does not call `sendrawtransaction`, and remains `executable=false`.

`BitcoinRecoveryMaturityVerifier` adds the separate live BIP68 gate required by
the relative `cancel` and `punish` branches. Prepared recovery evidence now binds
the exact parent txid/vout, its block height, and the signed relative block delay.
At evaluation time the verifier pins a synchronized genesis-checked Bitcoin tip,
re-queries that exact parent as unspent, derives its live block height from strict
confirmation arithmetic, and evaluates maturity for the next candidate block.
A changed/missing/spent parent, inconsistent height, moving tip, wrong deployment,
or premature branch fails closed. Its opaque evidence is the only public path to a
`<leg>_<cancel|punish>_mature` journal phase, and those delayed branches cannot add
a broadcast observation without it. Redeem and cooperative refund do not accept
this evidence because they have no relative CSV delay. The verifier never signs or
broadcasts and always reports `executable=false`.

The settlement journal's generic record append operation is private. Quote entry
now requires an opaque `AcceptedQuote` produced only after signature, deployment,
expiry and durable replay validation by `SignedQuoteAcceptor`; DLEQ, recovery
prepared and recovery signed phases likewise require their corresponding opaque
evidence types. Callers can no longer manufacture arbitrary hashes for
`prepared`, `signed`, `broadcast` or `confirmed` phases through the public API.
No caller can append those phases from arbitrary hashes.

`BitcoinRecoveryChainObserver` handles an externally submitted recovery artifact
without adding a submission method. Against a fresh synchronized/genesis-checked
node snapshot, it requires verbosity-zero `getrawtransaction` to reproduce the
exact signed raw bytes and the verbose response to reproduce the locally derived
txid/wtxid. For a mined transaction, the referenced active-chain header,
transaction confirmations, block height and node height must satisfy identical
confirmation arithmetic under one pinned tip. The required depth comes from the
signed V6 terms and is carried through prepared and signed opaque evidence.
Mempool observations cannot claim confirmation, and raw-byte, identity, genesis,
header, confirmation or tip substitutions fail closed. This observation is not
itself submission authority and remains `executable=false`. Once the exact signed
artifact already exists in the journal, a valid chain observation may append the
matching `<leg>_<branch>_broadcast` record using a stable commitment to the signed
raw identity rather than a changing confirmation snapshot. This records externally
observed propagation; it does not call a send RPC.

`BitcoinRecoveryConfirmationTracker` persists observations under that stable
broadcast identity using a locked, atomic, owner-only version-2 store. The exact
V6 settlement-terms commitment is carried from prepared evidence through signed
bytes and chain observation into every record; version-1 prototype stores are
rejected rather than migrated without that binding. The first threshold-confirmed
commitment is frozen as a stable accounting basis rather than overwritten as the
chain gains depth. Confirmation-depth
regression, node-height rollback, replacement of a previously observed block, or
return of a mined transaction to the mempool permanently latches the record into
`reorged` state across restarts. The matching `<leg>_<branch>_confirmed` journal
phase queries the locked tracker by exact broadcast identity and accepts only its
current non-reorged state at the depth committed by the signed terms, after the
exact broadcast record exists. Historical status objects are not accepted by this
API. The same current-state lookup protects later accounting gates. This remains
observation-only and `executable=false`.
Transaction disappearance is recognized only
when both forms of tip-pinned `getrawtransaction` return Bitcoin Core's validated
JSON-RPC `-5` error under the same synchronized, genesis-checked node snapshot.
That missing observation cannot begin tracking or append a broadcast event; after
a prior exact observation it permanently latches the tracker into reorg state.
Malformed errors, another error code, a mismatched response ID or a changing tip
remain ordinary rejection rather than evidence of disappearance.

The common `ChainAdapter` boundary is now strictly observational. Its prior direct
build-and-broadcast, redeem and refund methods and hard-coded fee estimates were
removed. Health, fee, transaction and balance results still require chain-specific
normalization, always remain non-authoritative, and cannot provide a shortcut around
template validation, journaling or a future separately gated signer.

## Implemented XMZ success-fee evidence boundary

For an external-coin-to-XMZ acquisition, `MonzeroSuccessTransactionEvidence`
requires the buyer's net XMZ delivery and protocol-fee payment to appear as the
only two declared destinations of one outgoing sender-wallet transaction. It
checks exact 11-decimal-chain atomic values, distinct committed addresses, the
native-fee ceiling, transaction identity, trusted fakechain node state, block
membership, orphan status and confirmation arithmetic. A pending transfer may be
recognized but always reports `feeEarned=false`; only an unlocked transaction at
the required confirmation depth reports the fee earned. It never authorizes
execution. Sender-wallet RPC acquisition and construction of that transaction
remain to be connected through a separately reviewed local boundary.

The terms-aware overload returns opaque success evidence only for a confirmed
BTC-to-XMZ acquisition. It rebuilds unexpired V6 terms, verifies the independently
pinned XMZ genesis and synchronized fakechain deployment, takes the stricter of
the signed principal/fee confirmation depths, and requires the same native fee to
fit both signed XMZ-side ceilings. Its domain-separated commitment binds the exact
terms, deployment, transaction, active block, heights, recipients, amounts, fee
and depth. Recipient, ceiling and genesis substitution tests fail closed.
`SettlementJournal::appendProtocolFeeEarned` is now the compound revenue gate: it
requires this opaque XMZ evidence, re-queries the locked Bitcoin tracker at append
time, accepts only a currently confirmed non-reorged `pay/redeem`, matches both
legs to the exact accepted V6 quote, and requires the exact external broadcast and
confirmed phases already in the journal. Its domain-separated artifact binds both
chain commitments. End-to-end tests use real low-S secp256k1 signatures and reject
quote substitution, unknown broadcast identities, and a latched block replacement.
If a later observation latches a reorg, `appendProtocolFeeRevoked` recomputes the
exact earlier earned artifact from that frozen confirmation basis and appends a
domain-separated revocation only beneath the matching earned record. A reorg that
never had matching earned revenue cannot manufacture a revocation. These are
accounting-evidence transitions only; they remain `executable=false` and never sign,
broadcast, return, or otherwise move funds.

The sighash serialization is pinned to
[`BIP143`](https://github.com/bitcoin/bips/blob/master/bip-0143.mediawiki).
The RPC field expectations are pinned to the Bitcoin Core 31 documentation for
[`decodepsbt`](https://bitcoincore.org/en/doc/31.0.0/rpc/rawtransactions/decodepsbt/),
[`testmempoolaccept`](https://bitcoincore.org/en/doc/31.0.0/rpc/rawtransactions/testmempoolaccept/),
and [`gettxout`](https://bitcoincore.org/en/doc/31.0.0/rpc/blockchain/gettxout/).
