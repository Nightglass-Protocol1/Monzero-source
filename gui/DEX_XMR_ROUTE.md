# XMR in the DEX: through BNB, ETH or SOL

Owner decision, 2026-11-13: Monero (XMR) trades through the project's own swap engine, against
BNB, ETH or SOL. XMZ/XMR is two swaps back to back (XMZ → BNB → XMR). This replaces the earlier
plan of routing through BTC with the upstream BTC/XMR software (`DEX_XMR_ROUTE.md` on `main`,
commit 4ee1dc7), which stays valid as a fallback but is not being built.

## Why not a direct XMZ/XMR swap

An atomic swap needs one side that can lock coins with a refund timer. Neither XMZ nor Monero
can. A direct swap would need a consensus change to XMZ (a hard fork with new cryptography);
that is not planned.

## How it works

Monero has the same keys, addresses and wallet RPC as XMZ. The XMZ swap (contract `XmzSwap.sol`
on BNB/ETH, the Solana program, the engine in `dex-contracts/engine`) therefore runs unchanged
with XMR in XMZ's place:

- A listing names the coin in its pair: `BNB/XMR`, `ETH/XMR`, `SOL/XMR`, and carries an `xmr-*`
  network (`xmr-mainnet`, `xmr-stagenet`, `xmr-testnet`). XMZ listings are byte-for-byte as before.
- The shared address is a Monero address (prefix 18 / 24 / 53). Amounts are XMR atoms (12 places).
- The wallet side talks to `monero-wallet-rpc` (official Monero release) instead of
  `monzero-wallet-rpc`. Before a trade starts the helper checks the wallet service holds the
  trade's coin, so an XMR trade cannot run on the XMZ wallet or the other way round.

XMZ/XMR for a user who holds XMZ and wants XMR:

1. Leg 1, `BNB/XMZ`: sell XMZ for BNB (live today).
2. Stop. The user holds BNB in their trade account. Nothing starts leg 2 by itself.
3. Leg 2, `BNB/XMR`: buy XMR with that BNB.

Each leg is atomic on its own. Between the legs the user holds BNB and carries its price
movement; no one else ever holds their coins. If no XMR offer suits them, they keep the BNB or
send it back to MetaMask.

## Node trust (new)

The wallet service believes its node. A lying Monero node could show an XMR lock that is not on
the real chain; the trader paying BNB would then release it for nothing. So the lock counts only
as deep as independent nodes see it: `witnessDaemons` in the helper's tick config, checked in
`xmz::witnessed_confirmations` (the fewest confirmations any witness reports; zero if one does
not know the transaction, has it only in its pool, saw a double spend, or does not answer).
A Monero main-network trade is refused with fewer than two witnesses. The apps need a list of
independent Monero nodes; a project-run Monero node as the wallet's own node is recommended.

## Mainnet gate

XMR offers on the main networks are refused by apps and relays until switched on:
`Rules.xmr_mainnet` (helper request `xmrMainnet`, relay config `xmrMainnet`), both off by
default. Test networks always accept XMR. Opening needs, in order:

1. Local Monero fakechain: `bash dex-contracts/engine/tests/run-xmr-local.sh` (wallet-level
   lock/sweep, full swaps, refunds, restarts). **Passed 2026-11-14** on official Monero
   0.18.5.4 (owner's run): wallet-level test 1/1, swap tests 11/11.
2. Public test networks: Sepolia + Monero stagenet through the helper's commands and a relay,
   `COIN=xmr cargo run --example market_e2e -- <run dir>` (needs stagenet XMR in a funder wallet).
3. App integration (below), then an app-to-app stagenet trade and a refund run.
4. Real funds: owner's own-accounts XMR/BNB swap on mainnet with small amounts, then signed
   control documents with XMR caps.

## What is built (branch `dex/xmr-evm`)

- Engine: Monero networks and addresses, pair names, `coin: "xmr"` on `post`, wallet-coin check,
  Monero fee margin, coin and decimals in trade status, witness nodes, mainnet gate.
- Relay: XMR pairs with their own boards, prices and candles in XMR units, `xmrMainnet` switch,
  `pairs` in `/v1/info`.
- Tests: engine 94 unit tests, relay 13; the local-chain integration tests run on either coin
  (`MONZERO_PRIVATE_COIN=xmr`, `MONZERO_XMR_BIN`). They pass on XMZ and on Monero (2026-11-14),
  with the chain's node as witness.
- Official Monero 0.18.5.4 (Linux x64 and Android arm64), checked against the release's signed
  hash list, in `core/build/monero-official/`.

## App integration still to do

Same pattern as XMZ in each wallet (desktop Qt, Android):

- A Monero wallet service pair per app: an XMR trading wallet (`monero-wallet-rpc`, the app's
  own wallet file, like the XMZ trading wallet) and the per-trade shared wallet. Android: the
  official arm64 `monero-wallet-rpc` packaged as `libmonero_wallet_rpc.so`; there is no official
  x86_64 Android build, so the emulator needs one built from source.
- Monero node settings: the wallet's node plus at least two witnesses.
- One market per (chain, coin): `BNB/XMR`, `ETH/XMR`, `SOL/XMR` next to the XMZ ones; helper
  requests carry `coin` and `xmrMainnet`.
- Bridge and page: XMR balance and receive address, "move XMR out" (helper `xmzWallet` send on
  the XMR wallet), XMR pair tabs, amounts with 12 places, and the two-step XMZ/XMR view with the
  stop between legs.
- Risk control: per-trade and per-day XMR caps in the signed control documents.
