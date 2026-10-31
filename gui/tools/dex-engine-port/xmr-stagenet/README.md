# BTC → XMR leg of the routed XMZ → BTC → XMR swap: stagenet test

The owner chose the routed path on 29 October 2026: XMZ → BTC with our engine, then
BTC → XMR with the upstream eigenwallet protocol. Two separate atomic swaps; the user
holds the BTC in their own wallet between them. This folder tests the second leg with
**unmodified upstream protocol code** on Bitcoin signet and Monero stagenet.

## Build

Upstream eigenwallet 4.14.0 (`0e17c7f7c`), clean copy at
`core/build/eigenwallet-upstream-xmr` (the DEX engine port is not touched), plus:

- [`../upstream-xmr-signet-stagenet.patch`](../upstream-xmr-signet-stagenet.patch):
  - monero-sys depends: libiconv and readline compile as C17 (GCC 16 defaults to C23);
  - test mode = Bitcoin **signet** + Monero stagenet (upstream uses testnet3), with two
    signet Electrum servers (mempool.space, wakiyamap.dev);
  - `swap/examples/headless_buy_xmr.rs`: runs upstream's own `buy_xmr` without the GUI.
    The only behaviour change in upstream code is in `request_maker_selection`: with no
    GUI it approves the maker **only when `MONZERO_TEST_AUTO_SELECT_MAKER` is set**;
  - `--monero-pool` in the driver, and title-case headers in `monero-rpc-pool`'s outbound
    connections.
- [`../simple-request-0.2.0-title-case-host-port.patch`](../simple-request-0.2.0-title-case-host-port.patch),
  vendored through `[patch.crates-io]`: title-case header names and a `Host` header that
  carries a non-default port (RFC 9110 §7.2).

Build: `gperf` must be installed; then, with the pinned DEX toolchain,
`cargo build --release -p swap --bin swap --example headless_buy_xmr -p swap-asb --bin asb`.

## Run (as on 29 October)

1. Maker: `asb --testnet --config asb.toml start` ([example](asb.toml.example); leave
   `daemon_url` out to use the node pool). Fund its stagenet address.
2. Buyer: `headless_buy_xmr --data-dir D --monero-pool --electrum … address` prints a
   signet deposit address and its own stagenet address. Fund the deposit address.
3. `MONZERO_TEST_AUTO_SELECT_MAKER=1 headless_buy_xmr … buy --seller /ip4/127.0.0.1/tcp/39939/p2p/<peer> --xmr-address <stagenet>`
   swaps the deposited balance, up to what the maker can cover.
4. If the buyer stops, resume with upstream's CLI:
   `swap --testnet --data-base-dir D resume --swap-id <id> --electrum-rpc … --monero-node-address …`.

`rpc_relay.py` is a logging HTTP relay used to diagnose node problems; it is not needed
with the pool.

## Result: swap `343a61a6-554e-4260-8082-38fb07775c7c` completed

| Step | Transaction | Detail |
| --- | --- | --- |
| BTC lock (buyer) | `519b9a9cb800d58b7a5c363c19e59b9bf7e1fa6027d35c43eb68774e6ffb8617` | 5,317 sats, 14:35 UTC |
| XMR lock (maker) | `73f1a84a4b17214026d6d8397b6096edff2f47310938a84c540ed8222a390e26` | stagenet height 2,239,321; change 0.002970517503 XMR |
| BTC redeem (maker) | `aefae87435a32ce75cfbcccb45167538ff08ed6d52af92d0da7b322968e78ecd` | 4,317 sats (1,000 sat fee) |
| XMR redeem (buyer) | `a05b6a4aa0f2acac2df907c0ad85e85be98f4d29b17de47d5b538b117c705d8` | 0.006877002497 XMR received, 15:56 UTC |

Rate 0.0074 BTC/XMR from the maker's live feeds. The swap was small because the faucet
paid 0.01 XMR; every protocol step still ran.

## Result: refund path, swap `7f79f910-1439-41f4-9932-eecb03dd2f29`

