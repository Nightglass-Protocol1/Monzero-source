"""Signed-but-unsent designated-VM power-loss acceptance path (test fixtures)."""
import hashlib
import json
import pathlib
from remote_light_recovery import _parts, _idle_states, idle_boundary, snapshot, _resume, require
from remote_recovery_checkpoint import capture_bindings, persist_checkpoint, RecoveryPhaseJournal, capture_native_wallets, read_private_json, read_phase_journal, phase_journal_digest
from remote_trade_workspace import VERIFY_EXISTING, ForwardProcess
from remote_native_wallets import RemoteNativeWallets
from vm_lab_control import LabVmControl
from vm_power_loss import power_cycle
from local_process_identity import local_process_identity, require_local_process


def restart_bob_after_power_loss(resident, workspace, light_wallet, native_wallets,
        checkpoint_path, forward_ports, function_timeout_ms=3600000, controller=None, vm_lease=None):
    """Caller owns the VM exclusively; only the unsigned-relay boundary is tested.

    A separate running trade must finish before invoking this path. No mainnet
    stage is accepted. No signing, relay or approval is performed here.
    """
    require(vm_lease is not None, 'Exclusive VM lease is missing')
    vm_lease.require_exclusive()
    require(native_wallets.stage in ('regtest','testnet'), 'Power-loss fixture requires a test chain')
    alice,bob,ping=_parts(resident,light_wallet)
    states=_idle_states(alice,bob,ping,'Power loss requires both roles idle')
    before=idle_boundary(states['bob']['lightFunding'])
    require(states['bob']['lightFunding']['operation']=='sign','Power-loss case requires signed but unsent funding')
    services=native_wallets.recovery_checkpoint()
    old_boot=services['claim']['bootId']
    saved=snapshot(workspace,light_wallet,before['tradeId'])
    names=['policy.json','peer-card.json','session.json','kit-terms.json','start-quote.json',
        'local.json','kit-ltc.sig','kit-ltc/identity.bin','kit-ltc/identity.passphrase',
        'kit-ltc/card.json','kit-ltc/tls.crt','kit-ltc/tls.key',
        'native-wallets/wallet-password','native-wallets/recipient-password']
    names += ['app/'+n for n in ('app.json','journal.json','peer.json','payment.json',
        'payment-rpc.json','reservation.json','reservation-rpc.json','redeem.json','redeem-rpc.json','ca.pem')]
    if native_wallets.stage=='testnet':names.append('control.json')
    for path in (light_wallet.setup['launcher'],light_wallet.setup['configurationFile'],light_wallet.tls_root):
        relative=pathlib.PurePosixPath(path).relative_to(workspace.root)
        require('..' not in relative.parts,'Light configuration path escapes workspace')
        names.append(str(relative))
    bindings=capture_bindings(workspace,names)
    native_saved=capture_native_wallets(workspace)
    checkpoint={'schema':'monzero-vm-powerloss-checkpoint-v1','tradeId':before['tradeId'],
        'retainedTxid':before['retainedTxid'],'services':services,'bindings':bindings,
        'before':before,'aliceProcess':local_process_identity(alice.process),'aliceReconnects':states['alice']['reconnects'],'nativeConfig':native_wallets.config(),
        'saved':saved,'nativeWallets':native_saved,'forwardPorts':list(forward_ports),'oldBootId':old_boot}
    validate_restore_bindings(checkpoint,native_wallets)
    persist_checkpoint(checkpoint_path,checkpoint)
    journal=RecoveryPhaseJournal(str(checkpoint_path)+".phases")
    journal.record('checkpoint-bound', {'sha256': checkpoint_digest(checkpoint)})
    vm_lease.begin_recovery(checkpoint_path,checkpoint_digest(checkpoint))
    issued=False
    def issue_hard_off():
        nonlocal issued
        issued=True  # An exception from the command leaves its outcome uncertain.
        return control.hard_off()
    try:
        control=controller or LabVmControl()
        vm_lease.require_exclusive()
        def recheck_signed_boundary():
            vm_lease.require_exclusive()
            require_local_process(alice.process,checkpoint['aliceProcess'])
            require(capture_bindings(workspace,names)==bindings,'Recovery bindings changed before power-off')
            require(snapshot(workspace,light_wallet,before['tradeId'])==saved,'Light wallet changed before power-off')
            require(capture_native_wallets(workspace)==native_saved,'Native wallet changed before power-off')
            latest=_idle_states(alice,bob,ping,'Power loss requires both roles still idle')
            funding=latest['bob']['lightFunding']
            require(funding['operation']=='sign' and idle_boundary(funding)==before,
                'Signed funding boundary changed before power-off')
            require(control.read_boot()==old_boot,'Guest boot changed before power-off')
        cycle=power_cycle(inspect_vm=control.inspect,read_boot=control.read_boot,
            power_off=issue_hard_off,start_vm=control.start,ping_alice=ping,expected_boot=old_boot,record_phase=journal.record,before_power_off=recheck_signed_boundary,prepare_start=control.prepare_start)
    except BaseException as error:
        if not issued:
            # Publish terminal evidence before releasing persistent ownership.
            journal.record('power-off-abandoned', {'errorClass':type(error).__name__})
            vm_lease.abandon_unissued_recovery(checkpoint_path,checkpoint_digest(checkpoint))
        raise
    return _restore_after_guest(resident,workspace,light_wallet,native_wallets,
        checkpoint_path,checkpoint,journal,cycle,ping,bob,function_timeout_ms,vm_lease=vm_lease)


