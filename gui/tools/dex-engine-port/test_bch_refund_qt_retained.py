#!/usr/bin/env python3
"""Attach the refund Qt worker to the retained Rust funding fixture.

Only copies fixture-owned private state; no wallet RPC or public network access.
The original fixture also verifies that its metadata and snapshots are unchanged.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def save(path, value):
    path.write_bytes(canonical(value))
    path.chmod(0o600)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def adapter():
    report = Path(os.environ["MONZERO_BCH_REFUND_QT_REPORT"])
    helper = Path(os.environ["MONZERO_BCH_ADMISSION_HELPER"])
    request = sys.stdin.buffer.read(513)
    if not report.exists():
        assert sys.argv[1] == "--funding-review-stdio-v1"
        funding = json.loads(Path(sys.argv[2]).read_bytes())
        admission = json.loads(Path(funding["admissionConfigFile"]).read_bytes())
        probe = Path(os.environ["MONZERO_BCH_REFUND_QT_PROBE"])
        assert digest(probe) == os.environ["MONZERO_BCH_REFUND_QT_PROBE_SHA256"]
        assert str(helper) == admission["helper"]
        assert digest(helper) == admission["helperDigest"]
        with tempfile.TemporaryDirectory(prefix="bch-refund-qt-retained-") as temporary:
            root = Path(temporary).resolve()
            for key, name in (("metadataDirectory", "metadata"), ("snapshotDirectory", "snapshots")):
                shutil.copytree(admission[key], root / name)
                admission[key] = str(root / name)
            receipt_path = root / "metadata" / (admission["tradeId"] + ".bch-refund-bch-funder-authenticated")
            old_receipt = json.loads(receipt_path.read_bytes())
            receipt_path.unlink()
            message = root / "peer.json"
            save(message, dict(schema="monzero-bch-peer-refund-protection-v1",
                frame=old_receipt["signed"]["bytes"], signature=old_receipt["signed"]["signature"]))
            config = root / "admission.json"
            save(config, admission)
            wrapper = root / "refund.json"
            save(wrapper, dict(schema="monzero-private-bch-refund-protection-v1",
                admissionConfigFile=str(config), peerMessageFile=str(message)))
            def invoke(mode):
                result = subprocess.run([str(probe), mode, str(wrapper)], capture_output=True, timeout=600)
                assert result.returncode == 0, result.stderr.decode(errors="replace")
                assert not result.stderr
                return json.loads(result.stdout)
            original_message = json.loads(message.read_bytes())
            altered_message = json.loads(message.read_bytes())
            altered_message["signature"][0] ^= 1
            save(message, altered_message)
            before = {p.name: p.read_bytes() for p in (root / "metadata").iterdir()}
            assert invoke("--reject-review") == {"rejected": True}
            assert not receipt_path.exists()
            assert before == {p.name: p.read_bytes() for p in (root / "metadata").iterdir()}
            save(message, original_message)
            accepted = invoke("--accept")
            assert receipt_path.is_file()
            retained = receipt_path.read_bytes()
            message.unlink()
            reopened = invoke("--status")
            assert accepted["receipt"] == reopened["receipt"]
            assert receipt_path.read_bytes() == retained
            report.write_bytes(canonical(dict(schema="monzero-bch-refund-qt-retained-test-v1",
                accepted=True, tamperedSignatureRejectedWithoutReceipt=True,
                restartWithoutPeerMessage=True, receiptUnchanged=True,
                tradeId=accepted["tradeId"], receipt=accepted["receipt"],
                helperSha256=digest(helper), probeSha256=digest(probe),
                fullCrossChainSettlement=False, executable=False)))
    result = subprocess.run([str(helper), *sys.argv[1:]], input=request, capture_output=True, timeout=180)
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    return result.returncode


def main():
    if len(sys.argv) > 1 and sys.argv[1].startswith("--funding-"):
        return adapter()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("helper", "machine-tests", "qt-probe", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    helper, machine, probe = (p.resolve(strict=True) for p in (args.helper, args.machine_tests, args.qt_probe))
    report = args.report.resolve()
    if report.exists():
        parser.error("report already exists; preserve prior evidence and choose a new path")
    script = Path(__file__).resolve()
    environment = os.environ.copy()
    environment.update(MONZERO_BCH_ADMISSION_HELPER=str(helper),
        MONZERO_BCH_ADMISSION_HELPER_SHA256=digest(helper),
        MONZERO_BCH_QT_FUNDING_PROBE=str(script), MONZERO_BCH_QT_FUNDING_PROBE_SHA256=digest(script),
        MONZERO_BCH_REFUND_QT_PROBE=str(probe), MONZERO_BCH_REFUND_QT_PROBE_SHA256=digest(probe),
        MONZERO_BCH_REFUND_QT_REPORT=str(report))
    result = subprocess.run([str(machine),
        "bch_funding_helper_reviews_retained_transaction_without_rpc_or_writes", "--ignored", "--nocapture"],
        env=environment)
    if result.returncode:
        return result.returncode
    assert report.exists(), "retained fixture did not invoke the Qt adapter"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
