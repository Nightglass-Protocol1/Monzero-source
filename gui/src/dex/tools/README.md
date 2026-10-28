# Isolated DEX market receiver

## Private quote receiver (research only)

`dex_private_quote_service` is a separate loopback-only mutual-TLS receiver for
fully signed quotes, not public offers. It requires `--state-dir`, `--tls-cert`,
`--tls-key`, `--peer-cert`, `--maker-key`, and `--taker-key`; optional `--port` and
`--run-seconds` default to an ephemeral port and no automatic shutdown. All file
paths must be private, owned regular files in private directories. Certificates
are single PEM certificates; the local key is an unencrypted RSA PEM protected by
filesystem permissions. Each input is bounded to 16 KiB. Do not pass key contents
as arguments. Certificate provisioning and rotation are external responsibilities.

The expected peer certificate is both the configured trust certificate and exact
leaf pin. Maker/taker arguments are canonical lowercase public-key hex, provided
out of band. Policy is fixed to the isolated BTC-regtest/XMZ-fakechain deployment
and `xmz-fakechain-lab-v1` signature domain. Accepted signed artifacts are retained
in `quotes.json`; stdout reports only readiness and accepted commitments, never
full quotes or private keys. There is no wallet authority or live execution.
Process-level tests deliver a dual-signed quote over TLS, restart the receiver,
retry the exact quote without duplicating acceptance, and recover its stored
signature artifact after expiry. Stdout acceptance is diagnostic output, not an
authenticated remote acknowledgement or proof of settlement. Deployment and
transaction execution remain disabled.

Private sessions now exchange canonical quote-acceptance acknowledgements inside
the pinned mutual-TLS connection. The receiver replies only after saving the
signed quote; the sender matches the exact outstanding commitment and applies a
ten-second deadline. Missing delivery confirmation remains uncertain, not proof
of rejection. These acknowledgements are session-authenticated peer claims, not
independently signed artifacts, durable sender receipts, or settlement evidence.
After restart, revalidate stored quotes and retry while still fresh; do not use
an expired quote to initiate another trade.

## Public market receiver

`dex_relay_service` runs the signed-market receiver as a separate process. It is
built only with the private DEX prototype and its test targets enabled. It has
no wallet keys, transaction signing, transaction broadcast, or settlement
execution. It does not originate offers, discover peers, or relay messages to
other peers. It is not a complete exchange daemon.

Build from the GUI source directory using an existing private DEX build:

```sh
cmake --build build-dex-private --target dex_relay_service -j2
mkdir -m 700 dex-receiver-state
build-dex-private/src/dex_relay_service --state-dir dex-receiver-state --port 0 --run-seconds 300
```

The state directory must already exist, be private, and on Unix belong to the
current user. Existing state must not be replaced to work around a startup
error. The service locks `relay.json` and `offers.json`, rejects unsafe files,
and attempts exact pending-message recovery before accepting connections.
Unresolved entries remain pending; their count is included in startup output.

Startup emits one compact JSON `ready` event with the selected port. The only
bind address is `127.0.0.1`, and the deployment is fixed to `isolated-btc-xmz`.
Clients send four-byte big-endian lengths followed by the canonical complete
market message produced by `RelayIngress::Coordinator::encodeMessage`. An
`applied` event reports successful durable ingress, not trade execution.
These stdout events are diagnostics, not authenticated network acknowledgements.

The private GUI's DEX page can manually read this directory's `offers.json` in
its isolated-receiver snapshot panel. It re-verifies all offer and cancellation
signatures against the fixed research deployment without opening a writer lock
or changing the file. Expired and cancelled offers are excluded at refresh time.
The backend caps returned rows at 200 and the panel shows the first 10, alongside
the total active count and snapshot time. Refresh to observe newer state; the
snapshot neither proves liquidity nor enables signing, publishing, or trading.
GUI refresh runs signature verification on a worker thread and allows one job
per engine at a time. The path and refresh control are disabled while it runs.
The worker captures no GUI or live-writer object, so closing the engine does not
leave a task accessing a destroyed engine. Failed refreshes replace the display
with an error rather than presenting the preceding snapshot as a fresh result.

`--run-seconds` accepts 0 through 86400; zero runs until externally stopped.
A finite duration provides orderly shutdown and releases the store locks.
No public listener, production startup unit, or GUI autostart is installed.
Outgoing identity/sequence management and authenticated acknowledgement,
synchronization, and transport-security protocols still require integration.

