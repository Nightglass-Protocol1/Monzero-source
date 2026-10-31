"""Offline checks for untrusted quotes entering local app configuration."""
import argparse
import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest

import trade_kit as kit
from nacl.signing import SigningKey


class ConfigureFixture:
    """Two identity folders, a signed quote and local settings in a private temporary root."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.keys = {role: SigningKey.generate() for role in ("ltc", "xmz")}
        self.terms = dict.fromkeys(kit.FIELDS, "1")
        self.terms.update(schema=kit.SCHEMA, swapId="test-swap",
                          **{role + "Funder": key.verify_key.encode().hex()
                             for role, key in self.keys.items()})
        self.envelope = self.signed(self.terms)
        self.quote = self.root / "quote.json"
        self.quote.write_text(json.dumps(self.envelope))
        self.cards = {}
        for role in self.keys:
            directory = self.root / role
            directory.mkdir()
            card = dict(schema="monzero-ltc-trade-card-v1", role=role,
                        funderKey=self.terms[role + "Funder"],
                        tlsCertificatePem="test certificate", tlsCertificateSha256="0" * 64)
            self.cards[role] = directory / "card.json"
            self.cards[role].write_text(json.dumps(card))
            (directory / "tls.crt").write_text("test certificate")
            for name in ("tls.key", "identity.bin", "identity.passphrase"):
                (directory / name).write_text("test placeholder, not a secret")
        self.helper = self.root / "helper"
        self.helper.write_bytes(b"test helper, never executed")
        (self.root / "policy.json").write_text("{}")
        self.local = dict(journalHelper=str(self.helper),
                     litecoin=dict(rpcPort=19332, wallet="test", anchor="test", cookieFile="test"),
                     xmz=dict(daemonPort=16175, claimWalletPort=16183, claimLoginFile="test",
                              sourceWalletPort=16184, sourceLoginFile="test", sourceAddress="test",
                              observerWalletPort=16185, observerLoginFile="test", walletPasswordFile="test"),
                     peer=dict(address="192.168.56.103", port=41000))
        (self.root / "local.json").write_text(json.dumps(self.local))

    def signed(self, terms):
        payload = kit.canonical(terms)
        return dict(schema=kit.ENVELOPE, payload=payload.decode(),
                    **{role + "Signature": key.sign(kit.signing_bytes(payload, role)).signature.hex()
                       for role, key in self.keys.items()})

    def configure(self, envelope=None, role="ltc", out=None, **overrides):
        self.quote.write_text(json.dumps(self.envelope if envelope is None else envelope))
        args = argparse.Namespace(dir=str(self.root / role), out=str(out or self.root / "app"),
                                  peer_card=str(self.cards["xmz" if role == "ltc" else "ltc"]),
                                  quote=str(self.quote), policy=str(self.root / "policy.json"),
                                  local=str(self.root / "local.json"))
        for key, value in overrides.items():
            setattr(args, key, str(value))
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            kit.cmd_configure(args)

    def staging_leftovers(self):
        return sorted(name for name in (p.name for p in self.root.iterdir()) if ".staging-" in name)


class ConfigureQuoteTests(ConfigureFixture, unittest.TestCase):
    def test_valid_quote_creates_ltc_app(self):
        self.configure(self.envelope)
        app = json.loads((self.root / "app/app.json").read_text())
        self.assertIn("reservationConfigFile", app)

    def test_valid_quote_creates_xmz_app(self):
        self.configure(self.envelope, "xmz")
        app = json.loads((self.root / "app/app.json").read_text())
        self.assertIn("fundingConfigFile", app)

    def write_local(self, litecoin):
        (self.root / "local.json").write_text(json.dumps(dict(self.local, litecoin=litecoin)))

    def test_xmz_seller_may_use_light_servers_instead_of_a_node(self):
        light = dict(servers=[dict(host="one.example", port=50002), dict(host="two.example", port=50002)],
                     maximumTipAge=5400)
        self.write_local(dict(light=light))
        self.configure(self.envelope, "xmz")
        for name in ("funding-rpc.json", "payment-rpc.json"):
            config = json.loads((self.root / "app" / name).read_text())
            self.assertEqual(config["ltcLight"], light)
            self.assertFalse({"ltcPort", "ltcWallet", "ltcAnchor", "ltcCookieFile"} & set(config))

    def test_light_servers_are_refused_unless_exactly_and_safely_configured(self):
        good = dict(servers=[dict(host="one.example", port=50002), dict(host="two.example", port=50002)],
                    maximumTipAge=5400)
        cases = {"buyer": ("ltc", dict(light=good)),
                 "node and light": ("xmz", dict(self.local["litecoin"], light=good)),
                 "one server": ("xmz", dict(light=dict(good, servers=good["servers"][:1]))),
                 "same server twice": ("xmz", dict(light=dict(good, servers=[good["servers"][0]] * 2))),
                 "tip age unbounded": ("xmz", dict(light=dict(good, maximumTipAge=86400))),
                 "string port": ("xmz", dict(light=dict(good, servers=[dict(host="a", port="1"), dict(host="b", port=2)]))),
                 "unknown key": ("xmz", dict(light=dict(good, trust=True)))}
        for name, (role, litecoin) in cases.items():
            with self.subTest(name=name):
                self.write_local(litecoin)
                with self.assertRaises(SystemExit):
                    self.configure(self.envelope, role)
                self.assertFalse((self.root / "app").exists())
                self.assertEqual(self.staging_leftovers(), [])

    def test_invalid_quotes_leave_no_app_files(self):
        cases = {}
        for role in self.keys:
            bad = copy.deepcopy(self.envelope)
            bad[role + "Signature"] = "00" * 64
            cases[role + " forged signature"] = bad
        bad = copy.deepcopy(self.envelope)
        changed = dict(self.terms, ltcAtoms="999999")
        bad["payload"] = kit.canonical(changed).decode()
        cases["amount altered after signing"] = bad
        for field, value in (("schema", "wrong"), ("payload", []), ("ltcSignature", "not hex")):
            cases[field + " malformed"] = dict(self.envelope, **{field: value})
        cases["unknown field"] = dict(self.envelope, extra=True)
        cases["missing signature"] = {k: v for k, v in self.envelope.items() if k != "xmzSignature"}
        cases["noncanonical terms"] = dict(self.envelope, payload=json.dumps(self.terms))
        cases["wrong terms schema"] = self.signed(dict(self.terms, schema="wrong"))
        cases["numeric term"] = self.signed(dict(self.terms, ltcAtoms=10))
        cases["unknown term"] = self.signed(dict(self.terms, extra="1"))
        cases["not an envelope"] = []
        cases["terms are field-name list"] = dict(self.envelope, payload=kit.canonical(kit.FIELDS).decode())
        for role in self.keys:
            for name, envelope in cases.items():
                with self.subTest(role=role, name=name):
                    with self.assertRaises(SystemExit):
                        self.configure(envelope, role)
                    self.assertFalse((self.root / "app").exists())
                    self.assertEqual(self.staging_leftovers(), [])

    def test_quote_naming_other_keys_leaves_no_app_files(self):
        stranger = SigningKey.generate()
        for role in self.keys:
            for swapped in ("ltc", "xmz"):
                with self.subTest(role=role, replaced=swapped):
                    keys = dict(self.keys, **{swapped: stranger})
                    terms = dict(self.terms, **{swapped + "Funder": stranger.verify_key.encode().hex()})
                    payload = kit.canonical(terms)
                    envelope = dict(schema=kit.ENVELOPE, payload=payload.decode(),
                                    **{r + "Signature": k.sign(kit.signing_bytes(payload, r)).signature.hex()
                                       for r, k in keys.items()})
                    with self.assertRaises(SystemExit):
                        self.configure(envelope, role)
                    self.assertFalse((self.root / "app").exists())
                    self.assertEqual(self.staging_leftovers(), [])


if __name__ == "__main__":
    unittest.main()
