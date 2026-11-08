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
id         32 random bytes (hex); becomes the swap nonce, so a print can be tied to it
pair       "ETH/XMZ" | "BNB/XMZ"
chainId    1 | 56   (testnets: 11155111 | 97 on a test relay)
contract   the pinned XmzSwap deployment for chainId
side       "sell-xmz": the maker sells XMZ and receives coin (maker = claimer)
           "buy-xmz":  the maker pays coin and receives XMZ (maker = owner)
value      coin amount in wei (string)
xmz        XMZ amount in atoms, 1e11 per XMZ (string)
maker      maker's EVM address
peer       maker's libp2p multiaddr with /p2p/<peer id>
xmzNetwork "mainnet" | "testnet" | "stagenet"
createdAt, expiresAt   unix seconds; at most 24 h apart
```

All or nothing, one fill. A maker cancels with a signed `monzero-dex-cancel/1` for the id.

## Relay HTTP API v1

```
GET  /v1/info                     version, chains and contracts, peer relays
GET  /v1/listings?pair=ETH/XMZ    active offers
POST /v1/listings                 signed offer
POST /v1/cancel                   signed cancel
POST /v1/prints                   {chainId, tx}: a claim to record
GET  /v1/prints?pair=             recorded trades, newest first
GET  /v1/candles?pair=&interval=  OHLCV, interval 3600 | 14400 | 86400
```

Relays pull `/v1/listings` and `/v1/prints` from their peer relays and check them again: offers by
signature, prints on chain. Apps query several relays and merge.

## Order of work

1. Offer type, signing and checks in the engine (`src/listing.rs`), shared by relay and app. 
2. Relay service (`relay/`), with tests against a local chain.
3. Website chart on /markets/ (TradingView Lightweight Charts, self-hosted).
4. App: post and take offers for ETH/BNB, the trade runs through the existing EVM helper; chart.
5. Public/dev build split; control documents eth-mainnet / bnb-mainnet at $25 / $250 (owner signs).
6. BTC through the same relays (its offers carry a BTC payout path instead of a contract).