def _restore_after_guest(resident,workspace,light_wallet,native_wallets,
        checkpoint_path,checkpoint,journal,cycle,ping,bob,function_timeout_ms,*,vm_lease,restored_phase='guest-ready'):
    original_ping=ping
    def ping():
        vm_lease.require_exclusive()
        return original_ping()
    vm_lease.require_exclusive()
    phases=('guest-ready','bindings-verified','tunnels-restored','wallet-services-requested','wallet-services-restored')
    require(restored_phase in phases,'Unsupported restoration phase')
    completed=phases.index(restored_phase)
    require_local_process(resident.parts['alice'].process,checkpoint['aliceProcess'])
    before=checkpoint['before'];saved=checkpoint['saved'];bindings=checkpoint['bindings']
    native_saved=checkpoint['nativeWallets'];services=checkpoint['services']
    forward_ports=checkpoint['forwardPorts']
    require(workspace.run(['cat','/proc/sys/kernel/random/boot_id']).decode().strip()==cycle['newBootId'],
        'Workspace SSH reached a different guest boot')
    # Verify before opening any recovered service or app that could mutate data.
    require(workspace.run(['python3','-I','-c',VERIFY_EXISTING,str(workspace.root)],
        json.dumps(bindings).encode())==b'verified\n','Immutable recovery bindings changed')
    require(snapshot(workspace,light_wallet,before['tradeId'])==saved,
        'Power loss changed retained wallet or protected trade state')
    require(capture_native_wallets(workspace)==native_saved,
        'Power loss changed native wallet files')
    if restored_phase=='guest-ready':
        journal.record('bindings-verified', {'newBootId':cycle['newBootId']})
    vm_lease.require_exclusive()
    for process in list(workspace.processes):
        if isinstance(process,ForwardProcess):process.terminate()
    workspace.forward(forward_ports)
    if completed<phases.index('tunnels-restored'):
        journal.record('tunnels-restored', {})
    previous=checkpoint['nativeConfig']
    recovered=RemoteNativeWallets(workspace,native_wallets.binary,native_wallets.daemon_port,native_wallets.stage)
    if completed<phases.index('wallet-services-requested'):
        journal.record('wallet-services-requested', {'roles':['claim','observer','recipient']})
    vm_lease.require_exclusive()
    prior_intent=completed>=phases.index('wallet-services-requested')
    try:
        require(recovered.resume_after_boot(services,restart_exited=prior_intent,require_unstarted=not prior_intent)==previous,'Recovered native service configuration changed')
    finally:
        # Keep each accepted service reachable even if a later role fails.
        native_wallets.services=recovered.services
    if completed<phases.index('wallet-services-restored'):
        journal.record('wallet-services-restored', {})
    vm_lease.require_exclusive()
    journal.record('app-reopen-requested', {})
    prepared,final=_resume(resident,workspace,light_wallet,bob,before,saved,ping,
        checkpoint['aliceReconnects'],function_timeout_ms)
    journal.record("app-reopened", {"generation":prepared["generation"]})
    resident._merge(True)
    vm_lease.complete_recovery(checkpoint_path,checkpoint_digest(checkpoint))
    return dict(cycle,tradeId=before['tradeId'],retainedTxid=before['retainedTxid'],
        generation=prepared['generation'],retainedFundingUnchanged=True,oldApprovalAbsent=True,
        alicePidUnchanged=True,aliceReconnectsAfter=final['reconnects'],
        walletFiles=saved['wallet'],protectedMetadata=saved['metadata'],
        checkpointFile=str(checkpoint_path),executable=False)


