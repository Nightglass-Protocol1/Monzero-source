import unittest
from fee_release_model import State, counterexamples, explore, successors


class FeeReleaseModelTest(unittest.TestCase):
    def test_exhaustive_reachable_states_include_honest_and_bypass_outcomes(self):
        states = explore()
        self.assertEqual(len(states), 4)
        self.assertTrue(any(s.successful_exchange and s.protocol_fee_paid for s in states))
        bad = counterexamples()
        self.assertEqual(len(bad), 1)
        state, trace = bad[0]
        self.assertEqual(len(trace), 2)
        self.assertTrue(state.buyer_has_full_xmz_authority)
        self.assertEqual(trace[-1], "buyer spends XMZ without fee")

    def test_no_xmz_spend_before_authority_release(self):
        next_states = list(successors(State()))
        self.assertEqual(len(next_states), 1)
        self.assertFalse(next_states[0][1].xmz_delivered)

    def test_terminal_spend_cannot_be_rewritten_by_accounting(self):
        for state, _ in counterexamples():
            self.assertEqual(list(successors(state)), [])
            # Refusing to label the exchange settled does not alter its assets.
            local_fee_earned = state.successful_exchange and state.protocol_fee_paid
            self.assertFalse(local_fee_earned)
            self.assertTrue(state.successful_exchange)


if __name__ == "__main__":
    unittest.main()
