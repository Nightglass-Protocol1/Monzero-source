#!/usr/bin/env python3
"""Offline subprocess checks for native-bch-admission; never starts a node.

Requires Python cryptography. Pass --helper with the built native helper path.
Successful negotiation admission needs the separate retained-negotiation fixture;
this suite checks quote review, consent rejection and missing-state rejection.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
import uuid

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


class AdmissionHelperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bch-admission-stdio-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name).resolve()
        self.helper = HELPER
        self.config_path = self.directory / "config.json"
        self.quote_path = self.directory / "quote.json"
        self.now = int(time.time())
        keys = [ed25519.Ed25519PrivateKey.from_private_bytes(bytes([i]) * 32) for i in (1, 2)]
        identities = [key.public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw) for key in keys]
        contract = lambda i: ec.derive_private_key(int.from_bytes(bytes([i]) * 32, "big"),
            ec.SECP256K1()).public_key().public_bytes(serialization.Encoding.X962,
                serialization.PublicFormat.CompressedPoint).hex()
        address = (CORE / "build/eigenwallet-engine-port/swap-core/src/xmz_address_vectors.txt").read_text().splitlines()[0]
        policy = dict(bch_network="bch-regtest", bch_genesis="b" * 64,
            xmz_network="xmz-mainnet", xmz_genesis="a" * 64,
            bch_confirmations=2, xmz_confirmations=10, cancel_blocks=20, punish_blocks=30,
            maximum_bch_atoms=1000000, maximum_xmz_atoms=2000000,
            maximum_bch_funding_fee=5000, maximum_xmz_fee=1000,
            minimum_covenant_fee=500, maximum_covenant_fee=3000, minimum_output_atoms=546)
        terms = dict(schema="monzero-native-bch-xmz-quote-v1", quoteId=str(uuid.uuid4()),
            swapId=str(uuid.uuid4()), bchFunder=identities[0].hex(), xmzFunder=identities[1].hex(),
            bchNetwork=policy["bch_network"], bchGenesis=policy["bch_genesis"],
            xmzNetwork=policy["xmz_network"], xmzGenesis=policy["xmz_genesis"],
            bchAtoms="100000", xmzAtoms="101000", xmzPayoutAtoms="100000",
            bchPayoutScript="76a914" + "03" * 20 + "88ac",
            bchRefundScript="76a914" + "04" * 20 + "88ac", xmzPayout=address, xmzRefund=address,
            bchSigningKey=contract(3), xmzSigningKey=contract(4), bchFundingFeeMaxAtoms="5000",
            bchLockFeeAtoms="1000", bchRecoveryFeeAtoms="2000", xmzFundingFeeMaxAtoms="1000",
            xmzClaimFeeMaxAtoms="1000", xmzRefundFeeMaxAtoms="1000", bchConfirmations="2",
            xmzConfirmations="10", cancelBlocks="20", punishBlocks="30", bchMinimumOutputAtoms="546",
            expiresAt=str(self.now + 1800), appFeeXmzAtoms="0")
        payload = canonical(terms)
        signatures = []
        for key, role in zip(keys, ("bch-funder", "xmz-funder")):
            domain = terms["schema"] + "-" + role
            message = f"MONZERO-DEX-OFFER-V1\n{len(domain)}:{domain}{len(payload)}:".encode() + payload
            signatures.append(key.sign(message).hex())
        commitment = hashlib.sha256(terms["schema"].encode() + b"\0" + payload).hexdigest()
        self.record = dict(schema="monzero-bch-quote-record-v1", admittedAt=self.now,
            commitment=commitment, terms=terms, bchSignature=signatures[0], xmzSignature=signatures[1])
        self.config = dict(schema="monzero-private-bch-admission-v1", helper=str(self.helper),
            helperDigest=hashlib.sha256(self.helper.read_bytes()).hexdigest(), quoteFile=str(self.quote_path),
            quoteCommitment=commitment, admittedAt=self.now, tradeId=terms["swapId"],
            identityPins=[list(identity) for identity in identities], policy=policy, role="native-funder",
            metadataDirectory=str(self.directory / "absent-metadata"),
            snapshotDirectory=str(self.directory / "absent-snapshots"),
            negotiationPins={name: [i] * 32 for i, name in enumerate(
                ("initial", "selection", "outgoing", "received", "agreement"), 1)})
        self.save(self.config_path, self.config)
        self.save(self.quote_path, self.record)

    def save(self, path, value):
        path.write_bytes(canonical(value))
        path.chmod(0o600)

    def invoke(self, action="review", commitment=None, raw=None):
        request_id = str(uuid.uuid4())
        request = dict(schema=f"monzero-bch-admission-{action}-request-v1", requestId=request_id)
        if commitment is not None:
            request["commitment"] = commitment
        result = subprocess.run([str(self.helper), f"--admission-{action}-stdio-v1", str(self.config_path)],
            input=canonical(request) if raw is None else raw, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=20)
        self.assertFalse(Path(self.config["metadataDirectory"]).exists())
        self.assertFalse(Path(self.config["snapshotDirectory"]).exists())
        return result, request_id

    def rejected(self, result):
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertEqual(result.stderr, b"BCH admission request failed; inspect retained state before retrying\n")

    def test_review_returns_correlated_verified_public_terms(self):
        result, request_id = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        value = json.loads(result.stdout)
        self.assertEqual(value["schema"], "monzero-bch-admission-review-response-v1")
        self.assertEqual(value["requestId"], request_id)
        self.assertEqual(value["tradeId"], self.config["tradeId"])
        self.assertEqual(value["review"]["details"]["terms"], self.record["terms"])
        self.assertFalse(value["admitted"])
        self.assertFalse(value["executable"])
        self.assertFalse(value["review"]["details"]["chainObserved"])
        self.assertFalse(value["review"]["details"]["negotiationVerified"])

    def test_submit_requires_exact_consent_and_existing_negotiation(self):
        result, _ = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        commitment = json.loads(result.stdout)["review"]["commitment"]
        for approved in (None, "00" * 32, commitment):
            self.rejected(self.invoke("submit", approved)[0])
        self.config["role"] = "bch-funder"
        self.save(self.config_path, self.config)
        self.rejected(self.invoke("submit", commitment)[0])

    def test_status_requires_saved_admission_and_rejects_approval_fields(self):
        self.rejected(self.invoke("status")[0])
        self.rejected(self.invoke("status", "ab" * 32)[0])

    def test_request_size_and_unknown_fields_fail_without_output(self):
        for raw in (b"", b" " * 513, canonical(dict(schema="wrong", requestId=str(uuid.uuid4()))),
            canonical(dict(schema="monzero-bch-admission-review-request-v1", requestId=str(uuid.uuid4()),
                funding=True))):
            self.rejected(self.invoke(raw=raw)[0])

    def test_modified_signed_record_and_identity_pins_fail(self):
        self.config["identityPins"].reverse()
        self.save(self.config_path, self.config)
        self.rejected(self.invoke()[0])
        self.config["identityPins"].reverse()
        self.save(self.config_path, self.config)
        self.record["terms"]["bchAtoms"] = "100001"
        self.save(self.quote_path, self.record)
        self.rejected(self.invoke()[0])

    def test_funding_review_requires_retained_admission_without_writes(self):
        wrapper_path = self.directory / "funding.json"
        self.save(wrapper_path, dict(schema="monzero-private-bch-funding-v1",
            admissionConfigFile=str(self.config_path), deploymentCommitment="11" * 32))
        request = dict(schema="monzero-bch-funding-review-request-v1", requestId=str(uuid.uuid4()))
        for role in ("native-funder", "bch-funder"):
            self.config["role"] = role
            self.save(self.config_path, self.config)
            before = {p.name: p.read_bytes() for p in self.directory.iterdir()}
            result = subprocess.run([str(self.helper), "--funding-review-stdio-v1", str(wrapper_path)],
                input=canonical(request), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
            self.rejected(result)
            self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir()})

    def test_unsafe_configuration_and_quote_files_fail(self):
        for path in (self.config_path, self.quote_path):
            path.chmod(0o644)
            self.rejected(self.invoke()[0])
            path.chmod(0o600)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--core", type=Path, default=Path(__file__).resolve().parents[3])
    args, remaining = parser.parse_known_args()
    HELPER = args.helper.resolve(strict=True)
    CORE = args.core.resolve(strict=True)
    unittest.main(argv=[__file__, *remaining])
