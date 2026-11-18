import importlib.util
import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("create-bitcoin-lock-psbt-fixture.py")
SPEC = importlib.util.spec_from_file_location("bitcoin_lock_psbt_fixture", SCRIPT)
FIXTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURE)


def transaction():
    witness = "21" + FIXTURE.KEY_A + "ad21" + FIXTURE.KEY_B + "ac"
    script = "002046cb3ce1c236a7be1f947851bb8d6214b4c6330d8a8f0c78fe98c994e20249ef"
    return {
        "version": 2, "locktime": 0,
        "vin": [{"txid": "d" * 64, "vout": 1,
                 "scriptSig": {"asm": "", "hex": ""}, "sequence": 4294967295}],
        "vout": [
            {"value": 0.00010000, "n": 0,
             "scriptPubKey": {"address": "shared", "hex": script,
                              "type": "witness_v0_scripthash"}},
            {"value": 0.00002000, "n": 1,
             "scriptPubKey": {"address": "change", "hex": "0014" + "c" * 40,
                              "type": "witness_v0_keyhash"}},
        ],
        "_witness_for_test": witness,
    }


class LockPsbtFixtureTests(unittest.TestCase):
    def test_commitment_matches_cpp_vector(self):
        value = transaction()
        value.pop("_witness_for_test")
        self.assertEqual(FIXTURE.template_commitment(value),
                         "84c03f0b4da834201c1e6425b540fd82a5651dc1f25c268aca1e887415e7705e")

    def test_rejects_signed_input(self):
        value = transaction()
        value.pop("_witness_for_test")
        value["vin"][0]["txinwitness"] = ["signature"]
        with self.assertRaisesRegex(RuntimeError, "signing material"):
            FIXTURE.template_commitment(value)


if __name__ == "__main__":
    unittest.main()
