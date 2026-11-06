// Fundraising goals and spending records (fund/goals.json, fund/spending.json, edited in /admin).
// Without the files the page keeps its built-in goals.
const text = (v, max) => typeof v === 'string' && v.trim() === v && v.length > 0 && v.length <= max && !/[\p{Cc}\p{Cf}\p{Cs}\u2028\u2029]/u.test(v);
const prose = (v, max) => typeof v === 'string' && text(v.replace(/\n/g, ' '), max);
const date = (v) => typeof v === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(v) && new Date(`${v}T00:00:00Z`).toISOString().slice(0, 10) === v;
const keys = (o, names) => o && typeof o === 'object' && !Array.isArray(o) && Object.keys(o).length === names.length && names.every((k) => Object.hasOwn(o, k));
const id = (v) => text(v, 64) && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(v);
export function link(v) {
  if (!text(v, 2048) || /\s/.test(v)) return false;
  if (/^\/(?!\/)/.test(v)) return !v.includes('\\');
  try { const u = new URL(v); return u.protocol === 'https:' && !u.username && !u.password && !!u.hostname; } catch { return false; }
}
const usd = (v) => typeof v === 'string' && /^(0|[1-9]\d{0,8})(\.\d{2})?$/.test(v);
export function validGoals(d) {
  if (!keys(d, ['schema', 'updated_at', 'records']) || d.schema !== 'monzero-fund-goals-v1' || !date(d.updated_at) || !Array.isArray(d.records) || d.records.length < 1 || d.records.length > 10) return false;
  const ids = new Set();
  return d.records.every((g) => {
    if (!keys(g, ['id', 'title', 'description', 'target_usd', 'status']) || !id(g.id) || ids.has(g.id)) return false;
    ids.add(g.id);
    return text(g.title, 120) && prose(g.description, 2000) && typeof g.target_usd === 'string' && /^[1-9]\d{0,8}$/.test(g.target_usd) && ['open', 'reached', 'closed'].includes(g.status);
  });
}
export function validSpending(d) {
  if (!keys(d, ['schema', 'updated_at', 'records']) || d.schema !== 'monzero-fund-spending-v1' || !date(d.updated_at) || !Array.isArray(d.records) || d.records.length > 500) return false;
  const ids = new Set();
  return d.records.every((s) => {
    if (!keys(s, ['id', 'date', 'goal', 'description', 'paid_to', 'amount', 'currency', 'amount_usd', 'evidence_url']) || !id(s.id) || ids.has(s.id)) return false;
    ids.add(s.id);
    if (!date(s.date) || s.date > d.updated_at || !id(s.goal) || !text(s.description, 400) || !text(s.paid_to, 120)) return false;
    if (typeof s.amount !== 'string' || !/^(0|[1-9]\d{0,17})(\.\d{1,11})?$/.test(s.amount) || /^0(\.0+)?$/.test(s.amount) || typeof s.currency !== 'string' || !/^[A-Z]{3,5}$/.test(s.currency) || !usd(s.amount_usd)) return false;
    // Every spend over $100 needs an invoice or receipt link.
    return s.evidence_url === null ? Number(s.amount_usd) <= 100 : link(s.evidence_url);
  });
}
const $ = (sel) => document.querySelector(sel);
const fmt = new Intl.NumberFormat('en-US', { style: 'currency', currency: 'USD', maximumFractionDigits: 0 });
function el(tag, cls, content) { const e = document.createElement(tag); if (cls) e.className = cls; if (content !== undefined) e.textContent = content; return e; }
function renderGoals(d) {
  const list = $('ol.goals');
  if (!list) return;
  const open = d.records.filter((g) => g.status !== 'closed');
  list.replaceChildren(...open.map((g, i) => {
    const li = el('li');
    li.append(el('h3', '', `${i + 1}. ${g.title}`), ...g.description.split(/\n+/).map((p) => el('p', '', p)));
    const fig = el('div', 'goal-figures');
    const raised = el('span', '', 'Raised: ');
    const amount = el('span', 'goal-raised', '$0'); amount.dataset.goal = String(i);
    raised.append(amount);
    const share = el('span', 'goal-percent', g.status === 'reached' ? 'goal reached' : '0%'); share.dataset.goal = String(i);
    fig.append(raised, share, el('span', '', `Target: ${fmt.format(Number(g.target_usd))}`));
    const bar = el('div', 'bar'); bar.setAttribute('aria-hidden', 'true');
    const fill = el('span', 'goal-bar'); fill.dataset.goal = String(i); fill.dataset.target = g.target_usd;
    bar.append(fill);
    li.append(fig, bar);
    return li;
  }));
  const heading = $('#goals-heading');
  if (heading) heading.textContent = open.length === 1 ? 'One goal, funded and reported separately.' : `${open.length === 2 ? 'Two' : open.length === 3 ? 'Three' : open.length} goals, funded and reported separately.`;
}
function renderSpending(d, goals) {
  const section = $('#spending');
  if (!section || !d.records.length) return;
  const names = Object.fromEntries((goals?.records || []).map((g) => [g.id, g.title]));
  $('#spending-rows').replaceChildren(...[...d.records].sort((a, b) => (a.date < b.date ? 1 : -1)).map((s) => {
    const tr = el('tr');
    tr.append(el('td', '', s.date), el('td', '', names[s.goal] || (s.goal === 'general' ? 'General' : s.goal)), el('td', '', `${s.description} (paid to ${s.paid_to})`),
      el('td', 'num', `${s.amount} ${s.currency}`), el('td', 'num', `$${s.amount_usd}`));
    const ev = el('td');
    if (s.evidence_url) { const a = el('a', '', 'Receipt'); a.href = s.evidence_url; a.rel = 'noopener noreferrer'; ev.append(a); } else ev.textContent = 'Under $100';
    tr.append(ev);
    return tr;
  }));
  section.hidden = false;
}
const get = (url) => fetch(url, { cache: 'no-cache' }).then((r) => (r.ok ? r.json() : null)).catch(() => null);
if (typeof document !== 'undefined' && $('ol.goals')) {
  Promise.all([get('/fund/goals.json'), get('/fund/spending.json')]).then(([goals, spending]) => {
    const okGoals = goals && validGoals(goals) ? goals : null;
    if (okGoals) renderGoals(okGoals);
    if (spending && validSpending(spending)) renderSpending(spending, okGoals);
    if (okGoals && typeof window.monzeroFundRender === 'function') window.monzeroFundRender();   // refill the bars with the live total
  });
}
