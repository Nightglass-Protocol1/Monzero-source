'use strict';
// XMZ <-> Solana leg pricing: the size-aware maker spread (reusing the canonical fee model
// in ./fee.js) applied to a mid rate to produce an all-in quote. Solana's on-chain cost is
// tiny (~5000 lamports), so for any non-dust trade the spread sits at the 0.1% floor; it
// only rises toward the 0.5% cap for very small trades. Works for native SOL (lamports) or
// an SPL token (base units) — pass the trade size in the same unit as `mid`.
const { dynamicSpread } = require('./fee');

// Solana leg defaults: ~1 signature (5000 lamports); rent is reclaimed on settle, so it is
// not counted. min/max match the DEX fee policy (0.1% -> 0.5%).
const SOL_OPTS = { onchainCostSats: 5000, safety: 2, min: 0.001, max: 0.005 };

/** Size-aware spread (fraction) for a SOL-leg trade of `tradeUnits`. */
function solSpread(tradeUnits, opts = {}) { return dynamicSpread(tradeUnits, { ...SOL_OPTS, ...opts }); }

/** Smallest trade where the spread cap still covers the on-chain cost; below it, it's all cap. */
function minTradeUnits(opts = {}) {
  const o = { ...SOL_OPTS, ...opts };
  return Math.ceil((o.onchainCostSats * o.safety) / o.max);
}

/** All-in quote. `mid` is what the taker would receive at the mid rate (same unit as
 *  `tradeUnits`); the maker keeps the spread, so the taker receives `mid * (1 - spread)`. */
function quote(mid, tradeUnits, opts = {}) {
  const m = Number(mid);
  const spread = solSpread(tradeUnits != null ? tradeUnits : m, opts);
  const receive = Math.floor(m * (1 - spread));
  return { spread, mid: Math.floor(m), receive, fee: Math.floor(m) - receive, label: (spread * 100).toFixed(2) + '%' };
}

module.exports = { solSpread, minTradeUnits, quote, SOL_OPTS };
