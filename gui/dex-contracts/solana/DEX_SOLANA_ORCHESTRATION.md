# XMZ ↔ Solana orchestration — binding the SOL leg to the XMZ swap

The Solana program (`program/`) is a drop-in for the **Bitcoin leg** of the existing,
proven XMZ (Monero-fork) atomic swap in `core/build/eigenwallet-engine-port`. This doc
works out how the two legs bind into one atomic trade, so revealing a secret on Solana is
exactly what the XMZ side needs. **It is design — the Monero-side wiring is not built yet.**

## The parties (same roles as the engine's `BobState`/`AliceState`)
- **Alice** = the maker/ASB: locks **XMZ**, wants the Solana asset.
- **Bob** = the taker: locks the **Solana asset** (SOL / USDC / USDT), wants XMZ.

(Alice/Bob mirror `swap-machine/src/{alice,bob}`. Bob is the one who locks the
non-Monero chain — Bitcoin today, Solana here.)

## Why Solana is *simpler* than the Bitcoin leg
The Monero spend key of the 2-of-2 lock is `s = s_a + s_b` (ed25519 scalars; Alice holds
`s_a`, Bob holds `s_b`). On the **Bitcoin** leg, Bob can't reveal `s_b` directly — Bitcoin
is secp256k1 — so the engine uses **adaptor signatures + a cross-group DLEQ proof** and an
`EncSig` exchange (`EncSigReadyToBeSent`/`EncSigSent`/`BtcRedeemed`) to leak the scalar.

On **Solana** none of that is needed, because:
1. Solana is **ed25519**, the same group as Monero → the key share `s_a` *is* an ed25519
   scalar, and its public share `S_a = s_a·B` is exactly the Solana swap's commitment.
2. Our program **reveals the secret on claim/refund natively** (it logs `s` and it's in the
   tx), so no adaptor-signature machinery is required.

So the whole `EncSig*` dance collapses into the program's built-in `claim(secret)`.

## The binding (the crux)
Set up the Solana swap (`New`/`NewSpl`) with:
- **`claim_commitment = S_a`** (Alice's Monero public key share). Alice is the Solana
  `claimer`; when she **claims the Solana asset she reveals `s_a`** on-chain. Bob reads
  `s_a` from the Solana tx, computes `s = s_a + s_b`, and **sweeps the XMZ** 2-of-2.
- **`refund_commitment = S_b`** (Bob's Monero public key share). Bob is the Solana `owner`;
  if the trade aborts he **refunds the Solana asset, revealing `s_b`**. Alice reads `s_b`,
  computes `s`, and **reclaims the XMZ**.

Both `S_a` and `S_b` are produced during the normal Monero key-exchange the engine already
does — no new keys, **no DLEQ proof** (the Bitcoin path's `CrossCurveDLEQ` is unnecessary).

## State mapping (Bob = Solana owner)
| engine `BobState` (BTC) | XMZ↔SOL equivalent |
|---|---|
| `Started` / `SwapSetupCompleted` | key + commitment exchange; agree `S_a`, `S_b`, amounts, timelocks |
| `BtcLockReadyToPublish` / `BtcLocked` | `SolLockReadyToPublish` / `SolLocked` — Bob sends `New`/`NewSpl` |
| `XmrLockTransactionSeen` / `XmrLocked` | same — Bob confirms Alice's XMZ lock (view-key + transfer proof) |
| `EncSigReadyToBeSent` / `EncSigSent` | **gone** — replaced by Bob calling `SetReady` on the Solana swap |
| `BtcRedeemed` | `SolClaimed` — Alice claims the Solana asset, revealing `s_a` |
| `XmrRedeemConstructed`→`XmrRedeemed` | same — Bob sweeps XMZ with `s_a + s_b` → **done** |
| `WaitingForCancelTimelockExpiration`→`CancelTimelockExpired` | Solana `timeout1` passes without a claim |
| `BtcCancelled`/`BtcRefunded` | `SolRefunded` — Bob refunds after `timeout2`, revealing `s_b` |
| `BtcPunished` | n/a yet (optional punish path) |
| `SafelyAborted` | `Aborted` |

## Timelock mapping
The Solana program's two timelocks carry the same meaning as the Bitcoin cancel/punish
timelocks: `timeout1` = Alice's claim window / Bob's cancel boundary; `timeout2` = Bob's
refund boundary. They must be set **relative to the Monero lock confirmation** and ordered
so Alice always has time to claim before Bob can refund, exactly as the BTC leg orders
`cancel_timelock` before `punish_timelock`.

## What is reused vs new
- **Reused unchanged** from the eigenwallet engine: the Monero 2-of-2 lock, the view-key +
  transfer-proof verification (`XmrLockTransactionSeen`), the Monero redeem/reclaim, the
  key-exchange that yields `s_a`/`s_b`.
- **New**: a Solana leg driver that replaces the Bitcoin transactions with our program's
  instructions (`New`/`SetReady`/`Claim`/`Refund`), and a coordinator state machine
  (`route/`) with the mapping above. The leg driver uses `SolSwapClient`.

## What is built (`route/`)
- The **coordinator state machine** (the table above), enforcing the safe ordering.
- A **`LegDriver` trait + drive loop** (`driver.rs`) — the coordinator drives it to a
  terminal `Outcome`; a mock proves a full success, a timeout refund, an early cancel, and
  rejection of a protocol-violating maker. The real driver maps the actions to
  `SolSwapClient` (Solana) + the eigenwallet engine (XMZ).
- The **secret binding** (`secret.rs`) — extracts the revealed `s_a`/`s_b` from a settling
  Solana instruction and verifies it against the commitment (`s*B == A`), i.e. confirms it
  is the Monero key share that unlocks the XMZ side.

## Honest gaps / risks (before this is a live trade)
1. **The real `LegDriver` is not wired yet** — the coordinator, driver interface and secret
   binding are built + tested with a mock, but the concrete driver (`SolSwapClient` calls on
   the Solana side, the eigenwallet 2-of-2 lock / sweep / reclaim on the XMZ side) is the
   next build, and it is **build-gated** by the engine's `monero-sys` dependency.
2. **Security review required.** Removing the adaptor/DLEQ layer is a real simplification,
   but the binding (`claim_commitment = S_a`, `refund_commitment = S_b`) and the timelock
   ordering must be audited — a mistake strands or leaks funds. Do not ship unaudited.
3. **Who runs the Solana relayer / ASB** and the exact confirmation targets are owner
   decisions, like the BTC/XMR legs.
