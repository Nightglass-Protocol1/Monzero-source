# xmzpool — Monzero (XMZ) mining pool

A small RandomX (`rx/0`) stratum pool for Monzero, written for this project
(2026-10-16). Python standard library only; no third-party packages.

- **Shares and blocks:** each miner job comes from the node's own
  `get_block_template` with a unique extra nonce. Every share is checked with
  the node's `calc_pow`, so the pool contains no hashing code. A share that
  meets network difficulty is submitted with `submit_block`.
- **Difficulty:** network difficulty comes from each block template (it
  adjusts with hash rate on mainnet). Share difficulty is per miner (vardiff,
  about one share per 15 s), or fixed with `ADDRESS+50000` as the login.
- **Payment scheme:** PPLNS over 2× network difficulty, 1% fee (config).
  Blocks are credited once, after 60 confirmations, and only if the chain still
  holds the exact block the pool submitted (orphans earn nothing).
- **Payouts:** off unless both `payouts.enabled` and `--enable-payouts` are
  set. A payment is recorded and balances deducted before the wallet is
  called. A definite wallet error restores the balances. An uncertain result
  (timeout, lost reply) halts all payouts until an operator reconciles it and
  deletes `<database>.payouts-halted`.
- **Stats API** (for the web page and miningpoolstats): `/api/stats`,
  `/api/miner?address=…`, and `/` serves `web/index.html`.

## Tests

    python3 tests/unit_test.py                    # 9 tests, no chain needed
    python3 tests/run_regtest.py --bin <dir with monzerod and monzero-wallet-rpc>

`run_regtest.py` starts a private offline regtest chain (fixed low difficulty
so a slow test miner can find blocks), two wallet-rpcs and the pool. It checks
stratum error handling, two miners finding blocks, the stats API, an orphan,
PPLNS crediting to the atomic unit, and a real payout arriving in both miners'
wallets. Result on 2026-10-16: **20/20 passed**.

The test miner (`tests/stratum_miner.py`) speaks XMRig's stratum protocol but
has not been checked against a real XMRig yet. Do that on testnet before
launch (see below).

## Deployment (not done; needs the owner's go-ahead)

Target: `monzero-eu` (node.monzero.org): 4 cores, 3.8 GB RAM.

1. **Node RPC.** The pool needs the node's unrestricted RPC on localhost
   (`calc_pow` is not available on restricted RPC). Keep the public
   restricted port and add a private one in `/etc/monzero/monzerod.conf`:

       rpc-restricted-bind-ip=0.0.0.0
       rpc-restricted-bind-port=6175
       rpc-bind-ip=127.0.0.1
       rpc-bind-port=6185

   Check the existing file first (it is root-only) and keep its other lines.
2. **Pool wallet.** Create a new wallet for the pool only, on the server,
   with a strong password. Write its seed down offline. Run
   `monzero-wallet-rpc --daemon-address 127.0.0.1:6185 --trusted-daemon
   --rpc-bind-ip 127.0.0.1 --rpc-bind-port 6190 --rpc-login pool:<secret>
   --wallet-file … --password-file …` as its own service user.
3. **Pool.** Copy this folder to `/opt/xmzpool`, write `/etc/xmzpool/pool.json`
   from `pool.example.json`, and install `systemd/xmzpool.service`. Start with
   payouts **off** and watch a few blocks get credited correctly.
4. **Firewall.** Open TCP 3333 and 5555 (stratum). Keep 8117 (API) on
   localhost and publish it through the web server as
   `https://pool.monzero.org/api/`.
5. **DNS.** `pool.monzero.org` → 82.165.112.237.
6. **Real miner check.** Point XMRig at the pool:
   `xmrig -o pool.monzero.org:3333 -u <XMZ address> -p rig1 -a rx/0`.
   Confirm shares are accepted and the page shows the hash rate.
7. **Payouts on.** Set `payouts.enabled` and add `--enable-payouts` to the
   service. Check the first payment against both wallets.
8. **miningpoolstats.** Send `outreach/04-miningpoolstats-submission.md` with
   the pool URL and `https://pool.monzero.org/api/stats`.

## Operating notes

- `payouts-halted` file present: list the wallet's outgoing transfers
  (`get_transfers` with `out`), find the payment's transaction, then set its
  row in `payments` to `sent` (with txid) or restore the balances, and delete
  the file.
- The pool wallet only ever holds coinbase rewards from the pool's own
  blocks. Never fund it from other wallets.
- Founder-run miners mining here should be disclosed like other founder
  mining (see `outreach/01-founder-holdings-statement.md`).
