#!/usr/bin/env python3
"""Run Qt admission tests with an independent signed quote and real Rust helper.

No nodes or wallet services are started. The fixture intentionally has no saved
negotiation, so the real-helper case must reject submit and disable resubmission.
"""
import argparse
import os
from pathlib import Path
import subprocess

import test_bch_admission_helper as fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--qt-tests", type=Path, required=True)
    parser.add_argument("--core", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    fixtures.HELPER = args.helper.resolve(strict=True)
    fixtures.CORE = args.core.resolve(strict=True)
    case = fixtures.AdmissionHelperTests("test_review_returns_correlated_verified_public_terms")
    try:
        case.setUp()
        environment = os.environ.copy()
        environment["MONZERO_BCH_QT_FIXTURE"] = str(case.config_path)
        return subprocess.run([str(args.qt_tests.resolve(strict=True))], env=environment,
            timeout=120).returncode
    finally:
        case.doCleanups()


if __name__ == "__main__":
    raise SystemExit(main())
