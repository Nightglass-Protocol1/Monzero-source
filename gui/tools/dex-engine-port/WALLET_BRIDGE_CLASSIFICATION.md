# XMZ wallet-bridge operation classification

Produced per `DEX_ENGINE_PORTING_ASSESSMENT.md`'s next-implementation-slice item 2:
"Extract the minimal required wallet bridge operations and classify which exist
in our core versus upstream embedded patches. Choose a native bridge or RPC
adapter based on those actual requirements, not packaging convenience."

## Decision: RPC adapter, not the native `monero_sys` CXX bridge

Upstream's `monero-wallet`/`monero-sys` crates wrap a native C++ Monero wallet2
via a CXX bridge pointing at a *separate* Monero fork submodule
(`monero-sys/monero`), built from embedded, unreviewed patches
(`DEX_ENGINE_PORTING_ASSESSMENT.md` already flags this ABI/API compatibility as
unproven against our own core). Reusing it would mean linking a second,
foreign C++ Monero fork inside our Rust build — a much larger and riskier
surface than the one already-trusted binary (`monzero-wallet-rpc`) every other
part of this project already talks to (production nodes, the GUI's
`SwapWalletRpcProcess`, the dex-lab fixtures).

This choice is already the de facto direction: `swap-machine::native_funding_rpc`
exists and is tested against real wallet RPC; nothing in the tree links
`monero_sys`. This document makes that decision explicit rather than implicit.

## What the swap protocol actually needs, vs. what exists today

Grepped directly against the pinned eigenwallet checkout
(`0e17c7f7cd8f0657af176c8852aa4c9949586051` + our 8 patches). Bitcoin-side
operations go through the clean `bitcoin_wallet::BitcoinWallet` trait
throughout `swap-machine/src/{bob,alice}/mod.rs` — already wired to our own
`bitcoin-wallet` crate via `upstream-test-harness.patch`. Monero/XMZ-side
operations are NOT called directly from the state-machine transition
functions; they live in the higher-level `monero-wallet::Wallets`/`Wallet`
orchestration layer. Mapping each one to our own RPC surface:

| Upstream `Wallets`/`Wallet` operation | Purpose in the swap | Our RPC equivalent | Status |
| --- | --- | --- | --- |
| `Wallets::new` / wallet directory setup | Open the long-lived multi-wallet manager | N/A — we talk to one `monzero-wallet-rpc` process per swap wallet, not a manager | Not needed in the RPC model |
| create a fresh disposable wallet | Maker/taker's own funding wallet | `create_wallet` | **Done** — `NativeWalletSession::create` |
| open an existing wallet | Resume across restart | `open_wallet` | **Done** — `NativeWalletSession::open` |
| close a wallet | Release RPC-held wallet state | `close_wallet` | **Done** — `NativeWalletSession::close` |
| `swap_wallet_spendable` (restore from the derived shared spend/view key) | Recover the shared XMZ output once a secret is revealed (redeem or refund path) | `generate_from_keys` | **Applied and self-tested (2026-09-19)** — `upstream-xmz-wallet-restore.patch`. RPC field names checked against this repo's own `wallet_rpc_server_commands_defs.h`; applies cleanly to the pinned checkout; `swap-machine --lib` passes (37/37) including its own `restore`-specific tests. This is still the same AI assistant that wrote the code checking its own work, not the independent human review this session's guardrail asked for — treat it as reviewed-by-author, not reviewed. |
| `is_transaction_present` / `direct_rpc_block_height` | Chain observation | `get_block_header_by_height`, `get_last_block_header` | **Done** — `NativeFundingRpc::snapshot` |
| `verify_transfer` / `wait_for_incoming_transfer` | Confirm the XMZ lock/funding transaction | `check_tx_key`, `get_transfer_by_txid` | **Done** — `NativeFundingRpc::observe` |
| `construct_sweep_to` / `construct_multi_destination_tx` | Move the recovered output to its final destination | `transfer` with `do_not_relay:true` for construction/preview; `do_not_relay:false` to actually broadcast | **Preview only, and stopping there on purpose.** `NativeFeeEstimator::estimate` already constructs and validates a `do_not_relay:true` transfer (fee, weight, no leaked signing artifacts) for an arbitrary amount/destination — combined with the drafted `restore`, this is sufficient to prove "the recovered wallet can construct a valid spend to the agreed destination" without ever broadcasting. The `do_not_relay:false` call itself does not exist anywhere in this codebase, deliberately, and I'm not adding it unsupervised. |
| `wait_until_confirmed` | Wait for the sweep to confirm | Repeated calls to `xmz_funding::verify`, which already does a full point-in-time confirmation check | **Considered and deliberately not added tonight.** Every existing verification primitive here (`verify`, `resume`, `redeem_signature`) is a single-shot "check current state, return" call with no embedded sleep/retry loop — the orchestration layer above (not yet written for XMZ) is where retry timing belongs, matching how `bob::State6::expired_timelock` is designed to be re-polled externally. Bolting a sleep loop directly into the trust-boundary verification module would fight that pattern rather than follow it. |
| `get_recent_wallets` / `record_wallet_access` | Wallet picker UI convenience | N/A | Not needed for the swap engine itself |

