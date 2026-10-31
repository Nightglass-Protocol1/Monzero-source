# Seller light-wallet redemption (built; regtest and signet runs passed)

Today the XMZ seller ("native-funder") redeems the buyer's BTC/LTC lock through a full
node (`LitecoinRpc`). This note scopes replacing that node with the in-app light wallet,
so a seller needs no Bitcoin Core. Status (26 Oct 2026) is in the last section.

## What the seller path asks of the node

Every call goes through the concrete `LitecoinRpc` type
(`litecoin_negotiation/execution/redeem*.rs`, `recovery/redeem.rs`, `native_relay.rs`):

| Node call | Purpose | Core RPC |
|---|---|---|
| `verify_live_lock` | Lock output exists, is unspent (mempool included), has the agreed script and amount, has enough confirmations, and the tip leaves `safety_blocks` before CSV cancellation | `gettxout … true`, tip before/after |
| `preflight_contract_spend` | Completed redeem would be accepted now | `testmempoolaccept` |
| `dispatch_contract_spend` | Broadcast the exact retained bytes | `sendrawtransaction` |
| `contract_spend_in_mempool` | Reconcile an uncertain broadcast | `getmempoolentry`, `getrawtransaction` |
| `confirmed_transaction`, `indexed_confirmed_transaction` | Redeem confirmed in a given block, with depth | `getrawtransaction` + block/header checks |
| `network` | Chain identity | — |

## Light-wallet equivalents

The light wallet already has a validated header chain (`HeaderSequence`, checkpoints
for testnet4, signet and mainnet), Electrum TLS sessions, merkle inclusion against
that chain (`inclusion_on`, `transaction_on`) and exact-byte broadcast (`session/broadcast.rs`).

| Need | Light source | Trust |
|---|---|---|
| Lock exists, script, amount, depth | Lock transaction bytes + merkle inclusion in a validated header | Proven (SPV) |
| Current tip / cancel margin | Validated header chain | Proven work, but **see stale tip** |
| Lock still unspent (incl. mempool) | Electrum `scripthash.get_history`/`listunspent` for the contract script | **Server's word only** |
| Redeem would be accepted | No `testmempoolaccept`; local script/witness check + broadcast result | Local + server |
| Broadcast | `blockchain.transaction.broadcast` | Server relays or not |
| Redeem confirmed | Merkle inclusion + depth on the validated chain | Proven (SPV) |

## Risks that need design, not just porting

1. **Hidden spend.** Electrum cannot prove an output is unspent. Before the CSV cancel
   timelock, though, the lock can only be spent by the seller's own redeem, so a hidden
   spend there is not a theft risk. It matters after the timelock (a hidden cancel) and
   for recovery. Keep release strictly before the timelock with the `safety_blocks`
   margin, and query at least two independently operated servers in recovery.
2. **Stale tip.** This is the real exposure: a server that withholds new headers makes the
   cancel margin look larger than it is, which defeats rule 1. Require the best header's
   time to be within a bound of local time (e.g. 90 minutes for Bitcoin), cross-check tips
   across servers, and fail closed.
3. **No mempool preflight.** Replace `testmempoolaccept` with local checks: exact input
   outpoint, sequence/CSV fields, fee within policy, witness size, and libbitcoinconsensus
   script verification if it can be vendored offline. A broadcast refusal must leave the
   retained bytes unchanged for retry, as today.
4. **Seller payout address.** The redeem output must pay a light-wallet address the seller
   controls, committed before the trade (`native_quote_destinations`). The wallet must
   reserve that address so it is not reused, and scan it after redemption.

## Proposed shape

- A `RedeemChain` trait with the six calls above; `LitecoinRpc` implements it unchanged,
  `LightRedeemChain` (session + headers + server set) implements it with the rules above.
  Execution code switches from `&LitecoinRpc` to `&impl RedeemChain`.
- App configuration `monzero-private-ltc-redeem-light-v1` beside the existing
  `…-redeem-rpc-v1`; the GUI selects one per trade and records which in the journal.
- Helper operations on `native-ltc-light`: `redeem-observe`, `redeem-send-review`,
  `redeem-send` (approval-gated like funding `send`).

## Tests

- Unit: each `RedeemChain` method against fixtures; wrong merkle path, wrong script,
  wrong amount, shallow depth, stale tip, servers disagreeing → refused, nothing released.
- Regtest with two electrs instances, one dishonest (hides a spend, withholds headers).
- Live: signet success and refund with a light-wallet seller, then the two-machine run.

## Size

Engine trait and light implementation: several days. Helper and app plumbing: a few
days. Live evidence: about a day of chain time. The hidden-spend and stale-tip rules
should be reviewed before anything is built.

## Status — 26 October 2026

Built in the engine (`litecoin_rpc/redeem_chain/`, regenerated into
`upstream-bitcoin-swap-chain.patch`):

- `RedeemChain` trait; `LitecoinRpc` implements it unchanged. Every seller path
  (native lock preparation and relay, redemption, cancel/punish recovery, native
  refund, settlement lookup) takes `&impl RedeemChain`; only the buyer's
  `sign_recovered_lock` still needs a node wallet.
- `light_rules::light_live_lock`: the lock rule as a pure function.
- `light::LightRedeemChain`: the trait over two or more Electrum servers
  (`LightServer`, implemented by `TlsEndpoint`).

