# XMZ ↔ Solana — remaining work (build-gated + owner-gated)

Everything testable in this environment is built and tested (program native/SPL/relayed,
client, coordinator + driver interface + secret binding, Solana-leg driver against the real
program in LiteSVM, Studio creation). What's left is gated on a build machine, funds, a
signing key, or the audit. This doc is the exact plan for each so it's ready to execute.

---

## #4 — XMZ-leg driver (build-gated: `monero-sys`)
The coordinator (`route/`) drives a `LegDriver`; the Solana half is done. The XMZ half wires
the coordinator's actions/events to the eigenwallet Monero engine. It is **not compilable
here** because the engine depends on `monero-sys` (the C++ Monero wallet), which needs the
full GUI/core build.

Implement `XmzLeg: route::driver::LegDriver` in the engine crate (where `monero-sys` is
available), mapping:
- **lock** (Bob funds the SOL side) → no XMZ action; the maker (Alice) locks XMZ into the
  2-of-2. The driver *observes* it: watch-only wallet + the transfer proof → `XmrLockSeen`,
  then confirmations → `XmrLockConfirmed` (reuse the eigenwallet `XmrLockTransactionSeen`
  path verbatim — it already does view-key + proof verification).
- `Action::PublishReady` → call `SolSwapClient.setReady` (via the app's Solana RPC path).
- `Action::RedeemXmr` → read `s_a` from Alice's Solana claim (the program logs it; or decode
  the claim instruction — `route::secret::extract_revealed_secret`), then
  `route::secret::reconstruct_spend_key(s_a, s_b)` and sweep the XMZ 2-of-2 with the
  eigenwallet sweep. `s_b` is Bob's own Monero key share (the swap's `refund_commitment`
  pre-image).
- `Action::PublishRefund` → `SolSwapClient.refund` (reveals `s_b`); the counterparty reclaims
  XMZ with the engine's reclaim path.

Keys: `claim_commitment = S_a`, `refund_commitment = S_b` are the two Monero public key
shares from the engine's existing key exchange — **no new crypto, no DLEQ** (the
homomorphism is proven in `swap-logic::combine_shares`). Reuse the engine's journal/recovery
exactly as the BTC leg does.

Gate to clear: a build with `monero-sys`, then an end-to-end stagenet/devnet run.

## #5 — App/GUI (C++/Qt) wiring (build-gated)
Add a Solana path to the DEX trade screen, mirroring the EVM path:
- A `SolTradeJob` (like `EvmTradeJob`) that runs the pinned, hash-checked helper +
  JSON-review/commitment pattern (`BackendExecutable`, approval gate, journal), driving the
  `route` coordinator + `XmzLeg`.
- A `MarketRegistry` entry `XMZ/SOL` (and `XMZ/USDC-SOL`, `XMZ/USDT-SOL`) — the registry is
  the source of truth for what the build supports (see `DEX_APP_BOOTSTRAP.md`).
- Wallet connect for signing the Solana side: Phantom/Solflare (web) or a Solana hardware
  path (Ledger Solana app). The SPL vaults + relayed claim are already in the program.
Build-gated: needs the Qt/core build (unavailable here). The Solana instruction layer it
calls (`SolSwapClient`) is done + tested.

## #6 — Mainnet program deploy (owner-gated: funds + audit)
Staged, **not executed** (spends real SOL; wait for the audit, #7).
1. Build for the mainnet loader: `cargo-build-sbf --arch v3` (mainnet uses the newer loader;
   our tests use `--arch v1` to match the pinned LiteSVM).
2. Generate a program keypair **outside the repo**, back it up.
3. Fund a deployer, then: `solana program deploy --program-id <keypair> target/deploy/monzero_sol_swap.so`.
4. Record the program id in `deployments/solana-mainnet.json` (the app pins it in the build,
   never fetches it from the website — same trust model as the EVM contracts).
5. Verify the on-chain program hash equals the built `.so`.
See `tools/deploy-solana.sh` (gated behind `MONZERO_SOL_MAINNET=I_ACCEPT_REAL_FUNDS_RISK`).

## #8 — DEX go-live for the SOL pair (owner-signed)
Staged. The pair is added to the website bootstrap as **`soon`** (display only — the DEX
stays gated off; the real gate is the signed operator control document + the `live` flag).
To go live, the owner:
1. Deploys the program (#6) after the audit (#7).
2. Sets the `XMZ/SOL` pair `status: beta` and, in `monzero-private/dex-status.php`, flips
   `live`/`launch_at` and adds the rendezvous — **owner-signed, never automated**.
3. Keeps the per-trade caps + kill switch, as on the BTC side.

Nothing here bypasses a gate: #4/#5 need the build box, #6/#8 need funds/keys/signature, and
all of it waits on #7 (audit), which happens in time.
