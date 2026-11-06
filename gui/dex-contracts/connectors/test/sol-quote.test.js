'use strict';
const assert = require('assert');
const Q = require('../sol-quote');

describe('sol-quote — XMZ<->Solana leg pricing', () => {
  it('spread floors at 0.1% for a normal (1 SOL) trade', () => {
    assert.strictEqual(Q.solSpread(1_000_000_000), 0.001);
  });
  it('spread hits the 0.5% cap for dust', () => {
    assert.strictEqual(Q.solSpread(1), 0.005);
    assert.strictEqual(Q.solSpread(0), 0.005, 'zero never throws');
  });
  it('minTradeUnits is where the cap just covers the on-chain cost', () => {
    const t = Q.minTradeUnits();
    assert.ok(Math.abs(Q.solSpread(t) - 0.005) < 1e-9);
    assert.ok(Q.solSpread(t + 1) < 0.005, 'just above it, the spread eases below the cap');
  });
  it('quote applies the spread and reports the fee', () => {
    const q = Q.quote(1_000_000_000, 1_000_000_000);
    assert.strictEqual(q.spread, 0.001);
    assert.strictEqual(q.receive, 999_000_000, '1 SOL minus 0.1%');
    assert.strictEqual(q.fee, 1_000_000);
    assert.strictEqual(q.label, '0.10%');
  });
  it('works for SPL token base units too (same formula)', () => {
    // 500 USDC @ 6dp = 500_000_000 base units; well above the floor
    assert.strictEqual(Q.solSpread(500_000_000), 0.001);
    const q = Q.quote(500_000_000, 500_000_000);
    assert.strictEqual(q.receive, 499_500_000);
  });
});
