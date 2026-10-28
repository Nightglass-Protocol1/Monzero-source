import unittest
from health import validate_node

GENESIS = "a" * 64


class HealthTests(unittest.TestCase):
    def test_bitcoin(self):
        info = dict(chain="regtest", initialblockdownload=False, blocks=101, headers=101)
        self.assertEqual(validate_node("BTC", info, GENESIS, GENESIS), [])
        for key, value in [("chain", "main"), ("initialblockdownload", 0),
                           ("blocks", True), ("headers", 102), ("headers", -1)]:
            with self.subTest(key=key):
                self.assertTrue(validate_node("BTC", dict(info, **{key: value}), GENESIS, GENESIS))

    def test_monzero(self):
        info = dict(nettype="fakechain", status="OK", synchronized=True,
                    untrusted=False, height=101, target_height=0)
        self.assertEqual(validate_node("XMZ", info, GENESIS, GENESIS), [])
        for key, value in [("nettype", "mainnet"), ("synchronized", 1),
                           ("status", "BUSY"), ("untrusted", True)]:
            self.assertTrue(validate_node("XMZ", dict(info, **{key: value}), GENESIS, GENESIS))
        for key in info:
            missing = info.copy()
            del missing[key]
            self.assertTrue(validate_node("XMZ", missing, GENESIS, GENESIS))

    def test_unknown_and_genesis(self):
        for response in [None, [], {}, "OK"]:
            self.assertTrue(validate_node("BTC", response, GENESIS, GENESIS))
        info = dict(chain="regtest", initialblockdownload=False, blocks=0, headers=0)
        self.assertTrue(validate_node("BTC", info, "b" * 64, GENESIS))
        self.assertTrue(validate_node("BTC", info, GENESIS, ""))
        self.assertTrue(validate_node("DOGE", info, GENESIS, GENESIS))


if __name__ == "__main__":
    unittest.main()
