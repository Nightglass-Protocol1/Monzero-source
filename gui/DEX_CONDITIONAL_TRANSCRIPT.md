# Candidate BTC/XMZ transcript: research only

This is a hypothetical consensus-extension transcript, not executable protocol
code or approval to change the chain. It refines
[the options assessment](DEX_CONSENSUS_OPTIONS.md). No safety proof is claimed.

## Explicit roles and authority

S funds XMZ and receives BTC. B funds BTC and receives XMZ. S generates a fresh
secret s and supplies H(s). Maker/taker labels are independent of these roles.
The initial case is acquisition by B with no existing XMZ. The agreed XMZ lock
must cover B's net delivery, the protocol fee, and the native fee budget. Quote
terms must say who economically pays each fee; do not silently reduce delivery.

| Output | Success authority | Refund authority |
| --- | --- | --- |
| Hypothetical XMZ lock, funded first | s plus the specified authorization; outputs restricted to B's delivery and fee recipient | S's signature after the later XMZ deadline |
| BTC lock, funded second | S's signature plus s | B's signature after the earlier BTC deadline |

**A preimage is not ownership.** S knowing s initially must not be able to redirect
the XMZ success outputs. Public knowledge of s must not let a third party redirect
BTC. Refunds require the relevant funder's authority, not just time passing.
The success branch need not become invalid at the refund deadline: if it stays
valid, competing success and refund spends must be modeled explicitly.

## Proposed order and durable records

1. Both parties authenticate a new-version quote binding roles, keys, chain IDs,
   hash, amounts, recipients, native fee limits and deadlines. Neither funds based
   on historical V6 quotes. Each validates its recovery data before funding.
2. S funds the restricted XMZ output. B verifies its actual on-chain conditions,
   funding amount and agreed confirmation depth, not an RPC Boolean from S.
3. B funds the BTC output only while sufficient XMZ recovery margin remains.
   S validates the actual BTC script, amount, confirmations and remaining time.
4. S persists all recovery/observation state, then redeems BTC early enough to
   tolerate the agreed inclusion and reorganization assumptions. This exposes s.
5. B observes and validates the witness and spends XMZ to the fixed delivery and
   fee outputs. Persisting s and contract data must precede any acknowledgement.
6. Both observe both legs at the specified depths. Only then may the local
   journal record completed settlement; no acknowledgement substitutes for this.

If S never discloses s, B can refund BTC and S can later refund XMZ, with native
fees but no protocol fee. This is a proposed abort path, not yet a verified one.

## What the ordering fixes, and what it does not

The earlier toy model exposes s from the XMZ leg before BTC redemption. This
candidate reverses that disclosure order and therefore needs a different model;
the old model is neither a proof against this transcript nor evidence of safety.

The BTC refund opportunity must precede the XMZ refund opportunity by a margin
covering observation, XMZ inclusion and confirmation, outages and reorg policy.
Chain heights cannot simply be subtracted across chains. No numeric production
deadline is selected here. Any bounded-delay assumption must be stated, monitored
and tested; no finite timeout guarantees safety under unlimited censorship.

Required attack traces include:

- S's BTC witness leaks but BTC refunds; B then obtains XMZ. A sufficiently early
  honest disclosure policy mitigates this only under explicit inclusion/finality
  assumptions. Waiting locally for confirmations cannot conceal a public witness.
- S redeems BTC and B goes offline until XMZ refunds. B loses the recovery race;
  the protocol fee is also absent. “B could have claimed” is not fee collection.
- B withholds XMZ settlement deliberately. A constrained public finalizer could
  improve liveness only if it has all witness/proof data and a native fee budget,
  without any ability to redirect funds. That mechanism remains unspecified.
- A reorganization removes either completed leg after the other's secret or
  spending authority has escaped. A confirmation threshold is an assumption,
  not irreversible cross-chain finality.

## Critical remaining requirement

Within a successful XMZ spend, restricted outputs could force delivery and fee
together. They cannot alone establish that BTC payment finalized. Nor does the
ability to claim XMZ guarantee that anyone does so. Consequently this transcript
does **not** yet establish success-only fee collection for the whole swap under
all required failure cases. Do not declare the fee earned merely on BTC redemption.

The [role-aware model](tools/dex-protocol-model/README.md) now separates both refund
windows, witness publication, confirmation and authorized spenders. It enumerates
26 reachable states with both terminal loss directions. Optional constrained
finalization alone does not change the reachable state set. These are unrestricted
adversarial schedules, not an implemented honest early-disclosure policy.

Next work must specify and justify bounded-inclusion assumptions and test an
explicit early-disclosure policy against them. Preserve adverse outcomes outside
those assumptions rather than deleting them to obtain passing tests. External/
external trades still require a separate three-leg construction.

## Conditional timing bound (not production parameters)

Let t be disclosure time, Rb the earliest BTC refund opportunity, Rx the earliest
XMZ refund opportunity, Db a bound to stable BTC inclusion, O a bound for B to
observe stable BTC and prepare/submit XMZ, and Dx a bound to stable XMZ inclusion.
For a policy that waits for stable BTC before submitting XMZ, sufficient timing
inequalities in this abstract model are:

`t + Db < Rb` and `t + Db + O + Dx < Rx`.

Equality is not safe: the competing refund can win. With discrete integer ticks,
the latest permitted disclosure is `min(Rb-Db-1, Rx-Db-O-Dx-1)`. A cutoff before
the current time means do not disclose; it does not justify extending a signed
contract locally. The model tests enumerate small bounds, verify every permitted
delay combination and retain failures just outside the cutoff or delay bounds.

These are conditional arithmetic results, not measured blockchain guarantees.
Mapping each chain's earliest refund opportunity into a common conservative time
frame, stable-inclusion/reorg assumptions, fee adequacy, wallet uptime and safe
funding lead time remain unresolved. A modified S can ignore the release policy;
a modified B can ignore submission. Thus these inequalities do not establish the
global success-only fee requirement against malicious parties. A fee collector
must not be described as trustless merely because an honest timing example works.

See `tools/dex-protocol-model/test_deadline_bounds.py`. No runtime signing gate,
deadline default or consensus validation is implemented by these tests.

## Primary reference

[BIP 199](https://github.com/bitcoin/bips/blob/master/bip-0199.mediawiki) describes
hash/preimage and signature-gated success with a timed refund, and explicitly
discusses the risk of revealing a preimage without getting the spend confirmed.
It is listed as Closed; it supplies background for this research, not an XMZ
implementation or an endorsement of the proposed transcript.
