import unittest
from unittest.mock import Mock
from test_litecoin_live_swap import send_retained_light_funding, retry_or_confirm_light_funding


def response(state, **fields):
    return {'lightFunding': dict(state=state, **fields)}


def review(action='send', digest='ab'*32, txid='cd'*32, commitment='ef'*32):
    return {'commitment': commitment, 'details': {'action': action, 'signedDigest': digest, 'txid': txid}}


class ReconcileTests(unittest.TestCase):
    def test_stale_approval_is_observed_then_freshly_approved_without_resigning(self):
        app = Mock()
        app.call.side_effect = [response('outcome-unconfirmed'),
            response('complete', result={'txid':'cd'*32, 'broadcastStatus':'no-local-attempt'}),
            response('complete', review=review(commitment='12'*32)),
            response('complete', result={'txid':'cd'*32})]
        self.assertEqual(send_retained_light_funding(app, 'cd'*32, review(), Mock()), {'txid':'cd'*32})
        self.assertEqual([c.args[1] for c in app.call.call_args_list],
                         ['light-send','light-observe','light-send-review','light-send'])
        self.assertEqual(app.call.call_args_list[-1].kwargs, {'commitment':'12'*32})

    def test_uncertain_broadcast_uses_retry_review_never_new_send(self):
        app = Mock()
        app.call.side_effect = [response('outcome-unconfirmed'),
            response('complete', result={'txid':'cd'*32, 'broadcastStatus':'outcome-unknown'}),
            response('complete', review=review(action='retry')),
            response('complete', result={'txid':'cd'*32})]
        send_retained_light_funding(app, 'cd'*32, review(), Mock())
        self.assertEqual([c.args[1] for c in app.call.call_args_list],
                         ['light-send','light-observe','light-retry-review','light-retry'])

    def test_changed_bytes_are_refused_before_second_send(self):
        app = Mock()
        app.call.side_effect = [response('outcome-unconfirmed'),
            response('complete', result={'txid':'cd'*32, 'broadcastStatus':'no-local-attempt'}),
            response('complete', review=review(digest='00'*32))]
        with self.assertRaisesRegex(RuntimeError, 'signed transaction bytes'):
            send_retained_light_funding(app, 'cd'*32, review(), Mock())
        self.assertEqual(app.call.call_count, 3)

    def test_failed_observation_cannot_authorize_resend(self):
        app = Mock()
        app.call.side_effect = [response('outcome-unconfirmed')]*4
        with self.assertRaisesRegex(RuntimeError, 'read remains uncertain'):
            send_retained_light_funding(app, 'cd'*32, review(), Mock())
        self.assertEqual(app.call.call_count, 4)

    def test_repeated_stale_reviews_are_bounded(self):
        app = Mock()
        sequence = [response('outcome-unconfirmed'),
            response('complete', result={'txid':'cd'*32, 'broadcastStatus':'no-local-attempt'}),
            response('complete', review=review())]
        app.call.side_effect = sequence + sequence[:2]
        with self.assertRaisesRegex(RuntimeError, 'bounded'):
            send_retained_light_funding(app, 'cd'*32, review(), Mock(), maximum_attempts=2)
        self.assertEqual(app.call.call_count, 5)


class InclusionTests(unittest.TestCase):
    def test_inclusion_after_uncertain_send_stops_rebroadcast(self):
        app = Mock()
        app.call.side_effect = [response('outcome-unconfirmed'), response('complete',
            result={'txid':'cd'*32, 'observation':{'position':'included'}})]
        send_retained_light_funding(app, 'cd'*32, review(), Mock())
        self.assertEqual(app.call.call_count, 2)

    def test_already_included_never_requests_retry(self):
        app = Mock()
        app.call.return_value = response('complete', result={'txid':'cd'*32, 'observation':{'position':'included'}})
        self.assertFalse(retry_or_confirm_light_funding(app,'cd'*32,'ab'*32,Mock()))
        self.assertEqual(app.call.call_count, 1)

    def test_inclusion_during_retry_review_is_reconciled(self):
        app = Mock()
        app.call.side_effect = [response('complete', result={'txid':'cd'*32,'observation':{'position':'reported-pending'}}),
            response('outcome-unconfirmed'),response('complete',result={'txid':'cd'*32,'observation':{'position':'included'}})]
        self.assertFalse(retry_or_confirm_light_funding(app,'cd'*32,'ab'*32,Mock()))
        self.assertEqual([c.args[1] for c in app.call.call_args_list], ['light-observe','light-retry-review','light-observe'])


class ReviewRaceTests(unittest.TestCase):
    def test_failed_fresh_send_review_reobserves_before_new_approval(self):
        app = Mock()
        observed = response('complete', result={'txid':'cd'*32, 'broadcastStatus':'no-local-attempt'})
        app.call.side_effect = [response('outcome-unconfirmed'), observed, response('outcome-unconfirmed'),
            observed, response('complete',review=review()),response('complete',result={'txid':'cd'*32})]
        send_retained_light_funding(app,'cd'*32,review(),Mock())
        self.assertEqual([c.args[1] for c in app.call.call_args_list],
            ['light-send','light-observe','light-send-review','light-observe','light-send-review','light-send'])

    def test_acknowledged_status_uses_retry(self):
        app = Mock()
        app.call.side_effect = [response('outcome-unconfirmed'),response('complete',
            result={'txid':'cd'*32,'broadcastStatus':'server-acknowledged'}),
            response('complete',review=review(action='retry')),response('complete',result={'txid':'cd'*32})]
        send_retained_light_funding(app,'cd'*32,review(),Mock())
        self.assertEqual(app.call.call_args.args[1],'light-retry')

    def test_retry_succeeds_only_with_same_bytes(self):
        for digest in ('ab'*32, '00'*32):
            app = Mock()
            app.call.side_effect = [response('complete',result={'txid':'cd'*32,'observation':{'position':'reported-pending'}}),
                response('complete',review=review(action='retry',digest=digest)),response('complete',result={'txid':'cd'*32})]
            if digest == 'ab'*32:
                self.assertTrue(retry_or_confirm_light_funding(app,'cd'*32,'ab'*32,Mock()))
            else:
                with self.assertRaisesRegex(RuntimeError,'signed transaction bytes'):
                    retry_or_confirm_light_funding(app,'cd'*32,'ab'*32,Mock())
                self.assertEqual(app.call.call_count,2)
