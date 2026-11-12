# Opening the DEX: offers, relays, the price chart and limits

Owner decision, 8 November 2026: open the pairs that passed a real-funds mainnet swap, with small
limits; offers found through open relays; a candlestick chart in the app and on the website.

| Decision | Choice |
|---|---|
| Discovery | Public relays plus peer-to-peer. Signed offers on open relays (the project runs the first ones; anyone can run another). Relays copy offers and trades from each other. Traders then talk directly over libp2p, as today. |
| Chart | Candles (price and volume) in the app and on monzero.org/markets/, from completed trades the relays checked on chain. Before the first trade: the $0.001 opening rate as a reference line. |
| Limits | $25 per trade and $250 a day per pair, in the owner-signed control document for each network. Raised later only by a new signed document. |
| Pairs | ETH, BNB, BTC through the relays; LTC too (same swap as BTC) once its own real-funds swap passes. SOL waits: its mainnet program was closed after the test, and the app has no SOL trade path (no XMZ side wired to the Solana program). |

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

## BTC and LTC through the relays

LTC/XMZ runs the same swap as BTC/XMZ, so everything below covers both. Offers are
`monzero-dex-utxo-listing/1`: pair `BTC/XMZ` (btc-* networks) or `LTC/XMZ` (ltc-* networks), the
amount in the coin's atoms (`coin`), signed over `"MONZERO-DEX-UTXO-LISTING-V1\n"` + canonical
JSON; relays serve them at `/v1/utxo/listings` for the networks in `utxoNetworks`.


BTC/XMZ trades are set up from both traders' identity cards and two signatures over the terms
(the trade kit), then negotiated by the two apps. Both steps now go through the relays:

- **Offers.** `trade_kit.py offer` posts a signed offer (network, side, the coin amount, XMZ atoms, the maker's Ed25519 identity key, its mailbox key; signed with that
  identity key). Relays copy them from peer relays. `trade_kit.py offers` lists the checked offers.
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

## Trading from the web page (ETH and BNB)

The website's order book (`website/dex/`, `dex-trade.js`) can trade, with MetaMask in the browser
and the Monzero wallet open on the same computer. **The page is the screen; the wallet stays the
engine and the authority.**

- **Link.** The wallet listens on loopback only (`DexWebBridge`, 127.0.0.1, first free port of
  38471-38475). It answers only requests whose `Origin` is `https://monzero.org` (or
  `www.`) and whose `Host` is the loopback address itself; dev builds also answer pages served
  from `localhost`. The site's policy lets `/dex/` connect to those five ports and nothing else.
- **Connecting.** The page asks MetaMask for the account and asks the wallet to connect. The
  wallet shows a prompt with the site, that account and a six-digit code; the page shows the same
  code. Accepting gives the page a token (kept as a hash in `dex/web-page.ini`). One page at a
  time; "Disconnect" on either side ends it.
- **What the page can do.** Read the trading wallet's balance, the trade account, our offers and
  the trade's state; ask to post, take or cancel an offer; ask for unused coin to be sent back.
  **What it cannot do:** approve a trade, name a payout address, read a key. A trade starts only
  when the user approves, in the wallet, the amounts the wallet reads from the signed offer
  (`DexWebPrompt.qml`, with a desktop notification). Proceeds and returned coin go only to the
  account accepted at connection time. A page that shows a wrong price changes nothing.
- **Money.** MetaMask pays the coin (the amount plus the fee reserve the engine wants) into the
  wallet's own trade account; the swap then runs exactly as an in-app trade. When it is over the
  wallet sends what the account holds back to the MetaMask account by itself. Coin paid in for
  an offer that was cancelled, refused or never taken goes back with "Send it back to MetaMask"
  (helper command `withdraw`: refused while any trade in that chain's folder is unfinished or an
  offer of ours is open).
- **Limits.** The page reads the same signed control document and refuses, before anything is
  paid in, what the wallet would refuse; the wallet's own check stays the one that counts.
- **Keys.** One step in the wallet: "Create trading keys" with a password (`setUpTradingKeys`);
  the key wallet is made in the app's data folder. The password is never typed into the page.

Tested 10 Nov 2026 with a headless browser and a stand-in for MetaMask that pays real Sepolia
ETH (`core/build/app2app-20261109`, evidence `web-page-eth-sepolia-1.json`): other origins and a
foreign `Host` are refused, connecting needs the wallet's acceptance, post + cancel + send back,
and a full swap taken from the page. Not tested: the real MetaMask extension, and the page served
from `https://monzero.org` (browsers ask the wallet's permission for a public page to reach a
local address; the bridge answers that preflight).

### The wallet has no trading page (public edition)

The public wallet shows no DEX page in its menu. Everything the wallet has to ask is a prompt on
top of whatever page is open (`components/DexWebPrompt.qml`):

- **Connect**: the code the web page shows, accept or refuse.
- **Trading password**: asked right after the first connection (it makes the trading keys) and
  whenever the page finds them locked (`POST /v1/unlock`). The password is typed in the wallet only.
