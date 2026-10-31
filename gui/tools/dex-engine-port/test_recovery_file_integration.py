"""Real checkpoint/journal/files into continuation; VM and app actions are doubles."""
import hashlib
import pathlib
import subprocess
import tempfile
import types
import unittest
from unittest.mock import Mock,patch
import remote_vm_recovery as recovery
from local_process_identity import local_process_identity
from remote_native_wallets import RemoteNativeWallets
from remote_recovery_checkpoint import persist_checkpoint,RecoveryPhaseJournal,capture_native_wallets,read_phase_journal
from test_remote_native_wallets import LocalWorkspace
from vm_power_loss import VM_NAME,VM_UUID

class FileWorkspace(LocalWorkspace):
    def run(self, argv, data=None, timeout=60):
        if argv==['cat','/proc/sys/kernel/random/boot_id']:
            return subprocess.check_output(argv,timeout=timeout)
        return super().run(argv,data,timeout)

OLD='00000000-0000-0000-0000-000000000001'
class FileIntegrationTests(unittest.TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        self.root=pathlib.Path(tmp.name);self.ws=FileWorkspace(self.root);self.ws.forward=Mock()
        self.child=subprocess.Popen(['sleep','60']);self.addCleanup(self.stop)
        self.resident=types.SimpleNamespace(parts={
            'alice':types.SimpleNamespace(process=self.child,call=lambda *a,**kw:{}),
            'bob':types.SimpleNamespace(signal_remote=Mock())},_merge=Mock())
        self.light=types.SimpleNamespace(setup={})
        self.config=self.root/'config.json';self.config.write_text('{}');self.config.chmod(0o600)
        native=self.root/'native-wallets';native.mkdir(mode=0o700)
        for role in ('claim','observer','recipient'):
            (native/role).mkdir(mode=0o700);(native/role/'wallets').mkdir(mode=0o700)
        self.keys=native/'recipient/wallets/payout.keys';self.keys.write_bytes(b'fixture-key');self.keys.chmod(0o600)
        self.native=RemoteNativeWallets(self.ws,'/fixture/wallet',1,'testnet')
        self.native.services={role:(i+2,str(native/role/'login')) for i,role in enumerate(('claim','observer','recipient'))}
        self.saved={'wallet':{'digest':'light-fixture'},'metadata':{}}
        checkpoint={'schema':'monzero-vm-powerloss-checkpoint-v1','oldBootId':OLD,
            'aliceProcess':local_process_identity(self.child),'aliceReconnects':0,
            'tradeId':'fixture-trade','retainedTxid':'fixture-tx',
            'before':{'tradeId':'fixture-trade','retainedTxid':'fixture-tx'},
            'saved':self.saved,'bindings':{'config.json':hashlib.sha256(self.config.read_bytes()).hexdigest()},
            'nativeWallets':capture_native_wallets(self.ws),'nativeConfig':self.native.config(),
            'forwardPorts':[1,5,6,7],
            'services':{role:{'bootId':OLD,'port':i+2,'launch':{'binary':'/fixture/wallet','sha256':'a'*64,'daemonPort':1,'stage':'testnet'}} for i,role in enumerate(('claim','observer','recipient'))}}
        self.path=self.root/'checkpoint.json';persist_checkpoint(self.path,checkpoint)
        journal=RecoveryPhaseJournal(str(self.path)+'.phases')
        journal.record('checkpoint-bound',{'sha256':recovery.checkpoint_digest(checkpoint)})
        for phase in ('power-off-requested','power-off-confirmed'):journal.record(phase,{'oldBootId':OLD})
        journal.record('start-requested',{'oldBootId':OLD,'vmUuid':VM_UUID,'startUnit':'monzero-dexlab-recovery-'+'a'*32})
        self.controller=Mock();self.controller.inspect.return_value={'name':VM_NAME,'uuid':VM_UUID,'state':'running'}
        self.controller.read_boot.return_value=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        self.controller.inspect_start_attempt.return_value={'unit':'fixture'}
        self.controller.require_live_start_attempt.return_value={'pid':123,'starttime':'456'}
        self.recovered=Mock(services=self.native.services)
        self.recovered.resume_after_boot.return_value=self.native.config()
        self.reopen=Mock(return_value=({'generation':1},{'reconnects':1}))
    def stop(self):
        if self.child.poll() is None:self.child.terminate()
        self.child.wait(timeout=5)
    def continue_trade(self):
        with patch.object(recovery,'snapshot',return_value=self.saved),patch.object(recovery,'RemoteNativeWallets',return_value=self.recovered),patch.object(recovery,'_resume',self.reopen):
            try:return recovery.continue_bob_after_power_loss(self.resident,self.ws,self.light,self.native,self.path,controller=self.controller,vm_lease=Mock())
            finally:
                self.controller.start.assert_not_called();self.controller.hard_off.assert_not_called()
    def test_real_files_continue_and_append_complete_ordered_history(self):
        result=self.continue_trade();self.assertTrue(result['retainedFundingUnchanged'])
        self.assertEqual([v['phase'] for v in read_phase_journal(str(self.path)+'.phases')],
            ['checkpoint-bound','power-off-requested','power-off-confirmed','start-requested',
             'guest-ready','bindings-verified','tunnels-restored','wallet-services-requested',
             'wallet-services-restored','app-reopen-requested','app-reopened'])
        self.recovered.resume_after_boot.assert_called_once();self.reopen.assert_called_once()
    def test_actual_changed_binding_prevents_restoration(self):
        self.config.write_text('{"changed":true}')
        with self.assertRaisesRegex(RuntimeError,'binding changed'):self.continue_trade()
        self.ws.forward.assert_not_called();self.recovered.resume_after_boot.assert_not_called();self.reopen.assert_not_called()
    def test_second_continuation_after_failed_check_uses_saved_ready_phase(self):
        self.config.write_text('{"changed":true}')
        with self.assertRaises(RuntimeError):self.continue_trade()
        phases=read_phase_journal(str(self.path)+'.phases')
        self.assertEqual(phases[-1]['phase'],'guest-ready')
        self.config.write_text('{}')
        self.continue_trade()
        phases=read_phase_journal(str(self.path)+'.phases')
        self.assertEqual(sum(v['phase']=='guest-ready' for v in phases),1)
        self.assertEqual(phases[-1]['phase'],'app-reopened')
        self.reopen.assert_called_once()

    def test_actual_changed_native_keys_prevent_restoration(self):
        self.keys.write_bytes(b'different-key')
        with self.assertRaisesRegex(RuntimeError,'native wallet files'):self.continue_trade()
        self.ws.forward.assert_not_called();self.reopen.assert_not_called()

if __name__=='__main__':unittest.main()
