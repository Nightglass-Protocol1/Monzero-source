// Service status page: renders /api/status/ (labels, states, ages; no secrets).
const $ = (id) => document.getElementById(id);
const WORDS = { ok: 'Working', warn: 'Needs attention', down: 'Not working' };

export function ago(seconds) {
  if (seconds < 120) return `${seconds} s ago`;
  if (seconds < 7200) return `${Math.floor(seconds / 60)} min ago`;
  if (seconds < 172800) return `${Math.floor(seconds / 3600)} h ago`;
  return `${Math.floor(seconds / 86400)} days ago`;
}

export function valid(body) {
  return body && body.schema === 'monzero-status-v1' && Number.isInteger(body.evaluated_at) && ['ok', 'warn', 'down'].includes(body.overall)
    && Array.isArray(body.checks) && body.checks.every((c) => typeof c.label === 'string' && ['ok', 'warn', 'down'].includes(c.state) && typeof c.detail === 'string');
}

function render(body) {
  const banner = $('overall');
  banner.className = `overall ${body.overall}`;
  banner.textContent = body.overall === 'ok' ? 'All services are working.' : body.overall === 'warn' ? 'Some services need attention.' : 'Some services are not working.';
  $('checked').textContent = `Checked ${new Date(body.evaluated_at * 1000).toISOString().replace('T', ' ').slice(0, 16)} UTC.`;
  $('checks').replaceChildren(...body.checks.map((c) => {
    const li = document.createElement('li');
    li.className = `check ${c.state}`;
    const head = document.createElement('div');
    const name = document.createElement('strong');
    name.textContent = c.label;
    const state = document.createElement('span');
    state.className = 'state';
    state.textContent = WORDS[c.state];
    head.append(name, state);
    const detail = document.createElement('p');
    detail.textContent = c.detail;
    li.append(head, detail);
    return li;
  }));
}

async function load() {
  try {
    const response = await fetch('/api/status/', { headers: { Accept: 'application/json' }, cache: 'no-store' });
    const body = await response.json();
    if (!response.ok || !valid(body)) throw new Error('bad status');
    render(body);
  } catch {
    $('overall').className = 'overall down';
    $('overall').textContent = 'The status check is unavailable right now.';
  }
}

if (typeof document !== 'undefined' && $('checks')) { load(); setInterval(load, 60000); }
