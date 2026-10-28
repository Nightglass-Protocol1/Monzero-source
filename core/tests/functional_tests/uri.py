#!/usr/bin/env python3
#encoding=utf-8

# Copyright (c) 2019-2022, The Monero Project
# 
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without modification, are
# permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other
#    materials provided with the distribution.
# 
# 3. Neither the name of the copyright holder nor the names of its contributors may be
#    used to endorse or promote products derived from this software without specific
#    prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Test URI RPC
"""

from __future__ import print_function
try:
  from urllib import quote as urllib_quote
except:
  from urllib.parse import quote as urllib_quote

from framework.wallet import Wallet

class URITest():
    def run_test(self):
      self.create()
      self.test_monero_uri()

    def create(self):
        print('Creating wallet')
        wallet = Wallet()
        # close the wallet if any, will throw if none is loaded
        try: wallet.close_wallet()
        except: pass
        seed = 'velvet lymph giddy number token physics poetry unquoted nibs useful sabotage limits benches lifestyle eden nitrogen anvil fewest avoid batch vials washing fences goat unquoted'
        res = wallet.restore_deterministic_wallet(seed = seed)
        assert res.address == 'FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf'
        assert res.seed == seed

    def test_monero_uri(self):
        print('Testing monzero: URI')
        wallet = Wallet()

        utf8string = [u'えんしゅう', u'あまやかす']
        quoted_utf8string = [urllib_quote(x.encode('utf8')) for x in utf8string]

        ok = False
        try: res = wallet.make_uri()
        except: ok = True
        assert ok
        ok = False
        try: res = wallet.make_uri(address = '')
        except: ok = True
        assert ok
        ok = False
        try: res = wallet.make_uri(address = 'kjshdkj')
        except: ok = True
        assert ok

        for address in [
            'FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf',
            'FZeBEpJDp9525WooY4BVmgdcKwZu5EksVZSZkDd6ooxSVVqQ4ubxXkhLF6hEqtw96i9cf3cVfLw8UWe95bdDKfRQeYtPwLm1Jiw78tbAKG',
            'Fs8Jn9XgkyYBGTY8psSNkJBg9SZgxxGGRUhGwRptBhgr5XSQ1XzmA9m8QAnoxydecSh5aLJXdrgXwTDMMZ1AuXsN1K5RWre'
        ]:
            res = wallet.make_uri(address = address)
            assert res.uri == 'monzero:' + address
            res = wallet.parse_uri(res.uri)
            assert res.uri.address == address
            assert res.uri.payment_id == ''
            assert res.uri.amount == 0
            assert res.uri.tx_description == ''
            assert res.uri.recipient_name == ''
            assert not 'unknown_parameters' in res or len(res.unknown_parameters) == 0
            res = wallet.make_uri(address = address, amount = 1100000000)
            assert res.uri == 'monzero:' + address + '?tx_amount=0.011' or res.uri == 'monzero:' + address + '?tx_amount=0.01100000000'
            res = wallet.parse_uri(res.uri)
            assert res.uri.address == address
            assert res.uri.payment_id == ''
            assert res.uri.amount == 1100000000
            assert res.uri.tx_description == ''
            assert res.uri.recipient_name == ''
            assert not 'unknown_parameters' in res or len(res.unknown_parameters) == 0

        address = 'FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf'

        res = wallet.make_uri(address = address, tx_description = utf8string[0])
        assert res.uri == 'monzero:' + address + '?tx_description=' + quoted_utf8string[0]
        res = wallet.parse_uri(res.uri)
        assert res.uri.address == address
        assert res.uri.payment_id == ''
        assert res.uri.amount == 0
        assert res.uri.tx_description == utf8string[0]
        assert res.uri.recipient_name == ''
        assert not 'unknown_parameters' in res or len(res.unknown_parameters) == 0

        res = wallet.make_uri(address = address, recipient_name = utf8string[0])
        assert res.uri == 'monzero:' + address + '?recipient_name=' + quoted_utf8string[0]
        res = wallet.parse_uri(res.uri)
        assert res.uri.address == address
        assert res.uri.payment_id == ''
        assert res.uri.amount == 0
        assert res.uri.tx_description == ''
        assert res.uri.recipient_name == utf8string[0]
        assert not 'unknown_parameters' in res or len(res.unknown_parameters) == 0

        res = wallet.make_uri(address = address, recipient_name = utf8string[0], tx_description = utf8string[1])
        assert res.uri == 'monzero:' + address + '?recipient_name=' + quoted_utf8string[0] + '&tx_description=' + quoted_utf8string[1]
        res = wallet.parse_uri(res.uri)
        assert res.uri.address == address
        assert res.uri.payment_id == ''
        assert res.uri.amount == 0
        assert res.uri.tx_description == utf8string[1]
        assert res.uri.recipient_name == utf8string[0]
        assert not 'unknown_parameters' in res or len(res.unknown_parameters) == 0

        res = wallet.make_uri(address = address, recipient_name = utf8string[0], tx_description = utf8string[1], amount = 100000000000)
        assert res.uri == 'monzero:' + address + '?tx_amount=1.00000000000&recipient_name=' + quoted_utf8string[0] + '&tx_description=' + quoted_utf8string[1]
        res = wallet.parse_uri(res.uri)
        assert res.uri.address == address
        assert res.uri.payment_id == ''
        assert res.uri.amount == 100000000000
        assert res.uri.tx_description == utf8string[1]
        assert res.uri.recipient_name == utf8string[0]
        assert not 'unknown_parameters' in res or len(res.unknown_parameters) == 0

        # external payment ids are not supported anymore
        ok = False
        try: res = wallet.make_uri(address = address, recipient_name = utf8string[0], tx_description = utf8string[1], amount = 100000000000, payment_id = '1' * 64)
        except: ok = True
        assert ok

        # spaces must be encoded as %20
        res = wallet.make_uri(address = address, tx_description = ' ' + utf8string[1] + ' ' + utf8string[0] + ' ', amount = 100000000000)
        assert res.uri == 'monzero:' + address + '?tx_amount=1.00000000000&tx_description=%20' + quoted_utf8string[1] + '%20' + quoted_utf8string[0] + '%20'
        res = wallet.parse_uri(res.uri)
        assert res.uri.address == address
        assert res.uri.payment_id == ''
        assert res.uri.amount == 100000000000
        assert res.uri.tx_description == ' ' + utf8string[1] + ' ' + utf8string[0] + ' '
        assert res.uri.recipient_name == ''
        assert not 'unknown_parameters' in res or len(res.unknown_parameters) == 0

        # the example from the docs
        res = wallet.parse_uri('monzero:FTsPTjyNHiwHDpDEUmZBWZfoQpdc6HaERCNmx1pEYL2rAcuwufPN9rXHHtyUA4QVy66qeFQkn6sfK8aHYjA3jk3o1BLT1wc?tx_amount=239.39014&tx_description=donation')
        assert res.uri.address == 'FTsPTjyNHiwHDpDEUmZBWZfoQpdc6HaERCNmx1pEYL2rAcuwufPN9rXHHtyUA4QVy66qeFQkn6sfK8aHYjA3jk3o1BLT1wc'
        assert res.uri.amount == 23939014000000
        assert res.uri.tx_description == 'donation'
        assert res.uri.recipient_name == ''
        assert res.uri.payment_id == ''
        assert not 'unknown_parameters' in res or len(res.unknown_parameters) == 0

        # malformed/invalid
        for uri in [
            '',
            ':',
            'monero',
            'notmonzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf',
            'MONERO:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf',
            'MONERO::FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf',
            'monzero:',
            'monzero:badaddress',
            'monzero:tx_amount=10',
            'monzero:?tx_amount=10',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_amount=-1',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_amount=1e12',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_amount=+12',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_amount=1+2',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_amount=A',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_amount=0x2',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_amount=222222222222222222222',
            'monzero:42ey1afDFnn4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJQAWDn?tx_amount=10',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf&',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf&tx_amount',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf&tx_amount=',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf&tx_amount=10=',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf&tx_amount=10=&',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf&tx_amount=10=&foo=bar',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_amount=10&tx_amount=20',
            'monzero:FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf?tx_payment_id=1111111111111111',
            'monzero:FZeBEpJDp9525WooY4BVmgdcKwZu5EksVZSZkDd6ooxSVVqQ4ubxXkhLF6hEqtw96i9cf3cVfLw8UWe95bdDKfRQeYtPwLm1Jiw78tbAKG?tx_payment_id=' + '1' * 64,
            'monzero:KcQfLgEbvdbfeK3KZdCqnYaMwZVFuXemPU8Ubw335rj2FN1CdMiWNyFV3ksEfMFvRp9L9qum5UxkP5rN9aLcPxbH1dKmy9x',
            'monzero:P5diDtsDQ4LYFrwwMw5VZtfuyqs1en7ttSf28kTeovDtKHERWLSzJmUEMWRtmtHCNc9ezMpG2A7edA5xqpSK1N4SLVgUNi5ENfx1VwZcE7Q8',
            'monzero:PFLRJVoLwXEg6u9Ci2xBcPiMfauV9S4aRfgZarMCVYd1RBQgqQFZ8uuj1Y1toDzK39ABPEeMsocN6Cv4RGUNW7X92bctvBgNA',
        ]:
            ok = False
            try: res = wallet.parse_uri(uri)
            except: ok = True
            assert ok, res

        # unknown parameters but otherwise valid
        res = wallet.parse_uri('monzero:' + address + '?tx_amount=239.39014&foo=bar')
        assert res.uri.address == address
        assert res.uri.amount == 23939014000000
        assert res.unknown_parameters == ['foo=bar'], res
        res = wallet.parse_uri('monzero:' + address + '?tx_amount=239.39014&foo=bar&baz=quux')
        assert res.uri.address == address
        assert res.uri.amount == 23939014000000
        assert res.unknown_parameters == ['foo=bar', 'baz=quux'], res
        res = wallet.parse_uri('monzero:' + address + '?tx_amount=239.39014&%20=%20')
        assert res.uri.address == address
        assert res.uri.amount == 23939014000000
        assert res.unknown_parameters == ['%20=%20'], res
        res = wallet.parse_uri('monzero:' + address + '?tx_amount=239.39014&unknown=' + quoted_utf8string[0])
        assert res.uri.address == address
        assert res.uri.amount == 23939014000000
        assert res.unknown_parameters == [u'unknown=' + quoted_utf8string[0]], res



if __name__ == '__main__':
    URITest().run_test()
