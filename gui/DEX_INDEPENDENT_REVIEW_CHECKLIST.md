# DEX independent review checklist

Completion criteria for the independent human review that `DEX_ADVERSARIAL_REVIEW_20260912.md`,
`RELEASE_CHECKLIST.md`, and `DEX_INDEPENDENT_REVIEW_ENGAGEMENT.md` all say this project still
needs before any real-value activation. Every prior "review" of this codebase — including this
document's own preparation — has been done by the same AI assistance that wrote the code, or by a
second AI assistant reviewing the first. That is useful triage, not the review this checklist is
for. This checklist is complete only when every box below is checked by someone who is not the
person or AI session that wrote the code being checked, per `DEX_INDEPENDENT_REVIEW_ENGAGEMENT.md`'s
own definition of "independent."

Two AI assistants (Claude and ChatGPT/Codex, per the project's session history) have worked on this
codebase, at different times, without a shared session. Read their track record as "two independent
drafts that happened to agree," not as two independent reviews of the same draft — neither one
reviewed the other's work with adversarial intent, only in passing while extending it.

Scope: this checklist covers the BTC/XMZ DEX feature only — protocol/settlement design, the native
wallet-bridge and execution/recovery machinery, and the GUI integration. It is **not** a full audit
of Monzero core consensus, its dependencies, or its release infrastructure; those are covered by
`RELEASE_CHECKLIST.md` in general and by `DEX_INDEPENDENT_REVIEW_ENGAGEMENT.md`'s "External evidence
required" list specifically.

## 0. Before starting

- [ ] Confirm you meet `DEX_INDEPENDENT_REVIEW_ENGAGEMENT.md`'s definition of independent: not
      affiliated with this project, not the person or AI session that wrote or reviewed this code,
      and not compensated contingent on a particular conclusion.
- [ ] Agree confidentiality terms before getting access; this is pre-release, unaudited, unsigned
      software, not covered by `SECURITY.md`'s public bug-bounty scope.
- [ ] Read, in order: `README.md` (root) and `WHITEPAPER.md`; `DEX_FIRST_RELEASE_SCOPE.md`;
      `DEX_SETTLEMENT_REVIEW_BRIEF.md`; `DEX_ADVERSARIAL_REVIEW_20260912.md`;
      `DEX_INDEPENDENT_REVIEW_ENGAGEMENT.md`. Then read this checklist's section 4 before touching
      any of the day's newer design docs — it tells you which ones matter most and why.
- [ ] Note the date you start and the exact commit/patch-count you're reviewing against (see 1.1)
      before reading anything else in the tree, so later claims of "N patches, M tests" in any doc
      can be checked against what you actually observed, not what the doc says you should see.

## 1. Reproduce, don't trust

Every number in every doc in this tree is a self-report by the tool that wrote the code. Confirm
each one yourself before relying on it anywhere below.

- [ ] **1.1 Patch count and reconstruction.** Count `*.patch` files in `monzero-gui/tools/dex-engine-port/`.
      Run `verify-upstream-patches.sh` from the core root and confirm it lists and reconstructs
      every one of them, in the order the README claims, against a byte-for-byte match with the
      live tree. If the count in the script doesn't match the count on disk, stop and find out why
      before trusting anything else about "reconstruction verified" in this tree.
- [ ] **1.2 Rust library tests.** Run `check-upstream.sh test` (`swap-core`) and
      `check-upstream.sh machine-test` (`swap-machine`) from a **freshly re-applied checkout** (a
      new clone at the pinned commit, patches applied in order by you, not by re-running against
      whatever is already sitting in `build/eigenwallet-engine-port`) and record the exact pass/
      fail/ignored counts. Compare against the last count the README/`DEX_PROGRESS_20260919.md`
      claim. A mismatch in either direction is worth understanding before continuing.
- [ ] **1.3 Ignored integration fixtures.** Build the `bitcoin_settlement` test binary and run every
      `#[ignore]`d test in it that the day's docs reference by name (`native_fee_estimation`,
      `native_swap_wallet_spendable`, `engine_redeem_is_accepted_on_regtest`,
      `engine_cancel_and_full_refund_are_accepted_on_regtest`, and whatever additional ignored tests
      were added after `native_swap_wallet_spendable` — `grep -rn '#\[ignore' swap-machine/tests/`
      for the current list, it has grown). Run the matching Python fixtures
      (`test_wallet_recovery.py`, including its `--native-spendable` mode if present,
      `test_swap_wallet_spendable.py`, `test_owned_fee_preview.py`, `test_gui_private_startup.py`,
      and any newer ones named in the day's docs) yourself, on your own machine, against binaries
      you built yourself from this source. Do not accept "passed on 2026-09-19" as evidence; accept
      only a pass you personally produced.
- [ ] **1.4 GUI/Qt tests.** Rebuild `dex_tests` and any QML test targets (`tst_DexRecovery.qml` and
      whatever else `grep -rn 'qmltestrunner\|dex_tests'` in the docs turns up) and confirm they
      pass headlessly, then confirm `qmllint` and whitespace checks pass.
- [ ] **1.5 Full-workspace build.** `DEX_PROGRESS_20260919.md` notes a full `monzero-wallet-gui`
      build was "running and has not yet completed" partway through the day, and later records it
      passing after CMake regeneration. Confirm a full, clean build of the GUI target passes on your
      own machine before relying on any downstream integration claim.

## 2. Confirm every safety gate is exactly as narrow as claimed

Do not just grep for the strings below and check they still say what they said this morning —
also check whether any *new* gate was introduced today that needs the same scrutiny.

- [ ] **2.1** `DexEngine::activationReadiness` in `src/dex/DexEngine.cpp` still returns
      `ready: false` unconditionally.
- [ ] **2.2** `NodeRpcValidator` (`src/dex/NodeRpcValidator.cpp`) still hard-rejects anything other
      than Bitcoin regtest and Monzero fakechain.
- [ ] **2.3** `RELEASE_CHECKLIST.md` and `DEX_ADVERSARIAL_REVIEW_20260912.md` are byte-for-byte
      unmodified from before today's work (`git diff` / mtime check) — no gate was marked satisfied
      by the same tooling that wrote the code it would be certifying.
- [ ] **2.4 New gate introduced today:** `--dex-private-backend <config>` (a new CLI option in
      `src/main/main.cpp`) requires **both** that flag **and** the environment variable
      `MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE` before the experimental
      DEX wallet service starts at all. Confirm both are still required (not just one), confirm
      normal/default startup is completely unaffected without them, and read
      `DEX_PRIVATE_BACKEND.md` in full for the config-file constraints (canonical absolute path,
      single-link, Linux-only) and confirm the code actually enforces every one it claims to.
- [ ] **2.5** Grep the entire diff of everything added today for any other boolean literal or env
      var that gates execution, relay, broadcast, or "live" behavior, and confirm every single one
      you find defaults to disabled. Do not assume 2.1–2.4 are the only ones; today added roughly
      60 patches and several new GUI entry points.
- [ ] **2.6** Confirm the fee scope hasn't silently drifted: `DEX_FIRST_RELEASE_SCOPE.md`'s
      zero-app-fee decision should still be what `DexEngine`/`NativeTradeQuote` actually enforce.
      `DEX_BUYER_RESERVE_QUOTE_V2.md` describes a v2 quote format explicitly **not** supported by
      current executors — confirm that's still true in the actual `native_trade_quote.rs` /
      `native_quote_signatures.rs` source, not just in that doc's own claim about itself.

## 3. Re-verify the AI-review checkpoints, don't just count them

Today's work includes several rounds where one AI assistant (Claude, invoked from inside the other
session) did a "read-only review" of the other's code and reported findings that were then fixed.
These are referenced by log file path in several docs (e.g. `build/claude-buyer-reserve-quote-review.log`,
`build/claude-sealed-context-tamper-review.log`, `build/claude-prepared-exact-net-review.log`,
`build/claude-recovery-key-lifecycle-review.log`) and inline in `DEX_PROGRESS_20260919.md`. Treat
every one of these as **unverified until you personally confirm the fix**, not as a review credit
already banked.

