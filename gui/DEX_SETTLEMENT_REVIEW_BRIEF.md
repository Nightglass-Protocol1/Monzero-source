# Settlement construction: implementation review brief

Status: unresolved design work; not approval to enable trading.

Baseline inspected: GUI `9f28aa71`, core `58f10f796`. Local working-tree changes
and build provenance must be checked again before reproducing results.

## Required outcome

Trades through this protocol pay a success-only protocol fee in XMZ. A trader
with no prior XMZ can acquire XMZ with another supported asset. An unsuccessful
swap must have a recovery path without collecting the success fee. Other
exchanges are outside the scope. Native chain transaction fees are separate.

The first construction to specify is BTC/XMZ on isolated chains. External/external
trading with an XMZ fee requires a further multi-chain construction; two sequential
swaps cannot silently stand in for atomic settlement.

## Roles to specify before signing code

Do not infer funding roles from offer maker/taker labels. For each direction,
identify the BTC funder, XMZ funder, BTC recipient, XMZ recipient, fee recipient,
and the owner of each refund destination. Map each to the exact signed V6 fields.
Specify which participant supplies each contract key and adaptor key, and prove
that the corresponding private authority belongs to the intended party.

### V6 role-binding gap found in the implementation

`SettlementTerms::Terms` signs maker/taker identity keys, pay/receive assets and
recipients, but has no explicit pay-leg funder, receive-leg funder or protocol-fee
payer field. `OfferFillReservationStore::commit` checks both participant keys
against the crossing and accepts either maker/taker ordering. Its asset/amount
checks do not establish which identity must fund which asset. A mutually signed
quote proves agreement to those bytes, not an unstated interpretation of them.

Consequently, a signing implementation must not derive funder authority from this
API's `pay`/`receive` names or from a GUI-local orientation. Specify an authenticated
role binding in the next protocol version, including fee responsibility for both
acquisition and sale directions. Require distinct opposite-leg funders drawn from
the authenticated participants, exact offer-side agreement, and proof of control
of the keys used for funding and recovery. Recipient strings alone are not proof
of control. Specify the treatment of externally supplied recipient addresses.

This is a requirement, not an adopted wire format. Changing V6 field semantics in
place would reinterpret existing signatures and persisted quotes. A future format
must use a new signing domain/version, reject cross-version negotiation, and keep
old evidence available for read-only recovery without promoting it to new signing
authority. Positive tests must cover both maker/taker permutations and trade
directions; swapped funders, fee payer, contract keys and refund roles must fail.

For every protocol message, record:

1. The exact serialized and authenticated bytes, their deployment/quote binding,
   and the evidence required before accepting them.
2. Secrets known before and after the message, including any secret extractable
   from a later chain transaction. Distinguish signing nonces, adaptor secrets,
   view keys and spend-key shares.
3. Every transaction a malicious recipient can construct with that knowledge,
   not just the transaction the honest GUI proposes.
4. Required durable records before disclosure, retry semantics after a crash,
   and recovery when the peer disappears immediately after receiving the message.

## Required proof obligations

| Obligation | Evidence needed before implementation can advance |
| --- | --- |
| No fee-free success | Show that obtaining the traded assets necessarily produces the specified XMZ fee, even with modified clients and alternative output construction. Quote signatures alone are insufficient. |
| Correct delivery | Bind both destinations, amounts, native fees, commitments and chain identities to the signatures actually accepted by consensus. |
| No confiscation on abort | Enumerate abort points and show how honest participants recover their entitled funds without collecting the protocol success fee. Account for native fees separately. |
| Offline recovery | Identify the pre-existing data and authority sufficient to recover without obtaining a new signature from the vanished peer. |
| Nonce safety | Define generation, commitments, session separation, durable consumption, retry and crash handling. Never reuse signing nonces to recreate lost state. |
| Reorganization handling | State finality assumptions and confirmation thresholds; show what authority has already escaped when a supposedly confirmed event is reorganized. |
| Current-chain compatibility | Adapted signatures and complete transactions must verify under the existing Monzero consensus rules; otherwise seek a separate chain-design decision. |

The key-release construction currently discussed in
[DEX_FEE_ENFORCEMENT.md](DEX_FEE_ENFORCEMENT.md) does not discharge the first
obligation. A participant with complete XMZ spending authority is not restricted
to the outputs proposed in the quote. A replacement design must explicitly show
why it does not grant that fee-free alternative authority.

## Executable acceptance package

Provide a pinned reference implementation and reproducible positive vectors for
verification, adaptation and extraction. For each positive vector, require
rejection of a changed quote, transaction digest, destination, amount, fee,
participant, deployment and session nonce. Include malformed points and malicious
partial signatures. Independent reproduction must not reuse our vector generator
as the sole source of expected results.

Then run two independent participants on fresh BTC regtest and XMZ fakechain:

- Acquire XMZ from zero initial XMZ, completing delivery and the required fee.
- Attempt a successful alternate spend omitting the fee; demonstrate why the
  protocol prevents it, not merely why local accounting refuses to label it paid.
- Withhold each message in turn and exercise the corresponding refund path.
- Restart each participant after every durable-write/secret-disclosure boundary.
- Reorganize each relevant confirmation and check both funds and fee accounting.
- Demonstrate that no outcome is inferred from a network ACK, stale PSBT, a
  decoded RPC response, or a caller-provided Boolean.

## What existing tests establish—and do not

The local suite covers authenticated transport, mutual quotes, durable history,
offer capacity, bounded RPC, proof verification and transaction evidence. Real
Bitcoin tests cover genesis/sync checks, disconnected blocks, unsigned funded
lock PSBT structure and rejection of spent inputs. Real Monzero tests cover the
pinned fakechain genesis and persistence of a generated block across restart.

These are not a two-party atomic swap, fee-enforcement proof, current CLSAG adaptor
implementation, independent binary reproduction or independent security review.
The pinned cryptographic helper verifies ECDSA adaptors and cross-curve DLEQ only.
See [DEX_XMZ_SIGNING_FEASIBILITY.md](DEX_XMZ_SIGNING_FEASIBILITY.md) for the core
signing interfaces and screened research candidates.

## Decision boundary

Do not replace these requirements with a prepaid nonrefundable fee, custodial
coordinator, undisclosed signer, or unapproved consensus change. A proposal that
requires any of those assumptions must state them explicitly and obtain a separate
user decision before implementation. Until a construction satisfies this brief,
live signing, broadcasting and DEX activation remain disabled.
