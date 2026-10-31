# Claude cold-boot review, 18 October

Actual Claude CLI completed a read-only review. Full output is retained at
core/build/codex-claude-coldboot-review-20261018.txt. The review covered an earlier
snapshot; subsequent implementation must be assessed separately.

Findings and disposition:

- SSH exception mismatch: the new LabVmControl adapter maps exit 255 to
  ConnectionError and subprocess timeouts to TimeoutError. Its boot parser
  validates UUID syntax. Controller retry tests cover these adapter exceptions.
- Orphan app with dead group leader: fixed by checking the recorded app identity
  as an alternate group member. Unknown live groups fail closed. Real orphan
  process regression test passes.
- Cleanup mutation after failed wallet inspection: fixed by registering cleanup
  only after successful inspection and an accepted attach/launch decision.
  Regression confirms no cleanup handles or extra remote calls on probe failure.
- Launch configuration binding: FIXED in new records/checkpoints. Binary path
  and SHA-256, daemon port and network stage are persisted and verified before
  inspection, launch and explicit restart. Old unbound records fail closed.
  53 native-service tests pass; actual native RPC resume/reattach/checkpoint smoke
  passed with synthetic old-boot records and verified cleanup. Evidence:
  core/build/codex-native-bound-resume-20261018.json.
- Mixed service boots after a second interruption: FIXED at the wallet-service
  layer. Old-boot records may name the retained original boot directly or via
  resumedFromBootId, with unchanged launch bindings. Current-boot services still
  require live inspection or explicit exited-service restart. 55 native tests
  passed; actual RPC mixed-boot synthetic smoke passed with cleanup. Evidence:
  core/build/codex-native-mixed-boot-resume-20261018.json. Full driver phase recovery
  and real repeated power-loss acceptance remain open.
- Python recipient password-bearing RPC: FIXED. A pinned HTTP connection is
  checked against the recorded boot/PID/starttime and the server socket inode
  held by that process. The initial digest challenge contains no wallet password;
  automatic reconnect is disabled. Tests reject an impostor listener and a server
  that closes after the challenge without sending the password. Actual native
  wallet creation/reopening passed with owned connections and cleanup (evidence:
  core/build/codex-native-owned-password-rpc-20261018.json). Other helper/client
  implementations still require their own custody review.
- Inspection/checkpoint path/process checks: additional hardening remains.
- Attached kit missing passphrase fallback: FIXED. Attached workspaces and any
  existing identity refuse a missing passphrase. Private-file checks reject
  symlinks, shared-readable secrets and empty passwords; fresh identity creation
  retains generation support. Five focused tests plus sixteen workspace tests pass.
- Durable pre-power-off checkpoint and separate power-loss reopen path have now
  been implemented; actual end-to-end power-loss verification remains unrun.
- Phase journal, complete retained-state coverage, stale-login startup, unclean
  wallet durability and partial-resume cleanup semantics remain acceptance work.

No roadmap completion or beta-readiness claim follows from this review.

## Claude follow-up triage (18 October, reviewed HEAD 4e4d910d9)

Read-only review evidence: core/build/codex-claude-coldboot-followup-20261018.txt
and ~/.claude/plans/read-only-follow-up-review-of-glimmering-music.md.

- All Python native-wallet RPC calls now use the same pinned, process-owned
  connection, including payout-address discovery, balances, transfer observations
  and readiness/checkpoint probes. The impostor regression checks every incoming
  body, not just authenticated bodies. 67 native-wallet/resume/checkpoint tests
  passed after this change. Actual native create/address/balance/checkpoint and
  cleanup evidence: core/build/codex-native-all-owned-rpc-20261018.json.
- Native-wallet snapshots now require recipient/wallets/payout.keys specifically.
  A surviving cache or another role's keys cannot satisfy that requirement. The
  regression leaves both behind and checks rejection; 14 checkpoint/coordinator
  tests passed. This detects missing files, not semantic wallet corruption.
- Still open: bind the running executable (not only its on-disk path/hash), lock
  service checkpoint inspection, and validate complete live commands consistently.
