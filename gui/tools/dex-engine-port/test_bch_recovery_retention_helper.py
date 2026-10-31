#!/usr/bin/env python3
"""Funding-fixture hook for real recovery-retention helper subprocesses.

Set MONZERO_BCH_QT_FUNDING_PROBE to this executable (with its SHA256 pin),
MONZERO_BCH_ADMISSION_HELPER to the new helper, and
MONZERO_BCH_RECOVERY_RETENTION_REPORT to a new result file.
Only copied fixture state is changed; the enclosing Rust test checks originals.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
from test_bch_refund_qt_retained import canonical, digest, save


def contents(path):
    return {p.name: p.read_bytes() for p in path.iterdir()}


def main():
    helper = Path(os.environ["MONZERO_BCH_ADMISSION_HELPER"])
    report = Path(os.environ["MONZERO_BCH_RECOVERY_RETENTION_REPORT"])
    request = sys.stdin.buffer.read(513)
    if not report.exists():
        assert sys.argv[1] == "--funding-review-stdio-v1"
        funding = json.loads(Path(sys.argv[2]).read_bytes())
        admission = json.loads(Path(funding["admissionConfigFile"]).read_bytes())
        assert str(helper) == admission["helper"] and digest(helper) == admission["helperDigest"]
        with tempfile.TemporaryDirectory(prefix="bch-recovery-retention-") as temporary:
            root = Path(temporary).resolve()
            for key, name in (("metadataDirectory", "metadata"), ("snapshotDirectory", "snapshots")):
                shutil.copytree(admission[key], root / name)
                admission[key] = str(root / name)
            metadata, snapshots = root / "metadata", root / "snapshots"
            prefix = admission["tradeId"] + ".bch-funding-"
            pin, private = metadata / (prefix + "recovery-pin"), snapshots / (prefix + "private-recovery")
            pin.unlink(); private.unlink()
            config = root / "admission.json"
            save(config, admission)

            def invoke(action, commitment=None, success=True):
                identity = str(uuid.uuid4())
                value = dict(schema=f"monzero-bch-recovery-retention-{action}-request-v1", requestId=identity)
                if commitment is not None:
                    value["commitment"] = commitment
                result = subprocess.run([str(helper), f"--recovery-retention-{action}-stdio-v1", str(config)],
                    input=canonical(value), capture_output=True, timeout=180)
                assert (result.returncode == 0) == success, result.stderr.decode(errors="replace")
                if not success:
                    assert not result.stdout
                    return None
                assert not result.stderr and len(result.stdout) <= 16384
                response = json.loads(result.stdout)
                assert set(response) == {"schema", "requestId", "tradeId", "executable", "chainObserved", "review", "recovery"}
                assert response["requestId"] == identity and response["tradeId"] == admission["tradeId"]
                assert response["schema"] == f"monzero-bch-recovery-retention-{action}-response-v1"
                assert response["executable"] is False and response["chainObserved"] is False
                return response

            before = contents(metadata), contents(snapshots)
            assert invoke("status")["recovery"] is None
            reviewed = invoke("review")
            assert reviewed["recovery"] is None
            details = reviewed["review"]["details"]
            assert set(details) == {"schema", "action", "swapId", "quote", "admission", "funding", "receipt",
                "fundingTxid", "cancelTxid", "refundTxid", "punishTxid"}
            invoke("submit", "00" * 32, success=False)
            invoke("review", "00" * 32, success=False)
            assert before == (contents(metadata), contents(snapshots))
            commitment = reviewed["review"]["commitment"]
            qt_probe = os.environ.get("MONZERO_BCH_RECOVERY_RETENTION_QT_PROBE")
            qt_saved = None
            if qt_probe:
                probe = Path(qt_probe)
                assert digest(probe) == os.environ["MONZERO_BCH_RECOVERY_RETENTION_QT_PROBE_SHA256"]
                def qt(action):
                    result = subprocess.run([str(probe), action, str(config)], capture_output=True, timeout=600)
                    assert result.returncode == 0, result.stderr.decode(errors="replace")
                    assert not result.stderr
                    return json.loads(result.stdout)
                qt_saved = qt("--accept")["recovery"]
                qt_before = contents(metadata), contents(snapshots)
                assert qt("--status")["recovery"] == qt_saved
                assert qt_before == (contents(metadata), contents(snapshots))
            accepted = invoke("submit", commitment)
            assert accepted["review"] == reviewed["review"]
            receipt = accepted["recovery"]
            if qt_saved is not None:
                assert receipt == qt_saved
            assert set(receipt) == {"commitment", "preparedAt", "cancelTxid", "refundTxid", "punishTxid"}
            for field in ("cancelTxid", "refundTxid", "punishTxid"):
                assert receipt[field] == details[field]
            retained = contents(metadata), contents(snapshots)
            assert invoke("submit", commitment)["recovery"] == receipt
            status = invoke("status")
            assert status["review"] is None and status["recovery"] == receipt
            assert retained == (contents(metadata), contents(snapshots))
            pin.unlink()
            assert invoke("status")["recovery"] is None
            assert invoke("submit", commitment)["recovery"] == receipt
            assert retained == (contents(metadata), contents(snapshots))
            original = private.read_bytes()
            private.write_bytes(b"{}")
            invoke("status", success=False)
            private.write_bytes(original)
            assert invoke("status")["recovery"] == receipt
            report.write_bytes(canonical(dict(schema="monzero-bch-recovery-retention-helper-test-v1",
                accepted=True, qtAcceptanceAndRestart=qt_saved is not None, wrongApprovalNoWrites=True, exactRetry=True, orphanPublicationRecovered=True,
                tamperedSnapshotRejected=True, publicFieldsOnly=True, helperSha256=digest(helper),
                executable=False, fullCrossChainSettlement=False)))
    result = subprocess.run([str(helper), *sys.argv[1:]], input=request, capture_output=True, timeout=180)
    sys.stdout.buffer.write(result.stdout);sys.stderr.buffer.write(result.stderr)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
