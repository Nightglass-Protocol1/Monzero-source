// Live countdown for the v1.0.0 upgrade (/api/v1/network/upgrade/).
const $ = (id) => document.getElementById(id);
export function describe(d, now = Date.now() / 1000) {
  if (d.active) return { count: 'The upgrade is active.', detail: `Block ${d.height.toLocaleString('en')} is past the upgrade height of ${d.fork_height.toLocaleString('en')}. If a wallet stopped syncing, update it to v1.0.0.` };
  const days = Math.max(0, (d.estimated_at - now) / 86400);
  const when = new Date(d.estimated_at * 1000).toLocaleString('en-GB', { dateStyle: 'medium', timeStyle: 'short', timeZone: 'UTC' });
  return { count: `${d.blocks_left.toLocaleString('en')} blocks to go`, detail: `Now at block ${d.height.toLocaleString('en')}. At two minutes per block that is about ${days < 1 ? Math.max(1, Math.round(days * 24)) + ' hours' : days.toFixed(1) + ' days'}, around ${when} UTC. Blocks can come faster or slower, so update now rather than at the last minute.` };
}
async function load() {
  try {
    const r = await fetch('/api/v1/network/upgrade/', { headers: { Accept: 'application/json' } });
    const body = await r.json();
    const d = body && body.data;
    if (!r.ok || !d || !Number.isInteger(d.height) || !Number.isInteger(d.fork_height)) throw new Error('bad');
    const text = describe(d);
    $('count').textContent = text.count;
    $('count').classList.toggle('active', d.active);
    $('count-detail').textContent = text.detail;
    $('node-ready').textContent = d.public_node_ready ? 'The public node node.monzero.org is running v1.0.0 and is ready for the upgrade.' : 'The public node has not confirmed the upgrade yet.';
  } catch {
    $('count').textContent = 'The upgrade is at block 21,000 (around 31 October 2026).';
    $('count-detail').textContent = 'The live block height is unavailable right now.';
    $('node-ready').textContent = '';
  }
}
if (typeof document !== 'undefined' && $('count')) { load(); setInterval(load, 120000); }
