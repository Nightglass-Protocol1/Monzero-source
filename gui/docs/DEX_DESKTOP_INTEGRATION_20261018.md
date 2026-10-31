# DEX desktop integration — 18 October 2026

Development checkout: `gui-wt-dex-desktop`, branch
`dex/codex-desktop-integration-20261018`. No artifact from this checkout has been
published, and this does not establish beta readiness.

The approved public GUI changes have been ported: transparent teal artwork,
software-rendering palette, bounded local-node startup, explicit asynchronous
CPU mining controls with live status, and private-by-default signed public-wallet
profiles. The conflict resolutions retain the latest Assets page and replace
its legacy font references; the obsolete external-mining-only instructions are
superseded by the user's requested explicit GUI controls. Existing DEX source and
wallet ownership controls remain present.

Commit `126ad2747` pins the released core
`c4165784db57d8ae410fc8af58861e4178ee1a42` and aligns the GUI version with v1.0.0.
The old DEX gitlink was `c4f2cc7c1`; no claims about prior binary validation are
transferred to this changed core. Core and nested dependencies were checked out
at their recorded revisions. Historical public-GUI verification documents copied
with the changes describe those earlier builds, not this integration checkout.

## Verification so far

- Native configuration passed with `WITH_DEX_PROTOTYPE`, `DEX_BUILD_TESTS` and
  `QML_TESTS` enabled, dynamic Qt 5.15.19, x86-64, Release; scanner disabled as in
  the existing native GUI build.
- 155 QML checks passed against the integration checkout using the existing
  public GUI test harness. The first attempt lacked generated `version.js` and
  the screenshot output directory; configuration supplied those dependencies.
- A transient incomplete backup-review binding emitted a runtime warning.
  Commit `956570186` renders the review from one validated snapshot, clears
  consent on preview changes and refuses restoration without complete displayed
  review data. All 106 DEX presentation checks then passed, with no TypeError or
  ReferenceError in that run.
- The first native build exposed the development-only asset-transfer API missing
  from the released core. Commit `5c7993f39` adds compile-time capability detection
  and disables transfer/burn controls when unavailable, preserving builds with
  the API. A C++14 smoke check covered both API variants.
- The second native build completed both `monzero-wallet-gui` and `dex_tests`.
  The new native harness then passed all 164 source-QML checks, including eight
  prepared-trade presentation results. There were no TypeError/ReferenceError
  entries in that run.
- Five focused native results passed for prepared-trade rejection without writes,
  fingerprint persistence, duplicate registration refusal and legacy discovery.
  These checks do not establish successful prepared-trade import or peer reconnect
  with a complete signed fixture; that acceptance remains open.

Evidence under `core/build`: `codex-dex-desktop-configure-20261018.log`,
`codex-dex-desktop-qml-configured-20261018.log`,
`codex-dex-desktop-backup-review-qml-20261018.log`, and
`codex-dex-desktop-build-20261018.log`. Build unit:
`monzero-dex-desktop-build-20261018.service`. Generated mock screenshots are in
this checkout's `build/qml-prebuilt-harness` and `build` directories.

## Remaining acceptance

Complete the broader native suite, rebuild after final review fixes, and inspect
the integrated desktop. Complete the end-user LTC/XMZ
setup and recovery journey; importing the existing public GUI controls alone
does not provide that journey. Active-trade backup restoration, public peer
rendezvous, portable packages, supported-platform verification and independent
review remain separate gates. No mainnet trades or DEX release are authorized.

The concurrently running VM-wallet public-testnet refund uses a separate frozen
checkout, `gui-wt-vm-testnet-refund` at `0091d81bb`; none of these changes modifies
its source, helpers, wallets or VM state.


The broader native suite completed with **81 passed, 0 failed, 21 skipped**.
The skipped cases require external fixtures; this run does not cover those
integrations. Evidence: `codex-dex-desktop-native-suite-20261018.log`.

## Prepared-trade import work in progress

A local app.json review checks both quote signatures without launching the helper.
The user sees the selected network, role, partner key, certificate fingerprint,
helper identity and exact signed terms. Import requires explicit consent tied to
that review, expires after two minutes, and remembers a v2 trade manifest binding
the configuration files. Reopening checks that binding; v1 manifests retain their
existing behavior. Explicit CLI attachment also registers the trade. No private
setup folder should be exchanged with a counterparty.

This does not implement in-app identity unlocking or quote creation/signing.
The existing peer-v2 setup still uses its private passphrase file; replacing that
with an in-app unlock flow is a separate task. Successful full-fixture import,
reopen, mutation and peer startup acceptance remains required before release.
Claude's follow-up source review is recorded in
`core/build/codex-claude-dex-setup-review-20261018.txt` when complete.

Additional evidence: `codex-dex-desktop-build-2-20261018.log`,
`codex-dex-desktop-build-3-20261018.log`, `codex-dex-desktop-own-qml-20261018.log`,
`codex-dex-trade-setup-qml-20261018.log`, `codex-dex-setup-native-20261018.log`.


## Follow-up review: open blockers

Claude completed the read-only review. The expected setup digest is now checked
inside `configureLitecoinApp` before it adopts settings; final native rebuild
and five focused results passed after that change. The following findings remain
open, so the prepared-trade work is a development draft, not accepted integration:

- Validators and jobs independently reread files. Their adopted bytes must be
  compared with the reviewed snapshot to close a local file-replacement race.
- Secret/TLS leaf files are outside the setup commitment; complete local identity
  validation is deferred until configuration.
- Failed configuration can leave an active registration that cannot be closed
  from the current GUI. Registration must become recoverable without overwrite.
- Legacy manifest migration and multiple-active-trade handling need explicit
  acceptance, including CLI compatibility.
- Review must show all effective helper paths, connection settings and policy.
- Successful signed-fixture import/reopen, mutation rejection, replay/expiry and
  identity mismatch acceptance remain missing. The current fingerprint test uses
  a missing app file; it does not prove rejection of a changed valid setup.

Final build evidence: `codex-dex-desktop-build-4-20261018.log`;
focused evidence: `codex-dex-setup-native-final-20261018.log`.
Public roadmap commit `ca34e031c` records only the verified build/interface
milestones and explicitly leaves prepared-trade acceptance open.
