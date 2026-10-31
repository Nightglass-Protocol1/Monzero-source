# Local light-wallet DEX helper

`native-ltc-light --dex-funding-<operation>-stdio-v1 /absolute/private/dex.json`
accepts one bounded JSON request on stdin and returns one JSON response on stdout.
These commands currently require LTC regtest. The GUI must verify its trusted
helper executable before launching it. Keys and passwords remain on the device.

The wrapper uses schema `monzero-private-ltc-light-dex-v1` and contains
`walletConfigFile`, `metadataDirectory`, `snapshotDirectory`, `tradeId`, `pins`
and `policy`. The wallet file is an existing activated
`monzero-private-ltc-light-v1` configuration. Trade journals must already contain
the approved start and completed key exchange. This helper does not create a
trade or accept a new peer identity from a funding request.

Both configuration files must be canonical absolute paths to private, singly
linked, owner-readable/writable files. Their leases stay held during the command.
The request's `approvedConfigDigest` is SHA-256 of compact, sorted-key JSON:

```text
{"dexDigest":SHA256(raw wrapper bytes),
 "schema":"monzero-ltc-light-dex-context-v1",
 "walletDigest":SHA256(raw wallet configuration bytes)}
```

Digest values are lowercase hexadecimal strings. Spaces/newlines above are for
readability; the hashed JSON has none. Any change to either file needs a new
configuration approval. This fingerprint does not replace executable verification.

Except for the read-only `terms` request described below, requests use schema `monzero-ltc-light-dex-funding-<operation>-request-v1`,
a non-nil `requestId`, `approvedConfigDigest`, `maximumDownloadHeaders` and `payment`.
The request ID is a message identity; the wallet operation always uses the wrapper's
`tradeId`, keeping its address/input reservations tied to the swap.

`payment` contains `address`, `amountAtomic`, `feeAtomic`, `minimumChangeAtomic`
and `minimumConfirmations`. Fresh operations validate these against the completed
negotiation before wallet preparation. Subsequent operations include `retainedReview`
with the returned `signingCommitment` and `changeAddress`.

| Operation | Additional request fields | Result |
| --- | --- | --- |
| `prepare` | `walletScan`; optional `retainedReview` for exact reopen | Reserved wallet payment, funding PSBT and recovery frame |
| `review` | `walletScan`, `retainedReview` | Funding signature review; requires retained peer recovery |
| `sign` | Above plus `approval`, `password` | Verified, locally retained funding signatures; no broadcast |
| `send-review` | `walletScan`, `retainedReview` | Separate send review binding signed bytes and TLS endpoint |
| `send` | Above plus send `approval` | Exact transaction submission; acknowledgement is not confirmation |
| `observe` | `retainedReview`, `maximumHistoryEntries` | Read-only original transaction status and selected-chain inclusion |
| `retry-review` | Same as `observe` | Retry review after a recorded attempt at the configured endpoint |
| `retry` | Above plus retry `approval` | Exact retained-byte retry; no vault or replacement transaction |

`walletScan` contains `gap`, `addressesPerBranch` and `historyEntriesPerBranch`,
using the existing light-wallet bounded scan rules. Cold operations reject scan
fields; fresh operations reject history-budget fields. Only `sign` accepts a
password, and only `sign`, `send` and `retry` accept an action approval.
Passwords travel through stdin, never command-line arguments. Unknown fields fail.

Every funding command except `terms` synchronizes the selected header chain within the supplied budget.
A send/sign review can therefore become stale between commands; redisplay the new
review instead of automatically approving it. Retry observes the original signed
transaction and recomputes its review, so changed status, acknowledgement or tip
also requires renewed approval. Included funding cannot be retried. Missing
reports do not prove that a transaction was never sent. An uncertain outcome must
preserve the claim and use observation/retry; never prepare replacement inputs.

Responses use the matching `...-response-v1` schema and include `requestId`,
`tradeId`, `configurationDigest`, `executable:false` and `result`. The result has
wallet payment details plus the mode-specific `review`, `recoveryFrame`,
`fundingPsbt`, observation or submission status. Response limit: 262144 bytes.
The recovery frame still needs the local offer identity signature and peer
exchange through the existing trade protocol before funding review succeeds.

