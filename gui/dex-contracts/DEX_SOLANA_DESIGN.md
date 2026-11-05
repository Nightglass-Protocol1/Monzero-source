# XMZ ↔ Solana atomic swap — design (before code)

Solana is **neither EVM nor UTXO**, so it needs its own on-chain program (Rust/BPF),
not the `XmzSwap.sol` contract. This spec mirrors the proven EVM/BTC design (lock →
setReady → claim → refund, secret-reveal, timelocks) and flags the Solana-specific
parts. Funds-critical + a new audit surface, so review this before implementing.

## Why Solana is a good fit
- **ed25519-native.** The XMZ side already uses ed25519 scalars. On EVM we commit the
  secret as `secp256k1 s·G` + a cross-group DLEQ proof (`XmzSwap.sol`). On Solana the
  secret can be an **ed25519** value, so the commitment can live in the **same group as
  XMZ** — simpler binding, one fewer cross-curve proof.
- **SPL tokens** = native **USDC / USDT on Solana** (deep liquidity), same as our EVM
  stablecoin pairs.
- Fast + cheap finality (sub-second, ~$0.00025/tx) → no minimum-trade-size problem.

## Program model (mirrors XmzSwap)
A single stateless program; each swap is a **PDA escrow account** (program-derived from
the swap id), so there is no owner/admin/upgrade (set the upgrade authority to none, or
use a verifiable build) — same non-custodial guarantee as the EVM contract.

Instructions:
1. **lock(claim_commit, refund_commit, timeout1, timeout2, amount, nonce)** — the
   EVM-side-equivalent "owner" moves SOL/SPL into the escrow PDA; records stage PENDING.
2. **set_ready()** — owner confirms the XMZ lock (only before timeout1) → READY.
3. **claim(secret)** — claimer reveals the secret matching `claim_commit`; program
   verifies it, pays the escrow to the claimer, emits the secret in a log → COMPLETED.
4. **refund(secret)** — owner reveals the refund secret (early while PENDING, or after
   timeout2) → COMPLETED.
5. **claim_relayed(secret, fee, sig)** — optional gasless claim (claimer signs, a relayer
   submits and takes `fee`), same shape as `claimRelayer`.

Carry over every rule that made the EVM/BTC version safe: checks-effects-interactions
(close/settle the escrow before transferring), reject the wrong/other secret, enforce
`fee <= amount`, bind the relayer, and timelock ordering (cancel window before punish).

## Solana-specific parts to get right
- **Secret verification.** Commit = an ed25519 public key `A = s·B`; `claim(s)` checks
  `s·B == A`. Do the scalar-mult **in-program** (curve25519-dalek works in BPF) rather
  than the `ed25519` native program, so the secret isn't exposed in a separate ix before
  the claim lands. (The reveal still ends up in the tx — that's intended, it's how the
  counterparty learns it.)
- **Account model.** The escrow is a PDA holding lamports (native SOL) or an SPL token
  account owned by the PDA. Handle **rent** (fund the escrow rent-exempt; return rent to
  the creator on close) and **close** the account on settle so nothing is reusable.
- **Timelocks** via the `Clock` sysvar (unix timestamp), same two-timeout structure.
- **SPL quirks:** reject fee-on-transfer / frozen mints, require the exact amount landed
  (balance delta, like the EVM `FeeOnTransferToken` check), use `transfer_checked`.
- **Replay / id:** derive the PDA from `(owner, claimer, commitments, amount, nonce)` so
  an identical swap can't collide, mirroring the EVM `SwapExists` check.

## Cross-chain secret binding
Two options:
- **(A) ed25519 secret shared with the XMZ side** — cleanest; the same scalar unlocks
  both the Solana escrow and the XMZ 2-of-2. Needs the protocol worked out so revealing
  it on Solana is exactly what the XMZ side needs (and vice-versa).
- **(B) secp256k1 + DLEQ**, identical to the EVM path, reusing the engine's
  `CrossCurveDLEQ`. More code but already proven on EVM/XMZ.
Recommend prototyping (A) since Solana is ed25519-native, falling back to (B) if the
protocol binding is cleaner.

## Build & test plan (what I'd do next)
1. Install the Anza/Solana toolchain + `cargo-build-sbf`; use **LiteSVM/bankrun** for
   fast in-process tests (no local validator needed).
2. Implement the program (Anchor or native). Port the EVM test suite scenario-for-
   scenario: happy path, wrong/other secret, timeouts, relayed claim, SPL + USDC-style,
   reentrancy/close-safety, refund.
3. A `SolSwapClient` mirroring `connectors/XmzSwapClient.js` (web3.js), so the app/web
   talk to it the same way.
4. Wallet support: Phantom / Solflare / **Ledger (Solana app)** for signing + receiving.

## Status (built — see `solana/`)
Implemented and tested (native SOL):
- **`solana/swap-logic`** — stage machine + timelock rules + ed25519 commit reference; 7
  host unit tests.
- **`solana/program`** — the on-chain program (PDA escrow, new/set_ready/claim/refund,
  secret verified via the curve25519 **syscall**, not in-BPF dalek); builds to BPF.
- **`solana/program-tests`** — 5 LiteSVM tests run the compiled `.so` on-chain: happy-path
  claim (**3988 CU**), wrong-secret reject, claim timelock, ready-state refund reject +
  after-timeout2 refund, early-cancel-while-pending. Option (A), ed25519, confirmed working.
- **SPL tokens (USDC/USDT)** — `NewSpl/ClaimSpl/RefundSpl` with a PDA-owned token vault +
  `transfer_checked`; 3 more LiteSVM tests (SPL claim 18111 CU).
- **Gasless relayed claim** — `ClaimRelayed`: claimer signs `(prefix, swap, fee, relayer)`
  as an Ed25519 instruction, program introspects the instructions sysvar to confirm it,
  pays claimer `amount−fee` + relayer `fee`; 3 LiteSVM tests (incl. fee-tamper + missing-auth).
- **`connectors/SolSwapClient.js`** — wallet-agnostic client (native + SPL + relay); 12 tests
  assert it reproduces the program's PDA / vault / instruction bytes / commitment / relay
  message byte-for-byte.

Still to do: the engine orchestration that **binds the secret to the XMZ side**, the
app/GUI wiring, and a mainnet deploy.

## Gates
- New **audit surface**: the Solana program must be in the audit/pentest, same as the EVM
  contract — do not ship it to mainnet unaudited.
- Decision for the owner: ship Solana/stablecoin liquidity now, or after BTC/LTC/EVM are
  live? The SOL side is now at parity with the EVM side (built + tested, pending audit + GUI).
