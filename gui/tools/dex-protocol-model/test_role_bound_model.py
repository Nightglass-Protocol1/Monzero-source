import unittest

from role_bound_model import State, explore, successors


class RoleBoundModelTest(unittest.TestCase):
    def test_both_loss_directions_remain_reachable(self):
        states = explore()
        self.assertTrue(any(s.btc == "refund" and s.xmz == "success" for s in states))
        self.assertTrue(any(s.btc == "success" and s.xmz == "refund" for s in states))

    def test_success_and_clean_abort_are_reachable(self):
        states = explore()
        self.assertTrue(any(s.btc == s.xmz == "success" for s in states))
        self.assertTrue(any(s.btc == s.xmz == "refund" and not s.public_secret
                            for s in states))

    def test_secret_exposure_is_not_btc_confirmation(self):
        exposed = State(public_secret=True)
        self.assertIn(exposed, explore())
        self.assertEqual(exposed.btc, "locked")

    def test_authority_and_deadlines(self):
        for state in explore():
            for event, following in successors(state):
                if following.btc != state.btc:
                    self.assertEqual(event.actor, "S" if following.btc == "success" else "B")
                    if following.btc == "refund":
                        self.assertGreaterEqual(state.window, 1)
                if following.xmz != state.xmz:
                    self.assertEqual(event.actor, "B" if following.xmz == "success" else "S")
                    if following.xmz == "refund":
                        self.assertEqual(state.window, 2)
                for asset in ("btc", "xmz"):
                    if getattr(state, asset) != "locked":
                        self.assertEqual(getattr(state, asset), getattr(following, asset))

    def test_every_stored_witness_replays(self):
        for expected, trace in explore().items():
            state = State()
            for event in trace:
                state = dict(successors(state))[event]
            self.assertEqual(state, expected)

    def test_optional_finalizer_does_not_guarantee_inclusion(self):
        self.assertTrue(any(event.actor == "finalizer"
                            for event, _ in successors(State(public_secret=True), True)))
        # Extra authority adds a way to settle, not a guarantee it is exercised.
        self.assertEqual(set(explore()), set(explore(public_finalizer=True)))


if __name__ == "__main__":
    unittest.main()
