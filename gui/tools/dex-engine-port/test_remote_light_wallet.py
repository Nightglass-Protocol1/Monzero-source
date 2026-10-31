import json
import pathlib
import tempfile
import unittest
from unittest.mock import Mock
from remote_light_wallet import RemoteLightWallet


class Workspace:
    root = pathlib.PurePosixPath('/home/vm/trade')

    def __init__(self):
        self.run = Mock()
        self.upload = Mock()

    def path(self, name):
        return str(self.root / name)


class RemoteLightTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.local = pathlib.Path(directory.name)
        self.helper = self.local / 'helper'
        self.helper.write_bytes(b'helper')
        self.workspace = Workspace()
        self.wallet = RemoteLightWallet(self.workspace, '/vm/bin/helper', self.helper, self.local, 'regtest')

    def report(self):
        return dict(address='rltc1' + 'a' * 40, helperSha256=self.wallet.digest,
                    configurationFile=self.wallet.directory + '/setupDirectory/wallet.json',
                    launcher=self.wallet.directory + '/launcher.json')

    def create(self):
        value = self.report()
        self.workspace.run.return_value = json.dumps(value).encode()
        self.assertEqual(self.wallet.create(35001), value)
        return value

    def test_install_uploads_only_public_certificate_and_code(self):
        self.workspace.run.return_value = json.dumps({'helperSha256': self.wallet.digest}).encode()
        self.wallet.install()
        uploads = self.workspace.upload.call_args_list
        self.assertEqual([c.args[1] for c in uploads], ['light-tls/root.pem', 'light_wallet_provision.py'])
        self.assertEqual(uploads[0].args[0], self.local / 'root.pem')

    def test_invalid_install_digest_rejects_before_upload(self):
        self.workspace.run.return_value = b'{"helperSha256":"wrong"}'
        with self.assertRaises(RuntimeError):
            self.wallet.install()
        self.workspace.upload.assert_not_called()

    def test_create_returns_only_public_fields(self):
        self.create()
        arguments = self.workspace.run.call_args.args[0]
        self.assertEqual(arguments[-5:], ['regtest', '10000000', '35001', self.wallet.tls_root, 'ltc'])
        self.assertNotIn('password', arguments)

    def test_bitcoin_signet_wallet_passes_its_coin_to_the_vm(self):
        self.wallet = RemoteLightWallet(self.workspace, '/vm/bin/helper', self.helper, self.local, 'signet', coin='btc')
        value = dict(self.report(), address='tb1q' + 'a' * 38)
        self.workspace.run.return_value = json.dumps(value).encode()
        self.wallet.create(35001)
        self.assertEqual(self.workspace.run.call_args.args[0][-5:],
                         ['signet', '10000000', '35001', self.wallet.tls_root, 'btc'])
        self.workspace.run.return_value = b'{"observedConfirmedAtomic":"0","pendingReceivedAtomic":"0"}'
        self.wallet.refresh()
        self.assertEqual(self.workspace.run.call_args.args[0][-1], 'btc')

    def test_report_with_secret_or_escaped_path_is_refused(self):
        for change in ({'password': 'secret'}, {'configurationFile': '/elsewhere/private.json'},
                       {'launcher': self.wallet.directory + '/../outside'}, {'helperSha256': 'wrong'}):
            value = self.report()
            value.update(change)
            self.workspace.run.return_value = json.dumps(value).encode()
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                self.wallet.create(35001)

    def test_invalid_inputs_never_reach_vm(self):
        for port in (True, 0, 65536, '5000'):
            with self.assertRaises(ValueError):
                self.wallet.create(port)
        with self.assertRaises(ValueError):
            RemoteLightWallet(self.workspace, '/vm/helper', self.helper, self.local, 'mainnet')
        for coin, network in (('btc', 'test'), ('btc', 'main'), ('ltc', 'signet'), ('xmr', 'regtest')):
            with self.subTest(coin=coin, network=network), self.assertRaises(ValueError):
                RemoteLightWallet(self.workspace, '/vm/helper', self.helper, self.local, network, coin=coin)
        self.workspace.run.assert_not_called()

    def test_attach_moves_password_only_on_vm_and_checks_refund(self):
        setup = self.create()
        resident = Mock()
        resident.call.return_value = {'lightFunding': {'state': 'complete', 'terms': {'refundAddress': setup['address']}}}
        self.wallet.attach(resident)
        move = self.workspace.run.call_args.args[0]
        self.assertEqual(move[-2:], [self.wallet.directory + '/light-password', '/home/vm/trade/resident/light-password'])
        self.assertEqual(resident.call.call_args_list[0].kwargs,
                         dict(helper=self.wallet.helper, digest=self.wallet.digest, launcher=setup['launcher']))
        resident.call.return_value = {'lightFunding': {'state': 'complete', 'terms': {'refundAddress': 'other'}}}
        with self.assertRaises(RuntimeError):
            self.wallet.attach(resident)

    def test_refresh_is_filtered_and_validated(self):
        self.create()
        expected = {'observedConfirmedAtomic': '100', 'pendingReceivedAtomic': '0'}
        self.workspace.run.return_value = json.dumps(expected).encode()
        self.assertEqual(self.wallet.refresh(), expected)
        for value in ({**expected, 'seed': 'bad'}, {**expected, 'observedConfirmedAtomic': '-1'}):
            self.workspace.run.return_value = json.dumps(value).encode()
            with self.assertRaises(RuntimeError):
                self.wallet.refresh()


if __name__ == '__main__':
    unittest.main()
