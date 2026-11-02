# Bitcoin side on light servers, and the Bitcoin mainnet opening (plan, 2 Nov 2026)

Owner decision (2 Nov 2026): build light-wallet support for the Bitcoin funder
("litecoin-funder", the buyer of XMZ) before the first real-funds Bitcoin swap, so
neither trader needs a Bitcoin node. No pruned node.

## Where the node requirement actually is

The engine library is already light-capable for the Bitcoin funder:

| Funder step | Engine function | Chain interface |
|---|---|---|
| Fund the lock | light wallet (`LitecoinLightFundingJob`, light helper) | light |
| Cancel / refund the lock | `submit_reviewed_recovery_spend`, `check_recovery_spend` | `&impl RedeemChain` |
| Claim XMZ (find the seller's redeem, read the secret) | `locate_native_settlement_block`, `prepare/relay/reconcile_native_payout` | `&impl RedeemChain` (via the same `ltc` argument) |
| Fund the lock from a node wallet | `review/submit_litecoin_funding`, `sign_recovered_lock` | node only, not needed with light funding |

The node requirement is only in:

1. `native-ltc-journal` wrappers: `reserve_litecoin` (recovery modes) and `payout_rpc`
   build `LitecoinRpc` unconditionally; their RPC files require node fields.
2. 15 `staged(policy)` gates in the helper: every path refuses anything but an
   approved test pairing (regtest + XMZ fakechain, BTC/LTC test networks + XMZ testnet).
   `isolated_native` accepts only an offline fakechain or a synchronized XMZ testnet.
3. App validators (`LitecoinJournalJob`, reservation/payout configuration) and the trade
   kit ("only the XMZ seller can use light servers").

## Plan

1. **Mainnet opening (helper).** `accepted(policy)` = today's staged pairs, plus
   exactly (BitcoinMainnet, XMZ mainnet) when the helper is built with the cargo
   feature `btc-mainnet`. Default builds stay test-only. `isolated_native` gains a
   mainnet branch: nettype mainnet, synchronized, genesis equal to both the trade's
   and the real XMZ mainnet genesis (84f9eb…). Litecoin mainnet stays refused.
2. **Funder recovery on light.** `ReservationRpc` accepts exactly one of node fields
   or `ltcLight {servers, maximumTipAge}` for the recovery modes (cancel/refund
   review, submit, check). Lock funding and reservation-prepare keep requiring a node
   (light funding replaces them).
3. **Funder payout on light.** `PayoutRpc` accepts exactly one of node or `ltcLight`;
   the same `seller_chain` builder (renamed `swap_chain`) supplies the chain.
4. **App and trade kit.** Accept `ltcLight` in the funder's reservation and payout RPC
   files; trade kit `LOCAL.json` `litecoin.light` allowed for both roles.
5. **Tests.** Helper unit tests for each config shape and each gate (mainnet refused
   without the feature, Litecoin mainnet always refused, mismatched genesis refused);
   `dex_tests`; live regtest and signet swaps with light servers on BOTH sides
   (success and refund) before any mainnet funds.
6. **Mainnet run.** Two app instances configured with the trade kit (light both
   sides, public Electrum servers with CA-valid TLS), the owner's signed btc-mainnet
   control document, small amounts (owner-funded light wallet, about 0.0013 BTC).

## Already verified for mainnet (2 Nov 2026)

- App stage `btc-mainnet` behind `MONZERO_DEX_MAINNET` and the signed control
  document; quotes must pair XMZ mainnet by name and genesis (dex/btc-mainnet 5628db8).
- Light wallet syncs Bitcoin mainnet from the pinned checkpoint 971,712 through
  public servers; tip agrees across Blockstream, ACINQ and BlueWallet.
- CA-valid public Bitcoin Electrum servers from independent operators:
  electrum.blockstream.info:50002, electrum.acinq.co:50002, electrum2.bluewallet.io:443,
  electrum.hodlister.co:50002, electrum.petrkr.net:50002, e.keff.org:50002.
