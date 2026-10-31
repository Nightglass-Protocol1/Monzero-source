"""Reject ambiguous paired-chain acceptance modes before starting any nodes."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import test_bch_native_relay_paired as runner


class ModeChecks(unittest.TestCase):
    def test_invalid_modes_never_start_chains(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / 'probe'
            probe.write_text('fixture')
            cases = [
                (False, True, {}, 'requires --owned-observer'),
                (True, True, {
                    'MONZERO_BCH_QT_NATIVE_PREPARATION_PROBE': str(probe),
                    'MONZERO_BCH_QT_NATIVE_PREPARATION_PROBE_SHA256': 'bad',
                }, 'digest mismatch'),
                (True, False, {'MONZERO_BCH_QT_NATIVE_RELAY_PROBE': str(probe)}, 'Select --qt'),
                (False, False, {'MONZERO_BCH_QT_NATIVE_RELAY_PROBE': str(probe)}, 'Select --qt'),
                (False, False, {'MONZERO_BCH_QT_NATIVE_RELAY_PROBE': ''}, 'Select --qt'),
                (False, False, {'MONZERO_BCH_NATIVE_OWNED_OBSERVER_QT_PAIRED': '1'}, 'Select --qt'),
            ]
            for owned, qt, environment, message in cases:
                with self.subTest(owned=owned, qt=qt, environment=environment):
                    with patch.dict(os.environ, environment, clear=True), patch.object(
                        runner.tempfile, 'mkdtemp', side_effect=RuntimeError('unexpected chain startup')
                    ), self.assertRaisesRegex(AssertionError, message):
                        runner.run(SimpleNamespace(report=root/'absent.json', owned_observer=owned, qt=qt))

    def test_optimized_python_cannot_report_acceptance(self):
        result = subprocess.run(
            [sys.executable, '-O', '-c', 'import test_bch_native_relay_paired as r; r.run(None)'],
            cwd=Path(__file__).resolve().parent, capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Acceptance checks require Python without optimization', result.stderr)


if __name__ == '__main__':
    unittest.main()
