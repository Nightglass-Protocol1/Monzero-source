import pathlib
import tempfile
import unittest
from unittest.mock import Mock
from remote_recovery_checkpoint import persist_checkpoint, RecoveryPhaseJournal
from remote_vm_recovery import inspect_power_loss_recovery, checkpoint_digest
from vm_power_loss import VM_NAME, VM_UUID

OLD='00000000-0000-0000-0000-000000000001'
NEW='00000000-0000-0000-0000-000000000002'
class ObservationTests(unittest.TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        self.path=pathlib.Path(tmp.name)/'checkpoint'
        self.checkpoint={'schema':'monzero-vm-powerloss-checkpoint-v1','oldBootId':OLD}
        persist_checkpoint(self.path,self.checkpoint)
        self.journal=RecoveryPhaseJournal(str(self.path)+'.phases')
        self.journal.record('checkpoint-bound',{'sha256':checkpoint_digest(self.checkpoint)})
        self.control=Mock();self.lease=Mock();self.ping=Mock()
        self.control.inspect.return_value={'name':VM_NAME,'uuid':VM_UUID,'state':'running'}
        self.control.read_boot.return_value=OLD
    def observe(self):
        try:return inspect_power_loss_recovery(self.path,controller=self.control,vm_lease=self.lease,ping_alice=self.ping)
        finally:
            self.control.start.assert_not_called();self.control.hard_off.assert_not_called()
    def started(self):
        for phase in ('power-off-requested','power-off-confirmed'):
            self.journal.record(phase,{'oldBootId':OLD})
        self.journal.record('start-requested',{'oldBootId':OLD,'vmUuid':VM_UUID,'startUnit':'monzero-dexlab-recovery-'+'a'*32})
    def test_unissued_abandonment_is_terminal_without_vm_actions(self):
        self.journal.record('power-off-requested',{'oldBootId':OLD})
        self.journal.record('power-off-abandoned',{'errorClass':'RuntimeError'})
        self.assertEqual(self.observe()['observation'],'abandoned-before-power-action')
        self.control.inspect.assert_not_called()

    def test_abandonment_after_confirmed_poweroff_rejected(self):
        self.started()
        self.journal.record('power-off-abandoned',{'errorClass':'RuntimeError'})
        with self.assertRaisesRegex(RuntimeError,'possible power action'):self.observe()

    def test_original_guest_observation(self):
        self.assertEqual(self.observe()['observation'],'original-guest-running')
        self.control.retain_start_attempt.assert_not_called()
    def test_lost_poweroff_reply_is_observed_without_retry(self):
        self.journal.record('power-off-requested',{'oldBootId':OLD})
        self.control.inspect.return_value['state']='poweroff'
        self.assertEqual(self.observe()['observation'],'power-off-observed')
    def test_lost_start_reply_observes_recorded_attempt(self):
        self.started();self.control.read_boot.return_value=NEW
        result=self.observe();self.assertEqual(result['observation'],'new-guest-running')
        self.control.retain_start_attempt.assert_called_once();self.control.inspect_start_attempt.assert_called_once()
    def test_new_boot_with_unverified_start_process_is_rejected(self):
        self.started();self.control.read_boot.return_value=NEW
        self.control.require_live_start_attempt.side_effect=RuntimeError('start unit not active')
        with self.assertRaisesRegex(RuntimeError,'not active'):self.observe()

    def test_retirement_observation_does_not_claim_verified_start_process(self):
        self.started();self.control.read_boot.return_value=NEW
        result=inspect_power_loss_recovery(self.path,controller=self.control,
            vm_lease=self.lease,ping_alice=self.ping,retirement_only=True)
        self.assertFalse(result['resumeEligible']);self.assertFalse(result['startProcessVerified'])
        self.control.require_live_start_attempt.assert_not_called()
        self.control.start.assert_not_called();self.control.hard_off.assert_not_called()

    def test_poweroff_after_start_intent_never_restarts(self):
        self.started();self.control.inspect.return_value['state']='poweroff'
        self.assertEqual(self.observe()['observation'],'start-outcome-unresolved')
    def test_old_boot_after_confirmed_poweroff_rejected(self):
        self.started()
        with self.assertRaisesRegex(RuntimeError,'contradicted'):self.observe()
    def test_second_reboot_is_not_silently_adopted(self):
        self.started();self.journal.record('guest-ready',{'oldBootId':OLD,'newBootId':NEW})
        self.control.read_boot.return_value='00000000-0000-0000-0000-000000000003'
        with self.assertRaisesRegex(RuntimeError,'rebooted again'):self.observe()
    def test_checkpoint_substitution_rejected_before_observation(self):
        self.path.write_text('{"schema":"monzero-vm-powerloss-checkpoint-v1","oldBootId":"'+NEW+'"}')
        with self.assertRaisesRegex(RuntimeError,'another checkpoint'):self.observe()
        self.control.inspect.assert_not_called()
    def test_phase_reordering_rejected(self):
        self.journal.record('guest-ready',{'oldBootId':OLD,'newBootId':NEW})
        with self.assertRaisesRegex(RuntimeError,'phases'):self.observe()
    def test_other_vm_rejected(self):
        self.control.inspect.return_value['uuid']='other'
        with self.assertRaisesRegex(RuntimeError,'another VM'):self.observe()
    def test_observation_timeout_propagates_without_mutations(self):
        self.control.inspect.side_effect=TimeoutError('temporary')
        with self.assertRaises(TimeoutError):self.observe()
    def test_exclusive_lease_required_before_read_or_observation(self):
        self.lease.require_exclusive.side_effect=RuntimeError('busy')
        with self.assertRaisesRegex(RuntimeError,'busy'):self.observe()
        self.control.inspect.assert_not_called()

if __name__=='__main__':unittest.main()
