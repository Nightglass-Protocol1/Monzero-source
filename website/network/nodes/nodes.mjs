// Country-level node map. Data holds per-country counts only; nothing finer exists to show.
const API = '/api/v1/network/nodes/?history=daily';
const LEVELS = [[1, '1'], [2, '2–4'], [5, '5–9'], [10, '10–24'], [25, '25+']];
const $ = (id) => document.getElementById(id);

export function level(count) {
  let lvl = 0;
  LEVELS.forEach(([min], i) => { if (count >= min) lvl = i + 1; });
  return lvl;
}

export function validCensus(body) {
  const d = body && body.schema === 'monzero-api-v1' && body.data;
  if (!d || !Number.isInteger(body.observed_at) || !d.totals || typeof d.countries !== 'object' || Array.isArray(d.countries)) return false;
  const ok = (c) => c && ['observed', 'reachable', 'connected'].every((k) => Number.isInteger(c[k]) && c[k] >= 0);
  return ok(d.totals) && Object.entries(d.countries).every(([code, c]) => /^[A-Z]{2}$/.test(code) && ok(c));
}

function cell(row, text, numeric = false) {
  const td = document.createElement('td');
  td.textContent = text;
  if (numeric) td.className = 'num';
  row.append(td);
}

function utc(seconds) { return new Date(seconds * 1000).toISOString().replace('T', ' ').slice(0, 16) + ' UTC'; }

async function loadMap() {
  const response = await fetch('world.svg?v=20261020');
  if (!response.ok) throw new Error('map outline unavailable');
  const svg = new DOMParser().parseFromString(await response.text(), 'image/svg+xml').documentElement;
  if (svg.nodeName !== 'svg') throw new Error('map outline invalid');
  svg.querySelectorAll('script, foreignObject').forEach((n) => n.remove());
  return document.importNode(svg, true);
}

function render(body, svg) {
  const d = body.data;
  const names = new Map();
  if (svg) svg.querySelectorAll('path[data-cc]').forEach((p) => names.set(p.dataset.cc, p.dataset.name));
  const name = (code) => code === 'ZZ' ? 'Unknown (not in database)' : (names.get(code) || code);
  const countries = Object.entries(d.countries).sort((a, b) => b[1].observed - a[1].observed || name(a[0]).localeCompare(name(b[0])));
  const located = countries.filter(([code]) => code !== 'ZZ').length;

  $('status').textContent = d.totals.observed
    ? `${d.totals.observed} observed nodes in ${located} ${located === 1 ? 'country' : 'countries'}.`
    : 'The last census observed no public nodes.';
  $('summary').textContent = `Collected ${utc(body.observed_at)} from ${d.geo_database}. ${d.vantage}`;
  if (d.stale) {
    const warn = document.createElement('strong');
    warn.className = 'stale';
    warn.textContent = ' This census is more than two hours old; the collector may be stopped.';
    $('summary').append(warn);
  }

  if (svg) {
    svg.querySelectorAll('path[data-cc]').forEach((path) => {
      const c = d.countries[path.dataset.cc];
      const title = document.createElementNS('http://www.w3.org/2000/svg', 'title');
      title.textContent = c ? `${path.dataset.name}: ${c.observed} observed, ${c.reachable} accepting connections` : `${path.dataset.name}: none observed`;
      path.append(title);
      if (c && c.observed > 0) path.classList.add(`lvl-${level(c.observed)}`);
    });
    $('map').replaceChildren(svg);
    const legend = $('legend');
    legend.replaceChildren(...[['', 'None'], ...LEVELS.map(([, label], i) => [`lvl-${i + 1}`, label])].map(([cls, label]) => {
      const item = document.createElement('span');
      const swatch = document.createElement('i');
      if (cls) swatch.className = cls;
      item.append(swatch, label);
      return item;
    }));
    $('map-figure').hidden = false;
  }

  const rows = countries.map(([code, c]) => {
    const tr = document.createElement('tr');
    cell(tr, name(code));
    cell(tr, String(c.observed), true);
    cell(tr, String(c.reachable), true);
    cell(tr, String(c.connected), true);
    cell(tr, d.totals.observed ? `${(100 * c.observed / d.totals.observed).toFixed(1)}%` : '–', true);
    return tr;
  });
  $('rows').replaceChildren(...rows);
  $('table-card').hidden = rows.length === 0;

  const history = Array.isArray(d.history) ? d.history.slice(-30).reverse() : [];
  $('history-rows').replaceChildren(...history.map((h) => {
    const tr = document.createElement('tr');
    cell(tr, String(h.date));
    cell(tr, String(h.totals.observed), true);
    cell(tr, String(h.totals.reachable), true);
    cell(tr, String(h.countries), true);
    return tr;
  }));
  $('history-card').hidden = history.length === 0;
}

async function main() {
  $('status').textContent = 'Loading the node map…';
  let body;
  try {
    const response = await fetch(API, { headers: { Accept: 'application/json' } });
    body = await response.json();
    if (response.status === 503) { $('status').textContent = 'No node census has been collected yet. Check back later.'; return; }
    if (!response.ok || !validCensus(body)) throw new Error('bad response');
  } catch {
    $('status').textContent = 'The node map is unavailable right now. Try again later.';
    return;
  }
  let svg = null;
  try { svg = await loadMap(); } catch { /* the table still shows every country */ }
  render(body, svg);
}

if (typeof document !== 'undefined' && document.getElementById('map')) main();