- [ ] **3.1** For each referenced review log under `build/`, read it, and confirm the finding it
      describes was actually fixed in the current source (not just acknowledged in a doc).
- [ ] **3.2** Specifically re-verify the wallet-close lifecycle in
      `swap-machine/src/native_funding_rpc/spendable.rs`. This function's close-on-exit behavior
      changed at least twice today: first added (always attempt close on any exit path), then
      corrected (per `DEX_PROGRESS_20260919.md`: "Restore now must succeed before this invocation
      attempts cleanup," because the first version could close a wallet that a *different,
      unrelated* caller had legitimately left open, if its address happened to match). Confirm the
      **current** code only attempts close when this call's own restore succeeded, and write a test
      yourself that opens a wallet from an unrelated session, then attempts `swap_wallet_spendable`
      with a colliding address, and confirms the unrelated wallet is left open and undisturbed.
- [ ] **3.3** Re-verify `NativeTransferRelay`'s single-shot/no-retry guarantee
      (`relay.rs`'s `dispatched` flag and `BroadcastOutcomeUnknown` error type, added after the
      initial version had only a doc-comment warning, not enforcement). Confirm dropping the
      future mid-flight, or constructing a second `NativeTransferRelay` against the same wallet,
      cannot silently double-dispatch.
- [ ] **3.4** Re-verify the spend journal (`spend_journal.rs` / `exact_net_journal.rs`): confirm the
      exclusive-creation + fsync ordering claimed in the docs actually happens before dispatch, not
      after, and confirm a torn/partial record on disk correctly blocks a second attempt rather than
      being ignored.
