import json
import os
import pathlib
import tempfile
import unittest
from remote_recovery_checkpoint import RecoveryPhaseJournal, read_phase_journal, read_private_json, phase_journal_digest

class JournalReaderTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        self.root=pathlib.Path(temp.name);self.path=self.root/'phases'
        self.journal=RecoveryPhaseJournal(self.path)
        self.journal.record('power-off-requested',{'oldBootId':'fixture'})
        self.journal.record('power-off-confirmed',{})
        self.first=self.path/'000-power-off-requested.json'
    def test_round_trip_is_read_only(self):
        before={p.name:p.read_bytes() for p in self.path.iterdir()}
        self.assertEqual([x['phase'] for x in read_phase_journal(self.path)],['power-off-requested','power-off-confirmed'])
        self.assertEqual(before,{p.name:p.read_bytes() for p in self.path.iterdir()})
    def test_attach_appends_without_rewriting_old_entries(self):
        before=self.first.read_bytes()
        attached=RecoveryPhaseJournal.attach(self.path)
        attached.record('start-requested',{})
        self.assertEqual(self.first.read_bytes(),before)
        self.assertEqual(read_phase_journal(self.path)[-1]['sequence'],2)

    def test_stale_attached_writer_cannot_add_duplicate_sequence(self):
        attached=RecoveryPhaseJournal.attach(self.path)
        self.journal.record('start-requested',{})
        with self.assertRaisesRegex(RuntimeError,'advanced'):attached.record('guest-ready',{})
        self.assertEqual(len(read_phase_journal(self.path)),3)

    def test_unpublished_torn_temp_is_not_a_phase(self):
        pending=self.path/('.recovery-pending-'+'a'*32)
        pending.write_bytes(b'{');pending.chmod(0o600)
        self.assertEqual(len(read_phase_journal(self.path)),2)
        attached=RecoveryPhaseJournal.attach(self.path);attached.record('start-requested',{})
        self.assertEqual(len(read_phase_journal(self.path)),3)
        self.assertEqual(pending.read_bytes(),b'{')

    def test_unpublished_symlink_is_rejected(self):
        (self.path/('.recovery-pending-'+'a'*32)).symlink_to(self.first)
        with self.assertRaises(ValueError):read_phase_journal(self.path)

    def test_attach_rejects_history_advanced_after_observation(self):
        digest=phase_journal_digest(read_phase_journal(self.path))
        self.journal.record('start-requested',{})
        with self.assertRaisesRegex(RuntimeError,'changed after observation'):
            RecoveryPhaseJournal.attach(self.path,expected_digest=digest)

    def test_same_length_changed_history_rejected_on_append(self):
        attached=RecoveryPhaseJournal.attach(self.path)
        value=json.loads(self.first.read_text());value['evidence']['oldBootId']='changed'
        self.first.write_text(json.dumps(value))
        with self.assertRaisesRegex(RuntimeError,'advanced'):attached.record('start-requested',{})
        self.assertEqual(len(read_phase_journal(self.path)),2)

    def test_matching_observation_can_attach_and_append(self):
        digest=phase_journal_digest(read_phase_journal(self.path))
        attached=RecoveryPhaseJournal.attach(self.path,expected_digest=digest)
        attached.record('start-requested',{})
        self.assertEqual(attached.history_digest,phase_journal_digest(read_phase_journal(self.path)))

    def test_missing_entry_rejected(self):
        self.first.unlink()
        with self.assertRaises(ValueError):read_phase_journal(self.path)
    def test_same_sequence_twice_rejected(self):
        (self.path/'000-other.json').write_bytes(self.first.read_bytes())
        with self.assertRaises(ValueError):read_phase_journal(self.path)
    def test_partial_write_rejected(self):
        self.first.write_bytes(b'{"schema":')
        with self.assertRaises(ValueError):read_phase_journal(self.path)
    def test_duplicate_json_key_rejected(self):
        self.first.write_bytes(b'{"phase":"one","phase":"two"}')
        with self.assertRaisesRegex(ValueError,'Duplicate'):read_phase_journal(self.path)
    def test_symlink_rejected(self):
        target=self.root/'record';self.first.rename(target);self.first.symlink_to(target)
        with self.assertRaises(OSError):read_phase_journal(self.path)
    def test_hardlink_rejected(self):
        os.link(self.first,self.root/'alias')
        with self.assertRaises(ValueError):read_phase_journal(self.path)
    def test_public_record_rejected(self):
        self.first.chmod(0o644)
        with self.assertRaises(ValueError):read_phase_journal(self.path)
    def test_public_directory_rejected(self):
        self.path.chmod(0o755)
        with self.assertRaises(ValueError):read_phase_journal(self.path)
    def test_phase_name_must_match_file(self):
        v=json.loads(self.first.read_text());v['phase']='start-requested';self.first.write_text(json.dumps(v))
        with self.assertRaises(ValueError):read_phase_journal(self.path)
    def test_boolean_sequence_rejected(self):
        v=json.loads(self.first.read_text());v['sequence']=False;self.first.write_text(json.dumps(v))
        with self.assertRaises(ValueError):read_phase_journal(self.path)
    def test_nonfinite_number_rejected(self):
        self.first.write_text('{"value":NaN}')
        with self.assertRaises(ValueError):read_private_json(self.first)
    def test_unknown_phase_preserved_for_coordinator_validation(self):
        self.journal.record('unknown-phase',{})
        self.assertEqual(read_phase_journal(self.path)[-1]['phase'],'unknown-phase')

if __name__=='__main__':unittest.main()