- Still open: load and continue durable coordinator checkpoints after host failure
  or another guest power loss; current phase records are write-only evidence.
- Still open: journal the concrete start unit, checkpoint digest, verified bindings,
  wallet-resume intent, completion/failure and partial-service recovery outcomes.
- Still open: recheck Bob's signed idle boundary immediately before power-off,
  compare workspace/controller guest identity and verify the full forwarding set.
- Still open: recovery from VM inspection failures/aborted starts, helper executable
  bindings, stale-login and unclean-wallet durability, and remaining adversarial
  orchestration tests identified in Claude's full review.
- Exclusive VM leasing and native-wallet/configuration/TLS snapshot coverage were
  added after Claude's reviewed revision; these need review of the newer revision.

The live swap remains on the unchanged frozen worktree. No actual guest power-off
acceptance run has occurred, and none of these changes closes the beta gate.

### Running-image binding and checkpoint locking

Launch readiness, resumed-service inspection and service checkpoints now hash
`/proc/PID/exe` and compare it to the retained executable hash, with boot,
starttime and owner checks before/after hashing. The on-disk path still has its
separate launch binding. Real-process tests replace the executable pathname while
it runs: the original mapped image still passes its original hash, while the new
on-disk hash is rejected. Missing binding and changed boot/starttime also reject.
Native wallet checkpoints acquire all three role-directory locks in fixed order
before reading any service record, and retain them until the snapshot completes.

The 62 executable/native-wallet/resume tests passed. Actual disposable native RPC
launch, wallet creation/address/balance, all-three-role checkpoint, native-file
snapshot and cleanup passed: core/build/codex-native-executable-check-20261018.json.
This does not prove unclean power-loss durability. Full command validation during
checkpoint, parent-path hardening and coordinator restart remain open.
The six focused executable tests also pass with the added contention regression:
a held recipient lock prevents checkpoint inspection of any role's records.

### Checkpoint command and final power-off boundary

Launch and inspection now share one command builder; service checkpoints also
compare the complete live command to the retained binary, network, daemon port,
RPC port and wallet/ringdb/log paths. The 31 resume/executable tests and actual
native-wallet checkpoint smoke passed after this change.

The power-cycle coordinator now supplies a final guard invoked after durable
power-off intent and immediately before the hard-off action. It rechecks the
exclusive lease and both idle roles, and requires Bob's operation still be sign
with exactly the retained funding boundary. Changed operation or retained txid
reject. Seventeen coordinator/power-cycle tests passed, including guard failure
preventing power actions and intent/guard/action ordering. This remains a test
fixture boundary check, not end-to-end power-loss acceptance or a general
concurrency freeze protocol. Coordinator checkpoint loading and phase continuation
remain open.

### Durable evidence reader

Added read-only private JSON and phase-journal loaders as the first part of
coordinator continuation. They reject links, nonprivate/nonowned files, oversized
or partial records, ambiguous duplicate JSON keys, nonfinite values, filename/body
mismatches and sequence gaps/duplicates. Journal reads use an open directory fd
and detect changes in the directory listing. Unknown phases remain explicit for
coordinator validation rather than being discarded. No read result grants
permission to repeat a VM power action.

Twenty-three reader/checkpoint tests pass, including round-trip byte preservation
and interrupted/corrupted evidence. The coordinator does not yet load and continue
from these records; wiring the phase-aware continuation policy remains required.
This is neither an active-trade backup nor a completed recovery milestone.

### Durable VM start identity

The controller now allocates its unique headless systemd unit before the
start-requested journal record. The power-cycle sequence persists that identity
and designated VM UUID before issuing start; journal failure prevents start.
A reconstructed controller can retain that uncertain start intent, reject a
second start, and inspect exactly the recorded unit through systemctl show.
Invalid/foreign VM or unit identities reject without any command. Missing or
incomplete observations do not imply retry permission.

Twenty-eight VM-control/power-sequencing/coordinator tests pass, including stable
preallocated identity, durable intent before start, failed intent writes and
observation-only handling of retained attempts. The phase-aware coordinator must
still wire these primitives into checkpoint continuation and reconciliation.
No actual VM power action was performed for these tests.

