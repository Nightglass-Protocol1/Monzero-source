# DEX adversarial review — 2026-09-12

## Review identity and verdict

Performed by the same AI assistant involved in implementation. **Not independent,
not an external audit, not a cryptographic proof, and not release certification.**
No outside reviewer was contacted or paid. The user's request for an independent
review remains unfulfilled by this document; this is an actionable self-review
and handoff for that reviewer.

Baseline: GUI `8ea4c5c63641e4ed9af253249b8b516b8e6e663e` plus local design/model
changes; core baseline `58f10f796` from the design assessment. Targeted source
review, not a comprehensive audit of the core, dependencies or infrastructure.

**Real-value trading: no-go.** Public participation can test implemented behavior,
but cannot exercise missing settlement primitives. A no-value research preview
is a useful next stage, not a substitute for the full DEX objective. Current
loopback services are not yet a deployed public testing environment.

## Findings, prioritized by impact if live execution were enabled

### R1 — Release blocker: no fee-enforced settlement construction

Evidence: `DEX_FEE_ENFORCEMENT.md`, `tools/dex-protocol-model/fee_release_model.py`,
`src/dex/DexEngine.cpp:220`.

Full XMZ spending authority permits alternative outputs omitting the protocol fee.
Signed quotes and a successful-payment observer cannot restrict that authority.
The abstract model reproduces this inference; it is not an exploit of a deployed
DEX. The activation gate correctly remains false.

Close with: an exact construction binding success to delivery and fee, malicious
alternative-spend rejection under actual consensus, and two-party recovery tests.
Neither lowering a GUI gate nor relying on cooperative clients closes this.

### R2 — Release blocker: proposed replacement is hypothetical

Evidence: `DEX_CONDITIONAL_TRANSCRIPT.md`, `DEX_CONSENSUS_OPTIONS.md`, and
`src/dex/CryptoVerifierProtocol.cpp` (`canonicalRequest`).

The helper accepts ECDSA-adaptor verification and cross-curve DLEQ verification;
that is not XMZ adaptor signing/adaptation/extraction or a conditional XMZ output.
The new transcript requires unimplemented consensus behavior and undefined
confidential-destination/amount proofs. Public users cannot test a missing branch.

Close with: a reviewed concrete specification, separate authorization if consensus
changes are chosen, isolated implementation, negative vectors and complete
two-party success/refund execution. Do not present paper-level primitives as
compatible, integrated code.

### R3 — High prospective integration risk: wallet metadata is not a peer proof

Evidence: `src/dex/MonzeroSuccessTransactionEvidence.cpp:61` onward and its header.

The validator checks `transfer.destinations`, amounts and confirmations supplied
as JSON, and transaction inclusion in a supplied block response. It does not
derive confidential destinations or amounts from raw transaction cryptography.
An attacker controlling those inputs can invent matching metadata. An evidence
commitment hashes the claim; it does not authenticate its source.

This is not a demonstrated remote attack on the current disabled implementation.
It is a trust boundary: use a trusted local wallet/node observation pipeline, or
specify and verify an appropriate transaction payment proof and chain linkage.
Never promote peer-supplied JSON to payment authority. The header's overly broad
“Proves” wording was corrected in this review; no behavior was changed.

Close with: documented authenticated provenance at every caller, rejection tests
for forged peer evidence, and an independently checkable payment-proof design
where one party must establish payment to another without trusting its wallet.

### R4 — High prospective signing risk: explicit funding roles missing in V6

Evidence: `src/dex/SettlementTerms.h:9` and
`src/dex/OfferFillReservationStore.cpp:452`.

Terms bind maker/taker keys but do not explicitly identify funders or fee payer.
Fill matching permits either maker/taker ordering. A future signer must not derive
funding authority from GUI orientation or ambiguous pay/receive labels.

Close with: a new signing domain and role-bound format, key-control validation,
both trade directions and maker/taker permutations tested. Preserve V6 history
for read-only recovery; do not reinterpret existing signatures in place.

### R5 — Unresolved security assumptions: timing and availability

Evidence: the conditional transcript and 17 abstract-model tests.

Witness disclosure, inclusion and stable settlement are distinct events. The
role-aware model retains both principal-loss directions under unrestricted delay.
The deadline tests establish only conditional arithmetic under assumed inclusion
bounds. Optional public finalization grants authority; it does not ensure action
or inclusion. Reorganization behavior and real chain-clock mapping are not modeled.

Important qualification: these traces do NOT prove all HTLC-style swaps unsafe.
They include late disclosure, withholding and unlimited-delay schedules; some
violate a correctly specified honest-participant policy. Define honest-party
safety, liveness and fee entitlement separately before making impossibility claims.
An adversary gifting its own funds is also not equivalent to stealing honest funds.

Close with: an explicit adversary/liveness model, safe disclosure and monitoring
rules, abort cases, native fee budgets, reorg assumptions, and adversarial tests
against real transactions. Native network fees remain separate from protocol fees.

## Verification performed and limits

- Reran all 17 Python model tests: passed. Several deliberately expect failures
  of the candidate designs; a green test suite is not a green release decision.
- Reran `dex_settlement`, `dex_monzero_success_evidence` and
  `dex_crypto_verifier_protocol`: 3/3 passed.
- Prior full local build and 42-suite results are recorded in
  `DEX_LOCAL_VERIFICATION_20260912.md`; not rerun in full for this review.
- Reviewed loopback listeners in `src/dex/tools/relay_service.cpp:90` and
  `src/dex/tools/private_quote_service.cpp:89`. They do not establish public
  exposure, public test readiness or VM deployment.
- No penetration test of live infrastructure, independent vector reproduction,
  signing-nonce audit, formal proof or exhaustive source audit was performed.

## Practical public-testing route

1. Publish a review package/source snapshot with this non-independent disclosure,
   known blockers and reproducible tests. No public upload was performed here.
2. Prepare a separate no-value environment: fresh disposable keys, isolated chain
   IDs/data, test assets only, no production wallets, no mainnet signing/broadcast.
   Current regtest/fakechain tests are local fixtures, not public-network services.
3. Initially test offers, quotes, restart recovery, malformed traffic and evidence
   rejection. Label settlement unavailable; do not simulate trades as completed
   blockchain payments. Keep RPC/wallet ports private and scope authorized load
   tests so they cannot harm production or third-party infrastructure.
4. Before inviting users, verify the actual deployed network isolation, reset
   procedure, artifact provenance and reporting channel. The owner must specify
   bounty rewards and scope; no payout promise or legal safe-harbor text is assumed.
5. Add actual no-value cross-chain settlement only when the missing construction
   exists. Require independent protocol review before real-value activation.

External/external pairs and their additional XMZ fee leg are still in scope for
the final product, but are not implemented or validated by a BTC/XMZ prototype.

## External primary references

- [BIP 199](https://github.com/bitcoin/bips/blob/master/bip-0199.mediawiki): distinguishes
  preimage/signature authorization and timed refund; explicitly discusses witness
  disclosure without confirmation. Its status is Closed, not an XMZ specification.
- [Atomic Swaps between Bitcoin and Monero](https://arxiv.org/html/2101.12332):
  describes the shared-spend-key construction underlying R1's fee-bypass inference.
  It does not claim to implement this project's mandatory XMZ fee policy.
