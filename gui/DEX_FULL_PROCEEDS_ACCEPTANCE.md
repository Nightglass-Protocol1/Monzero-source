# Full-proceeds settlement: implementation contract

Status: source-reviewed implementation plan, not implemented acceptance or release approval.
Reviewed 2026-09-19 against the engine's 63-patch baseline.
Claude review was attempted but unavailable because of its session limit.

An exact-net construction experiment has subsequently been added in patch 64.
`preview_exact_net` keeps the requested recipient amount unchanged and deducts the native fee exclusively from a second, explicitly supplied reserve-return destination.
It verifies all-and-only lock inputs and exact conservation of principal, reserve return and fee.
It exports no metadata or relay-capable object.
The reserve must exceed the fee ceiling, and all three addresses must be distinct in this experiment.
This restriction does not yet handle refund cases where principal and reserve belong at the same address.
The funded fixture invokes this preview using a test-only reserve destination; the actual relay remains the existing partial-payment path.
This is not evidence of funded exact-net quotes, completed reserve return, or production settlement.

## What the existing code actually establishes

- `native_funding_rpc/full_proceeds.rs::preview` constructs without relay or exported metadata.
  It checks that all and only the available intended lock inputs are selected and that recipient amount plus network fee equals the lock principal.
  It does not return a transaction that can be journaled or broadcast.
- `relay.rs::prepare_from_lock` produces private prepared metadata for a fixed recipient amount.
  Its input check requires sufficient value from the intended lock, not consumption of every lock output.
- `spend_journal.rs::reserve` persists `amountAtoms` before preparation.
  `record_prepared` requires that exact recipient amount to match.
- `spend_recovery.rs` reconciles the fixed intent amount against the observed transaction and destination.
  Changing the meaning of that stored amount would break recovery accounting.
- `SpendRequest::check_recorded_recovery_limits` requires payment plus the approved maximum network fee to fit inside the signed principal.
  This is explicitly a partial-recovery bound, not a full-settlement policy.
- `test_wallet_recovery.py` currently delivers 90% of the principal and checks a positive residual.
  Its passing result must not be described as full-proceeds settlement.

## Confirmed commercial decision

The user selected exact-net delivery on 2026-09-19: a quote for 100 XMZ must deliver exactly 100 XMZ, with the network fee funded separately.
Do not reinterpret existing signed quotes or v1 journal records.
Gross-lock fee deduction is not the selected production behavior.
The existing gross-lock preview may remain research evidence, but making it executable is not the next product milestone.

For gross-lock accounting with zero application fee:

`locked atoms = recipient atoms + actual native network fee atoms`

For the selected exact-net accounting, separately specify the funding reserve, its owner and the destination of any unused reserve.
Do not simply increase the lock without binding those obligations into the signed terms.
Application fees remain a separate, unfinished requirement; current signed quotes permit zero application fee only.

The next implementation design must bind quoted net principal, separately funded fee budget and reserve-return destination.
It must account for both success and cancellation fees, including the cost of returning reserve change.
The user subsequently selected the XMZ buyer as reserve funder and recipient of unused reserve.
No live reserve address or transaction is authorized by that choice alone.

### Shared-key ownership constraint

Do not place a buyer-owned reserve into the shared lock on the assumption that signed terms guarantee its return.
`bob::State5::xmr_keys` combines both key shares on success; Alice's `extract_monero_private_key_from_refund` recovers spending authority on refund.
These keys are not restricted to the honest client's selected destinations.
Consequently, a shared reserve would become spendable by the seller on cancellation.
Signing an obligation to return it does not remove that alternate spend.
The abstract tests in `tools/dex-protocol-model/test_buyer_reserve.py` preserve this counterexample; they are not an on-chain exploit or independent audit.

The next candidate keeps reserve outputs under a distinct buyer-only key.
Success must combine the recovered swap input and buyer reserve input while delivering the exact purchased principal and returning unused buyer reserve.
Current `prepare_from_lock` intentionally rejects unrelated inputs, and the wallet bridge restores one wallet; neither supplies this multi-key construction.
Do not relax that input check globally to make the experiment pass.
Define explicit approved input identities and prove ownership for the new path.
Cancellation leaves the buyer-only reserve inaccessible to the seller, so funding the seller's recovery fee needs an explicit policy or additional protocol.
This candidate is not yet implemented or verified and does not solve first-time buyers having no XMZ reserve.

The subsequent [delayed top-up candidate](DEX_BUYER_RESERVE_CANDIDATE.md) explores avoiding multi-key signing by moving the buyer reserve only after confirmed Bitcoin redemption and swap-key recovery.
It adds another native transaction, separate budget/journaling obligations and explicit reorg assumptions; it is not yet implemented.

## Gross-lock research sequence (superseded for production)

The following previously identified sequence describes the existing gross-lock experiment only.
It is retained to distinguish that experiment from the selected exact-net product requirement.
Do not implement it as a substitute for exact-net settlement.

1. Separate transaction accounting validation from preview-only artifact restrictions.
   Preserve the preview's prohibition on exported signing material.
   Add an explicit gross-lock preparation path that keeps metadata private and never relays during construction.
   Require positive recipient value, bounded positive fee, checked arithmetic, and exact approved input-set consumption.
2. Introduce an explicit full-proceeds intent format rather than overloading v1 `amountAtoms`.
   Reserve the same chain/wallet/lock identity before construction so partial and full modes cannot independently spend one lock.
   Persist gross principal, fee ceiling, destination and accounting mode.
   Persist the exact prepared transaction identity, actual recipient amount and actual fee before dispatch.
   Reject torn, inconsistent or unknown records; never overwrite them to enable a retry.
3. Extend recovery decoding and reconciliation for that format.
   Verify gross equals recipient plus fee both when loading records and when comparing chain evidence.
   Keep old fixed-recipient journals readable with their original semantics.
   Missing receipts or RPC errors must not authorize reconstruction or rebroadcast.
4. Exercise this through recovered keys from both real two-party funded fixture outcomes.
   Use the payout destination for redemption and the refund destination for cancellation.
   Preserve wallet ownership and best-effort close behavior, including errors after successful relay.
5. Integrate the selected commercial policy into signed terms, consent and whole-application restart recovery.
   Only then connect this path to GUI execution approval.

## Required acceptance evidence

| Case | Required observation |
| --- | --- |
| Redemption and refund | Confirmed destination amount plus actual network fee equals the entire intended lock; zero remaining lock balance |
| Multiple lock outputs | Every intended available input is consumed; omission is rejected |
| Unrelated wallet output | Remains unspent; selecting it causes preparation rejection |
| Fee boundaries | Ceiling violation, zero recipient and overflow fail before dispatch |
| Wrong context | Wrong genesis, wallet, lock or destination fails before dispatch |
| Restart before dispatch | Retained intent prevents an independent competing spend; no automatic retry |
| Accepted transaction with lost reply | Fresh reconciliation finds the exact transaction and amount without a second payment |
| Lost receipt | Prepared identity and on-chain evidence recover the outcome without treating receipt absence as failure to broadcast |
| Persistence failure | Failed durable preparation prevents dispatch; receipt failure does not erase an already successful relay |
| Legacy compatibility | Existing partial-payment journal and signed-quote rejection tests still pass |

Run maintained patch reconstruction, relevant unit tests, both funded outcomes and all-feature/all-target compilation.
Record precisely which crash boundaries were actually exercised, rather than inferring application recovery from process-level tests.
No production wallet, website package or live-trading gate should change merely because this experimental fixture passes.
