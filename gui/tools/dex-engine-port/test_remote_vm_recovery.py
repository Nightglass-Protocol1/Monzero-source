import types
import pathlib,tempfile
from vm_lab_lease import LabVmLease
import unittest
from unittest.mock import Mock,patch
import remote_vm_recovery as r

class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.patches=[]
        def mock(name,**kw):
            p=patch.object(r,name,**kw);self.patches.append(p);return p.start()
        self.addCleanup(lambda:[p.stop() for p in reversed(self.patches)])
        mock('local_process_identity',return_value={'pid':123,'starttime':'456','bootId':'host'})
        self.host_check=mock('require_local_process')
        self.lease=Mock()
        self.resident=Mock();self.resident.parts={'alice':Mock()}
        self.workspace=Mock(root='/vm/trade',processes=[])
        self.workspace.run.side_effect=lambda args,*a,**kw: b'new\n' if args[0]=='cat' else b'verified\n'
        self.native=Mock(stage='testnet',binary='/wallet',daemon_port=1)
        self.native.recovery_checkpoint.return_value={role:{'bootId':'old','port':2+i,'launch':{'binary':'/wallet','daemonPort':1,'stage':'testnet','sha256':'a'*64}} for i,role in enumerate(('claim','observer','recipient'))}
        self.native.config.return_value={'fixed':'config'}
        self.light=Mock(setup={'launcher':'/vm/trade/light/launcher.json','configurationFile':'/vm/trade/light/config.json'},tls_root='/vm/trade/light/root.pem');self.controller=Mock();self.controller.read_boot.return_value='old'
        mock('_parts',return_value=(Mock(),Mock(),Mock()))
        mock('_idle_states',return_value={'alice':{'reconnects':1},'bob':{'lightFunding':{'operation':'sign'}}})
        mock('idle_boundary',return_value={'tradeId':'trade','retainedTxid':'tx'})
        mock('snapshot',return_value={'wallet':{},'metadata':{}})
        mock('capture_native_wallets',return_value={'count':2,'digest':'fixture'})
        mock('capture_bindings',return_value={'config':'hash'})
        self.persist=mock('persist_checkpoint')
        self.journal_factory=mock('RecoveryPhaseJournal')
        self.cycle=mock('power_cycle',return_value={'newBootId':'new'})
        recovered=Mock(services={});self.recovered=recovered;recovered.resume_after_boot.return_value=self.native.config.return_value
        mock('RemoteNativeWallets',return_value=recovered)
        self.resume=mock('_resume',return_value=({'generation':1},{'reconnects':2}))
    def call(self):
        return r.restart_bob_after_power_loss(self.resident,self.workspace,self.light,self.native,
            '/private/checkpoint',[1,2,3],controller=self.controller,vm_lease=self.lease)
    def test_unissued_failure_releases_real_registry_but_prevents_reuse(self):
        with tempfile.TemporaryDirectory() as folder:
            with LabVmLease(True,folder,lambda:[]) as lease:
                self.lease=lease
                self.cycle.side_effect=RuntimeError('initial boot mismatch')
                with self.assertRaisesRegex(RuntimeError,'initial boot mismatch'):self.call()
                self.controller.hard_off.assert_not_called()
                self.assertIsNone(lease.registry.current())
                checkpoint=self.persist.call_args.args[1]
                with self.assertRaisesRegex(RuntimeError,'already abandoned'):
                    lease.begin_recovery('/private/checkpoint',r.checkpoint_digest(checkpoint))
            with LabVmLease(False,folder,lambda:[]):pass

    def test_uncertain_hard_off_keeps_real_registry_claim(self):
        with tempfile.TemporaryDirectory() as folder:
            with LabVmLease(True,folder,lambda:[]) as lease:
                self.lease=lease
                self.controller.hard_off.side_effect=RuntimeError('uncertain command')
                self.cycle.side_effect=lambda **kw:kw['power_off']()
                with self.assertRaisesRegex(RuntimeError,'uncertain command'):self.call()
                self.assertIsNotNone(lease.registry.current())
            with self.assertRaisesRegex(RuntimeError,'unfinished'):
                with LabVmLease(False,folder,lambda:[]):pass

    def test_final_guard_failure_releases_real_registry(self):
        with tempfile.TemporaryDirectory() as folder:
            with LabVmLease(True,folder,lambda:[]) as lease:
                self.lease=lease
                self.controller.read_boot.return_value='different'
                self.cycle.side_effect=lambda **kw:kw['before_power_off']()
                with self.assertRaisesRegex(RuntimeError,'Guest boot changed'):self.call()
                self.controller.hard_off.assert_not_called()
                self.assertIsNone(lease.registry.current())

    def test_checkpoint_failure_never_powers_off(self):
        self.persist.side_effect=OSError('disk failure')
        with self.assertRaises(OSError):self.call()
        self.cycle.assert_not_called();self.resume.assert_not_called()
    def test_changed_bindings_do_not_reopen_app(self):
        self.workspace.run.side_effect=lambda args,*a,**kw: b'new\n' if args[0]=='cat' else b'changed'
        with self.assertRaises(RuntimeError):self.call()
        self.resume.assert_not_called();self.workspace.forward.assert_not_called()
    def test_success_restores_tunnels_before_reopening_app(self):
        timeline=Mock()
        for name,action in [('snapshot',r.snapshot),('native_snapshot',r.capture_native_wallets),
                            ('persist',self.persist),('cycle',self.cycle),
                            ('workspace',self.workspace.run),('forward',self.workspace.forward),
                            ('native_resume',self.recovered.resume_after_boot),('app_resume',self.resume)]:
            timeline.attach_mock(action,name)
        result=self.call()
        self.assertEqual([call[0] for call in timeline.mock_calls],
            ['snapshot','native_snapshot','persist','cycle','workspace','workspace',
             'snapshot','native_snapshot','forward','native_resume','app_resume'])
        self.assertTrue(result['oldApprovalAbsent'])

    def test_changed_light_snapshot_prevents_all_restoration(self):
        r.snapshot.side_effect=[{'wallet':{},'metadata':{}},{'wallet':{'changed':True},'metadata':{}}]
        with self.assertRaisesRegex(RuntimeError,'retained wallet'):self.call()
        self.workspace.forward.assert_not_called()
        self.recovered.resume_after_boot.assert_not_called();self.resume.assert_not_called()

    def test_changed_native_snapshot_prevents_all_restoration(self):
        r.capture_native_wallets.side_effect=[{'count':2,'digest':'first'},{'count':2,'digest':'changed'}]
        with self.assertRaisesRegex(RuntimeError,'native wallet files'):self.call()
        self.workspace.forward.assert_not_called()
        self.recovered.resume_after_boot.assert_not_called();self.resume.assert_not_called()

    def test_failed_reopen_does_not_record_completion(self):
        self.resume.side_effect=RuntimeError('resume failed')
        with self.assertRaisesRegex(RuntimeError,'resume failed'):self.call()
        phases=[call.args[0] for call in self.journal_factory.return_value.record.call_args_list]
        self.assertIn('app-reopen-requested',phases);self.assertNotIn('app-reopened',phases)
        self.resident._merge.assert_not_called()
    def test_changed_signed_boundary_is_rejected_by_final_guard(self):
        self.call()
        guard=self.cycle.call_args.kwargs['before_power_off']
        r._idle_states.return_value={'alice':{'reconnects':1},'bob':{'lightFunding':{'operation':'send'}}}
        with self.assertRaisesRegex(RuntimeError,'boundary changed'):guard()

    def test_changed_native_files_stop_final_poweroff_guard(self):
        self.call();guard=self.cycle.call_args.kwargs['before_power_off']
        r.capture_native_wallets.return_value={'count':2,'digest':'changed'}
        with self.assertRaisesRegex(RuntimeError,'Native wallet changed before'):guard()

    def test_changed_light_files_stop_final_poweroff_guard(self):
        self.call();guard=self.cycle.call_args.kwargs['before_power_off']
        r.snapshot.return_value={'wallet':{'changed':True},'metadata':{}}
        with self.assertRaisesRegex(RuntimeError,'Light wallet changed before'):guard()

    def test_changed_immutable_files_stop_final_poweroff_guard(self):
        self.call();guard=self.cycle.call_args.kwargs['before_power_off']
        r.capture_bindings.return_value={'config':'changed'}
        with self.assertRaisesRegex(RuntimeError,'bindings changed before'):guard()

    def test_changed_retained_funding_is_rejected_by_final_guard(self):
        self.call()
        guard=self.cycle.call_args.kwargs['before_power_off']
        r.idle_boundary.return_value={'tradeId':'trade','retainedTxid':'different'}
        with self.assertRaisesRegex(RuntimeError,'boundary changed'):guard()

    def continuation(self,phase='start-requested',boot='new'):
        self.call()
        checkpoint=self.persist.call_args.args[1]
        self.resume.reset_mock();self.workspace.forward.reset_mock()
        observed={'observation':'new-guest-running','phase':phase,'oldBootId':'old',
            'observedBootId':boot,'checkpointSha256':r.checkpoint_digest(checkpoint),'journalSha256':'fixture-history'}
        return checkpoint,observed

    def continue_call(self,checkpoint,observed):
        with patch.object(r,'read_private_json',return_value=checkpoint), patch.object(r,'inspect_power_loss_recovery',return_value=observed):
            return r.continue_bob_after_power_loss(self.resident,self.workspace,self.light,
                self.native,'/private/checkpoint',controller=self.controller,vm_lease=Mock())

    def test_continuation_restores_after_lost_guest_ready_record(self):
        checkpoint,observed=self.continuation()
        result=self.continue_call(checkpoint,observed)
        self.workspace.forward.assert_called_once_with([1,2,3])
        self.resume.assert_called_once();self.assertTrue(result['oldApprovalAbsent'])
        self.controller.start.assert_not_called();self.controller.hard_off.assert_not_called()

    def test_continuation_after_verified_bindings_does_not_duplicate_record(self):
        checkpoint,observed=self.continuation(phase='bindings-verified')
        self.continue_call(checkpoint,observed)
        phases=[call.args[0] for call in self.journal_factory.attach.return_value.record.call_args_list]
        self.assertNotIn('bindings-verified',phases)
        self.assertEqual(phases[0],'tunnels-restored')
        self.resume.assert_called_once()

    def test_continuation_after_tunnels_rechecks_state_and_restores_without_duplicate_records(self):
        checkpoint,observed=self.continuation(phase='tunnels-restored')
        r.snapshot.reset_mock();r.capture_native_wallets.reset_mock()
        self.continue_call(checkpoint,observed)
        r.snapshot.assert_called_once();r.capture_native_wallets.assert_called_once()
        self.workspace.forward.assert_called_once_with([1,2,3])
        phases=[call.args[0] for call in self.journal_factory.attach.return_value.record.call_args_list]
        self.assertNotIn('bindings-verified',phases);self.assertNotIn('tunnels-restored',phases)
        self.assertEqual(phases[0],'wallet-services-requested')
        self.resume.assert_called_once()

    def test_continuation_tunnel_phase_still_rejects_changed_native_files(self):
        checkpoint,observed=self.continuation(phase='tunnels-restored')
        r.capture_native_wallets.return_value={'count':2,'digest':'changed'}
        with self.assertRaisesRegex(RuntimeError,'native wallet files'):self.continue_call(checkpoint,observed)
        self.workspace.forward.assert_not_called();self.resume.assert_not_called()

    def test_unrecorded_service_start_is_not_adopted_in_fresh_recovery(self):
        self.call()
        self.assertFalse(self.recovered.resume_after_boot.call_args.kwargs['restart_exited'])
        self.assertTrue(self.recovered.resume_after_boot.call_args.kwargs['require_unstarted'])

    def test_partial_wallet_start_is_reconciled_without_duplicate_intent(self):
        checkpoint,observed=self.continuation(phase='wallet-services-requested')
        self.recovered.resume_after_boot.reset_mock()
        self.continue_call(checkpoint,observed)
        self.recovered.resume_after_boot.assert_called_once_with(checkpoint['services'],restart_exited=True,require_unstarted=False)
        phases=[call.args[0] for call in self.journal_factory.attach.return_value.record.call_args_list]
        self.assertEqual(phases,['wallet-services-restored','app-reopen-requested','app-reopened'])

    def test_completed_wallet_start_is_reinspected_without_duplicate_completion(self):
        checkpoint,observed=self.continuation(phase='wallet-services-restored')
        self.recovered.resume_after_boot.reset_mock()
        self.continue_call(checkpoint,observed)
        self.recovered.resume_after_boot.assert_called_once()
        phases=[call.args[0] for call in self.journal_factory.attach.return_value.record.call_args_list]
        self.assertEqual(phases,['app-reopen-requested','app-reopened'])

    def test_wrong_service_boot_rejected_before_forwarding(self):
        checkpoint,observed=self.continuation()
        checkpoint['services']['claim']['bootId']='different'
        observed['checkpointSha256']=r.checkpoint_digest(checkpoint)
        with self.assertRaisesRegex(RuntimeError,'boot differs'):self.continue_call(checkpoint,observed)
        self.workspace.forward.assert_not_called();self.resume.assert_not_called()

    def test_missing_daemon_forward_rejected_before_forwarding(self):
        checkpoint,observed=self.continuation();checkpoint['forwardPorts']=[5,6]
        observed['checkpointSha256']=r.checkpoint_digest(checkpoint)
        with self.assertRaisesRegex(RuntimeError,'omits native daemon'):self.continue_call(checkpoint,observed)
        self.workspace.forward.assert_not_called();self.resume.assert_not_called()

    def test_invalid_forward_inputs_rejected_before_checkpoint(self):
        for ports in ([True,2],[1,1],[],[0,1],[1,65536]):
            with self.assertRaises(RuntimeError):
                r.restart_bob_after_power_loss(self.resident,self.workspace,self.light,self.native,
                    '/private/checkpoint',ports,controller=self.controller,vm_lease=Mock())
        self.persist.assert_not_called();self.cycle.assert_not_called()

    def test_changed_journal_rejected_before_restoration(self):
        checkpoint,observed=self.continuation()
        self.journal_factory.attach.side_effect=RuntimeError('journal changed after observation')
        with self.assertRaisesRegex(RuntimeError,'journal changed'):self.continue_call(checkpoint,observed)
        self.journal_factory.attach.assert_called_once_with('/private/checkpoint.phases',expected_digest='fixture-history')
        self.workspace.forward.assert_not_called();self.resume.assert_not_called()

    def test_continuation_wrong_workspace_boot_never_restores(self):
        checkpoint,observed=self.continuation(boot='different')
        with self.assertRaisesRegex(RuntimeError,'different guest boot'):self.continue_call(checkpoint,observed)
        self.workspace.forward.assert_not_called();self.resume.assert_not_called()

    def test_continuation_rejects_phase_after_wallet_mutations(self):
        checkpoint,observed=self.continuation(phase='app-reopen-requested')
        with self.assertRaisesRegex(RuntimeError,'before app reopening'):self.continue_call(checkpoint,observed)
        self.workspace.forward.assert_not_called();self.resume.assert_not_called()

    def test_continuation_rejects_changed_checkpoint(self):
        checkpoint,observed=self.continuation();checkpoint['retainedTxid']='changed'
        with self.assertRaisesRegex(RuntimeError,'changed after observation'):self.continue_call(checkpoint,observed)
        self.workspace.forward.assert_not_called();self.resume.assert_not_called()

    def test_continuation_rejects_replaced_host_trader(self):
        checkpoint,observed=self.continuation()
        self.host_check.side_effect=RuntimeError('Host trader differs')
        with self.assertRaisesRegex(RuntimeError,'Host trader differs'):self.continue_call(checkpoint,observed)
        self.workspace.forward.assert_not_called();self.resume.assert_not_called()

    def test_resume_intent_failure_prevents_wallet_start(self):
        def record(phase,evidence):
            if phase=='wallet-services-requested':raise OSError('journal unavailable')
        self.journal_factory.return_value.record.side_effect=record
        with self.assertRaises(OSError):self.call()
        self.recovered.resume_after_boot.assert_not_called();self.resume.assert_not_called()

    def test_partial_service_failure_retains_accepted_handles(self):
        partial={'claim':(123,'private-login-path')}
        self.recovered.services=partial
        self.recovered.resume_after_boot.side_effect=RuntimeError('observer failed')
        with self.assertRaisesRegex(RuntimeError,'observer failed'):self.call()
        self.assertIs(self.native.services,partial);self.resume.assert_not_called()
        phases=[call.args[0] for call in self.journal_factory.return_value.record.call_args_list]
        self.assertIn('wallet-services-requested',phases)
        self.assertNotIn('wallet-services-restored',phases)

    def test_app_intent_failure_prevents_app_reopening(self):
        def record(phase,evidence):
            if phase=='app-reopen-requested':raise OSError('journal unavailable')
        self.journal_factory.return_value.record.side_effect=record
        with self.assertRaises(OSError):self.call()
        self.recovered.resume_after_boot.assert_called_once();self.resume.assert_not_called()

    def test_lease_lost_after_wallet_restore_prevents_app_reopen(self):
        def restore(*args,**kwargs):
            self.lease.require_exclusive.side_effect=RuntimeError('lease lost')
            return self.native.config.return_value
        self.recovered.resume_after_boot.side_effect=restore
        with self.assertRaisesRegex(RuntimeError,'lease lost'):self.call()
        self.resume.assert_not_called()

    def test_reopen_ping_rechecks_lease(self):
        self.call();ping=self.resume.call_args.args[6]
        self.lease.require_exclusive.side_effect=RuntimeError('lease lost')
        with self.assertRaisesRegex(RuntimeError,'lease lost'):ping()

    def test_existing_recovery_owner_prevents_power_cycle(self):
        self.lease.begin_recovery.side_effect=RuntimeError('unfinished recovery')
        with self.assertRaisesRegex(RuntimeError,'unfinished recovery'):self.call()
        self.cycle.assert_not_called();self.resume.assert_not_called()

    def test_registry_completion_follows_app_reopened_record(self):
        timeline=Mock()
        timeline.attach_mock(self.journal_factory.return_value.record,'record')
        timeline.attach_mock(self.lease.complete_recovery,'complete')
        self.call()
        calls=timeline.mock_calls
        self.assertEqual(calls[-2].args[0],'app-reopened')
        self.assertEqual(calls[-1][0],'complete')

    def test_mainnet_rejected_before_checkpoint(self):
        self.native.stage='mainnet'
        with self.assertRaises(RuntimeError):self.call()
        self.persist.assert_not_called();self.cycle.assert_not_called()

if __name__=='__main__':unittest.main()
