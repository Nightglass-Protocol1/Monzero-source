# DEX integration acceptance — current worktree

This is the next integration sequence, not a release approval. The full objective
includes peer offers, XMZ acquisition, additional validated coins and XMZ protocol
fees. The zero-app-fee BTC/XMZ milestone does not satisfy that entire objective.

## Evidence checked

- `src/main/main.cpp` can now attach a separate SwapWalletSession through the
  explicitly opted-in `--dex-private-backend` configuration. Ordinary startup
  does not provision that service or enable trading.
- `src/dex/DexEngine.cpp` still returns false for signing, publishing, validation
  and matching. Do not remove these gates simply because component tests pass.
- `src/dex/ChainAdapter.h` is observational; it does not implement execution.
- The funded coordinator fixture passes create, preview, close, reopen and close
  against disposable fakechain RPC/helper processes, with unchanged funds and
  no fee-preview broadcast. This was rerun after preflight/teardown changes.

## Next vertical integration steps

1. Provision a backend in an explicitly isolated application mode. Select a
   separately verified helper/RPC installation from trusted local configuration,
   not offers or QML input. Bind executable provenance, network/genesis and private
   data directory before launch. Attach the existing coordinator and exercise the
   actual DEX page against a disposable wallet. Prove startup failure, shutdown,
   credential cleanup and no access to the user's existing wallet.
2. Connect one BTC/XMZ trade driver to that backend, rather than implementing
   signing on the read-only ChainAdapter. Use versioned, role-bound canonical
   terms; persist intent before each irreversible action. Test both participants
   through an actual offer/quote exchange with stale, duplicate and altered quotes.
3. Exercise success and cancellation/refund through that same driver, including
   restart after every funding/signing/broadcast boundary. Independently inspect
   both chain transactions and final balances. A pending-operation marker alone
   is not a durable trade journal or recovery reconciliation.
4. Connect the verified driver to the GUI; show actual terms, network fees,
   confirmation progress and recovery actions. Test interrupted GUI sessions and
   changed chain state. Do not present sequential multi-hop swaps as atomic.
5. Implement the explicitly selected XMZ fee service/rate/recipient and bind its
   payment evidence to the intended operation with replay prevention and disclosed
   failure/refund behavior. Network-fee estimation is not protocol-fee enforcement.
6. Add other assets only with their own settlement, refund, reorg and restart
   evidence. Demo LTC/BCH tabs are not supported-asset proof. Validate packaging,
   independent review/reproduction and platform behavior before live activation.

Each step needs end-to-end evidence at its own boundary. Successful test binaries,
source parity checks and GUI self-tests must not substitute for that evidence.
Keep live execution disabled throughout incomplete integration.

## Current verification checkpoint — 2026-09-19

The historical wiring gap below is partly superseded. The full GUI built, and
`test_gui_private_startup.py` exercised real wallet-RPC provisioning in an isolated
network/home namespace: unauthenticated requests were rejected, authenticated
version queries succeeded, no wallet was open, and normal exit removed credentials
and recorded stopped service state. This is not forced-exit recovery evidence.

The final separate-Alice-process redemption and cancellation/refund fixtures both
exited successfully. Each exercised protocol-recovered joint keys against native
fakechain funds, exact fee accounting and read-only reconciliation after RPC
restart with the success receipt withheld. The real helper response passed the
Qt recovery decoder. Ordered nineteen-patch source reconstruction also passed.

These are separate integration boundaries, not one funded GUI trade. The next
unproven requirements remain durable participant restart at irreversible actions,
authenticated role-bound offer/quote exchange, and GUI approval/settlement through
the same trade driver. The XMZ application fee policy, additional assets and
release/platform/security checks remain required by the original scope.
Live execution gates remain disabled.

## Historical addendum 2026-09-13: step 1 code inventory and wiring gap

This addendum was written by a Claude session with file read/edit access to
this repository but **no shell/build access on this machine** — it could not
compile, run, or test anything below. Treat every claim here as a reading of
the source, not executed evidence. It exists to make the next build-capable
session's job concrete, not to claim step 1 progress.

`src/dex/SwapWalletRpcProcess.{h,cpp}` and `src/dex/SwapWalletSession.{h,cpp}`
now exist (added after the "Evidence checked" section above was written) and
already implement most of what step 1 asks for at the class level:

- `SwapWalletRpcProcess` is described in its own header comment as "One-shot
  owner of a fresh or cleanly stopped separate service. Does not open/create a
  wallet, attest readiness, sign, transfer, or share the active GUI wallet's
  files." It exposes `startFreshWithDigest(program, expectedSha256,
  privateParent, daemonPort, walletPort)` — i.e. it already takes a pinned
  executable digest and a private data directory, which is exactly the
  "verified helper... bind executable provenance... private data directory"
  requirement in step 1.
- `SwapWalletSession` wraps that process and is explicitly documented as "not
  trade authority... No wallet password is retained here." It has
  `pinHelperDigest`, `create`, `open`, `close`, `cancelPendingOperation`, and a
  `state()` machine (`Closed/Open/Busy/RecoveryRequired/Stopped`).
- `DexEngine::attachSwapWalletSession(SwapWalletSession*)` already exists and
  is explicitly a "Read-only presentation attachment, native startup only.
  Does not take ownership, launch RPC, unlock a wallet, or enable trading." It
  wires `stateChanged`/`previewChanged` into the QML-visible
  `nativeWalletStatus`/`nativeFeePreview` properties.
- `signLocalOffer`, `publishOffer`, `validateOffer`, and `matchOffers` in
  `DexEngine.cpp` still hard-`return false`. That gate is untouched by any of
  the above and must stay untouched by this step.

**The gap is exactly one level up.** `src/main/main.cpp` (still last modified
in August, i.e. before any of the classes above existed) only does this,
under the existing `WITH_DEX_PROTOTYPE` build flag and the existing
`MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE` runtime
opt-in (around line 492–500):

```cpp
#ifdef WITH_DEX_PROTOTYPE
DexEngine dexEngine;
engine.rootContext()->setContextProperty("dexEngine", &dexEngine);
#endif
```

It never constructs a `SwapWalletRpcProcess`/`SwapWalletSession` and never
calls `attachSwapWalletSession`, so today the DEX page's native wallet status
is always `"unattached"` no matter how the classes above have matured.

Next concrete slice (still just step 1 — provisioning, not a trade driver):
inside that same `#ifdef WITH_DEX_PROTOTYPE` block, only when
`dexPrototypeEnabled` is true, construct a `SwapWalletRpcProcess` pointed at a
**disposable, non-default data directory** (never the user's real
`moneroAccountsDir`/`legacyMoneroAccountsDir`), start it with
`startFreshWithDigest` against a helper path/digest read from trusted local
config (not QML/offer input, per the existing rule), wrap it in a
`SwapWalletSession`, and call `dexEngine.attachSwapWalletSession(&session)`.
Both objects need a lifetime that safely outlives `dexEngine`'s use of them
and is cleanly torn down (including the helper process and its login file) on
every application exit path, not just the happy one.

Before this can be called done, per the acceptance rule above, it needs
actually-executed evidence on a machine that can build Qt/the daemon: the
prototype flag off leaves current behavior byte-identical; the flag on with no
verified helper present fails startup closed rather than silently degrading;
a normal quit *and* a forced/killed exit both stop the helper process and
leave no login/credential file behind; and the session never touches the
user's real wallet directory. None of that has been executed yet.