### Phase-aware power-loss observation

New checkpoints begin their journal with a canonical checkpoint SHA-256 binding.
The read-only inspect_power_loss_recovery entry point holds the caller to an
exclusive VM lease, loads the private checkpoint/journal, validates the supported
phase prefix and old-boot bindings, retains a recorded start attempt, and compares
VM identity/state/boot plus the start unit with durable evidence. It issues no
power-off or start commands. Unknown phases, swapped checkpoints, contradictory
old boots, unrecorded restarts and later reboots reject. Observation timeouts
propagate without repeating mutations. Older journals without checkpoint binding
are deliberately not accepted by this continuation path.

Fifty-two observation/coordinator/VM-control/journal-reader tests pass. Integration
into wallet-service/app continuation is still open; no real power-loss acceptance
claim follows. A new guest observation alone does not authorize reopening state,
restoring an approval, signing or broadcasting.

### Continuation before wallet-service restoration

Added continue_bob_after_power_loss for a surviving host fixture/Alice when the
coordinator was interrupted after VM start and before wallet restoration. New
checkpoints retain the funding boundary, prior Alice reconnect count and native
configuration. Continuation checks the bound phase history and unchanged
checkpoint, all three native launch bindings, and the actual workspace SSH boot.
It attaches the journal without overwriting prior entries, records a missing
guest-ready event, then uses the same immutable/light/native snapshot checks,
forwarding, native resume and app reopen path as uninterrupted power recovery.

Journal appends now serialize on the journal directory and reject stale writers.
Sixty-eight reader/checkpoint/observation/coordinator/VM-control tests pass,
including wrong guest, changed checkpoint, late phase rejection and lost
ready-event continuation. These are orchestration tests; actual guest power-loss
acceptance remains unrun. The full host-driver reconstruction, partial wallet
restoration, post-app-reopen interruption and repeated guest power loss are still
open. The new entry point deliberately rejects those unsupported phases.

### Service/app mutation intent and partial results

The journal now records verified bindings, wallet-services-requested before any
native resume operation, and app-reopen-requested before reopening Bob. A journal
write failure prevents the corresponding next action. Accepted native service
handles are copied back to the caller in a finally block even when a later role
fails; partial work is no longer hidden in a temporary coordinator object.

Seventy-one affected tests pass, including wallet/app intent-write failures and
an injected observer failure after an accepted claim service. This preserves
state and evidence for partial-resume handling; it does not yet implement retry or
reconciliation of all partial-service/app phases. The observation phase grammar
has been updated for the new records. Pre-change post-guest journal shapes reject
rather than being silently interpreted as the new sequence.

### Original host-trader identity

Checkpoints now retain Alice's host PID, /proc starttime and host boot ID. A live,
owned process is required when captured. The original identity is checked at the
final power-off guard, before continuation observations and before restoration.
A newly supplied resident Alice cannot satisfy the unchanged-host-trader claim
merely by being alive. Old checkpoints missing this binding reject continuation.

Twenty-eight local-process/coordinator/observation tests pass, including real
process liveness and changed PID/starttime/boot bindings, and a coordinator test
that rejects a replaced host trader before forwarding or app reopen. Full host
application restart/reconstruction remains outside this surviving-Alice path.

### Coordinator ordering and failed-restoration evidence

The orchestration regression now asserts the complete cross-component call order,
rather than merely checking that tunnel and reopen calls occurred. It proves
checkpoint persistence precedes the power cycle and that both retained snapshots
are checked before forwarding/native resume/app reopen. Additional regressions
reject changed light/native snapshots before all restoration calls and ensure a
failed app reopen never records app-reopened or merges a successful result.
Thirty-one coordinator/host-identity/observation tests pass.
The current combined 17-module recovery suite passes all 239 tests in 30.975s.
Evidence: core/build/codex-coldboot-integrated-current-20261018.log. This covers
workspace/attachment, light/native services, process/kit identity, checkpoints,
journals, coordinator observation/continuation and VM control/lease sequencing;
it does not replace the still-unrun real VM power-loss acceptance trade.

