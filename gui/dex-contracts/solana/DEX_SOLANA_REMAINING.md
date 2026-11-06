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

## #6 — Mainnet program deploy — DONE (owner controlled real-funds test, 2026-11-06)
Deployed to **mainnet-beta** as the project's own controlled real-funds test (the step your
release policy runs before any public trading). **This is NOT public go-live** — the DEX
bootstrap stays `live:false` and the `XMZ/SOL` pair stays `soon`; see #8.

- **Program id:** `B2oh6NHndUYAKj9Rpq8Aww3HZ8fVTT3g7nsh4eAtqpCb`  (record: `deployments/solana-mainnet-beta.json`)
- Built for the mainnet loader (`cargo-build-sbf --arch v3`); on-chain program hash **verified
  equal** to the built `.so` (`0008fca9…`), i.e. identical to the devnet binary.
- Upgrade authority: the deployer keypair `BRo1QVgQ…` (saved at
  `~/.config/monzero-dex/solana-deployer-mainnet.json`; the owner controls it). Program keypair
  at `~/.config/monzero-dex/solana-program-mainnet.json`.
- Cost: ~0.64 SOL held as ProgramData rent (recoverable by closing the program).
- **Verified with a live mainnet swap:** lock → set_ready → claim confirmed on-chain, claimer
  paid, escrow closed (claim tx `SrJVUpTP…`); test funds swept back to the deployer.

The app pins this program id in the build (never fetched from the website — same trust model
as the EVM contracts). `tools/deploy-solana.sh` stays gated behind
`MONZERO_SOL_MAINNET=I_ACCEPT_REAL_FUNDS_RISK` so a mainnet deploy is always a deliberate act.
A third-party audit (#7) still gates **public** go-live (#8), not this owner test.

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
