# Opening the DEX: offers, relays, the price chart and limits

Owner decision, 8 November 2026: open the pairs that passed a real-funds mainnet swap, with small
limits; offers found through open relays; a candlestick chart in the app and on the website.

| Decision | Choice |
|---|---|
| Discovery | Public relays plus peer-to-peer. Signed offers on open relays (the project runs the first ones; anyone can run another). Relays copy offers and trades from each other. Traders then talk directly over libp2p, as today. |
| Chart | Candles (price and volume) in the app and on monzero.org/markets/, from completed trades the relays checked on chain. Before the first trade: the $0.001 opening rate as a reference line. |
| Limits | $25 per trade and $250 a day per pair, in the owner-signed control document for each network. Raised later only by a new signed document. |
| Pairs | ETH and BNB first (the app already runs their trades), then BTC. SOL waits: its mainnet program was closed after the test and the app has no SOL path yet. |

## Trust model

- A relay holds nothing and signs nothing for traders. Every offer is signed by the maker's own
  trading account; a relay that drops, delays or invents offers can only cost time, never funds.
- A completed trade (a "print") is accepted only from the chain: the relay reads the claim
  transaction, checks it went to the pinned contract for that chain, and matches the swap's
  nonce to the offer's id. The coin amount is from the chain; the XMZ amount is from the
  maker-signed offer. A maker can still trade with itself to paint a price; that costs real
  fees, and the chart labels prints as self-reported settlements, not a market feed.
- The app enforces limits through the signed control document, never through the relay.

## Offer (listing) v1

`monzero-dex-listing/1`, signed with EIP-191 by `maker` (an EVM trading account, plain, no code):

```
id         32 random bytes; also the trade id, so the swap nonce (its first 8 bytes) ties a
           claim on chain to the offer
pair       "ETH/XMZ" | "BNB/XMZ"
chainId    1 | 56   (testnets: 11155111 | 97)
contract   the XmzSwap deployment pinned in the app for chainId
side       "sell-xmz": the maker sells XMZ and receives coin (maker = claimer)
           "buy-xmz":  the maker pays coin and receives XMZ (maker = owner)
value      coin amount in wei
xmz        XMZ amount in atoms, 1e11 per XMZ (decimal string)
maker      maker's EVM address
mailbox    maker's X25519 mailbox key: takes are sealed to it
xmzNetwork "mainnet" | "testnet" | "stagenet"
createdAt, expiresAt   unix seconds; at most 24 h apart
```

All or nothing, one fill. A maker cancels with a signed `monzero-dex-cancel/1` for the id.

## Taking an offer: sealed relay mail

Most traders cannot accept incoming connections, so the key exchange goes through relay
mailboxes (`engine/src/mail.rs`, `market.rs`). The taker seals a `take` (its account, its reply
mailbox, its signed key frame) to the maker's mailbox key; the maker answers with its own frame,
sealed to the taker's mailbox. Each letter uses a fresh X25519 sender key and ChaCha20-Poly1305;
relays see only ciphertext. Frames stay signed by the traders' EVM accounts and are checked as in
the direct exchange, so a relay can delay or drop letters but never change a trade. Both sides
build the same trade from the listing: trade id = listing id, both timeouts 2 h. A letter is
about 340 KB (the cross-curve proof); relays keep up to 512 KB per letter, 16 per mailbox, 2 h.

## Relay HTTP API v1

```
GET  /v1/info                     version, chains and contracts, peer relays
GET  /v1/listings?pair=ETH/XMZ    open offers
GET  /v1/listings/{id}            an open or recently closed offer
POST /v1/listings                 signed offer (the maker's account must back it)
POST /v1/cancel                   signed cancel
POST /v1/mail  {to, sealed}       leave a sealed letter
GET  /v1/mail?to=&since=          letters for a mailbox
POST /v1/prints {chainId, tx}     a claim to record (checked on chain once final)
GET  /v1/prints?pair=&since=      recorded trades, newest first
GET  /v1/candles?pair=&interval=  OHLCV, interval 3600 | 14400 | 86400
```

Relays pull `/v1/listings` and `/v1/prints` from their peer relays and check them again: offers by
signature, prints on chain. Apps query several relays and merge.

## In the app

- Helper commands (`monzero-dex-evm-helper`, one JSON request each): `listings`, `post`, `serve`
  (maker: answer takes; call again until taken), `take` (taker: call again until agreed), `cancel`,
  `report` (claimer: tell the relays where the claim is). Once agreed the trade folder holds the
  saved trade and `status` / `approve` / `tick` / `payout` run it as before.
