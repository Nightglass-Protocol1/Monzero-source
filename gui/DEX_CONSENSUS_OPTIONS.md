# XMZ fee enforcement: consensus design options

Status: research proposal, 2026-09-12. User permission covers exploring designs,
not implementing, activating or deploying consensus changes. Live DEX execution
remains disabled. Baseline: core `58f10f796`, GUI `8ea4c5c6`.

## What a chain change would and would not solve

The present full-spend-key construction permits alternative fee-free XMZ spends.
A consensus-enforced spending restriction could close that *local* alternative.
It would not, by itself, prove delivery on Bitcoin, prevent premature secret
disclosure, or guarantee a fee-free refund after a failed cross-chain exchange.
The [settlement review brief](DEX_SETTLEMENT_REVIEW_BRIEF.md) remains binding.

The core declares historical script variants in
`src/cryptonote_basic/cryptonote_basic.h`; that is not an available script engine.
`check_output_types` in `src/cryptonote_basic/cryptonote_format_utils.cpp` enforces
key/tagged-key output variants by fork version. A new restricted output must have
explicit validation, storage and spending semantics, not just another C++ variant.

## Options screened

| Candidate | Potential benefit | Unresolved cost or limitation |
| --- | --- | --- |
| Transaction-bound signing, unchanged chain | Avoid releasing unrestricted authority; smallest consensus impact | Compatible adaptor construction, secret ordering and refunds remain unproved; current helper is insufficient |
| Narrow XMZ conditional output with committed success/refund destinations | Consensus can reject a success spend without the XMZ fee | New consensus rules, privacy analysis and a complete cross-chain protocol required |
| External-chain proof verification in XMZ consensus | Can make an XMZ condition depend on authenticated foreign-chain evidence | Header/finality verification, reorganizations, proof availability, consensus upgrades and bounded validation cost for every supported chain |
| Committee/oracle-authorized settlement | Delegates external-event judgement | Adds censorship, collusion and key-compromise trust; not the requested trustless construction |

These are design assessments, not implemented capabilities. For an example of
transaction-template restrictions, see the draft
[BIP 119 specification](https://github.com/bitcoin/bips/blob/master/bip-0119.mediawiki).
Its template hash commits to outputs and other transaction fields. It is neither
an XMZ implementation nor an assumption that Bitcoin has activated that proposal.

## Preferred research candidate: narrow conditional XMZ output

Explore this before a general-purpose VM or foreign-chain verifier. This is a
bounded research preference, not a decision to hard-fork.

An abstract locked output would bind a versioned contract identifier to:

- Chain/genesis identity, quote commitment, explicit funder and recipient roles.
- Success destinations and exact XMZ delivery/fee obligations.
- Refund destination, authorizing keys, and timeout/branch conditions.
- Native transaction fee budget and explicitly limited fee-bumping authority.
- A single-use spending identifier and bounded witness format.

Success must consume the restricted funds and satisfy both delivery and fee;
refund must return the entitled funds without collecting the protocol fee.
Ordinary spending keys must not bypass either branch. No administrative escape
key, arbitrary migration spend, or cooperative fee-free success branch may be
silently added. Fees on ordinary XMZ transfers are outside this proposal.

This is an abstract predicate, **not** a serialization or cryptographic design.
Confidential amounts and one-time addresses make “check the destination and fee”
nontrivial: specify either public contract data (with disclosed privacy loss) or
a sound proof linking confidential commitments to the intended payment. An output
hash alone does not prove that recipients can identify and spend their payments.
Restricted outputs must not become spendable through an ordinary ring input;
ring membership, anonymity-set separation, nullifiers/key images, and output
indexing all need explicit rules and adversarial review.

## Cross-chain counterexamples to resolve first

1. **Withheld XMZ success:** BTC is redeemed but the buyer never submits the
   fee-bearing XMZ transaction. Does another party have safe authority and data
   to complete it? Otherwise asset delivery/fee liveness is not established.
2. **Secret exposed before confirmation:** an XMZ success witness is visible in
   the mempool, enabling BTC redemption, but the XMZ transaction is censored or
   loses to refund. A common secret alone is not proof of successful delivery.
3. **Timeout race:** refund becomes valid while an opposite-chain success is
   pending. Specify both chains' deadlines, finality assumptions and monitoring
   requirements; one nominal wall-clock timeout is insufficient.
4. **Fee-free refund laundering:** both traders cooperate to label the XMZ leg a
   refund while paying externally. Consensus cannot identify arbitrary off-protocol
   trades. Define precisely which locked-contract outcomes count as protocol
   success, and test collusion without promising control over other exchanges.
5. **External/external pair:** a refundable third XMZ fee deposit can be refunded
   after the other two assets move unless its conditions bind those outcomes.
   Solving BTC/XMZ does not solve this three-leg construction.

The reference [Bitcoin/Monero swap paper](https://arxiv.org/html/2101.12332)
describes full shared-spend-key recovery in its basic construction. Our concern
about alternative XMZ outputs is an inference from that authority, not a claim
that the paper implements this new fee policy.

## Upgrade and security gates

A [role-bound candidate transcript](DEX_CONDITIONAL_TRANSCRIPT.md) explores
XMZ-first funding and BTC-first secret disclosure, with earlier BTC and later
XMZ refund opportunities. It explicitly retains unresolved liveness, fee and
confirmation risks; it is not an adopted protocol.

The [conditional-output timing model](tools/dex-protocol-model/README.md)
now provides an executable counterexample for the premature-secret/refund race
under its explicit deadline assumptions. It does not implement this candidate
or establish a safe choice of deadlines.

Before any implementation proposal, deliver a full two-party BTC/XMZ transcript,
knowledge/authority table at every message, and counterexample-search model
including aborted sessions, withholding, mempool disclosure and reorganizations.
Do not model “foreign payment succeeded” as an unexplained trusted Boolean.

Then specify consensus serialization, bounded verification costs, conservation
and inflation checks, privacy trade-offs, wallet recovery/scanning, database and
RPC changes, and behavior of old nodes. Treat this as requiring an explicit
network upgrade unless compatibility is demonstrated; do not borrow Bitcoin's
soft-fork classification. Old funds and historical DEX evidence must remain
recoverable. New quotes need a new signing domain rather than changing V6 meaning.

Only after separate approval: isolated experimental implementation, independent
cryptographic review, adversarial tests and reproducible builds. Deployment and
activation each remain separate decisions. No current release or website should
describe these options as working settlement-enforced fees.