def checkpoint_digest(checkpoint):
    return hashlib.sha256(json.dumps(checkpoint, sort_keys=True,
        separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def inspect_power_loss_recovery(checkpoint_path, *, controller, vm_lease, ping_alice, retirement_only=False):
    """Reconcile durable intent with live observations without power mutations.

    Returned states are observations, not permission to relay, approve, or launch.
    The caller must separately verify the retained wallet/app files before reopen.
    """
    import uuid
    from vm_power_loss import VM_NAME, VM_UUID
    vm_lease.require_exclusive()
    checkpoint=read_private_json(checkpoint_path)
    require(checkpoint.get('schema')=='monzero-vm-powerloss-checkpoint-v1',
            'Unknown power-loss checkpoint schema')
    old_boot=checkpoint.get('oldBootId')
    require(isinstance(old_boot,str) and str(uuid.UUID(old_boot))==old_boot,
            'Invalid retained guest boot')
    entries=read_phase_journal(str(checkpoint_path)+'.phases')
    if entries and entries[-1]['phase']=='power-off-abandoned':
        require([e['phase'] for e in entries[:-1]] in
                (['checkpoint-bound'],['checkpoint-bound','power-off-requested']),
                'Abandonment follows a possible power action')
        require(entries[0]['evidence']=={'sha256':checkpoint_digest(checkpoint)},
                'Recovery journal belongs to another checkpoint')
        return {'phase':'power-off-abandoned','observation':'abandoned-before-power-action',
                'checkpointSha256':checkpoint_digest(checkpoint),
                'journalSha256':phase_journal_digest(entries),'retirementOnly':retirement_only}
    phases=('checkpoint-bound','power-off-requested','power-off-confirmed',
            'start-requested','guest-ready','bindings-verified','tunnels-restored',
            'wallet-services-requested','wallet-services-restored','app-reopen-requested','app-reopened')
    require(entries and len(entries)<=len(phases) and
            [entry['phase'] for entry in entries]==list(phases[:len(entries)]),
            'Recovery phases are incomplete or unsupported')
    require(entries[0]['evidence']=={'sha256':checkpoint_digest(checkpoint)},
            'Recovery journal belongs to another checkpoint')
    evidence={entry['phase']:entry['evidence'] for entry in entries}
    for phase in ('power-off-requested','power-off-confirmed','start-requested','guest-ready'):
        if phase in evidence:
            require(evidence[phase].get('oldBootId')==old_boot,'Recovery phase boot mismatch')
    if 'start-requested' in evidence:
        controller.retain_start_attempt(evidence['start-requested'])
    ping_alice()
    info=controller.inspect()
    require(info.get('name')==VM_NAME and info.get('uuid')==VM_UUID,
            'Recovery observed another VM')
    state=info.get('state')
    result={'phase':entries[-1]['phase'],'vmState':state,'oldBootId':old_boot,
            'checkpointSha256':checkpoint_digest(checkpoint),'journalSha256':phase_journal_digest(entries),
            'retirementOnly':retirement_only}
    if 'start-requested' in evidence:
        result['startAttempt']=controller.inspect_start_attempt()
    if state=='running':
        boot=controller.read_boot()
        require(isinstance(boot,str) and str(uuid.UUID(boot))==boot,'Invalid observed guest boot')
        result['observedBootId']=boot
        if boot==old_boot:
            require('power-off-confirmed' not in evidence,'Confirmed power-off contradicted by old boot')
            result['observation']='original-guest-running'
        else:
            require('start-requested' in evidence,'Unrecorded guest restart')
            if retirement_only:
                result['resumeEligible']=False
                result['startProcessVerified']=False
            else:
                result['startProcess']=controller.require_live_start_attempt(result['startAttempt'])
            if 'guest-ready' in evidence:
                require(evidence['guest-ready'].get('newBootId')==boot,'Guest rebooted again after recovery')
            result['observation']='new-guest-running'
    elif state=='poweroff':
        require('power-off-requested' in evidence,'Unrecorded guest power-off')
        result['observation']='start-outcome-unresolved' if 'start-requested' in evidence else 'power-off-observed'
    elif state in ('starting','stopping'):
        result['observation']='transition-in-progress'
    else:
        raise RuntimeError('Unexpected VM state during recovery observation')
    ping_alice()
    return result


def continue_bob_after_power_loss(resident,workspace,light_wallet,native_wallets,
        checkpoint_path,*,controller,vm_lease,function_timeout_ms=3600000):
    """Continue after guest boot, before service restoration, without VM actions.

    The original host Alice and resident fixture must still exist. Host application
    reconstruction and interruptions during app reopening remain separate work.
    """
    require(native_wallets.stage in ('regtest','testnet'),'Power-loss continuation requires a test chain')
    alice,bob,ping=_parts(resident,light_wallet)
    retained=read_private_json(checkpoint_path)
    vm_lease.require_recovery(checkpoint_path,checkpoint_digest(retained))
    require('aliceProcess' in retained,'Checkpoint lacks original host process identity')
    require_local_process(alice.process,retained['aliceProcess'])
    observed=inspect_power_loss_recovery(checkpoint_path,controller=controller,
        vm_lease=vm_lease,ping_alice=ping)
    require(observed['observation']=='new-guest-running' and
        observed['phase'] in ('start-requested','guest-ready','bindings-verified','tunnels-restored','wallet-services-requested','wallet-services-restored'),
        'Continuation requires a new guest before app reopening')
    checkpoint=read_private_json(checkpoint_path)
    require(checkpoint_digest(checkpoint)==observed['checkpointSha256'] and checkpoint==retained,'Checkpoint changed after observation')
    for field in ('before','saved','bindings','nativeWallets','services','forwardPorts','nativeConfig','aliceReconnects'):
        require(field in checkpoint,'Checkpoint lacks continuation data: '+field)
    require(checkpoint['before']['tradeId']==checkpoint['tradeId'] and
        checkpoint['before']['retainedTxid']==checkpoint['retainedTxid'],'Checkpoint funding identity mismatch')
    validate_restore_bindings(checkpoint,native_wallets)
    vm_lease.require_exclusive()
    journal=RecoveryPhaseJournal.attach(str(checkpoint_path)+'.phases',expected_digest=observed['journalSha256'])
    if observed['phase']=='start-requested':
        journal.record('guest-ready',{'oldBootId':observed['oldBootId'],'newBootId':observed['observedBootId']})
    cycle={'oldBootId':observed['oldBootId'],'newBootId':observed['observedBootId'],
           'powerOffConfirmed':True,'aliceStayedAlive':True}
    return _restore_after_guest(resident,workspace,light_wallet,native_wallets,
        checkpoint_path,checkpoint,journal,cycle,ping,bob,function_timeout_ms,
        vm_lease=vm_lease,restored_phase='guest-ready' if observed['phase']=='start-requested' else observed['phase'])


def validate_restore_bindings(checkpoint,native_wallets):
    services=checkpoint.get('services')
    require(isinstance(services,dict) and set(services)=={'claim','observer','recipient'},
        'Incomplete native service bindings')
    ports=[]
    for entry in services.values():
        require(isinstance(entry,dict) and set(entry)=={'bootId','port','launch'},
            'Malformed native service binding')
        require(entry['bootId']==checkpoint.get('oldBootId'),'Native service boot differs from checkpoint')
        require(type(entry['port']) is int and 0<entry['port']<65536,'Invalid native service port')
        ports.append(entry['port'])
        native_wallets.validate_launch_binding(entry['launch'])
    require(len(set(ports))==len(ports),'Native service ports collide')
    forwarded=checkpoint.get('forwardPorts')
    require(isinstance(forwarded,list) and forwarded and
        all(type(port) is int and 0<port<65536 for port in forwarded),
        'Invalid recovery forwarding set')
    require(len(set(forwarded))==len(forwarded),'Duplicate recovery forwarded ports')
    # Wallet RPCs live inside the VM; they do not require host forwarding.
    # The native daemon is host-owned and must be reachable after recovery.
    require(native_wallets.daemon_port in forwarded,'Recovery forwarding omits native daemon')


def quarantine_failed_lab_recovery(checkpoint_path, *, controller, vm_lease):
    """Explicit lab-only retirement; preserves files and never resumes or powers a VM."""
    from local_process_identity import require_local_process_dead
    vm_lease.require_exclusive()
    checkpoint=read_private_json(checkpoint_path)
    digest=checkpoint_digest(checkpoint)
    vm_lease.require_recovery(checkpoint_path,digest)
    services=checkpoint.get('services',{})
    stages={entry.get('launch',{}).get('stage') for entry in services.values()}
    require(set(services)=={'claim','observer','recipient'} and
            stages in ({'regtest'},{'testnet'}), 'Quarantine requires a test-chain checkpoint')
    def original_is_dead():require_local_process_dead(checkpoint['aliceProcess'])
    original_is_dead()
    observed=inspect_power_loss_recovery(checkpoint_path,controller=controller,
        vm_lease=vm_lease,ping_alice=original_is_dead,retirement_only=True)
    abandoned=observed['observation']=='abandoned-before-power-action'
    if not abandoned:
        require(observed.get('vmState') in ('running','poweroff'),
                'Cannot quarantine a transitioning or unobserved VM')
        controller.require_no_pending_power_commands()
        if observed['observation']=='start-outcome-unresolved':
            attempt=observed['startAttempt']
            require(attempt.get('ActiveState') in ('inactive','failed') and
                    attempt.get('MainPID')=='0', 'VM start action remains unresolved')
        late_phases=('wallet-services-requested','wallet-services-restored','app-reopen-requested','app-reopened')
        require(observed['phase'] not in late_phases or observed['vmState']=='poweroff',
                'Guest services may still be active; retirement requires confirmed poweroff')
    require(checkpoint_digest(read_private_json(checkpoint_path))==digest,
            'Checkpoint changed during quarantine')
    require(phase_journal_digest(read_phase_journal(str(checkpoint_path)+'.phases'))==observed['journalSha256'],
            'Journal changed during quarantine')
    vm_lease.require_recovery(checkpoint_path,digest)
    original_is_dead()
    if abandoned:
        vm_lease.registry.abandon_unissued(checkpoint_path,digest)
    else:
        vm_lease.registry.quarantine(checkpoint_path,digest,observed)
    return observed
