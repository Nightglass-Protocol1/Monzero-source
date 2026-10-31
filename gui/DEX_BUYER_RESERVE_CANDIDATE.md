# Buyer-funded claim reserve: delayed top-up candidate

Status: implementation design, not an executable protocol or security approval.
The user requires an exact quoted XMZ payout and buyer-funded native fees, with unused reserve returned to the buyer.
Application fees remain outside this design.

The proposed signed-term integration is specified in
`DEX_BUYER_RESERVE_QUOTE_V2.md`; current executors still accept v1 only.
The patch descriptions below are historical. Patch 72 and subsequent fixtures
now cover isolated exact-net relay, reserve return, lost replies and read-only
reconciliation. They do not yet prove negotiated buyer top-up dispatch.

Patch 65 supplies `preview_exact_net_with_top_up`, a non-broadcasting construction
entry point that binds exactly two source transactions and their individual totals.
The original lock must equal the principal and the top-up must equal the reserve;
selected key images must include all and only those approved outputs.
Unit tests cover source substitution, unavailable outputs and extra selected inputs.
Patch 66 adds a separate real-fakechain two-source construction fixture.
Both source payments come from a disposable mint wallet, not authenticated buyer/seller funding.
It checks positive construction and malformed-context rejection without balance or pool changes.
This fixture does not prove protocol top-up timing, reserve funder identity or actual reserve return.
Patch 67 adds engine-owned preparation with private zeroizing metadata and
public transaction/accounting fields. It has no metadata accessor, serialization
or relay method. This is a prerequisite for a durable journal, not one itself.
Patch 69 adds a write-once two-source public intent/preparation journal sharing
the old spending mode's original-lock reservation identity. It stores no signed
metadata. Patch 70 reloads historical public preparation against caller-retained
terms. Patch 71 adds trusted local wallet/daemon reconciliation of both destinations
and confirmations, without relay authority. Positive confirmation has synthetic
unit coverage; the real fixture currently covers rejection of unbroadcast preparation.
No top-up dispatch, source-funder proof or Bitcoin outcome authorization is
provided by these components.

## Avoid sharing the buyer's reserve before outcome resolution

The original shared-key swap releases full spending authority to the buyer on redemption or the seller on refund.
A buyer reserve deposited into that lock before resolution is therefore exposed to the seller on cancellation.
Local quote signatures cannot restrict the recovered key's possible destinations.
See `DEX_FULL_PROCEEDS_ACCEPTANCE.md` and the abstract buyer-reserve tests.

A candidate that avoids multi-key transaction signing is a delayed top-up:

1. The buyer retains its fee reserve in its own wallet throughout negotiation and funding.
2. Observe the exact Bitcoin redemption at the configured confirmation depth and recover the swap's XMZ key.
   Bind the key-derived address to the retained swap state and recheck both chains before any top-up dispatch.
3. Journal a buyer-approved transfer from the buyer wallet to that recovered swap address.
   This is a new payment with its own fee and durable transaction identity, not a repeat of the swap lock.
4. Confirm and import the top-up into the recovered wallet alongside the original lock.
5. Prepare a transaction using exactly the approved lock and top-up inputs.
   Deliver the quoted XMZ principal unchanged and return unused top-up value to the buyer's approved destination.
6. Persist exact prepared identity and accounting before dispatch; reconcile uncertain outcomes without reconstructing a second payment.

This is only a candidate.
It relies on Bitcoin outcome stability under the chosen confirmation/reorg assumptions, and adds native transaction cost and confirmation latency.
It does not guarantee recovery under arbitrary reorgs, compromised buyer keys or lost recovery records.
Do not describe finite confirmations as absolute finality.

## Existing components and required changes

`NativeWalletSession::import_transaction` accepts an explicit transaction hash in an exclusively owned wallet session.
`NativeTransferRelay` already separates construction from dispatch and reports unknown broadcast outcomes.
These are reuse candidates, not proof that the new flow is complete.

`prepare_from_lock` accepts only one approved source transaction.
Keep that behavior intact for existing callers.
A new exact-input path must bind both lock and top-up transaction identities, available outputs, amounts and complete selected key-image set.
It must reject unrelated donations, extra reserve payments and missing inputs rather than treating the whole wallet balance as authorized.
The wallet chooses its inputs before the engine validates their exact set.
Unrelated available outputs can therefore cause construction to fail if selected.
A dedicated wallet reduces this availability problem but does not prevent third-party donations.
Never relax input binding or silently sweep/freeze unrelated funds to compensate.

The current spend journal identifies a source by genesis, wallet and lock transaction.
Top-up dispatch needs a separate immutable intent linked to the same trade and a buyer-approved total budget.
The final claim needs a versioned record binding both source transactions, net principal, actual fee, reserve return and destination.
The top-up and claim cannot share an idempotency key or silently replace one another after a crash.

The signed quote must distinguish purchased principal from buyer fees.
Neither the price-band denominator nor the seller's negotiated lock amount should be increased by buyer reserve value.
Existing v1 quotes and journals retain their existing interpretation.

## Accounting and acceptance

With zero application fee and no other selected inputs:

`lock principal + top-up amount = exact recipient principal + reserve return + claim fee`

`buyer wallet debit = top-up amount + top-up transaction fee`

The total buyer fee budget must cover both transaction fees, not only the claim fee.
Any funding of the buyer's wallet is separate and must not be hidden in this equation.
Reject an insufficient reserve before Bitcoin commitment where possible; changing fee conditions after commitment requires a documented recovery path and explicit additional approval, not an unlimited automatic top-up.

Required real-node tests include both imported sources, unchanged recipient principal, confirmed reserve return, rejection of unrelated inputs, duplicate/lost-ack top-ups, crashes before and after each dispatch, unavailable inputs and reorg observations.
Test the final buyer balance change as well as transaction fields.
The existing two-output preview is construction evidence only and does not cover these cases.

## Remaining policy and onboarding questions

Cancellation must not automatically spend buyer reserve or expose its key.
The proposed policy is each party pays its own recovery transaction fee; user confirmation is pending.
If the buyer must pay the seller's recovery fee, an additional protected mechanism is needed.

A buyer with no XMZ cannot fund this top-up.
This design therefore does not yet meet first-time acquisition needs by itself.
A separately disclosed bootstrap route must be designed; no undisclosed sponsor, custody, credit or fee deduction from the purchased amount is assumed.

No production wallets or transactions were used in this source review.
Claude subsequently completed a read-only review of the prepared transaction work.
See `DEX_PREPARED_EXACT_NET_REVIEW.md` for verified findings, corrections and remaining tests.
No independent audit sign-off is claimed.