Rules as built (decided without the separate review this note asked for; they
fail closed and are listed here for that review):

| Call | Light rule |
|---|---|
| every call | Sync headers from every server; the heaviest valid branch wins |
| `verify_live_lock` | Lock txid merkle-proven on local headers; every server reports the lock confirmed at the same height, listed unspent, and no reported transaction on the contract script spends the lock outpoint (each is fetched and must match its txid; a cancel pays the lock's own script, so merely touching the script is not a spend); every server's tip (taken after its answers) equals the local tip; local tip time within `maximum_tip_age` (90 min proposed for Bitcoin) |
| `preflight_contract_spend` | Same lock checks for the spent contract output (script from the witness), plus: positive fee, output ≥ 546 sat, CSV block lock met by the proven depth, absolute lock time met, time locks refused. No script execution (no libbitcoinconsensus offline) |
| `dispatch_contract_spend` | Broadcast to every server; any matching acknowledgement counts |
| `contract_spend_in_mempool` | A server lists the txid as pending on the contract script **and** returns byte-identical bytes |
| `confirmed_transaction` / `indexed_…` | Every server returns byte-identical bytes (txids do not commit to witnesses, and recovery reads witnesses); every server places it in the same block via its first output's history; merkle proof against local headers; depth from local headers |

Residual risk, as designed: servers that all collude can hide a spend. Before the
cancel timelock only the seller's own redeem can spend the lock, and release is
refused inside the safety margin, so this is a stall, not a loss.

Tests: `light::tests` runs a fake Electrum server over a real regtest header
chain with real merkle proofs. It covers the honest path from lock check through
confirmation, a spend hidden by one server, colluding servers (documented as
accepted), withheld blocks, a lagging server, an unmet CSV, dust/no-fee/wrong
outpoint, an unconfirmed lock, and tampered witness bytes.

Wired in (gui-wt-btc 9b3158f15, 1b40cdb15, 10384a897):
- Journal helper: `ltcLight {servers, maximumTipAge}` in the seller's funding and
  refund RPC files, exactly one of node or light; each call syncs headers from the
  pinned checkpoint (no shared cache, so no lock contention with the wallet helper).
- App: accepts the light refund configuration (seller only) and passes
  `SSL_CERT_FILE`/`SSL_CERT_DIR` to every job that runs the journal helper.
- Trade kit: `LOCAL.json` `litecoin.light` for the XMZ seller.
- Harness: `--light-seller` (two `LiveElectrumPeer` TLS shims).

Live evidence (core/build), seller on two Electrum servers, no node access:

| Run | Result |
|---|---|
| `btc-regtest-lightseller-4-20261026.json` | Success, 881 s; servers 70/86 requests, no errors |
| `btc-regtest-lightseller-refund-1-20261026.json` | Refund, 2,650 s; seller read cancel and refund through the servers and recovered its XMZ |
| `btc-signet-lightseller-1-20261026.json` | Success on public signet, 2,918 s; redeem 424d020c… (190,000 sat), payout b383882a… (1 XMZ) |
| `btc-regtest-lightseller-dishonest-2-20261028.json` | Success with a dishonest server, 974 s; with one server withholding the newest block the approved redemption was refused before anything was recorded or sent; with it hiding the confirmed redeem the app check stayed unconfirmed; once honest, the trade completed (redeem d2e5e09f…, payout e6b2da6b…) |
| `btc-regtest-lightseller-punish-5-20261028.json` | Punish, 3,005 s; buyer silent after both locks; seller cancelled (e08eadc6…), waited the punish timelock and punished (3bb9df13…, 99,980,000 sat to its payout address), each confirmed by the app through the servers |

Earlier attempts stopped before release and found the two app bugs fixed above
(refund config shape; `LitecoinRedemptionJob` launched the helper with an empty
environment, so TLS failed with UnknownIssuer). Both shims read our own node, so
these runs test the rules and plumbing, not server independence.

## Update — 28 October 2026

- Seller cancel/punish: already in the app (`LitecoinReservationJob`, Dex.qml
  recovery buttons); this note wrongly listed it as missing. Its job launched the
  helper without `SSL_CERT_FILE`/`SSL_CERT_DIR`, the same bug fixed earlier in the
  redemption job; fixed.
- The first punish runs then found a real rule bug: the punish was refused as
  "contract output is spent or unknown to a server" because the cancel output's
  script history also holds the lock (the cancel pays the lock's own 2-of-2
  script). The rule now counts only a transaction that consumes the outpoint;
  regression test `a_cancel_paying_the_lock_script_leaves_its_output_spendable`.
  The rule was too strict (stall), never too loose.
- Harness: `--mode punish` (seller cancels and punishes a silent buyer) and
  `--light-seller-dishonest` (one seller server withholds the newest block during
  the redemption submit, then hides the confirmed redemption; the app must refuse
  both, then complete once the server is honest). Review reads saved data only;
  the chain gate is the submit preflight, which runs before the dispatch record. It also refuses to start when
  the disposable TLS fixture expires before the trade could end (the server
  certificate lasts 7 days; an expired one showed up only as a stalled
  light-wallet step).

Not done:
1. The GUI has no picker for light servers (the trade kit writes them).
2. Outside review of the rules above before any mainnet use.
3. Signet punish run (about eight hours of signet blocks).
