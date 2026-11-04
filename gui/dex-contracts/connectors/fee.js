// Canonical dynamic-fee math for the Monzero DEX — one source of truth shared by the
// app, the website preview and the docs. Mirrors the Rust engine's DynamicSpread
// (swap-feed/src/rate.rs): clamp( onchain_cost * safety / trade , min , max ).
'use strict';

const DEFAULTS = { min: 0.001, max: 0.005, onchainCostSats: 4000, safety: 2 };

// Effective spread (fraction) for a trade of `tradeSats` (quote-asset satoshis/atoms).
// Large trades hit the floor; small trades the cap (so the ~fixed per-swap on-chain fee
// is covered). As size -> 0 the cap is the limit.
function dynamicSpread(tradeSats, opts = {}) {
  const { min, max, onchainCostSats, safety } = { ...DEFAULTS, ...opts };
  const t = Number(tradeSats);
  if (!(t > 0)) return max;
  const coverage = (onchainCostSats * safety) / t;
  return Math.min(max, Math.max(min, coverage));
}

// Human label, e.g. "0.10%".
function spreadLabel(tradeSats, opts) { return (dynamicSpread(tradeSats, opts) * 100).toFixed(2) + '%'; }

// Smallest trade (in quote sats) where the cap spread still covers the on-chain cost —
// i.e. below this, even the max spread loses money. Use as the minimum trade size.
function minTradeSats(opts = {}) {
  const { max, onchainCostSats, safety } = { ...DEFAULTS, ...opts };
  return Math.ceil((onchainCostSats * safety) / max);
}

module.exports = { dynamicSpread, spreadLabel, minTradeSats, DEFAULTS };
