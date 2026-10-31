#!/usr/bin/env python3
"""Retained funding-fixture hook for offline refund helper checks.

Use as MONZERO_BCH_QT_FUNDING_PROBE with its digest pin; set
MONZERO_BCH_REFUND_RELEASE_REPORT to a new report path. The enclosing fixture has
stopped its mock node before invoking this driver. No refund is relayed.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid
from test_bch_refund_qt_retained import canonical, digest, save


def main():
    helper = Path(os.environ["MONZERO_BCH_ADMISSION_HELPER"])
    report = Path(os.environ["MONZERO_BCH_REFUND_RELEASE_REPORT"])
    request = sys.stdin.buffer.read(513)
    if not report.exists():
        assert sys.argv[1] == "--funding-review-stdio-v1"
        funding_path = Path(sys.argv[2])
        funding = json.loads(funding_path.read_bytes())
        admission = json.loads(Path(funding["admissionConfigFile"]).read_bytes())
        assert str(helper) == admission["helper"] and digest(helper) == admission["helperDigest"]
        def stores():
            return [{p.name: p.read_bytes() for p in Path(admission[key]).iterdir()}
                for key in ("metadataDirectory", "snapshotDirectory")]
        before = stores()
        wrapper = funding_path.parent / "refund-review.json"
        config = dict(funding, schema="monzero-private-bch-refund-v1", safetyBlocks=5)
        save(wrapper, config)
        def invoke(family, action, path, commitment=None, success=True):
            identity = str(uuid.uuid4())
            value = dict(schema=f"monzero-bch-{family}-{action}-request-v1", requestId=identity)
            if commitment is not None:
                value["commitment"] = commitment
            result = subprocess.run([str(helper), f"--{family}-{action}-stdio-v1", str(path)],
                input=canonical(value), capture_output=True, timeout=180)
            assert (result.returncode == 0) == success, result.stderr.decode(errors="replace")
            if not success:
                assert not result.stdout
                return None
            assert not result.stderr and len(result.stdout) <= 16384
            response = json.loads(result.stdout)
            assert response["requestId"] == identity and response["tradeId"] == admission["tradeId"]
            assert response["schema"] == f"monzero-bch-{family}-{action}-response-v1"
            assert response["executable"] is False and response["chainObserved"] is False
            return response
        recovery = invoke("recovery-retention", "status", funding["admissionConfigFile"])["recovery"]
        review = invoke("refund", "review", wrapper)
        assert set(review) == {"schema", "requestId", "tradeId", "executable", "chainObserved", "review", "relay", "observation"}
        details = review["review"]["details"]
        assert details["action"] == "broadcast-bch-refund"
        assert details["revealsNativeSpendShare"] is True
        assert details["punishmentMarginBlocks"] == 5
        assert len(details) == 20
        assert isinstance(details["fundingOutpoint"], str) and isinstance(details["recoveryOutpoint"], str)
        assert details["txid"] == recovery["refundTxid"]
        assert bytes(details["bchDeployment"]).hex() == config["deploymentCommitment"]
        assert review["review"]["commitment"] == hashlib.sha256(details["schema"].encode()+b'\0'+canonical(details)).hexdigest()
        assert review["relay"] == dict(state="not-attempted", txid=details["txid"], confirmed=False)
        assert review["observation"] is None
        assert invoke("refund", "status", wrapper)["review"] == review["review"]
        invoke("refund", "review", wrapper, "00"*32, success=False)
        save(wrapper, dict(config, deploymentCommitment="00"*32))
        invoke("refund", "review", wrapper, success=False)
        invoke("refund", "status", wrapper, success=False)
        for margin in (0, 28, 30, 4294967295):
            save(wrapper, dict(config, safetyBlocks=margin))
            invoke("refund", "review", wrapper, success=False)
            invoke("refund", "status", wrapper, success=False)
        save(wrapper, dict(config, safetyBlocks=6))
        changed = invoke("refund", "review", wrapper)
        assert changed["review"]["commitment"] != review["review"]["commitment"]
        save(wrapper, config)
        submission = funding_path.parent / "refund-submit.json"
        save(submission, dict(schema="monzero-private-bch-refund-submission-v1",
            refundConfigFile=str(wrapper), rpcFile=str(funding_path.parent / "absent-rpc.json")))
        invoke("refund", "submit", submission, "00"*32, success=False)
        invoke("refund", "observe", submission, success=False)
        assert stores() == before
        report.write_bytes(canonical(dict(schema="monzero-bch-refund-helper-test-v1", offlineReview=True,
            savedStatus=True, recoveryTxidMatched=True, wrongDeploymentRejected=True,
            malformedApprovalRejected=True, unsafeMarginsRejected=True, marginBindsApproval=True, noStoreChanges=True, helperSha256=digest(helper),
            actualRelayVerified=False, chainObservationVerified=False, executable=False)))
    result = subprocess.run([str(helper), *sys.argv[1:]], input=request, capture_output=True, timeout=180)
    sys.stdout.buffer.write(result.stdout);sys.stderr.buffer.write(result.stderr)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
