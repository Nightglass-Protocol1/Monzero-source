# XMZ-funded exchange plan

Status: local prototype. Funding previews cannot sign or broadcast transactions.

## Current verification boundary

The funding planner is implemented and covered by regression tests for acquisition,
external-coin fee shortfalls, exact balances, precision, invalid assets and overflow.
`funded` means only that caller-supplied numbers satisfy the arithmetic requirement.
Every planner result remains `executable=false` and `balanceVerified=false`.

The separate `XmzFundingCoordinator` accepts evidence obtained from the local,
allowlisted read-only RPC client. It verifies the configured fakechain genesis,
trusted synchronized node state, exact wallet/node height agreement, unlocked
balance invariants and the wallet multisig-import flag before atomically reserving
XMZ. Its success result may set `balanceVerified=true`, but always remains
`executable=false`: it is not permission to sign or broadcast.

Live trading is not implemented: chain adapters are observational only, and
live settlement/refund execution is absent. Complete authenticated market messages
now reach durable ingress through bounded TCP connection handlers and an opt-in
listener. The standalone research receiver binds only to loopback and recovers
pending state across process restarts. An outbound history stores validated messages
before transmission. The opt-in sender/receiver connection pair exchanges signed
receipts bound to the exact message and pinned receiver key; acknowledgements are
saved atomically and reverified on restart. These are peer acceptance claims, not
proof of funds or settlement. Peer discovery, application-level outgoing
sequence/identity integration, reconnect catch-up and market synchronization
remain unfinished. Encrypted signing-identity storage exists separately. A persistent
validating local order book and canonical maker-authenticated
offer/cancellation evidence are implemented,
but a valid signature does not prove liquidity or make an offer executable.
Read-only BTC-regtest and
XMZ-fakechain probes, wallet balance checks, durable XMZ reservations and signed
quote acceptance with persistent replay protection now exist. The next integration
milestone is an analyzed settlement/recovery protocol whose real chain objects are
verified against the newly bound lock/refund commitments. Do not remove activation
guards to ship this.

`SignedQuoteAcceptor::acceptMutualEvidence` additionally checks a distinct
taker-acceptance signature over the entire canonical settlement payload before
persisting acceptance. Maker-only acceptance never implies mutual authorization.
The schema-2 replay store now atomically retains the complete mutual signed
envelope alongside its acceptance commitment; legacy maker-only records have no
invented countersignature. `recoverMutualQuote` revalidates stored signatures,
canonical terms, deployment and commitment at the recorded acceptance time without
writing state. It returns a separate recovery-only type, not fresh acceptance,
even after expiry. The timestamp assumes trusted local storage; it is not external
proof of when a signature was produced. Mutual consent
alone does not authorize locks, signing, broadcast, or fee collection; quote
transport and the execution coordinator still require integration.

Quote replay/artifact storage also requires private regular storage, rejects
symlink targets and shared permissions, bounds both reads and saved files to
4 MiB, and requires owner-only atomic writes. Capacity exhaustion fails closed;
there is no automatic pruning of accepted-quote history.

Canonical mutual-quote envelopes now carry the V6 length-prefixed terms and both
signatures. Decoding is bounded and re-encoding must match byte-for-byte before
acceptance touches replay state. Deployment policy, signatures and quote expiry
are rechecked at receipt. These envelopes contain participant and settlement
details; do not publish them into the public offer relay. The private transport
foundation below is opt-in; participant-service deployment and complete recovery
orchestration remain unfinished.

`PrivateQuoteChannel` is an opt-in TLS client with certificate-chain/name
verification plus an out-of-band SHA-256 leaf certificate pin. It sends no
application payload before authentication, bounds messages and queues, and times
out incomplete handshakes/messages. Its loopback TLS fixture covers valid delivery,
wrong pins, wrong names and untrusted certificates. This is not yet a participant
service: accepted-socket mode now also requires a trusted client certificate and
its pinned leaf fingerprint, with positive two-certificate and wrong/missing-client
tests. Certificate provisioning/rotation and deployment are still required. The test
server's disabled client-certificate verification is fixture-only, not a service
configuration. Qt's TLS validation is retained; errors are never ignored.

