import unittest

from conditional_output_model import State, counterexamples, explore, successors


class ConditionalOutputModelTest(unittest.TestCase):
    def test_early_secret_allows_btc_redemption_and_xmz_refund(self):
        examples = counterexamples()
        self.assertTrue(examples)
        self.assertEqual(min(len(trace) for _, trace in examples), 4)
        for state, trace in examples:
            self.assertTrue(state.secret_public)
            self.assertFalse(state.fee_paid)
            current = State()
            for event in trace:
                current = dict(successors(current))[event]
            self.assertEqual(current, state)

    def test_honest_success_and_unrevealed_abort_are_reachable(self):
        states = explore()
        self.assertTrue(any(s.btc_redeemed and s.fee_paid for s in states))
        self.assertTrue(any(s.xmz_outcome == "refund" and not s.secret_public
                            and not s.btc_redeemed for s in states))

    def test_local_output_cannot_be_spent_twice(self):
        for state in explore():
            if state.xmz_outcome != "locked":
                for _, following in successors(state):
                    self.assertEqual(following.xmz_outcome, state.xmz_outcome)

    def test_refund_requires_deadline_and_redemption_requires_secret(self):
        for state in explore():
            if state.xmz_outcome == "refund":
                self.assertTrue(state.xmz_deadline_reached)
            if state.btc_redeemed:
                self.assertTrue(state.secret_public)
            self.assertEqual(state.fee_paid, state.xmz_outcome == "success")


if __name__ == "__main__":
    unittest.main()