The helper API does not activate a public market. Desktop wiring, full
light-wallet cross-chain settlement, active-trade recovery and release validation
remain separate acceptance gates. Endpoint migration after an uncertain send is
not implemented; retry requires evidence at the same endpoint.

The owned-node acceptance driver is `test_light_dex_helper.py`. It uses the ignored
`litecoin_light::funding_tests::helper_fixture::actual_light_dex_helper_context`
engine fixture solely for disposable trade setup and peer signatures. All wallet
preparation, signing, send, retry and confirmation commands go through fresh helper
processes. It never uses user wallets or public peers.

## Desktop process boundary

`LitecoinLightFundingJob` runs the funding operations and read-only terms query asynchronously. Configure
it with the private DEX wrapper and an independently trusted helper path/digest.
Pass a public request to `run`; pass the signing password in its separate byte
argument. Passwords cannot appear in the request object, status or command line.
Worker-owned buffers receive best-effort erasure; Qt process buffers are not
secure-memory storage.

The worker fingerprints both configuration files, checks them and the executable
before launch and after completion, bounds stdout/stderr and enforces the request,
trade, payment, retained claim and mode-specific response flags. Returned review
digests are recomputed with their separate signing/relay domains; review details
must identify the same trade, payment and configured endpoint. Errors, timeout
and cancellation produce `outcome-unconfirmed`, never a claim that funding was
not signed or submitted. The worker never automatically signs, sends or retries.

`prepared` emits the base64 funding PSBT only after a validated response. The
application still needs to connect it to the existing peer recovery exchange.
The bound worker retains exact payment/review context for restart; the caller
still coordinates trade operations. The desktop should use `configureWallet` for the active wallet
binding described below.
This worker alone does not implement a trading-screen journey.

Build `dex_litecoin_light_funding_probe` to exercise this boundary. Passing
`--qt-worker /absolute/path/to/dex_litecoin_light_funding_probe` to
`test_light_dex_helper.py` runs every DEX command through a fresh Qt worker while
preserving the original node-backed checks. `test_light_dex_qt_guards.py` covers
process-boundary faults without real keys or a network.

### Active wallet binding

Use `configureWallet(configuration, walletJob, trustedHelper, trustedDigest)` for
an attached desktop wallet. It requires that the DEX wrapper selects that job's
actual active configuration and that the job uses the independently trusted helper.
Setup, pending import, incomplete restore, non-LTC, non-regtest, missing signing
configuration and busy wallets cannot supply a funding configuration. No key material
is returned by `LitecoinLightJob::fundingConfiguration`.

Each command rechecks the wallet's original launcher, active configuration and
any retained bootstrap, activation or restore receipts through `sourcesMatch`.
The funding worker acquires an in-process wallet interlock before launch. Normal
wallet operations fail while it is held; wallet status exposes `dexFundingBusy`
and disables ordinary actions. The interlock is released after completion,
failure/cancellation or worker destruction. It does not replace the helper's
cross-process storage leases or persistently reserve funds by itself.

The integration driver option `--qt-bound` (with `--qt-worker`) attaches a desktop
wallet for every DEX operation and verifies both the busy interlock and its release.
The direct `configure` path remains a low-level process-boundary test interface;
application wallet selection should use the bound path.

### Durable desktop context

Bound workers use private, singly linked, mode-0600 files in the trade metadata
directory: `ltc-light-dex-ui-<trade>-payment.json` and `...-review.json`. Creation
uses exclusive open, a directory lease, file/directory fsync and readback. Existing
records must match exactly; partial, incompatible or orphaned records fail closed.
They do not share the personal-payment request namespace.

The payment intent is durable before the helper starts. It binds trade ID, both
configuration files' digest and the exact address, amount, fee, minimum change and
confirmation requirement. Once attempted, even a rejected operation cannot silently
change this intent. The controller must derive/review the correct trade payment
before initiating funding; this API does not implement intent replacement.

