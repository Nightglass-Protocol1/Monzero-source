#!/usr/bin/env python3
"""Regression checks for cleanup of the owned proof-fixture process group."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

from test_bch_native_proof_paired_qt import positive_timeout, stop_fixture_tree


class FixtureCleanupTests(unittest.TestCase):
    def check_cleanup(self, leader_exits):
        helper = "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(60)"
        leader = (
            "import subprocess,sys,time; "
            f"p=subprocess.Popen([sys.executable,'-c',{helper!r}]); "
            "print(p.pid,flush=True); "
            + ("sys.exit(0)" if leader_exits else "time.sleep(60)")
        )
        process = subprocess.Popen(
            [sys.executable, "-c", leader], start_new_session=True,
            stdout=subprocess.PIPE, text=True,
        )
        try:
            helper_pid = int(process.stdout.readline())
            self.assertEqual(os.getpgid(helper_pid), process.pid)
            if leader_exits:
                self.assertEqual(process.wait(timeout=5), 0)
            stop_fixture_tree(process)
            deadline = time.monotonic() + 5
            while True:
                try:
                    state = Path(f"/proc/{helper_pid}/stat").read_text().split(") ", 1)[1].split()[0]
                except FileNotFoundError:
                    break
                if state == "Z":
                    break
                self.assertLess(time.monotonic(), deadline, "fixture helper is still running")
                time.sleep(0.01)
            self.assertIsNotNone(process.returncode)
        finally:
            stop_fixture_tree(process)
            process.stdout.close()

    def test_running_leader_and_helper_are_stopped(self):
        self.check_cleanup(False)

    def test_helper_is_stopped_after_leader_exits(self):
        self.check_cleanup(True)

    def test_deadline_is_bounded(self):
        for value in ("1", "1800", "21600"):
            self.assertEqual(positive_timeout(value), int(value))
        for value in ("0", "-1", "21601"):
            with self.assertRaises(argparse.ArgumentTypeError):
                positive_timeout(value)


if __name__ == "__main__":
    unittest.main()
