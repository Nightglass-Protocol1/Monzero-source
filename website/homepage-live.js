// Independent public observations. A slow or failed service cannot block the others.
(function () {
  'use strict';
  const set = (id, text, cls) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    if (cls !== undefined) el.className = cls;
  };
  const get = async url => {
    const response = await fetch(url, { headers: { Accept: 'application/json' }, signal: AbortSignal.timeout(12000) });
    if (!response.ok) throw new Error('Unavailable');
    return response.json();
  };
  async function refreshSupply() {
    try {
      const s = await get('/api/v1/supply/');
      const value = s?.data?.circulating_supply;
      if (s.schema !== 'monzero-api-v1' || typeof value !== 'string' || !/^\d{1,20}(\.\d{1,11})?$/.test(value) || !Number.isSafeInteger(Math.floor(Number(value)))) throw new Error('Invalid supply');
      const dated = Number.isSafeInteger(s.observed_at) && s.observed_at > 0;
      if (dated && s.observed_at > Date.now() / 1000 + 30) throw new Error('Future observation');
      set('supply-note', dated ? `Supply observed ${new Date(s.observed_at * 1000).toLocaleString('en-GB')}${Date.now() / 1000 - s.observed_at > 600 ? ' (stale)' : ''}. Rounded down to whole XMZ; excludes rewards locked for 60 blocks.` : 'Supply observation time unavailable. Rounded down to whole XMZ; excludes locked rewards.');
      set('metric-supply', `${Math.floor(Number(value)).toLocaleString('en-GB')} XMZ`);
    } catch {
      set('metric-supply', 'Unavailable');
      set('supply-note', 'Supply data temporarily unavailable. Circulating supply excludes locked mining rewards.');
    }
  }
  async function refreshTiles() {
    const status = get('/api/status/');
    const census = get('/api/v1/network/nodes/').then(body => {
      const d = body?.data;
      if (!d || !Number.isSafeInteger(d.totals?.observed) || d.totals.observed < 0 || !d.countries || Array.isArray(d.countries) || typeof d.countries !== 'object') throw new Error('Invalid census');
      const countries = Object.keys(d.countries).filter(c => /^[A-Z]{2}$/.test(c) && c !== 'ZZ').length;
      set('tile-nodes', String(d.totals.observed));
      set('tile-nodes-detail', `in ${countries} ${countries === 1 ? 'country' : 'countries'}${d.stale ? ' (census delayed)' : ''}`);
    }).catch(() => { set('tile-nodes', '—'); set('tile-nodes-detail', 'Census unavailable right now'); });
    const faucet = Promise.all([get('/api/faucet/'), status.catch(() => null)]).then(([f, s]) => {
      if (!['open', 'paused'].includes(f?.status)) throw new Error('Unknown faucet state');
      const check = Array.isArray(s?.checks) ? s.checks.find(c => c.id === 'faucet') : null;
      set('tile-faucet', f.status === 'open' ? 'Open' : 'Paused', f.status === 'paused' || (check && check.state !== 'ok') ? 'warn' : '');
      set('tile-faucet-detail', f.status === 'paused' ? 'Claims are paused' : Number.isFinite(check?.days_left) && check.days_left >= 0 ? `Funded for about ${Math.floor(check.days_left)} days` : 'Check claim limits on the faucet page');
    }).catch(() => { set('tile-faucet', '—'); set('tile-faucet-detail', 'Faucet status unavailable'); });
    const services = status.then(s => {
      if (!['ok', 'warn', 'down', 'unknown'].includes(s?.overall) || !Array.isArray(s.checks) || !s.checks.length || !s.checks.every(c => c && typeof c.state === 'string')) throw new Error('Invalid service status');
      const bad = s.checks.filter(c => c.state !== 'ok').length;
      if (s.overall === 'ok' && bad > 0) throw new Error('Inconsistent service status');
      set('tile-status', s.overall === 'ok' ? 'All working' : bad ? `${bad} need${bad === 1 ? 's' : ''} attention` : 'Status uncertain', s.overall === 'ok' ? '' : s.overall);
      set('tile-status-detail', 'Node, pool, faucet and data feeds');
    }).catch(() => { set('tile-status', '—'); set('tile-status-detail', 'Status unavailable right now'); });
    await Promise.all([census, faucet, services]);
  }
  let updating = false;
  let lastAttempt = 0;
  async function refresh() {
    if (updating) return;
    updating = true;
    lastAttempt = Date.now();
    try { await Promise.all([refreshSupply(), refreshTiles()]); }
    finally { updating = false; }
  }
  refresh();
  window.setInterval(() => { if (!document.hidden) refresh(); }, 300000);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden && Date.now() - lastAttempt >= 300000) refresh();
  });
})();
