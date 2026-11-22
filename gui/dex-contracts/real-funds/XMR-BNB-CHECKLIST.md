# Real-funds test: XMR ↔ BNB (owner)

One public-offer trade on **BNB Smart Chain mainnet** and **Monero mainnet**: the maker pays BNB
for the taker's XMR. Both sides run on your PC from one command, through the same helper
commands the apps use. Same model as the first BNB/XMZ swap (31 Oct 2026): you hold both sides,
so the coins only move between your own two test wallets; the cost is network fees.

Proven before this: XMR stagenet swap (22 Nov 2026, 00:32 UTC) and the BNB mainnet contract
`0x9073290952DCDF3225D8f65d787cF9148a490feb` (first swap, 31 Oct 2026).

## Amounts (prices 22 Nov 2026: XMR £376.64, BNB £426.69)

| What | Amount | About |
|---|---|---|
| Trade: maker pays | `BNB_AMOUNT=0.044` | £18.80 |
| Trade: taker sells | `XMR_AMOUNT=0.05` | £18.80 |
| Send to the maker (BNB, BEP20) | 0.045 BNB (trade + 0.001 gas) | £19.20 |
| Send to the taker (BNB, BEP20) | 0.0005 BNB (gas to claim) | £0.21 |
| Send to the taker's XMR wallet | 0.051 XMR (trade + 0.001 fee room) | £19.20 |

About £39 in coins, plus exchange and withdrawal fees, out of the £50. Send a little more than
shown if a withdrawal minimum asks for it. The script refuses more than 0.1 BNB or 0.3 XMR.

## Steps

1. **Convert the BTC** (exchange or swap service): about £20 to **BNB on BNB Smart Chain
   (BEP20)**, about £20 to **XMR**. Keep them on the exchange until step 3 prints the addresses.
2. **Start the run** (any directory):
   ```
   BNB_AMOUNT=0.044 XMR_AMOUNT=0.05 bash ~/projects/Monzero-App/monzero-app-direct/gui/dex-contracts/real-funds/run-xmr-bnb.sh
   ```
   It checks the Monero programs against their signed hash list, makes a fresh test phrase in
   `~/.config/monzero-dex/xmr-bnb-mainnet-accounts.json` (private, never printed), picks the
   Monero nodes (your node 31.70.193.31 when it answers, else a public one; plus 2–3
   independent https "witness" nodes that must all confirm the XMR lock) and starts a local
   relay with XMR on mainnet turned on for this test only.
3. **Fund the three addresses it prints**: BNB to the maker, a little BNB to the taker, XMR to
   the taker's Monero wallet. It waits, showing what has arrived. XMR needs 10 blocks (~20
   minutes) before it can be spent.
4. **Leave it running** (1–2 hours): offer posted, taken, keys agreed, BNB locked, XMR locked
   and confirmed by the witnesses, BNB claimed, XMR swept to the maker. It ends with `PASSED`.
   Ctrl-C, a crash or a reboot is safe: run the same command again and it carries on.
5. **Send me `~/dex-real/xmr-bnb-1/run.log`** (no keys in it). I write up the evidence like
   `runs/mainnet-1.json`.

If it ends with `NO SWAP`, the swap timed out and every coin went back to the side that sent it
(that is the refund path working); send the log and we look before trying again.

## Afterwards

- The XMR the maker bought is in the maker's Monero wallet (`~/dex-real/xmr-bnb-1/run/maker/main`),
  the BNB the taker got on account 1 of the test phrase. Run the trade the other way, or send
  them back to your own wallets (tell me the addresses; I prepare the sends).
- The test wallets have no passwords (test only, folder 0700): don't leave coins in them for long.
- Nothing public changes: XMR on mainnet stays off in the apps and the public relays until you
  open it.

## Notes

- Monero programs: the official release in `core/build/monero-official` (v0.18.5.4), started by
  this script; that is why you run it, not Claude.
- The own node server (31.70.193.31) did not answer at 19:10 on 22 Nov (no ping, no ports);
  the script falls back to public nodes. Its Solana gateway sol.monzero.org is down with it.
