# XMZ settlement-fee enforcement: unresolved protocol requirement

Status: design blocker for live settlement, not a deployed vulnerability.
All execution remains disabled. The required outcome remains success-only XMZ
fees for trades through this protocol, including acquisition without prior XMZ.

## Reference and inference

[Atomic Swaps between Bitcoin and Monero, section 3.2](https://arxiv.org/html/2101.12332)
describes Bob obtaining both secret spend-key shares after Alice redeems Bitcoin.
Bob then controls the shared Monero output. The refund path instead releases
Bob's share to Alice. Maker/taker offer labels do not themselves establish these
funding roles.

Our inference for a direct XMZ adaptation: once the buyer has full spending
authority, a signed quote promising a fee cannot restrict their choice of outputs.
The buyer could spend to a personal address without the fee. Rejecting that spend
in the DEX's local accounting would not undo the already completed Bitcoin payment
or remove the buyer's XMZ spending authority. This inference is not a tested
Monzero exploit or an independent protocol review.

An [executable abstract model](tools/dex-protocol-model/README.md) enumerates the
honest and fee-free outcomes under that full-spend-authority assumption. Its tests
expect the counterexample; they are not evidence that fee enforcement works.

## Current code boundary

`MonzeroSuccessTransactionEvidence` checks the supplied sender-wallet and block
evidence for delivery plus fee outputs. `SettlementJournal` additionally checks
both legs before recording protocol revenue. These checks can establish that an
observed payment meets policy. They cannot prove that all possible successful
spends must meet policy. Its `feeEarned` field is an observation, not a guarantee
that a modified counterparty cannot bypass payment.

Neither certificate pinning, quote signatures, peer acknowledgements, a hidden
GUI button nor a separately prepaid XMZ fee resolves this cryptographic gap.
Prepayment also violates the required no-protocol-fee-on-refund outcome unless a
secure conditional refund construction is proved.

## Required construction and tests

The [settlement review brief](DEX_SETTLEMENT_REVIEW_BRIEF.md) turns this gap into
role definitions, proof obligations and an executable acceptance package. Existing
transport and real-node tests do not substitute for that protocol work.

The [local signing compatibility checkpoint](DEX_XMZ_SIGNING_FEASIBILITY.md)
maps the available CLSAG/multisig interfaces and the missing adaptor primitives.

Before adding signing, specify which participant controls each key and each
transaction, then demonstrate a construction that delivers XMZ and its fee without
granting a fee-free alternate spend. Do not assume a presigned transaction restricts
someone who later learns the complete spend key. Any proposed conditional signing
scheme must also prove recovery, malicious-party and collusion behavior without
introducing undisclosed custody.

Required adversarial cases include a buyer modifying destinations, withholding the
success spend after Bitcoin redemption, substituting transaction bytes, replaying
an old fee proof, disconnecting at each secret-release boundary, and confirmation
reorgs. Verify success implies delivery plus fee, and unsuccessful/refunded swaps
cannot collect that fee. A zero-XMZ buyer must still be able to acquire XMZ.

External/external pairs retain the additional multi-chain atomicity problem.
Sequential routes must disclose partial-completion risk and cannot be called one
atomic swap. No consensus change or custodial workaround is authorized by this
document. The mandatory-fee objective is not being relaxed.
