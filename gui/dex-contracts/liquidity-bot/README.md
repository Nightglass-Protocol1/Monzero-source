# DEX liquidity bot

Sells XMZ on the Monzero DEX as a ladder of offers on SOL, ETH and BNB, and sends what buyers pay
to the project's vault addresses. Its XMZ comes from a share of the pool's own fee (xmzpool
`liquidity`, 1% of the fee by default) and from whatever the owner sends it.

## How it works

* **Ladder:** for each pair, one offer per step of `ladder_percent` above `reference_usd`
  (default $0.001: +2%, +5%, +10%, +20%), each `offer_xmz` XMZ (default 2,000), converted to the
  coin at its live price (monzero.org/markets). Offers last `offer_hours`, and are re-posted when the
  coin moves more than `reprice_percent`.
* **Limits:** a new offer needs a valid control document for its network, signed with the pinned key
  (the DEX kill switch). It stays within the document's per-trade XMZ and coin limits, and its
  24-hour coin limit, counting sales and open offers. All open offers together never promise more XMZ
  than the wallet has unlocked. If new trades are paused, open offers are withdrawn.
* **Trades:** each ladder slot uses its own trade account (key index `key_base + slot`), so trades
  run side by side without sharing funds. A trade is approved only when it is the offer posted: the
  seller role, the right chain, and the exact coin and XMZ amounts. A trade already started always
  runs to its end (claim or refund).
* **Vault:** after each trade, what the trade account holds goes to the pair's `vault`. It keeps
  `keep` (in the coin's smallest unit) back for the next trade's network fees. This uses the helper's
  `keepWei` option on `payout`.
* **Records:** `ledger.jsonl` (posted, taken, closed, finished, with payout transactions) and
  `status.json` (balances, slots, alerts) in the state folder.

## Install on monzero-eu (owner)

1. Build the helper (Linux): `cargo build --release --locked --bin monzero-dex-evm-helper` in
   `../engine`, with `CARGO_TARGET_DIR=target/host-test`. On this PC, use the podman command in
   the commit message.
2. `bash deploy/install-liquidity-bot.sh`: installs it with the service stopped, makes its keys and
   XMZ wallet, and prints the XMZ address. **Back up** `/var/lib/monzero-liquidity/keys` and
   `/var/lib/monzero-liquidity/main-wallet` together.
3. Pool: `bash ~/projects/Monzero-App/pool/xmzpool/deploy/update-pool.sh`, then
   `bash .../deploy/enable-liquidity.sh on <bot XMZ address> 100`.
4. Optionally send XMZ to the bot's address yourself.
5. `bash deploy/install-liquidity-bot.sh accounts`: send each slot's trade account the network-fee
   money it lists (SOL 0.02, ETH 0.002, BNB 0.004 each).
6. `bash deploy/install-liquidity-bot.sh start`, then `... status`.

Stop with `... stop`. Open offers expire within `offer_hours`, and a running trade continues when the
bot starts again.

## Tests

`python3 test_liquidity_bot.py` runs pricing, limits, the offer, trade and vault flow, and the
approval checks against a stand-in helper. It also checks an RFC 8032 ed25519 vector.