- [ ] **3.5** `DEX_FULL_PROCEEDS_ACCEPTANCE.md` explicitly notes "Claude review was attempted but
      unavailable because of its session limit." That means the fee-enforcement thread — the one
      that matters most, since it's R1 below — got **less** cross-AI scrutiny today than the other
      threads. Give it correspondingly more of your own attention, not less.

## 4. Adversarial findings — verify against today's new claims, not the 2026-09-12 baseline

For each finding below: locate the specific new patch(es)/doc(s) that claim to address it (search
the day's docs for the relevant keywords), then independently verify the claim — reproduce the
close-with condition from the original finding yourself, don't accept the new doc's word that it's
closed.

- [ ] **R1 — fee-enforced settlement construction.** Original finding: full spending authority lets
      a participant construct an alternative output omitting the protocol fee. Today's relevant
      work: the "exact-net" patch series and `DEX_FULL_PROCEEDS_ACCEPTANCE.md` /
      `DEX_BUYER_RESERVE_CANDIDATE.md` / `DEX_BUYER_RESERVE_QUOTE_V2.md`. Since the first-release
      fee is zero (per `DEX_FIRST_RELEASE_SCOPE.md`), this finding's original fee-bypass shape is
      superseded, but re-derive whether the *new* buyer-funded-reserve construction has its own,
      differently-shaped fee-bypass problem: can a participant construct a valid alternative spend
      that keeps the exact quoted principal but redirects or drops the "unused reserve returned to
      buyer" leg? Read `DEX_EXACT_NET_RESTART_DESIGN.md` and confirm the reserve-return destination
      is bound into whatever gets signed, not just into GUI-side bookkeeping.
- [ ] **R2 — real XMZ signing/consensus primitive.** Original finding: the crypto verifier only
      handled ECDSA-adaptor/cross-curve DLEQ, not actual XMZ adaptor signing or a conditional XMZ
      output. Check whether anything shipped today changes this, or whether the day's work (quote
      negotiation, wallet-bridge, checkpoints) is all still built on the same ordinary-transfer
      primitive (`relay`/`transfer`) rather than a new consensus mechanism. If nothing changed here,
      confirm the docs still say so plainly rather than implying otherwise by proximity to all the
      other progress.
- [ ] **R3 — wallet metadata is not a peer proof.** Original finding: validators trusted
      peer-supplied JSON claims about transactions rather than deriving them from raw chain
      cryptography. Check the new recovery/reconciliation code
      (`native_execution_claim`/checkpoint work, `verify_native_settlement.py`) for the same
      pattern: does any new validator accept a counterparty's claimed amount/destination/confirmation
      count without independently deriving it from the local wallet/daemon's own observation? The
      fork-level spot-check today found the sampled patches deriving amounts from the caller's own
      committed request cross-checked against local RPC observation, not peer JSON — but that
      checked 3 of roughly 60 patches; check the rest yourself, especially anything touching Bitcoin
      submission observation or GUI-displayed recovery evidence.
