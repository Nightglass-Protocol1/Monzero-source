# Independent DEX protocol review — engagement package

This is the cover document for briefing an actual outside reviewer. It doesn't
replace `DEX_SETTLEMENT_REVIEW_BRIEF.md` — it's what to hand someone before
they read it, so they have the practical logistics and current status, not
just the protocol requirements.

## Why this review exists

`DEX_ADVERSARIAL_REVIEW_20260912.md` records a self-review of the DEX prototype,
explicitly flagged as non-independent: "Performed by the same AI assistant
involved in implementation. Not independent, not an external audit, not a
cryptographic proof, and not release certification." That review's own
"practical public-testing route" and this project's `RELEASE_STATUS.md` both
require a genuinely independent reviewer before any real-value activation.
This package exists to make that engagement fast to start, not to substitute
for it — nothing here authorizes skipping it.

## Scope

**In scope:** the BTC/XMZ settlement-terms signing format, offer/quote
authentication, the funding-role binding, the fee model as currently decided
(zero app fee for first release — see below), and the wallet-bridge design for
moving XMZ (not yet implemented). This is protocol/design review, not a full
audit of the core Monzero consensus code (that's a separate, larger
engagement — see `RELEASE_STATUS.md`'s "External evidence required" list) or
of `bitcoin-wallet`/`swap-machine`'s Bitcoin-side logic, which is largely
upstream eigenwallet code, not ours.

**Out of scope:** anything requiring real funds. No mainnet keys, no
production wallets, no request to test against `node.monzero.org`. Everything
reviewable today runs on BTC regtest / XMZ fakechain only.

## Reading order

1. `README.md` (root) and `WHITEPAPER.md` — what Monzero is, for context.
2. `DEX_FIRST_RELEASE_SCOPE.md` — **read this before the fee-enforcement
   documents below.** The user has since decided the first release carries no
   mandatory app trading fee. `DEX_FEE_ENFORCEMENT.md` and
   `DEX_SETTLEMENT_SECURITY.md` describe the fee-bearing design problem that
   decision superseded — still useful background on the underlying
   full-spending-authority issue, but not the current release blocker.
3. `DEX_SETTLEMENT_REVIEW_BRIEF.md` — the actual protocol requirements and
   proof obligations. This is the core of what needs review.
4. `DEX_ADVERSARIAL_REVIEW_20260912.md` — findings R1–R5, priority order.
   **Status update on these, current as of this engagement:**
   - R1 (fee-enforced settlement construction): superseded by the zero-fee
     decision in `DEX_FIRST_RELEASE_SCOPE.md`, not resolved by a construction.
   - R2 (hypothetical consensus replacement): still open.
   - R3 (wallet metadata is not a peer proof): still open.
   - R4 (explicit funding-role binding, the review brief's V6 gap above):
     **addressed** — `Terms::payerKey` added, format bumped to V6→V7, bound
     into `OfferFillReservationStore::commit`'s crossing check. Verify this
     independently rather than taking that line on trust; it's a small,
     isolated change and a natural place to start.
   - R5 (timing/availability adversary model): still open.
5. `DEX_XMZ_SIGNING_FEASIBILITY.md` and `DEX_CONSENSUS_OPTIONS.md` — signing
   primitive and consensus-option background referenced by the brief.
6. `tools/dex-engine-port/WALLET_BRIDGE_CLASSIFICATION.md` — current state of
   the XMZ wallet-bridge work: what's done via native RPC, what's drafted but
   unreviewed (`upstream-xmz-wallet-restore.patch`), and what doesn't exist
   yet (the actual fund-relay call). This is the newest document in scope and
   the most likely to still be actively changing.

## What to actually verify, not just read

- Independently reproduce the `dex_*` test suite result (40 tests, excluding
  the two live-node integration tests) and `cargo test -p swap-core --lib`
  (79 tests) — don't take passing-test claims on trust; the whole point of
  independent review is a second execution.
- Check `upstream-xmz-wallet-restore.patch` against the review brief's nonce
  safety, key-control, and role-binding obligations before it's applied.
- Attempt the adversarial cases the review brief lists in "Executable
  acceptance package" — alternate-spend, withheld messages, restarts, reorgs —
  against the isolated dex-lab VM (BTC regtest / XMZ fakechain), not just
  read the code and reason about it.

## Access

Two private GitHub repos exist for exactly this kind of read access, ahead of
the org-hardening checklist in `docs/MONZERO_PUBLIC_REPOSITORIES.md` that
gates the public mirror: `Nightglass-Protocol1/monzero-core` and
`Nightglass-Protocol1/monzero-gui-ci`. Add the reviewer as an outside
collaborator with read access rather than sharing a credential or archive.

## Confidentiality and disclosure

This is pre-release, unaudited, unsigned software with a genesis identity not
yet deployed publicly at scale. Agree confidentiality terms before sharing
access — this isn't covered by `SECURITY.md`'s public bug-bounty program,
which is a different, already-public track for reports against deployed
infrastructure. Findings from this engagement should be reported privately to
whoever commissions it, with a coordinated-disclosure timeline agreed up
front, mirroring `SECURITY.md`'s cadence (acknowledge within 7 days, status
within 14) even though that policy's scope doesn't technically cover this.

## What "independent" means here

Not affiliated with this project, not the same person who wrote or reviewed
this code, and not compensated in a way that's contingent on reaching a
particular conclusion. If the reviewer is also asked to be the second
reproducible-build operator for the core coin release (a separate,
non-overlapping gate in `RELEASE_STATUS.md`), that's fine — the two roles are
complementary, not a conflict — but say so explicitly in whatever you send
them, so their report is clear about which hat they're wearing.
