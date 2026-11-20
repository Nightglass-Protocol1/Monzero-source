// Dependency-free checks of the in-app exchange's arithmetic (components/dexExchange.js): the
// order book, which offer a market order takes (the engine's partial-fill rule), limit totals,
// 24 h figures and relay prints. Run: node tests/dex-exchange-test.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const X = vm.createContext({});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../components/dexExchange.js'), 'utf8').replace(/^\.pragma library\r?\n/, ''), X);
const same = (a, b) => assert.deepEqual(JSON.parse(JSON.stringify(a)), b);
let n = 0; const t = (name, f) => { f(); n++ }
t('units', () => { assert.equal(X.units('123450000000', 11), '1.2345'); assert.equal(X.units('5', 11), '0.00000000005'); assert.equal(X.units('100000000000', 11), '1') })
t('price', () => { assert.equal(X.price(0.0000143), '0.0000143'); assert.equal(X.price(0.000143456789), '0.000143457'); assert.equal(X.price(12.3456789), '12.3457'); assert.equal(X.price(0), '—') })
t('usd', () => { assert.equal(X.usd(0), '$0'); assert.equal(X.usd(15.3245), '$15.32'); assert.equal(X.usd(0.0102255), '$0.0102255') })
t('amount', () => { assert.equal(X.amount(1500, 2), '1,500'); assert.equal(X.amount(1234567.891, 2), '1,234,567.89'); assert.equal(X.amount(999, 0), '999'); assert.equal(X.amount(-1500, 0), '-1,500') })
t('xmzAtoms', () => { assert.equal(X.xmzAtoms('1500'), 1500e11); assert.equal(X.xmzAtoms('0.5'), 0.5e11); assert.equal(X.xmzAtoms('1.123456789012'), -1); assert.equal(X.xmzAtoms('0'), -1); assert.equal(X.xmzAtoms('abc'), -1); assert.equal(X.atomsText(1500e11), '1500') })
const now = 1794760000
const L = [
  { id: 'a1', pair: 'SOL/XMZ', side: 'sell-xmz', valueWei: String(215039000), xmzAtoms: String(1500e11), maker: '0xbot', expiresAt: now + 3600 },
  { id: 'a2', pair: 'SOL/XMZ', side: 'sell-xmz', valueWei: String(252987000), xmzAtoms: String(1500e11), maker: '0xbot', expiresAt: now + 3600 },
  { id: 'a3', pair: 'SOL/XMZ', side: 'sell-xmz', valueWei: String(300000000), xmzAtoms: String(2000e11), minXmzAtoms: String(100e11), maker: '0xother', expiresAt: now + 3600 },
  { id: 'b1', pair: 'SOL/XMZ', side: 'buy-xmz', valueWei: String(100000000), xmzAtoms: String(1000e11), minXmzAtoms: String(50e11), maker: '0xme', expiresAt: now + 3600 },
  { id: 'b2', pair: 'SOL/XMZ', side: 'buy-xmz', valueWei: String(90000000), xmzAtoms: String(1000e11), maker: '0xz', expiresAt: now + 3600 },
  { id: 'x', pair: 'SOL/XMZ', side: 'sell-xmz', valueWei: '1', xmzAtoms: String(1e11), maker: '0xz', expiresAt: now - 1 },
  { id: 'e', pair: 'ETH/XMZ', side: 'sell-xmz', valueWei: '1', xmzAtoms: String(1e11), maker: '0xz', expiresAt: now + 10 },
]
const b = X.book(L, 'SOL', 9, '0xME', now)
t('book', () => {
  same(b.asks.map(r => r.id), ['a1', 'a3', 'a2'])
  same(b.bids.map(r => r.id), ['b1', 'b2'])
  assert.ok(Math.abs(b.bestAsk - 0.215039 / 1500) < 1e-15)
  assert.equal(b.bids[0].mine, true); assert.equal(b.asks[0].mine, false)
  assert.equal(b.asks[2].total, 5000)
  assert.equal(b.asks[1].partial, true); assert.equal(b.asks[1].min, 100)
})
t('fillOk', () => {
  const a3 = b.asks[1], a1 = b.asks[0]
  assert.equal(X.fillOk(a1, 1500e11), true); assert.equal(X.fillOk(a1, 1000e11), false)
  assert.equal(X.fillOk(a3, 100e11), true); assert.equal(X.fillOk(a3, 99e11), false)
  assert.equal(X.fillOk(a3, 1950e11), false); assert.equal(X.fillOk(a3, 1900e11), true); assert.equal(X.fillOk(a3, 2000e11), true); assert.equal(X.fillOk(a3, 2001e11), false)
  assert.equal(X.nearestFill(a3, 1950e11), 1900e11); assert.equal(X.nearestFill(a3, 50e11), 100e11); assert.equal(X.nearestFill(a1, 5e11), 1500e11)
})
t('market buy', () => {
  let p = X.planMarket(b, 'buy', 1500e11)
  assert.equal(p.ok, true); assert.equal(p.row.id, 'a1'); assert.ok(Math.abs(p.coin - 0.215039) < 1e-12)
  p = X.planMarket(b, 'buy', 500e11)             // a1 whole only -> a3 partial fills it
  assert.equal(p.ok, true); assert.equal(p.row.id, 'a3'); assert.ok(Math.abs(p.coin - 0.075) < 1e-12)
  p = X.planMarket(b, 'buy', 5000e11)            // nothing fills: suggests the best
  assert.equal(p.ok, false); assert.equal(p.suggestion.row.id, 'a1'); assert.equal(p.suggestion.atoms, 1500e11)
  p = X.planMarket(b, 'buy', 1500e11, 'a3')      // picked offer only
  assert.equal(p.ok, true); assert.equal(p.row.id, 'a3')
  p = X.planMarket(b, 'buy', 1500e11, 'gone')
  assert.equal(p.ok, false); assert.equal(p.reason, 'gone')
})
t('market sell skips own offer', () => {
  let p = X.planMarket(b, 'sell', 1000e11)
  assert.equal(p.ok, true); assert.equal(p.row.id, 'b2')
  p = X.planMarket({ asks: [], bids: [] }, 'sell', 1e11)
  assert.equal(p.reason, 'empty')
})
t('limit', () => {
  assert.equal(X.limitCoin(0.0000143, 1500e11, 18), '0.02145')
  assert.equal(X.limitCoin(0.000143359, 1500e11, 9), '0.2150385')
  assert.equal(X.defaultMin(1500e11), 150); assert.equal(X.defaultMin(5e11), 1)
})
t('dayStats', () => {
  const c = [{ t: now - 30 * 3600, o: 1, h: 1, l: 1, c: 1, v: 10, vc: 10, n: 1 },
             { t: now - 10 * 3600, o: 1.1, h: 1.3, l: 1.05, c: 1.2, v: 5, vc: 6, n: 2 },
             { t: now - 2 * 3600, o: 1.2, h: 1.25, l: 0.9, c: 1.1, v: 3, vc: 3.3, n: 1 }]
  const s = X.dayStats(c, now)
  const old = X.dayStats([c[0]], now); assert.ok(isNaN(old.change)); assert.equal(old.last, 1)
  assert.equal(s.last, 1.1); assert.equal(s.open, 1); assert.ok(Math.abs(s.change - 10) < 1e-9)
  assert.equal(s.high, 1.3); assert.equal(s.low, 0.9); assert.equal(s.volume, 8); assert.equal(s.trades, 3)
  const e = X.dayStats([], now); assert.ok(isNaN(e.change)); assert.equal(e.last, 0)
})
t('chartCandles fills gaps', () => {
  const c = X.chartCandles([{ t: 0, o: 1, h: 1, l: 1, c: 1, v: 1, vc: 1, n: 1 }, { t: 7200, o: 2, h: 2, l: 2, c: 2, v: 1, vc: 1, n: 1 }], 3600, 3 * 3600 + 5, 100)
  same(c.map(x => [x.t, x.c, !!x.flat]), [[0, 1, false], [3600, 1, true], [7200, 2, false], [10800, 2, true]])
  assert.equal(X.chartCandles(c, 3600, 1000 * 3600, 5).length, 5)
})
t('trades from relay prints', () => {
  const tr = X.trades([{ at: 5, value: '0x1714340', xmz: '200000000000000', tx: 'abc' }, { at: 9, value: '0x0', xmz: '1' }], 9)
  assert.equal(tr.length, 1); assert.ok(Math.abs(tr[0].coin - 0.0242) < 1e-12); assert.equal(tr[0].xmz, 2000); assert.ok(Math.abs(tr[0].price - 0.0000121) < 1e-15)
})
t('offer lifetimes', () => {
  assert.equal(X.lifetimeText(6), '6 h'); assert.equal(X.lifetimeText(24), '1 day'); assert.equal(X.lifetimeText(168), '1 week')
  assert.equal(X.lifetimeText(336), '2 weeks'); assert.equal(X.lifetimeText(744), '31 days'); assert.equal(X.lifetimeText(36), '1.5 days')
  assert.equal(X.customHours('14', 'd'), 336); assert.equal(X.customHours('48', 'h'), 48); assert.equal(X.customHours('31', 'd'), 744)
  assert.equal(X.customHours('32', 'd'), 0); assert.equal(X.customHours('0', 'h'), 0); assert.equal(X.customHours('abc', 'd'), 0)
  assert.equal(X.timeLeft(now + 21000, now), '5.8 h left'); assert.equal(X.timeLeft(now + 6.5 * 86400, now), '6 days left')
})
t('a whole offer written as version 2 (minimum = all) is whole in the book', () => {
  const l = [{ id: 'w', pair: 'SOL/XMZ', side: 'sell-xmz', valueWei: '1000', xmzAtoms: String(100e11), minXmzAtoms: String(100e11), maker: '0xz', expiresAt: now + 9e5 }]
  const r = X.book(l, 'SOL', 9, '', now).asks[0]
  assert.equal(r.partial, false); assert.equal(X.fillOk(r, 100e11), true); assert.equal(X.fillOk(r, 50e11), false)
})
t('NoirTrade points', () => {
  same(X.noirPoints({ trades: [[7200500, 0.004], [1000, 0.002], [0, 9], [5000, -1], [3601000, 'x'], [3700000, 0.003]] }),
    [{ t: 1, p: 0.002 }, { t: 3700, p: 0.003 }, { t: 7200, p: 0.004 }])
  same(X.noirPoints(null), [])
})
const H = { SOL: [[3600, 100], [7200, 110], [10800, 120]] }
t('USD at the time of a swap', () => {
  assert.equal(X.usdAt(H, 'USDT', 5, 0), 1); assert.equal(X.usdAt(H, 'USDT-BSC', 5, 0), 1)
  assert.equal(X.usdAt(H, 'SOL', 100, 130), 100)        // before the history: its first price
  assert.equal(X.usdAt(H, 'SOL', 7300, 130), 110)       // the hour it falls in
  assert.equal(X.usdAt(H, 'SOL', 10800, 130), 120)
  assert.equal(X.usdAt(H, 'SOL', 14400, 130), 130)      // newer than the history: the price now
  assert.equal(X.usdAt(H, 'ETH', 5, 2000), 2000)        // no history: the price now
  assert.equal(X.usdAt(H, 'ETH', 5, 0), null)
})
t('chart points', () => {
  const by = { SOL: [{ at: 7300, price: 0.0001, xmz: 100, tx: 'b' }, { at: 3700, price: 0.0002, xmz: 5, tx: 'a' }], 'USDT': [{ at: 5000, price: 0.002, xmz: 10, tx: 'c' }] }
  same(X.chartPoints(by, ['SOL'], 'SOL', H, {}).map(p => [p.t, p.p]), [[3700, 0.0002], [7300, 0.0001]])
  const usd = X.chartPoints(by, ['SOL', 'USDT'], 'USD', H, { SOL: 130 })
  same(usd.map(p => [p.t, p.coin]), [[3700, 'SOL'], [5000, 'USDT'], [7300, 'SOL']])
  assert.ok(Math.abs(usd[0].p - 0.02) < 1e-12 && Math.abs(usd[2].p - 0.011) < 1e-12 && usd[1].p === 0.002)
  assert.equal(X.chartPoints(by, ['SOL'], 'USD', {}, {}).length, 0)   // no SOL price: nothing to plot in USD
})
t('candles from swaps, no filler', () => {
  const pts = [{ t: 10, p: 2, xmz: 1 }, { t: 20, p: 3, xmz: 2 }, { t: 30, p: 1, xmz: 1 }, { t: 7300, p: 5, xmz: 4 }]
  same(X.candlesOf(pts, 3600), [{ t: 0, o: 2, h: 3, l: 1, c: 1, v: 4, n: 3 }, { t: 7200, o: 5, h: 5, l: 5, c: 5, v: 4, n: 1 }])
  assert.equal(X.autoKind(pts, X.candlesOf(pts, 3600)), 'trades')
  const many = Array.from({ length: 20 }, (_, i) => ({ t: (i % 5) * 3600, p: 1, xmz: 1 }))
  assert.equal(X.autoKind(many, X.candlesOf(many.sort((a, b) => a.t - b.t), 3600)), 'candles')
})
t('axis: a tiny swap does not stretch it', () => {
  const items = [{ lo: 1.21e-5, hi: 1.21e-5, v: 2000 }, { lo: 1.44e-4, hi: 1.44e-4, v: 50 }, { lo: 0.01, hi: 0.01, v: 1 }, { lo: 4.2e-5, hi: 4.2e-5, v: 7002 }]
  const r = X.axisRange(items, [2.8e-5, null])
  assert.equal(r.lo, 1.21e-5); assert.equal(r.hi, 1.44e-4); same(r.off.map(i => i.hi), [0.01])
  assert.equal(X.axisRange([], []), null)
  assert.equal(X.axisRange([], [3]).hi, 3)
})
t('NoirTrade steps', () => {
  const pts = [{ t: 1, p: 0.002 }, { t: 3700, p: 0.003 }, { t: 3800, p: 0.004 }]
  same(X.noirSteps(pts, 'USD', 'SOL', H, 130, 3600, 0, 3 * 3600 + 5), [{ t: 0, p: 0.002 }, { t: 3600, p: 0.004 }, { t: 10800, p: 0.004 }])
  same(X.noirSteps(pts, 'SOL', 'SOL', H, 130, 3600, 3600, 3700).map(s => s.p), [0.004 / 100])
  same(X.noirSteps(pts, 'USD', 'SOL', H, 130, 3600, 99999, 99999), [{ t: 97200, p: 0.004 }])   // the price standing when the chart starts
  same(X.noirSteps([], 'USD', 'SOL', H, 130, 3600, 0, 99999), [])
})
console.log('dex exchange: ' + n + ' checks passed')