After a validated helper response, the worker retains the wallet claim commitment,
change address and txid before exposing success or emitting a prepared PSBT. The
claim record binds the payment record's digest. A fresh bound worker loads these
records during configuration, exposes `retainedPayment`, `retainedReview` and
`retainedTxid` in status, rejects changed payment/review data and supplies a saved
review when the caller omits it. It never stores passwords, private keys or sign/
send approval commitments. Every consequential action still needs a fresh helper
review and explicit approval. Missing claim records do not prove that preparation
never occurred; the same trade ID remains the engine's operation identity.

The node-backed `--qt-bound` acceptance path omits caller-retained reviews after
preparation and checks exact preparation/signing/sending/observation/retry through
fresh processes. This is restart coverage for retained desktop context, not an
exhaustive process-kill, power-loss or full-swap recovery test.

### Read-only negotiated funding terms

`--dex-funding-terms-stdio-v1` accepts only `schema` (the matching terms request
schema), a non-nil `requestId` and `approvedConfigDigest`. It validates the private
wrapper and wallet configurations, resumes the existing trade and requires a
current quote, Litecoin-funder role and completed key exchange. It returns the
usual response envelope with `monzero-ltc-light-funding-terms-v1` as the result.

The result contains `tradeId`, `quoteCommitment`, `network` (chain identity/genesis),
`contractAddress`, `amountAtomic`, `maximumFeeAtomic`, `minimumChangeAtomic`,
`lockConfirmations`, `refundAddress`, `expiresAt`, `watchOnly:true` and
`broadcastAuthorized:false`. Atomic amounts and expiry are decimal strings.
`lockConfirmations` describes swap-lock confirmation policy; the wallet's input
`minimumConfirmations` remains a separate payment-selection setting.

This operation makes no chain requests, reserves no wallet inputs, writes no
payment intent and accepts no password, payment or signing/relay approval. It
is available in `LitecoinLightFundingJob` with the same executable/configuration
checks and active-wallet interlock. The desktop can obtain the contract and amount
from these terms before asking the user to review a fee and commit a payment intent.
The terms response itself does not authorize preparation, signatures or sending.

### Trade controller and desktop controls

`LitecoinLightFundingController` binds a worker to the expected trade and quote,
derives preparation from returned terms plus an explicitly selected fee, and exposes
separate review/approval methods for signatures, send and retry. Approvals are
accepted only for the current review kind/commitment. Observation and retry can
load retained context without fresh terms. Prepared PSBTs remain native signals.

`DexEngine` selects the open local wallet using the source-checked negotiation
context, retains the immutable selected-wallet wrapper and connects preparation
to the existing peer recovery session. `LightDexFundingPanel.qml` exposes those
steps only in the private Litecoin flow. It clears password/consent on use or a
changed review and displays acknowledgement separately from confirmation.

`test_light_dex_helper.py --qt-controller PROBE` tests this controller with owned
wallets/nodes and a separately pinned fixture quote. It does not establish full
app/private-peer or cross-chain settlement acceptance. The QML suite separately
tests presentation and action routing with a mock backend.

### Retained local refund address

Terms and every fresh funding operation (prepare, review, sign, send-review and
send) require the negotiated refund address to match an allocation retained in
the selected wallet's address book. Matching derives from its public account,
checks network identity, includes explicitly retained discovery coverage, and
rechecks the address-book bytes. The validated journal bounds the search to
10,000 entries. It never allocates a replacement address or modifies the quote.
The vault still separately authenticates the actual signing key.

An unrelated, unallocated or wrong-network address fails before chain sync or
wallet reservation. The user must arrange a new quote with an address already
saved by the selected local wallet. Existing cold observe/retry-review/retry
operations retain their transaction/endpoint checks without requiring this fresh
ownership check, so already-sent transaction recovery is not gated by it.


### Resume the saved peer recovery phase

After process restart, `resumeLitecoinLightRecovery()` is independent of funding
reopen and fresh terms. `LitecoinNegotiationSession::resumeRecovery()` loads only
the retained recovery PSBT from the private trade record and delegates its exact
checkpoint validation to the existing recovery-review helper before identity
signing/exchange. Missing or malformed records fail; no replacement reservation
is created. Observation/retry retain their separate transaction approvals.