`RelayOutboxStore` is the separate outbound persistence boundary. A sender builds
a complete signed message, appends it successfully, then sends the stored bytes.
The store requires explicit create/open modes, a private directory, exclusive
locking, the expected sender key, and consecutive sequence numbers starting at
one. It validates messages before atomic persistence and revalidates every stored
wrapper and inner artifact at the wrapper's creation time on restart. Exact
retries do not allocate another sequence; changed content at an existing sequence
is rejected. History is bounded to 1,024 entries and 8 MiB with no automatic reset
or pruning. Retain it alongside the encrypted signing identity.

Outbox retrieval is historical, not permission to send expired messages. Receivers
still enforce current expiry and their own strict replay policy. Delivery
acknowledgements, reconnect catch-up, late-joining peer synchronization and safe
history retirement are not implemented. The standalone receiver does not yet
originate messages or use the outbox automatically.

`RelayReceipt` provides a signed acknowledgement primitive for a pinned receiver
key and the exact relay commitment. Creation requires accepted ingress evidence
matching the message; verification rejects a different receiver, message, altered
body, noncanonical encoding, or future acknowledgement timestamp. A delayed
acknowledgement may follow envelope expiry, including after durable recovery.
This is a peer's claim of acceptance, not independent proof of disk durability,
funds, liquidity, or settlement. `RelayPeerConnection` optionally takes a receiver
identity (which must outlive it) and returns a length-framed signed receipt only
after successful ingress persistence. Exact retries can receive receipts again;
failed receipt writes close the connection without undoing persisted acceptance.
`RelayPeerServer` can explicitly receive the same externally provisioned identity
and supplies it to its bounded accepted connections; the identity must outlive the
server. Tests restore an encrypted identity and reopen the outbox across listener
recreation, preserving the pinned key and exact-message retry behaviour. The
default listener constructor leaves this mode disabled. On Unix, the standalone
service opts in with `--identity-file /private/path/identity.enc --passphrase-stdin`.
Supply one newline-terminated passphrase (1–1024 bytes) through private standard
input, for example from a supervising process's anonymous pipe. Do not put the
passphrase in command arguments, shell history, or logs. Terminal input is refused;
startup waits for the input line before opening the listener. Only an existing
encrypted identity is accepted, and its lock is held for the service lifetime.
The ready event publishes its public key for comparison with an independently
provisioned pin, never as an automatically trusted identity. The temporary
passphrase buffer is wiped after unlocking; this does not protect a compromised
host, process memory inspection, or the supervising process's buffers.
`RelaySender` uses a
dedicated socket and a pinned receiver key, sends only validated durable outbox
messages, and allows one outstanding delivery. It parses bounded length-framed
receipts with a fixed ten-second deadline and emits acknowledgement only after
signature/message verification. Wrong-key or malformed receipts close the
connection without deleting outbox history. The caller must retain the outbox,
codec and socket for its lifetime on the same thread. Receiver identity
provisioning and application startup are not yet integrated; a socket write must
not be interpreted as acknowledgement.
Outbox schema 2 retains up to 16 signed receiver receipts per message and verifies
them again on load. Schema 1 history loads without inventing acknowledgements and
is upgraded on its next successful save. The sender persists a verified receipt
before emitting acknowledgement; failed persistence closes the connection but
never removes send history. Corrupt receipt history fails closed. These files
still assume trusted local storage: rollback/deletion by a local attacker is not
prevented, and loss of a receipt does not mean the peer failed to accept a message.
Live execution remains disabled.

## Optional real Bitcoin regtest integration

The shared C++ RPC client consumes responses incrementally, with a body limit
and a bounded Qt read buffer. Exceeding the body limit aborts the request before
completion; an oversized stream is not allowed to run until the timeout while
accumulating its entire body. The fixed total request deadline still applies.
These are application-buffer bounds, not a claim about all HTTP/kernel memory.

Build `dex_bitcoin_regtest_integration_tests` and supply
`MONZERO_DEX_BITCOIN_BIN` as the absolute directory containing independently
verified `bitcoind` and `bitcoin-cli` binaries. Without this explicit setting the
test skips; a skipped test is not real-node validation. The test does not download
or authenticate those executables itself.