### Continuation through verified bindings, tunnels and partial wallet startup

Continuation now supports bindings-verified, tunnels-restored,
wallet-services-requested and wallet-services-restored phases. Every attempt
rechecks unchanged light/native wallet snapshots before any restoration, rebuilds
owned forwarding, and reconciles native roles using their retained boot/port/
executable/command bindings. Completed journal entries are not duplicated.
App-reopen-requested remains unsupported because Bob may already have reopened.

Claude's focused review completed (core/build/codex-claude-coldboot-continuation-
20261018.txt; ~/.claude/plans/read-only-focused-review-of-tranquil-perlis.md).
Its unrecorded-service finding is addressed: before any previously retained wallet
resume intent, every role must report needs-resume and restart_exited is false.
Current-boot running/exited services are rejected without adoption, restart or
cleanup registration. Only a previously persisted wallet-services-requested (or
completed) phase permits reattachment/explicit restart reconciliation.

Remaining review findings to fix: atomic publication of checkpoint/phase files;
require a live, correctly bound start unit for accepting a new guest; compare the
journal at attach to the observation snapshot; durable exclusion of a second
recovery run under another checkpoint path; full forwarding/boot/lease checks;
pre-power-off wallet snapshot consistency; observation-error handling; stale
forwarder removal; and real-file-to-continuation acceptance tests. Host restart,
repeated guest reboot, uncertain power action continuation and app-reopen
interruption remain unfinished. No real VM power action was performed here.
Seventy-five coordinator/native-resume/observation/journal tests pass after these
changes, including retained-phase continuation and unrecorded service rejection.

### Atomic checkpoint and phase publication

Addressed Claude's torn-record finding: records are written and fsynced under a
private pending filename, then published with Linux renameat2(RENAME_NOREPLACE)
and the parent directory is fsynced. Existing evidence cannot be overwritten.
Only strictly named private regular pending files are ignored by phase reading;
they never count as a saved action intent. Torn published legacy records still
reject rather than inventing intent. Sixty-two affected tests pass, including
failed file fsync with no final record, no replacement, and interrupted temporary
files that do not prevent reading/continuing the last complete journal prefix.

The frozen VM-wallet public-Litecoin-testnet success run is now terminal/passed:
core/build/codex-vm-light-testnet-success-20261018.json and -status.json. Both
payouts, exact balances/fees, VM-local custody, pending funding retry, cleanup and
unchanged test sources passed (6291 seconds). The public roadmap records this
milestone, explicitly retaining private XMZ testnet/shared-node limitations and
open power-loss, backup, refund/recovery and release gates.

### Recorded VM start process verification

New-guest observations now require the recorded systemd unit to be loaded, active
and running with a nonzero MainPID. That process must be owned by this user,
non-zombie, mapped to /usr/lib/virtualbox/VBoxHeadless, and have the exact designated
VM UUID/VRDE-off command. Starttime and unit properties are rechecked after reading
process evidence. A foreign/inactive unit cannot authorize guest continuation.
The start command no longer uses --collect, retaining exited-unit observations
instead of deliberately discarding them. This does not turn an absent unit into
permission to retry an uncertain action.

Fifty-eight VM-control/coordinator/observation tests pass. An additional focused
27-test run includes filesystem-backed process fixtures for exact identity,
wrong VM, replaced executable, zombie state and changed unit PID. No VM start or
power-off was performed. The real leased acceptance run remains outstanding.

### Observation-to-attachment journal binding

Recovery observation now returns a canonical SHA-256 of the validated journal
entries. Continuation requires that digest when attaching, rejecting any appended
or altered history since observation. Journal objects retain a history digest and
check it as well as sequence length under the append lock; replacing an existing
entry without changing the count no longer passes stale-writer detection.

Sixty-seven reader/checkpoint/observation/coordinator tests pass, including changed
history before attach, same-length changes before append, matching-history append,
and coordinator rejection before forwarding or app reopening. The caller's
exclusive VM lease remains required; full multi-process recovery-run ownership
and real-file-to-continuation integration acceptance remain outstanding.