`PrivateQuoteServer` now supplies the bounded listener (eight connections total,
two per address, handshake/message deadlines and idle cleanup). Each channel
limits input to 64 complete messages per second before signature processing,
with at most eight dispatched per event-loop turn.
`PrivateQuoteSession` binds the TLS channel to explicitly configured maker/taker
signing keys and the quote acceptor. Incoming participant mismatches are rejected
before replay writes; outgoing mutual consent is persisted before sending. Local
TLS integration tests cover both accepted delivery and participant mismatch.
Certificate pins and signing-key mappings still need external provisioning, and
the private session is not yet wired into the GUI or a deployed participant
service. Socket send success remains distinct from remote quote acknowledgement.

The journal now binds recovered mutual quotes to an existing `quote_accepted`
event for the exact swap and commitment. Its recovered-quote recovery-preparation
overload also requires matching Bitcoin RPC-verified terms before recording a
prepared recovery artifact. Local RPC-fixture coverage exercises this after quote
expiry. This does not prove a transaction is mature, signed, broadcast, confirmed,
or safe to execute; those separate checks and execution orchestration remain.

The standalone private quote service now persists a `quote_accepted` event in
`settlement.json` before acknowledging the quote. Its stable swap ID is the
lowercase quote commitment hex. A journal-backed session also journals outgoing
consent before transmission. Exact retries preserve the original journal event
and timestamp; a journal failure prevents acknowledgement while retaining the
already saved quote for recovery. Tests cover a restart between the two writes
and an unavailable journal. The legacy session overload remains quote-storage-only;
its ACK must not be interpreted as journal persistence. No ACK grants execution
authority, and recovering an expired quote never recreates a missing journal event.

Cross-curve proofs can now be reverified after expiry through a separate
`verifyRecoveredEvidence` entry point requiring an opaque `RecoveredMutualQuote`.
It checks the recorded acceptance time and exact terms commitment, then reruns
the pinned verifier on the proof bytes; the ordinary fresh-terms path still
rejects expired quotes. Reopen/expiry tests cover positive verification, wrong
party, modified proof, reused quote nonce, missing recovery evidence and backwards
time. This is recovery evidence only, not renewed consent or signing authority.
Acceptance timestamps rely on protected local history, not an external timestamp
attestation; execution still needs the matching swap journal and chain checks.

Settlement journals now require an existing private directory and a regular,
non-symlink journal owned by the running user on Unix. Shared permissions are
rejected rather than silently repaired. Loads are byte-bounded and writes require
owner-only permissions and atomic replacement. Preserve the original journal if
startup rejects its location; move it into private storage without resetting swap
history. These checks do not protect against a compromised account or journal
rollback by an attacker who controls local storage.

At the 10,000-record journal limit, exact retries of already durable events now
remain successful without rewriting storage. Conflicting retries and all new
records are still rejected. A full-size on-disk fixture verifies first/last-event
retries and byte-for-byte preservation. This does not solve capacity planning:
live admission must reserve room for every outstanding swap's recovery events,
or introduce a reviewed archival design, before signing can be enabled. Never
delete active history to make room.

Journal startup indexes validated events incrementally rather than rescanning
the full preceding history for each transition. The index is transient, populated
only after a record passes validation, and retains both exact-chain and any-chain
lookups using the same transition policy as appends. Full-size tests also reject
a correctly rehashed second acceptance for an already accepted swap; the index
does not bypass hash-chain, sequence, schema or transition checks.

## User flow

1. Open the exchange and choose the coin to pay and the coin to receive.
2. If buying XMZ using BTC, LTC or BCH, show gross XMZ, all fees and net XMZ.
   The seller supplies the XMZ needed for settlement; the buyer can start with
   zero XMZ. Fees are deducted from the acquired amount on successful settlement.
3. For BTC/LTC and other external-coin trades, require unlocked XMZ sufficient
   for the signed quote's protocol fee and Monzero network fee. Show the exact
   shortfall and a Buy XMZ action. Native chain miner fees remain separate.
4. Reserve balances against concurrent orders, reconfirm an expiring quote,
   and display both the success and refund outcomes before wallet signing.

## Implemented foundation

`DexEngine::fundingPlan` validates coin selection and exact 11-decimal XMZ
amounts, calculates shortfalls with checked integer arithmetic, supports
zero-balance XMZ acquisition, and keeps every returned plan non-executable.
Its fee inputs must eventually come from an authenticated, expiring quote;
they are not a price oracle, live fee estimate or proof of wallet funds.
Existing preview rates (0.10% protocol fee and 0.0025 XMZ network fee) are demo
values, not an approved production tariff.

## Delivery stages