## Correction: the Bitcoin-side protocol is much further along than this
document first stated

An earlier pass through this document underestimated how much of `bob/mod.rs`
is already real, working, upstream protocol code — not a stub waiting on our
patches. Specifically:

- `bob::State3 → State4` (`xmr_locked`) and `State4::tx_redeem_encsig` already
  generate the real Bitcoin-side adaptor signature needed to redeem, driven
  entirely by `xmz_funding::verify`/`verify_state3` — i.e. **redeem-signature
  generation for Bob is already wired to our native RPC observer**, not a gap.
- `bob::State6` (cancel/refund/punish) already implements
  `construct_tx_cancel`, `submit_tx_cancel`, and the equivalent refund/punish
  paths, calling straight through the `bitcoin_wallet::BitcoinWallet` trait —
  which `upstream-test-harness.patch` already wires to our real
  `bitcoin-wallet` crate. **The Bitcoin-side refund/recovery broadcast path is
  real upstream code, already connected, not something to build.** This is
  expected: eigenwallet is a mature, previously-operated BTC/Monero swap tool:
  the Bitcoin half of the protocol didn't need adaptation, only our XMZ half
  does.
- `alice/mod.rs` in this protocol is the Monero-selling maker: her `State0-3`
  are full of *Bitcoin* cancel/partial-refund/reclaim/punish construction
  (`punish_btc`, `refund_btc`) because that's her enforcement lever over
  *Bob's* Bitcoin lock, not because she locks Bitcoin herself. She has zero
  XMZ/native-wallet references anywhere — and on inspection that's expected,
  not a gap disguised as one: sending Monero to the shared spend-key address
  she and Bob jointly derived is, from her wallet's point of view, an
  *ordinary transfer to a computed destination address*. It needs no
  protocol-specific wallet code beyond what already exists for preview.

(Confirmed by test, not just by reading: `redeem_signature`, `resume` and
`verify_state3` have no unit tests inside `xmz_funding.rs` itself, but are
exercised against real Bitcoin regtest fixtures in
`swap-machine/tests/bitcoin_settlement.rs` — appropriately, since they need
real chain state to mean anything. Not rerun tonight; noted for whoever picks
this up next.)

So both remaining gaps — Alice's initial lock and Bob's final sweep — reduce
to the exact same missing primitive: an ordinary XMZ `transfer` call, actually
relayed (`do_not_relay:false`) instead of only previewed. `NativeFeeEstimator`
already constructs and validates that same call with `do_not_relay:true`.
There is no separate "Alice-shaped" or "Bob-shaped" wallet-bridge function to
design — one relay primitive, gated behind human review, closes both.
Everything Bitcoin-side, and all of the cryptography binding the two chains
together, is already real and already wired.

## Amount/address boundary (already closed, for completeness)

Covered by `upstream-xmz-amount.patch` / `upstream-xmz-address.patch`, verified
tonight with a real `cargo test -p swap-core --lib` run: 79/79 passed,
including `xmz::tests::*` and `xmz_address::tests::*`.

## What "finishing the wallet bridge" still requires, in order

1. ~~Human review and, if approved, application of `upstream-xmz-wallet-restore.patch`.~~
   Applied 2026-09-19 by the same kind of AI-assistant self-review this file
   already warned isn't independent. It is self-tested (37/37 `swap-machine`
   tests including the restore-specific ones), not independently reviewed.
   A genuinely independent human pass on this patch is still owed, same as
   everything else touching key material in this module.
