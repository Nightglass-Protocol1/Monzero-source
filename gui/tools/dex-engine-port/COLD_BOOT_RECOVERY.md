# Cold-boot fixture recovery — implementation in progress

This branch adds prerequisites for the designated mz-dexlab power-loss test.
It does not establish the power-loss acceptance gate and is not yet wired into
an end-to-end recovery driver.

`RemoteTradeWorkspace.attach_existing` verifies caller-retained SHA-256 bindings
without writing or regenerating files. The caller must stop writers before
verification and bind the complete immutable identity/configuration set. Reading
current remote hashes and immediately trusting them is not a recovery checkpoint.
Private bytes remain on the VM. Mutable journal reconciliation is separate.

`RemoteNativeWallets.resume_after_boot` accepts the retained boot ID and fixed
port for each role. Existing private wallet directories, password files and
process records are required. The previous boot must differ from the current
boot, and the recorded port must match. The launcher tests port availability,
then durably replaces the process record before releasing the child to exec.
A registered handle remains available if the launch reply is lost. RPC login
contents regenerate normally; wallet passwords are never regenerated.

An inspection path can reattach an already resumed service after a lost reply,
checking its original boot binding, current PID/starttime/group, exact command
and authenticated RPC. It never launches a duplicate. An exited current-boot service is classified separately. Passing
`restart_exited=True` permits a new launch only after the launcher rechecks its
old-boot binding and absence of the recorded live process under the directory
lock. A live process cannot be duplicated, and a reused PID is never signalled.

Verification: 65 focused tests across remote native wallets, resume, workspace
attachment and forwarding. Failure cases include changed/missing bindings,
unsafe files, same/wrong boot, changed/occupied port, missing password and record
persistence failure before execution. These tests use synthetic previous-boot
records and local stand-ins, not an actual reboot.

Remaining: retained checkpoint creation/completeness, driver recovery integration,
service and tunnel restoration, app reopen with fresh approval state, designated
VM hard power-off/new-boot verification while Alice stays running, retained-wallet
and journal reconciliation, settlement accounting and boot-aware cleanup. No
public milestone or release is justified by these prerequisites alone.

Actual native v1.0.0 wallet RPC smoke also passed: three services started,
stopped, resumed with synthetic previous-boot records, then reattached to the
same live processes. Ports and password hashes were unchanged; all owned
services were stopped afterward. No wallet was created and no real reboot
occurred. Evidence: core/build/codex-native-coldboot-resume-smoke-20261018.json.

Partial-service smoke with the actual native binary also passed: the observer
was stopped, then explicitly restarted; claim and recipient retained their PIDs.
Configuration and passwords were unchanged, and cleanup completed. Evidence:
core/build/codex-native-coldboot-partial-smoke-20261018.json.

`recovery_checkpoint()` now obtains current boot IDs and configured ports only
from live, authenticated RPC services. It rejects mixed boots, changed ports and
partial service sets. The actual native RPC checkpoint smoke passed with cleanup.
This service checkpoint is not an active-trade backup; the driver still must
persist complete immutable trade bindings and retained recovery evidence before
power-off. The signed-but-unsent branch of `light_lock` is the first intended
integration boundary; Alice must remain alive while Bob services/tunnels reopen.

App cleanup prerequisite completed: remote resident launch now persists
app.process.json with boot and process-start identities. signal_remote refuses
missing/unsafe records and never signals an old-boot or reused-PID group. Six
new tests and all twelve existing remote-fixture tests passed. Existing app.pid
format remains compatible with restart inspection; old fixture directories
without the new record cannot use the new signal operation.

Reopen integration can reuse remote_light_recovery._resume after restoring
services and tunnels: it retires the old local SSH supervisor, prepares a fresh
resident generation, reopens the same trade, checks Alice stayed alive, checks
retained funding fields and absence of old approval, and compares wallet and
protected metadata snapshots. The actual power-off orchestration and durable
pre-reboot checkpoint still need implementation.

VM adapter now has 13 controller/adapter tests, including transient VM-state
observation timeouts without repeated power mutations. Read-only inspection of
the actual live fixture identified immutable checkpoint inputs: root policy.json,
peer-card.json, session.json, kit-terms.json, start-quote.json, control.json,
local.json and kit-ltc.sig; kit-ltc identity.bin, identity.passphrase, card.json,
tls.crt and tls.key; app top-level configuration JSON and ca.pem. Native wallet
passwords must remain bound as well. Mutable app/meta, app/secrets and light
wallet data need separate retained-state/durability verification; treating only
the configuration list as an active-trade backup would be incomplete.