1. Funding planner and fee-policy tests (this change).
   Canonical signed discovery offers now bind an explicit protocol/network domain,
   offer identifier, supported pair, direction, exact atomic amount/price and a
   maximum 24-hour lifetime to an Ed25519 maker identity. Verification produces an
   opaque, non-executable evidence object and rejects mutation, domain replay,
   expiry and ambiguous encodings. A locked, atomically replaced owner-only local
   order book now re-verifies those signatures and commitments both on insertion and
   restart, rejects conflicting identifier reuse and corruption, and filters/prunes
   expired offers. Maker-signed cancellation now binds the original commitment and
   survives restart without unsigned deletion; conflicting identities, content or
   timing fail closed. Deterministic crossing now revalidates both active,
   uncancelled offers, requires distinct makers and opposite sides of one pair, and
   commits both original capacities, their maximum common base quantity, shared
   validity deadline and the valid sell-ask to buy-limit interval. Cancellation is
   effective from its signed time so earlier evidence remains reproducible. The
   matcher deliberately does not choose an execution price from unauthenticated
   clock priority: a later signed quote must select an exact price inside that
   interval. A canonical relay envelope now signs a length-prefixed artifact
   commitment, protocol/network domain, message type, lifetime and nonzero sequence.
   Its locked, owner-only atomic replay store re-verifies every envelope and requires
   a new sender to start at sequence one, followed by exact increments. Exact retries
   are idempotent; skipped, stale and conflicting sequences fail closed across
   restarts. Relay ingress now independently re-verifies the wrapper and its
   maker-signed offer or cancellation, rejects artifact/type substitution before
   consuming a sequence, records the replay boundary before the idempotent order-book
   mutation, and supports recovery by exact retry. A relay peer need not be the maker,
   because the inner artifact retains its independent maker signature. Bounded TCP
   framing, per-connection rate limits, listener admission caps and idle cleanup
   now have loopback coverage. The standalone receiver has process-level restart,
   pending-recovery and unsafe-startup tests. Peer discovery, secure synchronization,
   explicit replacement policy and liquidity proof remain required. A separate crash-safe fill
   ledger now makes short-lived quantity reservations against both offer capacities,
   rejects aggregate overfill across different counterparties, rechecks cancellation
   and expiry before commit, and revalidates embedded signatures, crossings and
   aggregate committed capacity after restart. Released/expired tentative holds free
   capacity. A committed fill can now advance to a durable settled state only from
   opaque journal evidence that revalidates the exact accepted quote, external
   redeem confirmation, XMZ delivery and XMZ protocol-fee payment against current
   chain state, including a locked lookup in the durable sticky-reorg tracker.
   Settled quantity remains consumed.
   Typed XMZ refund evidence now validates the principal and protocol-fee refund
   legs independently: each signed intent binds its leg, deployment, recipient,
   amount, fee ceiling and refund height, and each observation must come from a
   synchronized trusted node plus a confirmed sender-wallet transaction after the
   signed refund deadline. The journal now combines those two distinct XMZ proofs
   with the exact accepted quote and a currently confirmed, non-reorged external
   `pay_refund` branch. Only its opaque compound outcome can mark a committed fill
   refunded; the ledger also requires both XMZ transaction identities and block
   hashes to be currently confirmed in the durable tracker before that transition
   frees the quantity for reuse. Unknown, disappeared, replaced or ever-reorged
   transactions fail closed. A fill can become
   committed only with an accepted V6 settlement quote that binds its exact
   reservation commitment, both offer identities, the reserved external amount and
   an XMZ principal within the crossed price interval.
2. Read-only chain adapters: validate network/genesis, synchronization,
   confirmations and reorgs; estimate native fees; obtain wallet-owned unlocked
   balances. The node/genesis and XMZ wallet-balance subset is implemented for the
   isolated lab. A durable generic confirmation state machine now checks exact
   block-height arithmetic and makes reorg detection sticky. BTC Core 31 transaction
   and block-header evidence can now be normalized with txid, active-chain,
   destination, exact satoshi amount and confirmation checks. Pre13 XMZ incoming
   wallet evidence is also normalized against a trusted fakechain node and explicit
   block membership, with daemon block-count conversion handled separately from tip
   height. A crash-recoverable BTC regtest fixture and live C++ validation are now
   verified. A crash-recoverable XMZ fixture is likewise verified through the
   receiver wallet, fakechain block evidence, durable tracking and unlocked-balance
   reservation. Fee estimation and the production UI remain outstanding.
