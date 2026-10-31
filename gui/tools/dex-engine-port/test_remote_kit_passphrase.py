import pathlib
import subprocess
import sys
import tempfile
import unittest
from remote_trade_workspace import KIT_RUN

class PassphraseTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=pathlib.Path(self.temp.name);(self.root/'kit-ltc').mkdir(mode=0o700)
        (self.root/'trade_kit.py').write_text('import pathlib; pathlib.Path(__file__).with_name("ran").touch()')
    def run_kit(self,mode):
        return subprocess.run([sys.executable,'-c',KIT_RUN,str(self.root),mode],capture_output=True)
    def test_missing_attached_passphrase_never_runs(self):
        self.assertNotEqual(self.run_kit('attached').returncode,0)
        self.assertFalse((self.root/'ran').exists())
    def test_existing_identity_without_password_never_regenerates(self):
        (self.root/'kit-ltc/identity.bin').write_bytes(b'fixture')
        self.assertNotEqual(self.run_kit('fresh').returncode,0)
        self.assertFalse((self.root/'ran').exists())
    def test_new_identity_can_generate_password(self):
        self.assertEqual(self.run_kit('fresh').returncode,0)
        self.assertTrue((self.root/'ran').exists())
    def test_private_existing_password_is_preserved(self):
        p=self.root/'kit-ltc/identity.passphrase';p.write_text('fixture');p.chmod(0o600)
        self.assertEqual(self.run_kit('attached').returncode,0)
        self.assertEqual(p.read_text(),'fixture')
    def test_public_password_rejected(self):
        p=self.root/'kit-ltc/identity.passphrase';p.write_text('fixture');p.chmod(0o644)
        self.assertNotEqual(self.run_kit('attached').returncode,0)
        self.assertFalse((self.root/'ran').exists())

if __name__=='__main__':unittest.main()