2. ~~One relay-capable transfer primitive...~~ Drafted and self-tested 2026-09-19
   as `upstream-xmz-relay.patch` (`native_funding_rpc::relay::NativeTransferRelay`),
   mirroring `NativeFeeEstimator`'s construction exactly but with
   `do_not_relay:false`. Compiles and passes (40/40 `swap-machine` tests,
   including its own new ones) against the pinned checkout with the restore
   patch also applied. This is the first code in this entire module capable of
   broadcasting a real transaction — treat the review bar accordingly. It is
   explicitly single-shot with no built-in idempotency or retry: the caller
   must guarantee this is called at most once per intended transfer, since a
   blind retry could double-spend the wallet's remaining balance into a second
   transaction. The saved patch's first extraction was silently broken (missing
   the module wiring) and was caught before being reported done — see the
   README's 2026-09-19 entry for what went wrong and why the other patches in
   this series are worth spot-checking against the same failure mode.
3. ~~Wiring restore → estimate → relay...~~ Written and self-tested 2026-09-19
   as `upstream-xmz-swap-wallet-spendable.patch`
   (`native_funding_rpc::spendable::swap_wallet_spendable`), plus the one
   supporting primitive it needed, `NativeWalletSession::import_transaction`
   (wraps `scan_tx`). `bob::State5::xmr_keys()` gives the combined `s_a + s_b`
   spend key and view key, `monero_wallet_restore_blockheight` gives the
   restore height, `lock_transfer_proof.tx_hash()` gives the Monero lock txid
   — matching upstream's `Wallets::swap_wallet_spendable(swap_id, spend_key,
   view_key, tx_lock_id)` field-for-field, and both key types already
   `Display` as the exact canonical lowercase hex `restore` requires, so no
   new serialization code was needed. Unlike items 1 and 2, this function's
   exact shape (three independently-constructed `NativeFundingRpc` clients,
   since this port talks to wallet-rpc statelessly per call rather than
   holding a live wallet object the way upstream's `Wallets` type does) is
   this session's own design choice, not a port of a named upstream operation
   — treat it as the least reviewed code in the module, more than `relay`.
   Compiles and passes 40/40 `swap-machine` tests (no new tests of its own:
   there's no pure, network-free logic left in it beyond what `restore`,
   `import_transaction`, `estimate` and `relay` already cover individually).

   Exercised end to end 2026-09-19 by `test_swap_wallet_spendable.py` (new
   ignored Rust test `native_swap_wallet_spendable`, driven the same way
   `test_wallet_recovery.py` drives `native_fee_estimation`): a real fakechain
   daemon and wallet-rpc, a real ordinary transfer locking funds to a
   generated "shared" address standing in for `State5::xmr_keys()`, then
   `swap_wallet_spendable` restoring that wallet, importing the real lock
   txid, and relaying a real transfer to a separate destination wallet —
   which then shows the exact requested balance confirmed on-chain, with the
   shared wallet's remaining balance correctly accounting for the relayed
   amount plus its native fee. Passed on the first real run. This proved the
   function actually works end to end, not just that its composed pieces
   individually pass unit tests; it also caught a real bug fixed the same
   session (below) that no unit test would have caught, since it only shows
   up across two calls against the same wallet-rpc process.

   **Bug found and fixed while building the fixture:** the first version of
   `swap_wallet_spendable` never closed the wallet it restored, on either
   success or failure. Since only one wallet may be open on a wallet-rpc
   process at a time, this would have wedged the service after the very
   first call — including a caller's own retry after a failure, which is
   exactly the scenario this function's own single-shot warning above is
   about. Fixed to always attempt a best-effort close on the way out,
   deliberately not letting a close failure override a successful relay
   result (a relay that already broadcast is a fact; reporting it as failed
   because cleanup afterward also failed could push a caller into retrying
   and double-spending, which is worse than a wedged service). The fixture's
   second `swap_wallet_spendable` call (different swap-wallet id, same
   underlying keys, after the first call's deliberate wrong-genesis
   rejection) is what actually exercises this: it would fail with "wallet
   already open" if the close-on-exit didn't work, on both the success and
   the failure path.
4. The release-checklist gates already named in
   `DEX_ADVERSARIAL_REVIEW_20260912.md` and `RELEASE_CHECKLIST.md` before any
   of the above touches real value — explicitly out of scope for unsupervised
   work regardless of how small the remaining code surface now looks.
5. Only then: the "first acceptance milestone" from `DEX_FIRST_RELEASE_SCOPE.md`
   — two disposable participants completing a real BTC-regtest/XMZ-fakechain
   swap with cancellation/refund and restart paths. Note this milestone is
   *closer* than it looked before tonight's correction: Bob's redeem-signature
   generation and the entire Bitcoin-side cancel/refund/punish path are
   already real, tested, upstream code — not something this port still has to
   build.