3. Signed maker quotes: bind pair, amounts, XMZ fee, fee recipient, networks,
   expiry, nonce, confirmation requirements and refund deadlines. Reject replay,
   stale quotes, missing liquidity and incompatible protocol versions. Canonical
   V6 terms, configured protocol/deployment policy, maker signatures, network-domain
   separation, expiry and persistent nonce replay rejection are implemented. V6
   also binds per-chain confirmation/refund heights, native-fee ceilings, refund
   recipients, and lock/refund commitments for both principals and the XMZ fee leg.
   Construction and semantic verification of those committed chain objects plus
   liquidity proof are still outstanding.
4. Isolated BTC/XMZ settlement prototype, then LTC/XMZ and BCH/XMZ. Evaluate
   cryptographic compatibility with XMZ before choosing an atomic-swap protocol.
   Persist recovery state before broadcasts and test every crash/refund boundary.
   `DEX_PROTOCOL_COMPATIBILITY.md` records the pinned reference implementations,
   known incompatibilities and evidence required to pass this gate. In particular,
   Monzero's 11-decimal atomic unit is incompatible with COMIT's hard-coded
   12-decimal XMR conversion, so that implementation cannot be relabelled or used
   unchanged.
   A canonical Bitcoin unsigned-template commitment now binds ordered input
   outpoints/sequences and output scripts/amounts. Read-only validation covers the
   reference protocol's lock shape and single-input CSV/final settlement spends,
   including native-fee ceilings, but not script satisfaction, signatures, adaptor
   proofs or end-to-end timing.
   PSBT funding provenance is now validated by matching every witness UTXO to an
   independently queried, confirmed `gettxout` at one pinned Bitcoin tip and by
   reconciling exact input/output totals with the reported fee. An idempotent
   isolated-lab generator now creates an unsigned, unbroadcast, wallet-funded PSBT
   for the exact COMIT P2WSH lock and persists its ordered keys, script, amount,
   fee ceiling, confirmation policy and independently recomputable template
   commitment with owner-only permissions.
   The read-only adapter sequence itself is now implemented with a before/after
   best-block pin, strict PSBT decoding and one `gettxout` query per input. The live
   C++ lab probe now consumes the persisted real lock PSBT through this boundary;
   execution on the isolated VM awaits an unlocked administrative SSH session.
   A terms-aware opaque wrapper now rejects caller substitution of amounts, keys,
   fee ceilings, lock commitments, chain identity or genesis. It holds node identity
   stable across the pinned PSBT snapshot and is the only public evidence type that
   can append a `<leg>_lock_prepared` journal transition beneath the matching quote.
   It remains validation-only and cannot sign or broadcast.
   Refund/redeem PSBT evidence now independently binds the sole witness UTXO to
   the exact live P2WSH settlement output at a pinned tip, checks confirmations,
   amount, script, committed sequence and recipient, reconciles the reported fee,
   and rejects signing or mutable material. Read-only RPC orchestration now pins
   the tip before and after decoding/querying the exact expected parent, rejects
   missing or spent output evidence, and applies the conservative fee-estimate
   policy. Script-path satisfaction checks remain outstanding.
   The pinned COMIT shared-output descriptor has now been reproduced byte for
   byte as `c:and_v(v:pk(A),pk_k(B))`: ordered compressed secp256k1 keys with
   `CHECKSIGVERIFY`/`CHECKSIG`, committed through SHA-256 into P2WSH. The validator
   rejects invalid curve points, duplicate or reordered keys, opcode changes and
   output-hash substitution. Chain-specific Bitcoin keys are now ordered fields
   in signed V6 terms. Both lock-funding and
   refund/redeem evidence validators derive the shared P2WSH program from those
   exact keys and reject substitution before accepting chain evidence.
   A native SegWit-v0 BIP143 `SIGHASH_ALL` calculator now serializes the validated
   unsigned spend template and the exact derived witness script. It matches the
   published Bitcoin BIP143 vector, rejects signed/ambiguous inputs and excessive
   amounts, and is invoked by spend-PSBT evidence. A validation-only secp256k1
   verifier also matches that vector's published ECDSA signature and requires
   compressed keys, canonical DER, low-S and the exact `SIGHASH_ALL` byte.
   A V6 recovery-policy wrapper selects the exact redeem/cancel/refund/punish
   child commitment, requires the correct signed parent template, applies the
   lock-relative cancel or cancel-relative punish sequence, and reuses the strict
   PSBT/live-prevout/fee/script/sighash boundary. All four branches have positive
   and substitution regression coverage but remain non-executable.
   Read-only RPC orchestration now pins a single tip while decoding the child,
   retrieving the exact verbose parent and checking its live output. Signed
   on-chain unlocking data is permitted only in the observed-parent reconstruction;
   the txid and all unsigned template fields remain bound, and tip/parent mutation
   tests fail closed.
   Successful recovery snapshots now produce an opaque prepared-evidence
   commitment over the terms, branch, chain, templates, sighash, pinned tip and
   fee policy. The journal records it only beneath the exact accepted V6 quote as
   the corresponding branch's `prepared` event; it grants neither signing nor
   broadcast authority. Recovery and confirmed-success evidence revalidate an
   already accepted V6 payload without enforcing its quote-acceptance expiry, so
   an expired quote cannot start a swap but also cannot disable recovery of one
   already funded. The resulting commitment remains byte-identical and the journal
   still requires its prior accepted-quote record.
   Cancel and punish now have a live maturity verifier that rechecks the exact
   parent output as unspent at a stable genesis-validated tip, derives its block
   height from confirmations, and applies BIP68 to the next candidate block. The
   resulting opaque maturity evidence is mandatory before either delayed branch
   can record broadcast observation; tests cover a mature path and a path 66
   blocks early. It remains read-only and non-executable.
   Adaptor-signature verification and signer isolation remain outstanding.
   The validation-only crypto-verifier V1 wire contract is now implemented. It
   pins COMIT and its `ecdsa_fun`, `sigma_fun`, and `bincode` versions; commits
   every binary input with a fresh request nonce; rejects response substitution,
   unknown fields and ambiguous Boolean/error states; and never authenticates a
   verifier merely because its JSON is valid. A minimal Rust verifier now
   implements both operations with a checked-in lockfile;
   real adaptor and 252-commitment cross-curve proofs pass deterministic tests, and
   a cross-language test confirms C++/Rust request-response binding. The GUI
   client now hash-pins and rechecks a non-writable verifier and runs it
   over bounded direct stdin/stdout inside a networkless, capability-free
   Bubblewrap sandbox with no wallet/home mount. Incorrect hashes, unsafe
   permissions, process failures and response drift fail closed. V6 terms also
   bind both parties' adaptor secp256k1 keys, Monzero/Ed25519 swap keys, and exact
   DLEQ proof commitments before quote signing. Published DLC
   adaptor fixtures cover a valid and a deliberately wrong proof; two same-host
   clean locked builds are byte-identical. Independent reproduction and an
   externally sourced cross-curve vector remain outstanding.
   The cross-curve evidence coordinator now rebuilds unexpired signed V6 terms,
   selects the maker/taker keys, rejects proof or nonce substitution, invokes the
   sandboxed verifier, and returns settlement/proof/request commitments suitable
   for durable journaling. It remains validation-only and non-executable.
   Successful results now have an opaque evidence type and domain-separated
   journal commitment. The journal accepts maker/taker DLEQ verification only
   after the exact matching V6 quote commitment, and rejects unauthenticated or
   cryptographically failed results. A locally generated deterministic DLEQ
   fixture now exercises the C++ coordinator's positive verification and opaque
   commitment path through the hash-pinned sandboxed Rust binary. Mutated proofs
   and invalid party values are rejected. Maker and taker journal append paths
   now have positive coverage, including quote mismatch rejection, exact retries
   and restart persistence. A fixed independently sourced positive cross-curve
   vector remains outstanding; the local fixture does not satisfy that gate.
   The shared chain-adapter interface is now observational only; legacy direct
   broadcast/redeem/refund methods and hard-coded fee guesses were removed so a
   future implementation cannot bypass these validation boundaries through the
   generic adapter.
   A locked, logically append-only settlement journal now atomically commits a
   hash-chained sequence of artifact commitments. It rejects malformed history,
   duplicate ambiguity, mutation and concurrent writers. Settlement integration
   must persist the relevant prepared/signed commitment before any broadcast and
   externally checkpoint journal heads to make tail truncation detectable.