The buyer locked BTC, then the maker was stopped before it locked any XMR. The buyer waited
out the cancel timelock (36 signet blocks after the lock confirmed) and got its BTC back
without the maker's help.

| Step | Transaction | Detail |
| --- | --- | --- |
| BTC lock (buyer) | `cb1360787ece48069123a46b71fe05a5c601f7ea3549f35a0141ced66a3db647` | 7,687 sats, published 16:49 UTC, confirmed at signet 328,541 |
| BTC cancel (buyer) | `8d78c2b2b721fc8156675c22103d54823aa47113424dcfdc80830e00b150d3ea` | signet 328,577 (lock + 36), 21:40 UTC; 1,000 sat fee |
| BTC refund (buyer) | `e69325a309d0761935f7f3eaac3f8c3d5d508327affe4f6e0934cdc3dc21e5f8` | 5,687 sats to the buyer's `tb1qn750…jqm6`, signet 328,578; 1,000 sat fee |

The buyer ended in `btc is refunded` (exit "success", 21:51 UTC) and lost only the two
1,000-sat fees. The maker was restarted afterwards: it had not saved the swap before it was
stopped, so it has no record of it and its XMR balance stayed 0.012970517503. A maker that
*had* saved the swap and comes back after the cancel was not exercised by this test.

## Maker restart after the cancel (swap `3411d43e`, 29–30 October)

The maker's only route to its Monero node went through a local relay. Once the buyer's BTC
lock was published the relay was cut, so the maker could not lock XMR. The maker saved the
swap as `btc is locked`, failed its XMR lock attempt and was stopped (22:44 UTC). It was
started again only after the buyer had cancelled and refunded.

| Step | Transaction | Detail |
| --- | --- | --- |
| BTC lock (buyer) | `1f0d53b0afabdd2ff1da1ce0310f6ca3b6dc51ce387663e1e68c18deeccdaf33` | 7,918 sats, 22:42 UTC, confirmed at signet 328,587 |
| BTC cancel (buyer) | `699b68fc03bb5c57861ada89920c3e77036eecc61ffa0f170569e36f058b8331` | published 04:52 UTC |
| BTC refund (buyer) | `6339f19e2138cc4eab01bbe37628b7ff2e63f3b5ae91d2b6a18f37c5eeeed08a` | published 05:04 UTC; buyer exited `success` at 05:19 |

On restart (05:19 UTC, relay back up) the maker resumed the swap in `btc is locked`, checked
the cancel timelock and logged "We did not manage to lock the Monero funds before the
timelock expired. Aborting swap." `asb history` shows the swap as `safely aborted`. The
relay forwarded **0** `send_raw_transaction` calls in both runs, and the maker's XMR balance
stayed 0.012970517503 before and after. A maker that comes back after the cancel does not
lock XMR.

## What went wrong on the way (and is relevant to the real DEX)

- **Single public nodes are unreliable.** `xmr-lux.boldsuck.org` reset or stalled requests
  erratically and reset plain HTTP from our IP for 10–30 s after any TLS handshake (Monero's
  C++ wallet tries TLS first). The maker's first XMR lock broadcasts were lost there; the
  lock went through once the maker used `node.monerodevs.org`. Two buy attempts stopped at
  the pre-lock health check (no funds moved). **Use the node pool or the user's own node,
  never one fixed public node.** With the pool the health check passed 3/3.
- Header case was suspected and patched, but tests did **not** prove it to be the cause.
- The 4.14.0 buyer does not acknowledge a repeated transfer proof; the maker logs failures
  until the end. Harmless here; fixed upstream in 4.16.1, which does not change the RPC
  client (same monero-oxide `c8be5d3`, simple-request 0.2.0).
- The buyer can stall if its own Monero connection fails while waiting; stopping it and
  running `swap resume` continued the swap exactly where it was.

## Not done yet

- Rebase on 4.16.1 for its transfer-proof fixes.
- Wiring the route into the app: XMZ → BTC (our engine) followed by this leg, with
  separate approval, fees and recovery for each leg.
