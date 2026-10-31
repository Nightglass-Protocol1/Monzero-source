import json
import hashlib
import pathlib
import socket
import subprocess
import tempfile
import unittest
from test_remote_native_wallets import LocalWorkspace, FakeWorkspace, BINARY, launched, write_private
from remote_native_wallets import LAUNCH, INSPECT_RESUME, RemoteNativeWallets

OLD_BOOT = '00000000-0000-0000-0000-000000000000'
FAKE_BINDING = {'binary':BINARY,'sha256':'a'*64,'daemonPort':18081,'stage':'testnet'}

class ResumeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.ws = LocalWorkspace(self.root)
        self.directory = self.root / 'native-wallets' / 'claim'
        self.directory.parent.mkdir(mode=0o700)
        self.directory.mkdir(mode=0o700)
        for name in ('wallets', 'ringdb'):
            (self.directory/name).mkdir(mode=0o700)
        for name in ('wallet-password', 'recipient-password'):
            write_private(self.directory.parent/name, 'persistent-secret')
        self.sock = socket.socket()
        self.addCleanup(self.sock.close)
        self.sock.bind(('127.0.0.1', 0))
        self.port = self.sock.getsockname()[1]
        self.record = self.directory/'process.json'
        write_private(self.record, json.dumps({'pid': 12345, 'starttime': '99', 'bootId': OLD_BOOT, 'port': self.port}))
        self.original = self.record.read_bytes()
        self.marker = self.root/'executed'
        self.binary = self.root/'wallet'
        self.binary.write_text('#!/bin/sh\ntouch "'+str(self.marker)+'"\n')
        self.binary.chmod(0o700)
        self.binding={'binary':str(self.binary),'sha256':hashlib.sha256(self.binary.read_bytes()).hexdigest(),'daemonPort':18081,'stage':'regtest'}
        value=json.loads(self.original);value['launch']=self.binding
        self.record.write_text(json.dumps(value));self.original=self.record.read_bytes()

    def launch(self, script=LAUNCH, boot=OLD_BOOT, port=None):
        return self.ws.run(['python3', '-c', script, str(self.binary), str(self.directory),
                           '18081', 'regtest', boot, str(self.port if port is None else port)],json.dumps(self.binding).encode())

    def assert_refused(self, **kwargs):
        with self.assertRaises(RuntimeError): self.launch(**kwargs)
        self.assertFalse(self.marker.exists())
        self.assertEqual(self.record.read_bytes(), self.original)

    def test_occupied_port_does_not_change_record_or_launch(self):
        self.assert_refused()

    def test_wrong_boot_does_not_launch(self):
        self.sock.close()
        self.assert_refused(boot='11111111-1111-1111-1111-111111111111')

    def test_same_boot_does_not_launch(self):
        self.sock.close()
        boot=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        value=json.loads(self.original); value['bootId']=boot
        self.record.write_text(json.dumps(value)); self.original=self.record.read_bytes()
        self.assert_refused(boot=boot)

    def test_changed_port_does_not_launch(self):
        self.assert_refused(port=self.port-1)

    def test_missing_password_does_not_regenerate(self):
        self.sock.close()
        path=self.directory.parent/'wallet-password'; path.unlink()
        self.assert_refused()
        self.assertFalse(path.exists())

    def test_record_persistence_failure_does_not_execute(self):
        self.sock.close()
        broken=LAUNCH.replace('json.dump(state,f);', 'raise OSError("injected persistence failure");')
        self.assert_refused(script=broken)

    def test_record_replaced_before_child_exec_and_passwords_preserved(self):
        self.sock.close()
        # This stand-in exits immediately: readiness must fail, but the durable
        # replacement must already identify this boot before exec is allowed.
        with self.assertRaises(RuntimeError): self.launch()
        self.assertTrue(self.marker.exists())
        record=json.loads(self.record.read_bytes())
        self.assertEqual(record['port'], self.port)
        self.assertEqual(record['bootId'], pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip())
        for name in ('wallet-password','recipient-password'):
            self.assertEqual((self.directory.parent/name).read_text(), 'persistent-secret')

    def test_resume_all_roles_preserves_ports_and_never_generates_passwords(self):
        ws=FakeWorkspace([b'{"state":"needs-resume"}',launched(101,30001),b'{"state":"needs-resume"}',launched(102,30002),b'{"state":"needs-resume"}',launched(103,30003)])
        wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
        checkpoint={r:{'bootId':OLD_BOOT,'port':30001+i,'launch':FAKE_BINDING} for i,r in enumerate(('claim','observer','recipient'))}
        wallets.resume_after_boot(checkpoint)
        self.assertEqual(len(ws.calls),6)
        self.assertTrue(all(call[0][-2]==OLD_BOOT for call in ws.calls))
        self.assertEqual(wallets.services['claim'][0],30001)

    def inspect(self):
        return json.loads(self.ws.run(['python3','-c',INSPECT_RESUME,str(self.binary),
            str(self.directory),'18081','regtest',OLD_BOOT,str(self.port)],json.dumps(self.binding).encode()))

    def test_inspection_of_old_boot_is_read_only(self):
        self.assertEqual(self.inspect(), {'state':'needs-resume'})
        self.assertEqual(self.record.read_bytes(), self.original)
        self.assertFalse(self.marker.exists())

    def test_unbound_current_boot_is_rejected(self):
        state=json.loads(self.original)
        state['bootId']=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        self.record.write_text(json.dumps(state))
        with self.assertRaisesRegex(RuntimeError,'Unbound current-boot'):
            self.inspect()

    def test_unrelated_live_process_is_not_adopted_or_signalled(self):
        child=subprocess.Popen(['sleep','60'],start_new_session=True)
        try:
            state=json.loads(self.original)
            state.update(pid=child.pid,
                starttime=pathlib.Path('/proc/%d/stat'%child.pid).read_text().rsplit(')',1)[1].split()[19],
                bootId=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                resumedFromBootId=OLD_BOOT)
            self.record.write_text(json.dumps(state))
            with self.assertRaisesRegex(RuntimeError,'command mismatch'): self.inspect()
            self.assertIsNone(child.poll())
        finally:
            child.terminate();child.wait(timeout=5)

    def test_unrecorded_current_boot_service_is_neither_adopted_nor_restarted(self):
        for state in ('running','needs-restart'):
            ws=FakeWorkspace([json.dumps({'state':state,'process':json.loads(launched(101,30001))}).encode()])
            wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
            checkpoint={r:{'bootId':OLD_BOOT,'port':30001+i,'launch':FAKE_BINDING} for i,r in enumerate(('claim','observer','recipient'))}
            with self.assertRaisesRegex(RuntimeError,'without retained resume intent'):
                wallets.resume_after_boot(checkpoint,require_unstarted=True)
            self.assertEqual(len(ws.calls),1);self.assertEqual(ws.processes,[])
            self.assertEqual(wallets.services,{})

    def test_already_running_roles_are_not_launched_again(self):
        ws=FakeWorkspace([json.dumps({'state':'running','process':json.loads(launched(101+i,30001+i))}).encode() for i in range(3)])
        checkpoint={r:{'bootId':OLD_BOOT,'port':30001+i,'launch':FAKE_BINDING} for i,r in enumerate(('claim','observer','recipient'))}
        wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
        wallets.resume_after_boot(checkpoint)
        self.assertEqual(len(ws.calls),3)
        self.assertTrue(all(call[0][2]==INSPECT_RESUME for call in ws.calls))
        self.assertEqual(wallets.services['observer'][0],30002)

    def test_exited_resumed_service_requires_explicit_restart(self):
        self.sock.close()
        with self.assertRaises(RuntimeError): self.launch()
        self.assertEqual(self.inspect(), {'state':'needs-restart'})
        self.marker.unlink()
        with self.assertRaises(RuntimeError): self.launch()
        self.assertFalse(self.marker.exists())
        with self.assertRaisesRegex(RuntimeError,'did not become ready'):
            self.ws.run(['python3','-c',LAUNCH,str(self.binary),str(self.directory),
                '18081','regtest',OLD_BOOT,str(self.port),'restart-exited'],json.dumps(self.binding).encode())
        self.assertTrue(self.marker.exists())

    def test_explicit_restart_cannot_duplicate_live_process(self):
        child=subprocess.Popen(['sleep','60'],start_new_session=True)
        try:
            state=json.loads(self.original)
            state.update(pid=child.pid,
                starttime=pathlib.Path('/proc/%d/stat'%child.pid).read_text().rsplit(')',1)[1].split()[19],
                bootId=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                resumedFromBootId=OLD_BOOT)
            self.record.write_text(json.dumps(state));self.sock.close()
            with self.assertRaisesRegex(RuntimeError,'still alive'):
                self.ws.run(['python3','-c',LAUNCH,str(self.binary),str(self.directory),
                    '18081','regtest',OLD_BOOT,str(self.port),'restart-exited'],json.dumps(self.binding).encode())
            self.assertIsNone(child.poll())
            self.assertFalse(self.marker.exists())
        finally:
            child.terminate();child.wait(timeout=5)

    def test_exited_service_is_not_automatically_restarted(self):
        ws=FakeWorkspace([b'{"state":"needs-restart"}'])
        wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
        checkpoint={r:{'bootId':OLD_BOOT,'port':30001+i,'launch':FAKE_BINDING} for i,r in enumerate(('claim','observer','recipient'))}
        with self.assertRaisesRegex(RuntimeError,'explicit restart'):
            wallets.resume_after_boot(checkpoint)
        self.assertEqual(len(ws.calls),1)

    def test_live_checkpoint_returns_only_boot_and_ports(self):
        expected={r:{'bootId':OLD_BOOT,'port':30001+i,'launch':FAKE_BINDING} for i,r in enumerate(('claim','observer','recipient'))}
        ws=FakeWorkspace([json.dumps(expected).encode()])
        wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
        wallets.services={r:(v['port'],'/private/login') for r,v in expected.items()}
        self.assertEqual(wallets.recovery_checkpoint(),expected)

    def test_checkpoint_rejects_changed_ports_and_mixed_boots(self):
        for change in ('port','bootId'):
            expected={r:{'bootId':OLD_BOOT,'port':30001+i,'launch':FAKE_BINDING} for i,r in enumerate(('claim','observer','recipient'))}
            ws=FakeWorkspace(); wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
            wallets.services={r:(v['port'],'/private/login') for r,v in expected.items()}
            expected['observer'][change]=40000 if change=='port' else '11111111-1111-1111-1111-111111111111'
            ws.outputs=[json.dumps(expected).encode()]
            with self.assertRaises(RuntimeError):wallets.recovery_checkpoint()

    def test_checkpoint_requires_all_services(self):
        ws=FakeWorkspace(); wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
        with self.assertRaises(RuntimeError):wallets.recovery_checkpoint()
        self.assertEqual(ws.calls,[])

    def test_failed_inspection_registers_no_mutating_cleanup(self):
        ws=FakeWorkspace([RuntimeError('Missing process record')]);wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
        checkpoint={r:{'bootId':OLD_BOOT,'port':30001+i,'launch':FAKE_BINDING} for i,r in enumerate(('claim','observer','recipient'))}
        with self.assertRaises(RuntimeError):wallets.resume_after_boot(checkpoint)
        self.assertEqual(ws.processes,[])
        wallets.stop();self.assertEqual(len(ws.calls),1)

    def test_changed_binary_is_rejected_before_execution(self):
        self.sock.close();self.binary.write_text(self.binary.read_text()+'# modified binary\n')
        self.assert_refused()
        with self.assertRaisesRegex(RuntimeError,'launch configuration changed'):self.inspect()

    def test_changed_launch_arguments_are_rejected_before_remote_calls(self):
        for field,value in (('binary','/other-wallet'),('daemonPort',19000),('stage','regtest')):
            ws=FakeWorkspace();wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
            binding=dict(FAKE_BINDING);binding[field]=value
            checkpoint={r:{'bootId':OLD_BOOT,'port':30001+i,'launch':binding} for i,r in enumerate(('claim','observer','recipient'))}
            with self.assertRaises(ValueError):wallets.resume_after_boot(checkpoint)
            self.assertEqual(ws.calls,[])

    def test_changed_record_binding_is_rejected(self):
        self.sock.close();state=json.loads(self.original);state['launch']['daemonPort']=99
        self.record.write_text(json.dumps(state));self.original=self.record.read_bytes()
        self.assert_refused()

    def test_second_boot_recognizes_original_checkpoint_lineage(self):
        state=json.loads(self.original)
        state.update(bootId='11111111-1111-1111-1111-111111111111',resumedFromBootId=OLD_BOOT)
        self.record.write_text(json.dumps(state));self.sock.close()
        self.assertEqual(self.inspect(),{'state':'needs-resume'})
        with self.assertRaisesRegex(RuntimeError,'did not become ready'):self.launch()
        self.assertTrue(self.marker.exists())
        self.assertEqual(json.loads(self.record.read_text())['resumedFromBootId'],OLD_BOOT)

    def test_second_boot_with_unrelated_lineage_is_rejected(self):
        state=json.loads(self.original)
        state.update(bootId='11111111-1111-1111-1111-111111111111',resumedFromBootId='22222222-2222-2222-2222-222222222222')
        self.record.write_text(json.dumps(state));self.original=self.record.read_bytes();self.sock.close()
        with self.assertRaises(RuntimeError):self.inspect()
        self.assert_refused()

    def test_invalid_checkpoint_has_no_remote_side_effects(self):
        ws=FakeWorkspace(); wallets=RemoteNativeWallets(ws,BINARY,18081,'testnet')
        with self.assertRaises(ValueError): wallets.resume_after_boot({})
        self.assertEqual(ws.calls,[])

if __name__ == '__main__': unittest.main()
