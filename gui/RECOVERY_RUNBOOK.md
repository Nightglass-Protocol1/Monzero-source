# LTC/XMZ trade recovery runbook

For users and support. It covers what to do when a Litecoin ⇄ XMZ trade stops
partway. Recovery depends on keeping the trade files and acting before the
agreed block-height deadlines. Missing a refund deadline can let the other
trader take the Litecoin punishment output. A backup alone cannot prevent that.

> Status: written for the testnet beta (2026-10-15). Review the steps against
> the app before mainnet. Text in *italics* names buttons and status messages
> in the DEX screen.

## Golden rules

1. **Never delete a trade folder.** It is under your app data directory at
   `dex/trades/<trade id>/`. It holds the keys that recover your funds. Back it
   up with your wallet. Closing a trade in the app keeps every file.
2. **Never restore, recreate or "start over" a wallet that a trade screen tells
   you to preserve.** Messages such as *"may have completed"* mean exactly that.
3. **Keep the app running and monitor an active trade until it settles or is
   refunded.** Recovery paths have block-height deadlines, which can arrive in
   much less than a day. Read the *cancellation delay* and *punishment delay*
   before approving the terms. Block times vary; a wall-clock estimate is not
   a guaranteed recovery window. If you must stop the app, reopen it with
   enough time to check the chain and confirm the recovery transactions before
   the applicable deadline.
4. If a step is unclear, stop and ask for support **before** pressing anything.
   Waiting past a recovery deadline is unsafe. An uncertain operation needs a
   chain check before any new approval; do not create a replacement trade.

## Backup coverage today

The light-wallet backup restores its seed and address allocations. It does
**not** restore active swap journals, retained signed transactions or recovery
agreements. The app reports `activeSwapRecovery=false` for this backup format.
Keep the original trade and wallet files even after checking a seed backup.

Verified VM restart recovery reopens files already present on that machine;
it does not prove recovery from a backup after disk loss. Copying files while
an app or helper is writing them is not a verified consistent snapshot. The
active-trade encrypted backup and restore workflow remains an acceptance gate.
Until it is implemented and tested, preserve the original installation and
use the existing reopen/check flow for interrupted test trades.

## Roles

| Role in app | You pay | You receive |
| --- | --- | --- |
| *Pay LTC, receive XMZ* (Litecoin funder) | LTC | XMZ |
| *Pay XMZ, receive LTC* (native funder) | XMZ | LTC |

## Stages and what to do

### 1. Before anyone has funded (start, key exchange, recovery exchange)
No coins have moved. If the other trader disappears, close the app. The trade
can be left alone or closed (*Close trade*). Nothing is at risk.

### 2. LTC locked, XMZ not yet locked
- **Litecoin funder:** wait. If XMZ is never locked, then after the cancellation
  delay the app offers the cancellation and then the refund. Approve them. Your
  LTC returns minus Litecoin network fees.
- **Native funder:** only lock XMZ when the app shows the Litecoin lock as
  confirmed. If you don't want to continue, simply don't fund. You lose nothing.

### 3. Both sides locked, waiting for redemption
- **Native funder:** after the redemption is released to you, approve the
  Litecoin redemption. That is the moment you receive LTC.
- **Litecoin funder:** after the XMZ lock is confirmed, approve *release
  redemption*. When the native funder redeems, the app claims your XMZ. If the
  app was closed, reopening it resumes the claim from the saved trade.
- If the redemption is never released, the cancellation delay passes and the
  Litecoin funder refunds the LTC. The refund itself gives the native funder's
  app what it needs to restore the XMZ wallet and send the XMZ back
  (*refund-wallet restore / reopen / verify*, then refund payout).

### 4. Cancelled, but the Litecoin funder never refunds
After the punishment delay, the **native funder** may take the LTC (punishment)
to make up for the locked XMZ. **Litecoin funder: refund before the punishment
delay ends** — this is the deadline that matters most.

### 4a. The other trader shows as disconnected mid-trade
The app retries a dropped transport connection automatically, and resumes the
saved recovery agreement and settlement steps already approved for that trade.
A protocol-validation failure is not retried as a network interruption. If the
connection does not recover, preserve the trade folder, inspect the status and
reopen the app if needed. A disconnect does not pause either chain's deadlines;
follow stages 2–4. Some asymmetric settlement interruptions may need you to
repeat the relevant check or review after reconnecting; never assume that a
missing reply means a transaction was not sent.

### 5. App says *recovery required* / *may have completed*
An operation was sent but its result could not be confirmed (crash, power loss,
dropped connection). The app will not retry on its own.
1. Do not reinstall or restore wallets.
2. Copy the whole trade folder and the wallet files somewhere safe.
3. Reopen the app and use the *check* buttons. They only read the chain.
4. Contact support with the trade id (first 8 characters are enough) and the
   status text. **Never send seeds, passwords or the trade folder contents.**

### 6. *New trade blocked* message
New trades are paused or over a limit set by the operator (kill switch / caps).
**Trades already started are not affected.** Funding, redemption, refund and
recovery still work. Nothing needs to be done for existing trades.

### 7. Trade listed as *could not be opened* or *unreadable*
The app found the folder but could not load it. Keep the folder. Start the app
with `--dex-trade <trade id>` to see the exact error, and contact support.

## Support checklist (operator side)
- Ask for: trade id, role, network, the last status line, and time last opened.
- Check both chains for the lock, cancel, refund and redeem transactions shown
  in the app (transaction ids are safe to share).
- Never ask for seeds, passwords, private keys or trade folder contents.
- If many trades fail at once, publish a control document with
  `newTradesEnabled=false` (see `tools/dex-control/`). Existing trades keep
  their recovery paths.
