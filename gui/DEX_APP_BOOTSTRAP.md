# App ↔ website DEX bootstrap

How the Monzero app "connects to the DEX on the website". The website is **not** a
trading backend — the DEX is non-custodial atomic swaps. The app fetches a small
**bootstrap config** from the site, then connects **peer-to-peer** (libp2p rendezvous)
and to blockchain nodes to actually trade.

## Endpoint (live)
`GET https://monzero.org/dex/api.php` → JSON (`schema: monzero-dex-bootstrap-v1`):
```json
{
  "live": false,
  "launch_at": null,
  "min_app_version": null,
  "rendezvous": ["/dns4/.../tcp/.../p2p/12D3KooW..."],
  "pairs": [{"pair":"XMZ/BTC","status":"beta","fee":"0.1%-0.5%"}, ...],
  "trust": "Discovery/display only ..."
}
```
Owner-controlled via `monzero-private/dex-status.php` (validated server-side;
malformed rendezvous multiaddrs are dropped). Source: `website/dex/api.php`.

## What the app does with it
1. **Gate the DEX UI.** If `live` is false, show "coming soon"; do not offer
   trading. No launch date is published: `launch_at` stays null until `live` is true. (This is presentation only — real trading is
   also gated by the signed operator control document in `DexRiskControl`.)
2. **Nudge updates.** If `min_app_version` is set and newer than this build, show an
   "update to trade" prompt.
3. **Display** the `pairs`/`fees` list (reconcile with the in-app `MarketRegistry`,
   which stays the source of truth for what the build actually supports).
4. **Peer discovery.** Pass `rendezvous` to the engine's `rendezvous_point` config so
   the swap engine can find makers, then negotiate and settle P2P.

## Implementation (follow the existing pattern)
`DexRiskControl.cpp` already fetches an HTTPS URL with `QNetworkAccessManager` +
`QNetworkRequest` + `QNetworkRequest::NoLessSafeRedirectPolicy` on a poll timer. Add a
`DexBootstrap` class (or a method on `DexEngine`) that:
- GETs the endpoint on DEX-screen open and on a slow poll (e.g. 10 min);
- parses JSON defensively (reject on wrong `schema`, cap sizes, ignore unknown fields);
- **caches the last good response** and works offline from it — the app must function if
  the website is down (fall back to pinned defaults, never block on the fetch);
- exposes `live`, `launchAt`, `pairs`, `rendezvous` as Qt properties for `Dex.qml`.

## Trust model (must hold)
Nothing fetched here is fund-authoritative:
- **Contract addresses are NOT served** — they are **pinned in the app build** (from
  `dex-contracts/deployments/<chain>.json`). A compromised website can never redirect
  funds to a malicious contract.
- **Fees** shown here are display only; the real fee is enforced in the **maker's signed
  quote** that the app verifies, not taken from the website.
- **Rendezvous** is a discovery hint; a hostile value can at worst point the app at bad
  makers — **atomicity still prevents theft** (swaps fail/refund, never steal).
- Use HTTPS with the standard no-less-safe redirect policy; a cert pin is optional
  hardening. The `live` flag is convenience, not the trading gate (the control document
  is).

## Status
Website endpoint: **done, deployed, tested** (input validation + offline-safe shape).
App side: **spec only** — the C++ `DexBootstrap` fetch/parse/cache needs the full GUI
build (unavailable in the current sandbox) to implement and test.
