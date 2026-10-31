"""Finite integer timing checks, not estimates of real chain confirmation times.

Assume S discloses only with a valid BTC success transaction; both submissions
are made, included and stable within the stated bounds. B waits for stable BTC,
then observes/prepares/submits XMZ within an additional bounded delay. A success
at exactly the refund boundary is unsafe because refund may win that ordering.
"""
import itertools
import unittest


def latest_disclosure(btc_refund, xmz_refund, btc_bound, observation_bound, xmz_bound):
    values = (btc_refund, xmz_refund, btc_bound, observation_bound, xmz_bound)
    if any(type(value) is not int or value < 0 for value in values):
        raise ValueError("model requires nonnegative integer ticks")
    if xmz_refund <= btc_refund:
        raise ValueError("candidate requires later XMZ refund")
    return min(btc_refund - btc_bound - 1,
               xmz_refund - btc_bound - observation_bound - xmz_bound - 1)


def safe_times(disclosure, btc_delay, observation_delay, xmz_delay, btc_refund, xmz_refund):
    btc_stable = disclosure + btc_delay
    xmz_stable = btc_stable + observation_delay + xmz_delay
    return btc_stable < btc_refund and xmz_stable < xmz_refund


class DeadlineBoundsTest(unittest.TestCase):
    def test_all_bounded_schedules_before_cutoff(self):
        for btc_refund, gap, btc_bound, observation, xmz_bound in itertools.product(
                range(1, 6), range(1, 5), range(3), range(3), range(3)):
            xmz_refund = btc_refund + gap
            cutoff = latest_disclosure(btc_refund, xmz_refund, btc_bound, observation, xmz_bound)
            for disclosure in range(cutoff + 1):
                for delays in itertools.product(range(btc_bound + 1),
                                                 range(observation + 1), range(xmz_bound + 1)):
                    self.assertTrue(safe_times(disclosure, *delays, btc_refund, xmz_refund))
            # One tick beyond the bound admits an unsafe worst-case schedule.
            if cutoff >= 0:
                self.assertFalse(safe_times(cutoff + 1, btc_bound, observation,
                                            xmz_bound, btc_refund, xmz_refund))

    def test_insufficient_time_has_no_permitted_disclosure(self):
        self.assertLess(latest_disclosure(2, 3, 2, 1, 1), 0)

    def test_violating_bound_restores_failure(self):
        cutoff = latest_disclosure(10, 20, 2, 1, 3)
        self.assertEqual(cutoff, 7)
        self.assertTrue(safe_times(cutoff, 2, 1, 3, 10, 20))
        self.assertFalse(safe_times(cutoff, 3, 1, 3, 10, 20))
        self.assertFalse(safe_times(cutoff, 2, 1, 10, 10, 20))

    def test_reject_ambiguous_inputs(self):
        for values in ((10, 9, 1, 1, 1), (10, 20, -1, 1, 1),
                       (10, 20, True, 1, 1), (10, 20, 1.5, 1, 1)):
            with self.assertRaises(ValueError):
                latest_disclosure(*values)