### Real-file continuation integration

A new filesystem integration fixture writes actual atomic checkpoint and phase
files, uses the real phase observer/reader/attachment/coordinator, verifies an
actual private configuration through VERIFY_EXISTING, hashes actual native-wallet
fixture files and checks the complete appended phase order. A live disposable
host process supplies Alice's real boot/PID/starttime binding. Changed config or
native keys prevent forwarding/reopen. A second continuation after a failed
binding check consumes the saved guest-ready record without duplicating it.

Fifty-nine affected tests passed before adding the retry-after-failed-check case;
all four focused file-integration cases then passed. Hypervisor controls, tunnel
creation, light-wallet snapshot and wallet/app restoration remain doubles in this
fixture. It closes the file-to-coordinator test gap, not real guest power-loss,
wallet durability or settlement acceptance.

### Restore binding validation before power or forwarding

A shared validator now checks all three native service records before checkpoint
publication and before continuation: exact record shape, retained old-boot match,
valid distinct RPC ports, and launch bindings matching the supplied native-wallet
configuration. Forward ports must be a nonempty list of distinct integer ports
(no booleans, zero or out-of-range values) and include the host native daemon.
VM-owned wallet RPCs do not themselves require host forwards. Full binding of the
Litecoin/Electrum/peer endpoints to the retained app configuration remains open.

Forty-three coordinator/file-integration/observation tests pass. Wrong service
boots and missing daemon forwards reject before tunnel restoration; malformed
forward lists reject before checkpoint persistence or power-cycle invocation.

### Final snapshots and ongoing lease checks

The last pre-power-off guard now re-captures immutable bindings, the light-wallet
snapshot and native-wallet files against the durable checkpoint before its final
idle/signed boundary check. A change rejects before the power action. Restoration
rechecks the exclusive lease before forwarding, native service restoration and app
reopening; the ping callback used throughout app recovery also rechecks the lease.

Forty-nine coordinator/file-integration/power-sequencing tests pass, including
changed final snapshots, lease loss after native restore preventing app reopen,
and a recovery ping refusing a lost lease. This reduces the gap between initial
snapshot and power-off; it is not a general multi-writer filesystem freeze or an
unclean-wallet durability proof. Real power-loss acceptance remains outstanding.

### Persistent designated-VM recovery ownership

LabVmLease now uses a persistent private recovery registry under
~/.local/state/monzero-dexlab-recovery (isolated directories in tests). Initial
recovery claims the checkpoint path and canonical digest before any power action.
An unfinished record blocks another checkpoint/run and shared-lease VM trades,
even after the original process exits. Continuation requires the exact retained
owner. Successful app reopen records an immutable completion marker before the
active record is removed and directory-fsynced; completed checkpoints cannot be
used to initiate another power cycle. New registry ancestor entries are fsynced,
and symlink ancestors/records reject. The global VM lease serializes operations.

Forty-seven registry/lease/coordinator/file-integration tests pass, covering lease
release/reacquisition, another filename, matching-only completion, shared-lease
refusal, linked records, registry failure blocking power actions and completion
ordering. No production recovery claim or VM power action was made. Recovery of
an interruption during app reopening/completion-marker publication remains open;
this change preserves and blocks on that unfinished state rather than bypassing it.

### Pre-acceptance failure paths (Claude review of fb624e592)

Actual read-only review: core/build/codex-claude-coldboot-preacceptance-review-20261018.txt.
A proposed reconnect-counter change was reverted after checking the C++ state
machine: the counter advances when the session is rebuilt, before connection;
`wait-reconnected` also requires the new peer negotiation to complete.

Pre-command failures now publish terminal abandonment evidence before releasing
the persistent claim. Issuing hard-off marks the outcome uncertain before the
command runs; command failures retain ownership. Abandoned checkpoints cannot
be reused. The last guard rechecks the guest boot. An explicit lab-only quarantine
function preserves failed-run files, requires the original host process dead,
records a stable VM observation and journal digest, and permanently disallows
reusing that checkpoint. It never powers, resumes, signs or broadcasts. It does
not run automatically during cleanup.