- [ ] **R4 — explicit funding-role binding.** Already marked addressed as of 2026-09-15
      (`Terms::payerKey`, V6→V7). Confirm nothing added today reintroduces ambiguous maker/taker
      funding-role inference, especially in the new quote-negotiation/reservation code.
- [ ] **R5 — timing/availability adversary model.** Original finding: no explicit adversary/liveness
      model; disclosure, inclusion and stable settlement are distinct events. Today's checkpoint/
      recovery/restart series (Alice-checkpoint, signing-boundary restart, read-only reconciliation)
      is the most direct attempt at this. Confirm: (a) every claimed crash/restart test actually
      injects the crash at the boundary it claims to (not just "before" or "after" a whole
      operation, but at the specific irreversible sub-step — e.g. after broadcast but before
      receipt persisted); (b) reorg handling is still not modeled (confirm the docs still say so);
      (c) the explicit adversary model this finding asked for (safe disclosure/monitoring rules,
      abort cases, fee budgets under delay) still doesn't exist as a written spec anywhere in the
      tree — if it does now, review it as a new artifact in its own right, not as a checkbox tick.

## 5. Executable acceptance package (from `DEX_SETTLEMENT_REVIEW_BRIEF.md`)

Run these yourself against the current tree, on isolated BTC regtest / XMZ fakechain, with two
processes that do not share memory (the "separate-participant" fixture from today is a starting
point, not a substitute — it's still one trusted test harness controlling both sides):

- [ ] Acquire XMZ from zero initial XMZ, completing delivery and (if applicable this release) the
      fee.
- [ ] Attempt a successful alternate spend omitting the fee/reserve-return leg; confirm the protocol
      itself prevents it, not merely that local accounting refuses to label it paid.
- [ ] Withhold each protocol message in turn and exercise the corresponding refund/recovery path.
- [ ] Restart each participant after **every** durable-write/secret-disclosure boundary you can
      identify from the source — not just the boundaries the existing fixtures already restart at.
- [ ] Reorganize each relevant confirmation and check both funds and fee/reserve accounting after.
- [ ] Confirm no outcome is ever inferred from a network ACK, a stale PSBT, a decoded RPC response
      alone, or a caller-provided boolean — every acceptance must trace back to an independent
      chain/wallet observation.

## 6. Decisions that are the project owner's, not yours

These block real-value activation but are not things for a reviewer to approve or reject —
flag them as outstanding and move on:

- [ ] `DEX_RECOVERY_KEY_LIFECYCLE.md` asks the owner to choose between a separate recovery
      passphrase (recommended in that doc) and wallet-password-based unlock. Confirm this is still
      unresolved, and confirm the code does not default to either choice silently in the owner's
      absence.
- [ ] Any fee recipient, service description, or pricing for a future XMZ-denominated app service
      (`DEX_FIRST_RELEASE_SCOPE.md`) — confirm none has been invented or defaulted by any patch
      today; this must come from the owner, not from either AI session.

## 7. Cross-reference against `RELEASE_CHECKLIST.md`

Only the boxes actually relevant to shipping this feature at all, even in a no-value preview:

- [ ] "Independent reviewer approves the release scope" — this is the box this entire document
      exists to let someone actually check, honestly, for the first time.
- [ ] "Consensus-affecting changes identified explicitly" — confirm none of today's ~60 patches
      touch core consensus (they shouldn't; this is a GUI/engine-port/wallet-RPC feature), and if
      any do, treat that as a scope escalation requiring the full consensus-safety section of that
      checklist, not just this one.
- [ ] "User-visible changes and known limitations written" — confirm the GUI's new DEX
      recovery/private-backend surfaces carry the same explicit no-retry/no-trading/experimental
      warnings the source claims they do, by actually looking at the running UI, not just the QML
      source.
- [ ] Everything under that checklist's "Assets-specific gate" remains not-applicable here (this is
      not an Assets release) — confirm nothing today quietly expanded scope into that territory.

## Sign-off

This checklist is complete only when every box above is checked, with your own evidence (command
output, a log you generated, a test you personally ran and watched fail-then-pass), and you can
write a one-paragraph statement in your own words — not copied from any doc in this tree — of:
who you are, what you checked, what you found, and whether you'd let this handle real money today.
If the answer to that last question is no, say exactly what would need to change, in terms
specific enough that someone could act on it without you.
