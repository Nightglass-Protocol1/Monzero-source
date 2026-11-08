#!/usr/bin/env python3
"""Offline checks of relay_market: sealed letters, offer signatures and the terms a maker builds."""
import json, secrets, time, unittest

import relay_market as rm
import trade_kit as kit
from nacl.signing import SigningKey


class SealedMail(unittest.TestCase):
    def test_only_the_recipient_opens_a_letter(self):
        secret, other = secrets.token_bytes(32), secrets.token_bytes(32)
        sealed = rm.seal(rm.mailbox_public(secret), b"terms")
        self.assertEqual(rm.open_sealed(secret, sealed), b"terms")
        self.assertIsNone(rm.open_sealed(other, sealed))
        self.assertNotEqual(sealed, rm.seal(rm.mailbox_public(secret), b"terms"), "a fresh sender key each time")
        tampered = sealed[:-1] + bytes([sealed[-1] ^ 1])
        self.assertIsNone(rm.open_sealed(secret, tampered))
        self.assertIsNone(rm.open_sealed(secret, b"short"))


class FakeIdentity:
    def __init__(self, role):
        self.signing = SigningKey.generate()
        self.mailbox = secrets.token_bytes(32)
        self.role = role
        self.card = {"schema": "monzero-ltc-trade-card-v1", "role": role, "funderKey": self.signing.verify_key.encode().hex(),
                     "tlsCertificateSha256": "0" * 64, "tlsCertificatePem": "x", "mailbox": rm.mailbox_public(self.mailbox).hex()}

    def key(self):
        return self.signing


class Offers(unittest.TestCase):
    def offer(self, maker):
        now = int(time.time())
        return rm.signed_listing(maker, {"schema": rm.LISTING_SCHEMA, "id": "0x" + "ab" * 32, "pair": "BTC/XMZ",
            "network": "btc-regtest", "side": "sell-xmz", "coin": "200000", "xmz": "200000000000",
            "makerKey": maker.card["funderKey"], "mailbox": maker.card["mailbox"], "createdAt": now, "expiresAt": now + 3600})

    def test_a_signed_offer_checks_and_any_change_breaks_it(self):
        maker = FakeIdentity("xmz")
        signed = self.offer(maker)
        self.assertEqual(rm.check_listing(json.loads(json.dumps(signed)), int(time.time()))["coin"], "200000")
        for field, value in (("coin", "200001"), ("pair", "LTC/XMZ"), ("side", "buy-xmz"), ("makerKey", "00" * 32), ("network", "btc-mainnet")):
            changed = json.loads(json.dumps(signed))
            changed["listing"][field] = value
            self.assertIsNone(rm.check_listing(changed), field)
        self.assertIsNone(rm.check_listing(signed, int(time.time()) + 7200), "expired")

    def test_the_maker_builds_terms_from_the_offer_both_cards_and_each_sides_destinations(self):
        maker, taker = FakeIdentity("xmz"), FakeIdentity("ltc")
        listing = self.offer(maker)["listing"]
        params = {f: "1" for f in kit.FIELDS if f not in rm.VARIABLE and f not in rm.OWN_ADDRESSES["ltc"]}
        params.update(ltcNetwork="btc-regtest", ltcPayout="bcrt1-maker", xmzRefund="xmz-maker")
        payload, terms = rm.build_terms("xmz", listing, params, maker.card, taker.card, {"ltcRefund": "bcrt1-taker", "xmzPayout": "xmz-taker"})
        self.assertEqual(kit.validate_terms(payload)[1], terms)
        self.assertEqual((terms["ltcAtoms"], terms["xmzAtoms"]), ("200000", "200000000000"))
        self.assertEqual((terms["xmzFunder"], terms["ltcFunder"]), (maker.card["funderKey"], taker.card["funderKey"]))
        self.assertEqual((terms["ltcPayout"], terms["xmzRefund"], terms["ltcRefund"], terms["xmzPayout"]),
                         ("bcrt1-maker", "xmz-maker", "bcrt1-taker", "xmz-taker"))
        self.assertLessEqual(int(terms["expiresAt"]) - int(time.time()), rm.QUOTE_SECONDS)


class Payout(unittest.TestCase):
    def test_payout_is_the_lock_less_the_reserve(self):
        self.assertEqual(rm.xmz_payout("200000000000", "10000000000", "9000000000"), "190000000000")
        self.assertIsNone(rm.xmz_payout("10000000000", "10000000000", "1"))      # nothing left to pay
        self.assertIsNone(rm.xmz_payout("200000000000", "10000000001", "1"))     # fee cap above the limit
        self.assertIsNone(rm.xmz_payout("200000000000", "0", "1"))


if __name__ == "__main__":
    unittest.main()