5. External-coin trading with XMZ fees. Compare a three-chain atomic protocol
   against a disclosed two-leg route through XMZ. Two independent swaps are not
   atomic: the first can complete while the second fails. Do not advertise that
   route as an atomic external-coin trade. A fee escrow must be cryptographically
   bound to successful settlement, with no protocol revenue on refunds.
   For the BTC-to-XMZ acquisition case, sender-wallet success evidence now
   requires buyer delivery and protocol fee to be exact destinations of the same
   XMZ transaction. Pending transfers prove no earned fee; only a confirmed,
   non-orphaned and unlocked transaction at the required depth does. This does
   not solve the separate external/external three-chain construction. Confirmed
   BTC-to-XMZ evidence is now also bound to the exact V6 terms and independently
   pinned XMZ genesis in an opaque commitment, using the stricter signed depth and
   both signed XMZ-side native-fee ceilings. A compound journal gate now rechecks
   the locked Bitcoin tracker's current state and records earned revenue only for
   a non-reorged `pay/redeem` whose broadcast, confirmation, XMZ success evidence,
   and accepted quote share the exact V6 commitment. It remains accounting-only;
   a later latched Bitcoin reorg now appends a revocation bound to the immutable
   first-confirmed basis and exact prior earned artifact. Actual fund recovery and
   all live signing/broadcast remain outstanding.
