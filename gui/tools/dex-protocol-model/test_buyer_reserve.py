"""Abstract ownership checks, not transaction, cryptographic or consensus tests.

Assume key release grants unrestricted authority over all value in the shared
lock. This matches the authority exposed by Bob State5::xmr_keys and Alice's
refund key extraction. Enumerate allocations in tiny integer units to expose
why local signed terms alone cannot protect a buyer-funded shared reserve.
Network fees are fixed model inputs; neither scheduling nor signing is modeled.
"""
import unittest


def allocations(total, fee):
    """All possible buyer/seller allocations by an unrestricted key owner."""
    if not 0 <= fee <= total:
        raise ValueError("invalid model fee")
    return [(buyer, total - fee - buyer) for buyer in range(total - fee + 1)]


class BuyerReserveOwnershipTests(unittest.TestCase):
    def test_shared_reserve_has_honest_refund_but_also_theft(self):
        principal, reserve, fee = 5, 3, 1
        possible = allocations(principal + reserve, fee)
        # Honest seller returns the buyer's unused reserve.
        self.assertIn((reserve - fee, principal), possible)
        # The same full key can retain all remaining value instead.
        self.assertIn((0, principal + reserve - fee), possible)
        self.assertGreater(principal + reserve - fee, principal)

    def test_local_signed_destination_does_not_remove_alternate_spend(self):
        possible = allocations(8, 1)
        approved = (2, 5)
        accepted_by_local_policy = [value for value in possible if value == approved]
        self.assertEqual(accepted_by_local_policy, [approved])
        self.assertIn((0, 7), possible)
        self.assertNotIn((0, 7), accepted_by_local_policy)

    def test_separate_buyer_output_is_outside_seller_key_authority(self):
        # Explicit assumption of a distinct buyer-only spending key. This does
        # not prove that a joint-input claim can be signed or always included.
        seller_lock, buyer_reserve, fee = 5, 3, 1
        for buyer_from_lock, seller_from_lock in allocations(seller_lock, fee):
            buyer_total = buyer_reserve + buyer_from_lock
            self.assertGreaterEqual(buyer_total, buyer_reserve)
            self.assertEqual(buyer_total + seller_from_lock + fee,
                             seller_lock + buyer_reserve)

    def test_invalid_fee_has_no_accounting_fallback(self):
        for fee in (-1, 9):
            with self.assertRaises(ValueError):
                allocations(8, fee)


if __name__ == "__main__":
    unittest.main()
