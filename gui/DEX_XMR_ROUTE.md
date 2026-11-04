# XMR via the BTC route — implementation spec

**Status (2026-11-04).** Monero support is a **routed** trade: `XMZ → BTC → XMR`,
two independent atomic swaps with the user holding the BTC in their own wallet in
between. No bridge, no wrapped token, no custody of anyone else's funds (owner
decision, 2026-10-29). This spec turns the already-approved routed design
(`DEX_MULTICOIN_PROGRESS.md` → "Approved routed-trade design") into something
implementable. It adds **no new cryptography** — it orchestrates two proven legs.

## What already works
- **Leg 1 — XMZ ↔ BTC:** our engine, **real mainnet swaps** done (see `monzero-dex-btc`).
- **Leg 2 — BTC ↔ XMR:** unmodified upstream eigenwallet protocol, **full stagenet
  swap + refund + maker-restart-after-cancel passed** (`tools/dex-engine-port/xmr-stagenet/README.md`).
- So both halves are demonstrated; the missing piece is the **guided orchestration**
  plus mainnet validation of leg 2.

## The route is NOT atomic — the cardinal rules
Carried over verbatim from the approved design, and they are non-negotiable:
1. Each leg shows **its own** status, confirmations, fees and recovery actions.
2. Completing or approving **leg 1 must never auto-start leg 2.** The user explicitly
   starts leg 2 after leg 1 has fully settled and the BTC is in their wallet.
3. If leg 2 is unavailable (no XMR maker, bad price, user changes their mind), the user
   simply **keeps the BTC** — it is already theirs. That is the safe stopping point.
4. The UI must never render the pair as a single atomic "XMZ/XMR" swap.

## Orchestration model
A **Route** is a thin coordinator over two existing, independent swap jobs — it tracks
progress and guides the user; it does **not** hold funds or bridge the legs.

```
state: Quote
  -> Leg1Running      (XMZ↔BTC job; user approves; locks; settles BTC to USER's BTC addr)
  -> HandoffReady     (BTC confirmed in the user's own wallet; route pauses here by design)
  -> Leg2Offered      (user explicitly chooses to continue; a BTC↔XMR quote is fetched)
  -> Leg2Running      (eigenwallet BTC↔XMR; user funds the deposit from their BTC; settles XMR)
  -> Done             (XMR in the user's Monero wallet)
```
- `HandoffReady` is a **hard stop**, not a transition the coordinator takes on its own.
- Each leg is an ordinary job with its **own journal and recovery** (leg 1 = our BTC
  refund path; leg 2 = eigenwallet's BTC refund path). The coordinator only reads their
  status and advances the *UI*, never their funds.

## App integration (reuse, don't reinvent)
- **Leg 1** is the existing XMZ↔BTC path (`DexEngine` / Litecoin-style light funding on
  the BTC side).
- **Leg 2** runs the **upstream eigenwallet `swap`/`buy_xmr`** via the same pinned,
  hash-checked **helper + JSON review/commitment** pattern already used by
  `EvmTradeJob` / `LitecoinJournalJob` (`BackendExecutable`, approval gate, journal).
  Add an `XmrRouteJob` that owns a `leg2` helper handle; it never signs or holds funds.
- A `RouteRegistry` entry marks `XMZ→XMR` as **routed** with both legs' metadata so the
  trade screen can render two stacked leg cards.
- Reuse the engine's recovery doc (`/dex/recovery/`) — it already covers both chains.

## Fees
Two independent spreads, shown transparently:
- **Leg 1:** the Monzero XMZ↔BTC dynamic spread (0.1%→0.5%), to the owner's BTC Ledger.
- **Leg 2:** the BTC↔XMR maker's spread. Either Monzero runs the XMR ASB (revenue, own
  XMR liquidity) or the user picks an upstream maker. Show the **all-in** XMZ→XMR rate
  **and** each leg's fee — never hide that it is two trades with two fees.

## Mainnet gates (before XMR is live)
1. **Leg 2 on Bitcoin mainnet + Monero mainnet** — currently only signet/stagenet.
   Owner real-funds test, like the BTC leg.
2. **Rebase upstream to 4.16.1** — fixes the transfer-proof-ack retries and the daemon
   RPC client issues hit on stagenet (we are on 4.14.0 with a vendored header patch).
3. **End-to-end route test on mainnet** — one XMZ→BTC→XMR run, both refunds, a restart
   mid-route, verifying the `HandoffReady` stop and that leg 2 can't auto-start.
4. **UI** — two leg cards, the "not atomic / you hold BTC in between" banner, and the
   explicit "continue to leg 2" action.
5. **Audit/pentest** — leg 2 is unmodified upstream (well-reviewed), but the
   orchestration + helper wiring are ours and must be in the community pentest.

## Open items for the owner
- Does Monzero **run the XMR ASB** (own XMR liquidity, earns leg-2 spread) or route to
  upstream makers at launch? Running it = revenue + reliability but needs XMR inventory.
- XMR mainnet ASB node/pool choice (the stagenet work found public nodes reset the Rust
  daemon client; the `--monero-pool` + title-case-header path was the fix).