Retirement-only observation does not claim a verified running start executable.
This host's setuid VBoxHeadless makes /proc/PID/exe unreadable. Normal continuation
still requires that proof and fails closed; this portability issue remains open.
Quarantine only releases the cooperative lab claim after a failed test, and is
not a trade recovery mechanism. Unknown/transitioning VM observations reject.

Power-loss acceptance now requires --keep and an explicit storage parent, and
rejects tmpfs/ramfs. The configured core/build parent is btrfs. The default registry
uses the operating-system user's home rather than the HOME environment variable.
The 265-check recovery suite passed before adding the retirement-only observation
regression. Actual guest power-loss acceptance remains unrun.

### Follow-up retirement review

Actual Claude read-only review of f22b5938d completed at
core/build/codex-claude-coldboot-abandonment-review-20261018.txt. It found no blocking
power-cycle safety issue, but identified retirement gaps. The follow-up now:

- reconciles a terminal abandonment journal whose registry release was interrupted;
- refuses retirement while a same-user VBoxManage command targets this VM by UUID
  or name, or a retained start attempt remains active/unresolved while VM is off;
- requires confirmed poweroff for retirement from wallet-service restoration onward,
  since guest process quiescence has not been established by this entry point;
- preserves an existing matching quarantine marker across retries even if the
  observation changes; every retirement observation is explicitly labelled;
- accepts only recognised persistent filesystem types for acceptance storage.

91 affected tests pass, including unresolved starts, pending commands, live guest
services, abandoned-journal reconciliation, marker retry, dead-process PID reuse,
prior boot, zombie and unreadable /proc checks. Physical storage durability below
the filesystem layer is not inferred from its type. Unexpected VM transitions
still fail closed rather than being silently retired. Older uncooperative drivers
remain an operational limitation. A process scan cannot establish quiescence of
arbitrary unrelated future launchers.

The running acceptance checkout remains frozen at d0fa85b3f; these later changes
have not been injected into it. A successful uninterrupted coordinator power cycle
would prove that specific trade-recovery case, not host-driver reconstruction or
retirement as a substitute for recovering a failed trade.

### Actual signed-but-unsent cold-boot acceptance passed

The first real designated-VM hard power-off case passed through complete swap
settlement and cleanup in 839 seconds (836-second settlement duration). Frozen
source: gui-wt-powerloss-acceptance d0fa85b3fe2cd45795a016d07e180dc7fcb93fdd.
Report/status/provenance/log: core/build/codex-vm-powerloss-signed-20261018*.
Workspace: core/build/ltc-xmz-live-regtest-8oohhw74; checkpoint and eleven durable
phase records retained. Swap b3ae2a2b-6e6a-4cdd-8432-97557b9f0c6f.

Old guest boot db9c63ae-8b22-4f13-b2cb-97ad6f546329 changed to
915ae9a8-ba64-4bc4-973c-c6c6a6255861 after confirmed hard-off. Alice's original
process survived; the reopened Bob retained six light-wallet files and eight
protected metadata files unchanged, and native wallet snapshots passed. Old
approval was absent and rejected; fresh approval relayed the original signed txid.
Both payouts confirmed. Litecoin: 100000000 locked, 99990000 redeemed, 10000
redemption fee, 1000 funding fee, 4999000 remaining from 105000000 funded. XMZ:
100000000000 payout + 99042560000 reserve return + 957440000 fee = 200000000000
locked atomic units. Cleanup passed; Electrum errors empty; source hashes unchanged.
The funding transaction was included before retry, so this run does NOT prove an
actual pending retry. VM identities and both wallet types remained VM-owned.

Scope: isolated regtest chains and one signed-but-unsent power loss with surviving
host coordinator. Does not prove host restart, repeated interruptions, portable
continuation, public-network power loss, active-trade backup, GUI journey or beta
readiness. Later retirement fixes in 0091d81bb were not injected into this run.
