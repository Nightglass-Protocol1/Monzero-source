# Prepared exact-net review triage

Claude completed a read-only second-pass review on 2026-09-19.
Raw review: `build/claude-prepared-exact-net-review.log` in the core checkout.
This is AI-assisted review, not an independent cryptographic audit or release sign-off.

## Accepted follow-ups

Patch 68 factors the exact-net post-construction wallet/snapshot check behind a
private `ConstructionContext` seam used by both preview and preparation.
Deterministic tests cover stable ordering, wallet replacement/removal, changed
genesis/tip/height and propagation of wallet/snapshot RPC failures.
This addresses helper-level failure coverage, not a full mocked construction
sequence, concurrent wallet manipulation, reorg recovery or atomic observation.
Claude reviewed this narrow follow-up and reported no identified helper/test defect.
Raw follow-up: `build/claude-exact-net-context-review.log`.
Its caveat about non-atomic observations is retained: a change that occurs and
reverts between checks can be missed, regardless of whether either comparison succeeds.
The review does not establish a bound on the probability of such events.

- Add deterministic tests for wallet/chain changes between construction and post-validation.
  The current real-fakechain fixtures test stable success and invalid initial context, not injected mid-flight changes.
  Reuse the existing `fee_estimation::DisplayContext` testing pattern where appropriate.
- Document coin-selection availability: the wallet chooses inputs before the engine checks the exact approved set.
  Unrelated available funds may cause selection to fail that check.
  Do not weaken the exact-input check, sweep unrelated funds or automatically freeze outputs to hide this failure.
  A dedicated restored wallet reduces accidental unrelated funds but cannot prevent third-party transfers to a known address.

## Corrections and limits

- It is not a strict requirement that no unrelated outputs exist.
  A selection containing only the approved outputs remains valid; a selection containing unrelated outputs is rejected.
  The issue is construction availability, not silent authorization to spend extra funds.
- The `#[ignore]` funded tests are explicitly invoked by `verify_native_settlement.py`.
  They were executed for patch 67; the review's suggestion that such tests are not run was not evidence about our runner or CI.
- The quoted XMZ recipient is the XMZ buyer, not the seller.
  Any review wording calling it the seller's payout must not be carried into the signed policy or GUI.
- Metadata retention without a relay accessor is intentional at this milestone.
  It exercises real preparation, then discards the private object until a durable journal can safely control dispatch.
- Repeated hash-format helpers are a maintenance observation, not a demonstrated settlement defect.
  Different boundaries may deliberately reject zero hashes differently; consolidation needs preserved semantics.

The review reported no identified fund-safety defect in the accounting checks it inspected.
That is a bounded finding, not evidence that complete settlement or reserve ownership is enforced.
Journaling, real top-up ownership/timing, GUI execution, cancellation policy and live readiness remain incomplete.

## Historical loader follow-up (patch 70)

Claude's read-only report is `build/claude-exact-net-reload-review.log`.
Current public call paths construct expected intent objects internally and compare
loaded intent against them before `check_terms`; malformed JSON values do not
reach its object `expect`. Genesis format is checked by shared load/reserve helpers.
Thus the report's panic/genesis observations are dependency-hardening suggestions,
not demonstrated externally reachable failures in these paths.

Canonical path checking precedes opening the directory; file reads are subsequently
anchored to the opened descriptor and checked for owner, mode, links, size and
modification. This is not a proof against hostile replacement of ancestor paths.
Trusted directory ancestry remains an explicit deployment assumption, and the
historical loader supplies no authority to relay even after a successful load.

Useful remaining fast tests include malformed top-level file values at the actual
reader boundary and exact equality of actual fee to approved ceiling.
The real fresh-process test lives in `tests/bitcoin_settlement.rs`, outside the
three implementation files reviewed, and is explicitly run by the funded driver.
No independent audit or full recovery sign-off follows from this review.

## Reconciliation follow-up (patch 71)

Claude reviewed `reconcile` and `verify_transfer`; raw report:
`build/claude-exact-net-reconcile-review.log`.
It found no concrete defect within the inspected checks and noted the remaining
real positive-confirmation fixture gap. Its account-number comparison suggestion
is consistency hardening, not demonstrated acceptance of invalid evidence:
current raw JSON equality rejects mismatched encodings.
Any repeated observation is read-only; an unresolved result never authorizes a
new broadcast, regardless of the report's broad wording about caller retries.
Local RPC trust, non-atomic observations and limited coverage remain unchanged.