It starts a fresh disposable regtest directory under the working directory (allow
adequate disk space), disables listening and peer discovery/connections, and binds
cookie-authenticated RPC to loopback on a temporary port. It tests genesis pinning,
initial-sync rejection, wrong authentication, method allowlisting, then mines one
regtest block via a separate fixture CLI and checks readiness and zero peers.
It then disconnects and restores that block using fixture-only CLI commands,
checking the live tip and header confirmations (`1`, `-1`, then `1`). A detached
block remains queryable, so successful RPC decoding alone cannot establish
confirmation. Both chain-mutating commands remain forbidden in the adapter.
The fresh node's `estimatesmartfee` response contains no fee rate; the test passes
that real response to `BitcoinFeeEvidence` and requires rejection. Node readiness
therefore does not imply readiness of funded PSBT validation. A positive lock-PSBT
integration run still needs adequate estimator history; no hard-coded fallback is
relabeled as a verified network estimate.
Separately, the fixture creates a disposable regtest wallet, matures coinbase
outputs, and confirms a wallet-owned self-transfer. It builds an unsigned P2WSH
lock PSBT from that non-coinbase output and validates actual `decodepsbt`/`gettxout`
responses through `BitcoinPsbtEvidence`. The 10,000-satoshi shared output and
200-satoshi fixture fee are explicit test amounts, not market fee estimates.
Incorrect amount, insufficient depth and substituted prevout value must fail.
The lock PSBT itself is never signed or broadcast; this positive structural/
provenance check does not prove the full RPC fee policy or mutual settlement.
The fixture then spends that input back to its disposable wallet in a competing
transaction. `BitcoinPsbtRpcVerifier` must reject the unchanged lock PSBT both
while the competing spend is in the mempool and after confirmation. A previously
valid static snapshot cannot stand in for fresh unspent-output evidence. Only the
fixture CLI signs/broadcasts this wallet self-spend; the DEX lock stays unsigned.
It never loads an existing wallet, signs a swap or uses real funds. Normal test
completion and assertion failure terminate the child before removing its temporary
data. This is Bitcoin RPC integration only, not two-chain settlement validation.

## Optional real Monzero fakechain integration

Build `dex_monzero_fakechain_integration_tests` and set `MONZERO_DEX_MONZEROD`
to an absolute, verified current daemon executable. An unset variable causes a
skip, not proof of integration. The test creates a new private fakechain directory
under the working directory; it never opens a user's wallet or node database.
It uses `--offline --regtest --keep-fakechain`, loopback RPC/P2P bindings and no
ZMQ/IGD. No experimental asset hard fork is enabled.

Two successive starts check the independently pinned fakechain genesis, network
identity, height, node-readiness validation and zero peers. A fixture-only `curl`
request (with proxies disabled and a deadline) generates one block to the existing
public functional-test address; no wallet is opened. Height two and the exact
non-genesis block hash must survive restart, catching accidental database reset.
Mutation requests remain rejected by the C++ client's allowlist. Both child
instances exit cleanly; normal test cleanup removes the disposable data. This
checks daemon RPC and block persistence, not funded swaps or XMZ spend recovery.

## Public offer sender usage

`dex_relay_send` delivers one existing signed outbox message to a loopback
`dex_relay_service` configured to issue signed receipts:

```sh
build-dex-private/src/dex_relay_send --outbox-file /private/path/outbox.json \
  --sender-key <sender-public-key-hex> --receiver-key <pinned-receiver-public-key-hex> \
  --sequence 1 --port <loopback-receiver-port>
```

Provision the receiver pin independently. Both keys are public, not wallet keys.
The command only opens existing history; it cannot create identities or messages.
Success means a matching signed receipt was verified and saved. An
`already-acknowledged` result uses the stored verified receipt without connecting
again. Connection/receipt failures retain the original outbox and exit nonzero;
the overall attempt has a twenty-second deadline. Preserve history on failure.
This transports public offers, not private TLS quotes, and never authorizes trades,
signing, broadcast or fee collection. GUI startup is not integrated with this tool.

## Private quote queue limits

Private TLS send admission counts both the plaintext and encrypted Qt queues.
[Qt documents these separately](https://doc.qt.io/archives/qt-5.15/qsslsocket.html#encryptedBytesToWrite):
encryption is not evidence that bytes have left the socket. New framed messages
are refused when the combined queued bytes would exceed the admission budget.
This is not an exact process-memory cap: TLS framing and kernel buffers are
separate. A refused send is not peer acknowledgement and does not erase durable
quote state.

## Private quote journal persistence

`dex_private_quote_service` uses both `quotes.json` and `settlement.json` in its
private state directory. Acceptance is acknowledged only after both are durable.
Swap IDs for these service quotes are their lowercase commitment hex. Preserve
both files together; never delete a journal to restart a swap. A fresh exact retry
can repair interruption between quote persistence and initial journal insertion.
An expired quote cannot be newly accepted to reconstruct missing history.
These records and the TLS acknowledgement do not authorize signing or broadcast.

Before listening, the private quote service revalidates every persisted mutual
quote, including both signatures and its deployment binding. Expired valid
history is recoverable; malformed signatures, future acceptance timestamps or
legacy records without mutual-signature artifacts fail startup without rewriting
history. The read-only inventory is all-or-nothing. It does not regenerate missing
journal events or provide protection against deletion/rollback by a local attacker.