- `EvmMarketJob` runs them; `DexEngine` exposes `refreshEvmOffers`, `postEvmOffer`, `takeEvmOffer`,
  `cancelEvmOffer`, checks the kill switch and caps before posting or taking, and hands an agreed
  trade to `EvmTradeJob`. Startup flag `--dex-evm-market <config>` with `--dex-evm-seed-file`.
  Config `monzero-private-xmz-evm-market-v1`: helper, helperDigest, tradesDir, evmRpc, chainId,
  accountIndex, mainWallet, swapWallet, relays (optional evmLogChunk, localContract for chain 31337).
- One trade at a time: a second agreed trade waits until the first is finished.

## Public and dev editions

`-DDEX_PUBLIC_EDITION=ON` (needs `WITH_DEX_PROTOTYPE=ON` and `MONZERO_DEX_CONTROL_PUBKEY`):
- no `MONZERO_ENABLE_PRIVATE_DEX` opt-in; mainnets only (Bitcoin mainnet for BTC; chains 1 and 56
  for EVM); test networks are refused, so there is no path without a document;
- control documents come only from `https://monzero.org/dex/control/<network>.json`, checked
  against the pinned key; local `--dex-control-file`, `--dex-evm-control-file` and
  `--dex-control-url` are refused. No document, an expired one or `newTradesEnabled: false` =
  no new trades; trades already started always finish or refund.
The dev edition (option OFF) keeps today's behaviour for the team.

Opening limits (owner, 8 Nov): $25 per trade, $250 per day per network, XMZ at the $0.001 opening
rate (25,000 XMZ per trade). `gui/tools/dex-control/opening-limits.py --sequence N` prints the
`sign-control.py` commands at today's prices; the owner signs with the control key and publishes
`btc-mainnet.json`, `eth-mainnet.json`, `bnb-mainnet.json` under `website/dex/control/`. Documents
last at most 30 days (7 by default), so they are re-signed regularly; raising limits is a new
document with a higher sequence.

## BTC through the relays

BTC/XMZ trades are set up from both traders' identity cards and two signatures over the terms
(the trade kit), then negotiated by the two apps. Both steps now go through the relays:

- **Offers.** `trade_kit.py offer` posts a signed BTC offer (`monzero-dex-btc-listing/1`: network,
  side, sats, XMZ atoms, the maker's Ed25519 identity key, its mailbox key; signed with that
  identity key). Relays serve them at `/v1/btc/listings` for the networks they list in
  `btcNetworks`, and copy them from peer relays. `trade_kit.py offers` lists the checked offers.
- **Taking.** `trade_kit.py take` sends the taker's card and destinations as a sealed letter;
  the maker's `trade_kit.py serve` answers the first taker with the terms (the offer's amounts,
  both keys, both sides' destinations, the maker's fees and timelocks) and its signature; the
  taker checks the terms against the offer and its own destinations, signs, and both assemble
  the same signed quote. Then each side runs `configure` with LOCAL.json
  `"peer": {"relays": [...]}`.
- **Negotiation.** The apps talk through `RelayMailTransport` (a mode of `PrivateQuoteChannel`):
  letters authenticated by both mailbox keys, in order, exactly once; a restart shows as a lost
  connection and is resumed from the saved trade, as with the socket. No port is opened.

Evidence (regtest, two separate app processes, `core/build`): `btc-regtest-relay-1` (the apps
connected only through the relay; 1018 s, passed) and `btc-regtest-relay-offer-1` (the whole
setup from a relay offer as well).

- **In the app.** The DEX screen's BTC/XMZ offer board (`BtcMarketJob`, `BtcTradeSetup`): refresh,
  post, take, cancel. Each offer or take gets its own identity and folder; when both signatures
  are in, the folder holds the signed quote and an app folder whose peer is the relay, and
  "Review and start" opens it in the normal trade setup review. The app and the kit speak the
  same protocol: `btcOffersBetweenTheAppAndTheKit` (dex_tests, with a live relay) agrees trades
  both ways (app takes a kit offer; kit takes an app offer), checks both sides hold the same
  signed quote and that the app's folder passes the trade setup review. Startup flag
  `--dex-btc-market <config>` (`monzero-private-btc-market-v1`: network, relays, tradesDir,
  journalHelper, local services, params with our four destinations, policy).

Not yet: a signet/mainnet run through a public relay. BTC prints are not charted yet (a claim would need a Bitcoin chain check).

## Order of work

1. Offer type, mail, take logic and relay. Done.
2. Website market view (TradingView Lightweight Charts, self-hosted). Done, not deployed.
3. App: post and take offers for ETH/BNB, trade runs through the existing EVM helper. Done.
4. Public/dev edition split; limits tool. Done; the owner signs and publishes the documents.
5. BTC through the relays (above): kit and app offer boards, relay negotiation. Done.
6. The app's own chart (the website chart's data, in the DEX screen).