- **Approve a trade**: the terms the wallet holds.
- **Add XMZ** (`POST /v1/xmz-add`): opens the wallet's own Send page with the trading wallet's
  address filled in; the user enters the amount and confirms as for any payment.
- **Move XMZ back** (`POST /v1/xmz-return`): a confirmation; the only destination is this wallet's
  own main address.

The page can only ask for these screens (at most one request every three seconds). Developer
builds keep the in-wallet trading screen behind the menu entry; it opens on a short "connect"
view (`components/DexConnect.qml`) with the full screen one click away.

### Phones (Android wallet)

The same link works on an Android phone, with the page in the phone's own browser. The wallet app
carries the engine: the trade helper and `monzero-wallet-rpc`, both built for Android and run as
programs by the app (`android/wallet/dex-native/`), and a port of the parts of `DexEngine` the page
drives (`android/wallet/app/src/main/java/com/m2049r/xmrwallet/dex/`).

- **Switching it on.** In an open wallet: menu, "Trade on the DEX", "DEX link on this phone". A
  foreground service (`DexService`) then listens on the same five loopback ports, with the same
  origin and host checks, and stays up while the user is in the browser. That wallet's address is
  remembered as the only place trading XMZ can be moved back to.
- **Notifications instead of windows.** A page cannot bring the wallet to the front on Android, so
  everything the wallet has to ask arrives as a notification that opens the wallet's prompt
  (`DexPromptActivity`): the connection (the same six-digit code as on the page), the trading
  password, each trade to approve (the amounts the wallet holds from the signed offer), and moving
  XMZ back. Opening the wallet app while a request waits shows it too. No cloud push service is
  used: the page and the wallet are on the same phone. After an answer the prompt closes and the
  phone is back in the browser.
- **Keys.** The phone makes twelve BIP-39 recovery words of its own and keeps them encrypted under
  the trading password (Argon2id, AES-256-GCM). The trade account is `m/44'/60'/0'/0/0` of those
  words (checked against ethers), so the words also open it in MetaMask. They are shown when the
  keys are made and on request.
- **Limits.** The same owner-signed control documents, key and rules (`DexRisk`); the wallet's DEX
  screen shows what they allow. The mainnet build trades on main networks only and answers only
  `https://monzero.org`.
- **XMZ node.** The wallet services use the wallet's remote node, untrusted; there is no node on
  the phone.
- **While a trade runs** the service holds a wake lock and refuses to be switched off. If the app
  is closed anyway, the next start asks for the trading password (a notification) and the trade
  resumes from its journal, as on the desktop.

One change in the helper for Android: its HTTPS client for EVM nodes checks servers against the
built-in Mozilla roots there (`chain.rs`, `http_client`), because a stand-alone program cannot use
Android's certificate checker. Other systems are unchanged.

Tested 12 Nov 2026 on an Android 15 emulator (x86_64, `devnet` debug build), with the page in the
emulator's Chrome, the stand-in for MetaMask of the desktop page tests, the test relay, Sepolia
and the XMZ testnet (evidence `core/build/app2app-20261109/web-page-phone-eth-sepolia-1.json`,
driver `phone_trade.py`):

- other origins and a foreign `Host` are refused; the page's connection request raises a
  notification, its prompt shows the page's code, and accepting hands the page its token once;
- keys made with a trading password, recovery words shown, a wrong password refused, unlock after
  an app restart (asked by the page, and by the wallet itself when a trade is waiting);
- a full swap taken from the page: the phone bought 20 XMZ for 0.001 Sepolia ETH from a desktop
  app's relay offer; approval from the notification; both sides `Done(Swapped)`; the XMZ arrived in
  the phone's trading wallet and the ETH left in the trade account went back to the paying account
  by itself;
- moving the XMZ back to the wallet's own address, confirmed from its notification;
- the mainnet build fetched the three live control documents, checked the owner's signature and
  showed their limits; a device self-check (`DexSelfCheck`, debug builds) covers changed, expired,
  foreign-network and wrongly signed documents, the per-trade and 24-hour limits, and the key file
  (25 checks). No offer was posted and nothing was traded on a main network from the phone.

Not built for phones: BTC/LTC (no light wallets there), the wallet's own trading screen, iOS.
Not tested: a real phone (only the emulator), the real MetaMask or Phantom app's built-in browser,
the page served from `https://monzero.org` to a phone (the browser's local-network prompt), a
trade interrupted by Android's battery saving.

## Order of work

1. Offer type, mail, take logic and relay. Done.
2. Website market view (TradingView Lightweight Charts, self-hosted). Done, not deployed.
3. App: post and take offers for ETH/BNB, trade runs through the existing EVM helper. Done.
4. Public/dev edition split; limits tool. Done; the owner signs and publishes the documents.
5. BTC through the relays (above): kit and app offer boards, relay negotiation. Done.
6. The app's own chart (the website chart's data, in the DEX screen).
