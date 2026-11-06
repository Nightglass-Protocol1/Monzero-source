import { validGoals } from './fund/campaigns.mjs';

// Public-chain currencies returned by the existing donations endpoint. Requiring
// the complete set prevents an incomplete or duplicate response becoming a total.
const currencies = new Map([['btc', 8], ['ltc', 8], ['bch', 8], ['doge', 8], ['eth', 18], ['usdt-sol', 6]]);
const validTime = value => Number.isSafeInteger(value) && value > 0;

export function summariseDonations(data, now = Date.now() / 1000) {
  if (data?.schema !== 'monzero-donations-v1' || !Array.isArray(data.coins) || data.coins.length !== currencies.size) throw new Error('Incomplete records');
  const seen = new Set();
  const times = [data.pricedAt];
  let received = 0;
  let stale = false;
  for (const coin of data.coins) {
    if (!coin || !currencies.has(coin.id) || seen.has(coin.id) || coin.decimals !== currencies.get(coin.id)) throw new Error('Invalid currency record');
    seen.add(coin.id);
    const price = data.pricesUsd?.[coin.id];
    if (typeof coin.received !== 'string' || !/^\d{1,40}$/.test(coin.received) || typeof price !== 'number' || !Number.isFinite(price) || price <= 0) throw new Error('Incomplete amounts or prices');
    // This is explicitly an approximate USD valuation, never a spendable balance.
    received += Number(coin.received) / 10 ** coin.decimals * price;
    times.push(coin.fetchedAt);
    stale ||= coin.current !== true;
  }
  if (!Number.isFinite(received) || times.some(t => validTime(t) && t > now + 300)) throw new Error('Invalid observation');
  stale ||= times.some(t => !validTime(t) || now - t > 1200);
  const observedAt = times.every(validTime) ? Math.min(...times) : null;
  return { received, stale, observedAt };
}

async function loadFunding() {
  const set = (id, value) => { document.getElementById(id).textContent = value; };
  const usd = new Intl.NumberFormat('en-GB', { style: 'currency', currency: 'USD', maximumFractionDigits: 0 });
  const get = async url => {
    const response = await fetch(url, { cache: 'no-cache', signal: AbortSignal.timeout(12000) });
    if (!response.ok) throw new Error('Unavailable');
    return response.json();
  };
  try {
    const goals = await get('fund/goals.json');
    if (!validGoals(goals)) throw new Error('Invalid goals');
    // Match the fund page's order, retaining a reached goal until it is closed.
    const goal = goals.records.find(g => g.status !== 'closed');
    if (!goal) {
      set('support-goal-name', 'No active fundraising goals');
      set('support-status', 'See the funding page for past goals and spending records.');
      return;
    }
    const target = Number(goal.target_usd);
    set('support-goal-name', goal.title);
    set('support-goal-target', usd.format(target));
    const summary = summariseDonations(await get('api/donations/'));
    set('support-raised', usd.format(summary.received));
    set('support-remaining', usd.format(Math.max(0, target - summary.received)));
    const age = summary.observedAt ? `Oldest source: ${new Date(summary.observedAt * 1000).toLocaleString('en-GB')}.` : 'Source timestamps incomplete.';
    set('support-status', `${summary.stale ? 'Stale or undated data. ' : ''}${age} Approximate USD value of public-chain receipts; excludes private XMZ donations. The gap compares receipts with the first target, not funds available after spending. See the funding page for allocations.`);
  } catch {
    set('support-status', 'Contribution totals temporarily unavailable or incomplete. Check the funding page for individual records; missing data is not zero.');
  }
}

if (typeof document !== 'undefined' && document.getElementById('support-status')) loadFunding();
