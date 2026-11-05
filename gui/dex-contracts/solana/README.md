# Monzero XMZ ↔ Solana atomic swap

The Solana side of the DEX. Solana is neither EVM nor UTXO, so it needs its own on-chain
program (Rust/BPF) rather than `XmzSwap.sol`. This mirrors the proven EVM/BTC design —
lock → set_ready → claim / refund, each side revealing an **ed25519** secret at the right
time, bounded by two timelocks — and is non-custodial (PDA escrow, no admin/upgrade in the
logic). Design rationale: [`../DEX_SOLANA_DESIGN.md`](../DEX_SOLANA_DESIGN.md).

## Layout
| dir | what | tested |
|-----|------|--------|
| `swap-logic/` | funds-critical core: stage machine + timelock rules + ed25519 commit reference | host unit tests (7) |
| `program/` | the on-chain program: PDA escrow, **native SOL + SPL token** lock/claim/refund, syscall secret verify | builds to BPF |
| `program-tests/` | LiteSVM integration tests: runs the compiled `.so` on-chain | 8 passing (5 native + 3 SPL) |
| `../connectors/SolSwapClient.js` | wallet-agnostic client (native + SPL) | 9 cross-language tests |

## Why ed25519 + the curve25519 syscall
A secret `s` is an ed25519 scalar; its commitment is `A = s·B` — the **same group as the
XMZ side**, so no cross-curve DLEQ proof (unlike EVM's secp256k1 path). On-chain the program
verifies `s·B == A` with Solana's **curve25519 syscall** (`multiply_edwards`), not in-BPF
dalek — dalek's precomputed basepoint tables overflow the 4 KB BPF stack. The host logic
keeps a dalek reference (`commit`/`verify_secret`) and a test asserts the two agree. Measured
cost of a claim (with the syscall verify): **3988 compute units** — negligible.

## Build & test
```
./build-and-test.sh
```
Needs the Anza toolchain (`cargo-build-sbf`) and a Rust toolchain. Normally `cargo-build-sbf`
uses `rustup`; if `rustup` is absent the script shims it onto the platform-tools rust. The
program is built `--arch v1` to match the pinned LiteSVM test harness; a mainnet deploy can
use the default `v3`.

Test coverage (`program-tests/tests/swap.rs`):
- `happy_path_claim` — lock → set_ready → claim pays the claimer; escrow closed; prints CU.
- `wrong_secret_rejected` — a non-matching claim secret is refused.
- `claim_too_early_then_allowed_after_t1` — timelock: no claim before timeout1 (unless Ready).
- `refund_after_timeout2` — no refund while Ready before timeout2; allowed after.
- `early_cancel_while_pending` — owner can cancel an un-readied lock; wrong refund secret refused.
- `spl_happy_path` / `spl_wrong_secret_rejected` / `spl_refund_after_timeout2` — the same flow
  over an SPL mint: a PDA-owned token vault, `transfer_checked` in/out, vault+state closed on
  settle. SPL claim measured at **18111 CU**.

## SPL tokens (USDC/USDT)
`NewSpl` escrows tokens into a **vault token account** (PDA `["vault", swap_pda]`) whose
authority is the swap PDA; `ClaimSpl`/`RefundSpl` move the vault out with `transfer_checked`
(signed by the swap PDA) and close both the vault and the state. Classic SPL token program
only (USDC/USDT are classic mints); `set_ready` is shared with the native path.

## Status & gates
- **Native SOL and SPL tokens** implemented + tested.
- **Relayed (gasless) claim** (`claim_relayed`) is designed but not yet implemented here.
- **Not audited.** A Solana program is a new audit surface and must be in the community
  pentest before any mainnet deploy, exactly like the EVM contract.
- The cross-chain **secret binding to the XMZ side** (so revealing `s` on Solana is what the
  XMZ 2-of-2 needs) is engine-orchestration work still to come, alongside the app/GUI wiring.
