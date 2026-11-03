# Gated public DEX build — requirements and open decisions

Scope note, 3 November 2026. Captures what a public DEX build must enforce before it can
allow real trades with small limits, based on the current code. It is a spec, not a change:
the whole DEX is still prototype-gated (`WITH_DEX_PROTOTYPE` OFF in release builds, plus the
runtime env `MONZERO_ENABLE_PRIVATE_DEX`). No shipped build enables DEX trading today.

## Current state of the safety gate (verified)
- UTXO swaps (LTC/BTC/BCH): the start of a new trade IS gated. `DexEngine::litecoinStartRefusal`
  and `submitLitecoinStart` call `DexRiskControl::refusal()` and `recordStart()`
  (DexEngine.cpp ~1046-1059). The kill switch + per-trade + 24h caps come from a signed,
  network-scoped control document (DexRiskControl.h; Ed25519; sequence must increase).
- EVM swaps (XMZ<->BNB/ETH via EvmTradeJob): **NOT gated.** `approveEvmTrade`, `stepEvmTrade`,
  `payoutEvmTrade` never consult DexRiskControl. The control document is network-scoped and
  `m_riskControl->setNetwork(DexDeployment::litecoinNetwork())` ties it to the UTXO stage, which
  for an EVM-configured app defaults to the unused `ltc-regtest` (needs no document). So in a
  build where EVM trades were enabled, the owner's kill switch and caps would not cover them.

## Requirement 1 — the kill switch and caps must cover EVM trades
Before a public build enables BNB/ETH trading, an EVM trade's first fund-committing step must
pass `DexRiskControl::refusal(tradeId, nonXmzAtoms, xmzAtoms)` and be recorded with
`recordStart` (count before dispatch, as the UTXO path does: a crash after recording only makes
the limit stricter). This needs a network identity for the EVM side so the right control
document applies.

### Open decision (owner): the control-document model for EVM
The control document is per-network and the owner signs it. Options:
1. A separate document per EVM network (e.g. `bnb-mainnet`, `eth-mainnet`), signed like
   `btc-mainnet` is today (sign-control.py already generalised to Bitcoin networks; extend to
   the EVM names). An EVM-configured app sets the risk-control network accordingly.
2. One document that carries caps for several networks at once (schema change; one signature).
Recommendation: option 1 — smallest change, one network per app instance, consistent with the
current per-stage model. It needs the EVM network name wired into DexRiskControl::setNetwork for
EVM-configured apps, and sign-control.py to accept the EVM network names.

## Requirement 2 — the edition split
Two editions (owner decision 2026-10-31, "do all 4 items"):
- Public, limited: real trades allowed from day one but with small caps enforced by the signed
  document; new trades pause instantly when the owner flips the kill switch.
- Dev, live: the full prototype build for the team.
Today both are the same prototype build. The public edition should drop the
`MONZERO_ENABLE_PRIVATE_DEX` local-prototype gate in favour of the signed-document gate, and must
fail closed: no control document (or an expired/younger-sequence one) = no new trades, for every
enabled pair, on both the UTXO and EVM sides.

## Requirement 3 — the 1% project fee
Needs the owner's per-coin fee addresses (BTC, LTC, BCH, BNB). The fee is taken on the non-XMZ
side; BNB needs the swap-contract v2 with the fee built in. Blocked on the addresses.

## Requirement 4 — in-app offer taking
The trading screen's Start button is disabled until offers can be posted and taken in-app
(DexTradeCard). This is the main functional gap before any public beta and is separate from the
safety gate above.

## What is already done
- Real-funds test kit (dex/trading-ui).
- Community pentest kit (gui/tools/dex-pentest-kit).
- Signed kill switch + caps for UTXO swaps, per-network control documents, Bitcoin-network
  signing in sign-control.py.

## Suggested order
1. Owner: fee addresses + EVM control-doc model decision.
2. Wire EVM trades through DexRiskControl (Requirement 1) with tests.
3. In-app offer taking (Requirement 4).
4. 1% fee (Requirement 3) once addresses exist.
5. Build the public/dev edition split (Requirement 2); fail-closed verified on both sides.
6. Community pentest round against the public beta.
