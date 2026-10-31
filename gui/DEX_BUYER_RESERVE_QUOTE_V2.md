# Buyer-funded exact-net quote v2: implementation contract

Status: proposed wire contract, not supported by the current executors.
Current v1 validators, signatures, reservations and recorded quotes remain unchanged.
Do not admit this draft through a v1 `VerifiedQuote` or silently upgrade old records.

## Signed terms

Use a new schema, commitment domain and role-separated signature domains.
Retain the existing identity, chain, principal, destination, expiry, price-policy
and timeout bindings. Both pinned parties must sign the complete v2 payload.
Unknown fields and noncanonical atomic amounts remain errors.

The v2 profile needs these explicit semantics:

| Term | Meaning |
| --- | --- |
| `xmzAtoms` | Exact purchased principal and seller lock amount; price denominator unchanged |
| `xmzReserveAtoms` | Exact separate buyer top-up amount, not part of the price or seller lock |
| `xmzTopUpFeeMaxAtoms` | Maximum fee of the buyer's reserve-funding transaction |
| `xmzClaimFeeMaxAtoms` | Maximum fee of the final two-source claim transaction |
| `xmzBuyerDebitMaxAtoms` | Maximum temporary debit from the buyer wallet for reserve funding |
| `xmzReserveReturn` | Buyer-approved return address, distinct from the seller's refund address |
| `btcRedeemConfirmations` | Pinned depth required before reserve funding; not absolute finality |
| `appFeeXmzAtoms` | Exactly zero in the currently approved initial-release scope |
| `reserveFundingPolicy` | Versioned delayed-top-up policy, never funding into unresolved shared control |

These names are a draft until both codecs and their shared vectors are implemented.
Do not overload v1 `xmzNativeFeeMaxAtoms` with a new aggregate meaning.
Cancellation policy remains unresolved; no v2 live admission may assume who funds
the seller's recovery. Signing a quote is not itself a top-up dispatch capability.

## Checked accounting

All operations use bounded integer atoms and reject overflow before any funding.

```
reserveAtoms > claimFeeMaxAtoms > 0
topUpFeeMaxAtoms > 0
buyerDebitMaxAtoms = reserveAtoms + topUpFeeMaxAtoms
actualBuyerDebit = reserveAtoms + actualTopUpFee
actualTopUpFee <= topUpFeeMaxAtoms
actualClaimFee <= claimFeeMaxAtoms
reserveReturnAtoms = reserveAtoms - actualClaimFee
sellerLockAtoms + reserveAtoms = xmzAtoms + reserveReturnAtoms + actualClaimFee
sellerLockAtoms = xmzAtoms
```

The temporary debit cap is not the eventual network-fee cost: unused reserve
returns to the buyer. The eventual two-transaction fee ceiling is the checked sum
of the top-up and claim fee ceilings. Both transactions use the same XMZ chain;
they are separate fee-bearing operations, not different fee markets.
The buyer must already have sufficient unlocked reserve plus top-up fee before
Bitcoin commitment where feasible. Zero-XMZ buyer onboarding remains an open
product requirement; never silently deduct fees from the promised principal.

## Required consumers and order

1. Implement distinct Qt/Rust v2 validation types and shared canonical/signature
   vectors. Preserve all v1 vectors and historical recovery behavior.
2. Extend trusted deployment policy and GUI approval to display principal,
   both fee caps, maximum temporary debit, reserve return and funding timing.
   Pin policy independently; peer-supplied limits do not define acceptable limits.
3. Bind the complete commitment into authenticated negotiation and durable
   execution state. A v2 quote cannot enter an executor that ignores its reserve.
4. Add a separate durable top-up dispatch intent after observed Bitcoin outcome
   and recovered-address verification. Existing patches 69–72 journal the final
   claim; they do not implement this buyer-wallet top-up dispatch journal.
5. Import the exact confirmed top-up, prepare from only the approved sources and
   bind the resulting transaction to encrypted recovery storage before relay.
6. Run a real two-party full-proceeds fixture using negotiated state, not the
   separately minted construction wallets. Include insufficient funds, changed
   fee conditions, lost replies, restart, cancellation and reorg cases.

Source inspection: Qt `NativeTradeQuote` and Rust `native_trade_quote.rs` plus
`native_quote_signatures.rs` currently enforce the v1 domain and field set.
Claude review: `build/claude-buyer-reserve-quote-review.log`.
The review informed this plan but is not implementation or independent audit
evidence. Recovery unlocking and cancellation policy are still pending decisions.