6. Adversarial testnets, recovery drills, independent review and a separately
   authorized release. Public pre13 remains separate from this prototype.

## Decisions before live execution

Choose protocol fee rate and recipient/governance, liquidity provider model,
supported network versions and confirmation depths, and the cross-chain
settlement protocol. Ordinary native miner fees cannot be replaced by XMZ
without a separately specified sponsor that pays those chains' fees.
Possessing XMZ is a funding requirement, not proof that a trade settled.

## Acceptance cases

- Zero-XMZ buyer receives positive XMZ after disclosed fees.
- External-coin trade is unfunded below the full XMZ requirement.
- Exact required balance passes funding, but never enables execution by itself.
- Overflow, unsupported pairs and malformed decimal amounts are rejected.
- Concurrent quotes cannot spend the same reserved XMZ.
- Reservations survive abrupt process termination and are recovered without
  allowing a second live process to open the same store.
- An accepted maker nonce cannot authorize changed terms, including after restart;
  invalid signatures, wrong network domains and expired quotes are rejected before
  persistence. Only the resulting opaque accepted-quote evidence can start a
  settlement journal; raw phase/hash appends are not public API.
- Signed V6 terms bind each chain deployment, confirmation depth, anchor/refund
  height, native-fee ceiling, refund recipient, and principal/XMZ-fee lock and
  refund commitment; V1 terms cannot pass the current builder.
- Confirmation evidence must agree with node and block heights. Transaction
  disappearance, block replacement and confirmation rollback create a persistent
  reorg state that cannot be mistaken for confirmed settlement.
- An XMZ protocol fee can be journaled as earned only when the same quote has a
  currently confirmed Bitcoin pay/redeem in the persistent tracker; a historical
  status, cancel/refund branch, different quote, unknown broadcast, or latched
  reorg must fail closed.
- XMZ incoming-payment evidence must match the wallet-owned address and exact atomic
  total, be free of a double-spend signal, and appear in the trusted non-orphaned
  fakechain block claimed by the wallet. Locked funds never report spendable.
- Bitcoin transaction evidence must pay the exact committed satoshi total to the
  expected address and agree with a separately queried active-chain block header;
  malformed outputs, duplicate indexes and fractional satoshis fail closed.
- Bitcoin recovery evidence must derive a BIP143 `SIGHASH_ALL` digest from the
  exact V6-bound shared script, parent amount and committed unsigned transaction;
  pre-populated script or witness data fails closed. A completed recovery witness
  must then contain exactly the two strict low-S signatures in signed contract-key
  order followed by the exact shared witness script, and must still reproduce the
  prepared transaction-template commitment before a `signed` journal transition.
- A signed recovery transaction must pass a tip-pinned, non-mutating Bitcoin Core
  `testmempoolaccept` call with the same locally derived txid, wtxid, virtual size,
  and exact prepared fee. Rejection, fee/identity substitution or a changing tip
  fails closed and never becomes broadcast authority.
- Reorg, crash, timeout and counterparty disappearance preserve recovery paths.
- Externally submitted Bitcoin recovery transactions are observed only when the
  synchronized, signed-genesis node returns the exact raw bytes and txid/wtxid;
  mined status requires active-chain header and confirmation arithmetic under a
  stable tip and the immutable V6 confirmation depth. Only such an observation
  can record the stable broadcast identity after the exact signed journal entry;
  confirmation remains gated on persistent reorg tracking. A validated, stable-tip
  Bitcoin Core `-5` response from both raw and verbose lookups can only mark a
  previously observed identity as permanently reorged; it cannot manufacture an
  initial observation or journal transition.
- No production claim until the latter settlement cases pass on isolated chains.
