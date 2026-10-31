// Dependency-free check of the display helper; not a QML layout test.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const helper = fs.readFileSync(path.join(__dirname, '../js/DexDemo.js'), 'utf8');
const context = vm.createContext({});
vm.runInContext(helper.replace(/^\.pragma library\r?\n/, ''), context);
const price = context.price;
for (const anchor of [1172500, 1325, 6840]) {
    assert.equal(price(anchor, '0.08421'), anchor.toFixed(5));
    const asks = [0.08512, 0.08491, 0.08475, 0.08462, 0.08439].map(p => Number(price(anchor, p)));
    const bids = [0.08410, 0.08392, 0.08370, 0.08344, 0.08318].map(p => Number(price(anchor, p)));
    assert(asks.every(p => p > anchor));
    assert(bids.every(p => p < anchor));
    assert(asks.every((p, i) => !i || asks[i - 1] > p));
    assert(bids.every((p, i) => !i || bids[i - 1] > p));
}
for (const [anchor, sample] of [[0, 0.08421], [1325, 'invalid'], [Infinity, 0.08421]])
    assert.equal(price(anchor, sample), '—');
console.log('PASS: demo anchors, spreads, ordering and invalid inputs for all three pairs');
