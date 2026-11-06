// Dated posts edited from /admin (announcements, roadmap updates). A missing file means none yet.
const text = (v, max) => typeof v === 'string' && v.trim() === v && v.length > 0 && v.length <= max && !/[\p{Cc}\p{Cf}\p{Cs}\u2028\u2029]/u.test(v);
const prose = (v, max) => typeof v === 'string' && text(v.replace(/\n/g, ' '), max);
const date = (v) => typeof v === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(v) && new Date(`${v}T00:00:00Z`).toISOString().slice(0, 10) === v;
export function safeLink(v) {
  if (typeof v !== 'string' || /\s/.test(v)) return false;
  if (/^\/(?!\/)/.test(v)) return !v.includes('\\');
  try { const u = new URL(v); return u.protocol === 'https:' && !u.username && !u.password; } catch { return false; }
}
const keys = (o, names) => o && typeof o === 'object' && !Array.isArray(o) && Object.keys(o).length === names.length && names.every((k) => Object.hasOwn(o, k));
export function validPosts(data, schema) {
  if (!keys(data, ['schema', 'updated_at', 'records']) || data.schema !== schema || !date(data.updated_at) || !Array.isArray(data.records) || data.records.length > 200) return false;
  const ids = new Set();
  return data.records.every((p) => {
    if (!keys(p, ['id', 'date', 'title', 'body', 'url']) || !text(p.id, 64) || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(p.id) || ids.has(p.id)) return false;
    ids.add(p.id);
    return date(p.date) && p.date <= data.updated_at && text(p.title, 120) && prose(p.body, 1500) && (p.url === null || safeLink(p.url));
  });
}
export async function showPosts(url, schema, section, list, limit = 10) {
  let data;
  try {
    const response = await fetch(url, { headers: { Accept: 'application/json' }, cache: 'no-cache' });
    if (!response.ok) return;
    data = await response.json();
  } catch { return; }
  if (!validPosts(data, schema) || !data.records.length) return;
  list.replaceChildren(...data.records.slice(0, limit).map((p) => {
    const li = document.createElement('li');
    const time = document.createElement('time');
    time.dateTime = p.date; time.textContent = p.date;
    const h = document.createElement('h3');
    h.textContent = p.title;
    li.append(time, h, ...p.body.split(/\n+/).map((para) => { const el = document.createElement('p'); el.textContent = para; return el; }));
    if (p.url) {
      const a = document.createElement('a');
      a.href = p.url; a.textContent = 'Read more';
      if (!p.url.startsWith('/')) a.rel = 'noopener noreferrer';
      li.append(a);
    }
    return li;
  }));
  section.hidden = false;
}
if (typeof document !== 'undefined') {
  const announcements = document.getElementById('announcements');
  if (announcements) showPosts('/community/announcements.json', 'monzero-announcements-v1', announcements, document.getElementById('announcement-list'), Number(announcements.dataset.limit) || 10);
  const updates = document.getElementById('roadmap-updates');
  if (updates) showPosts('/roadmap/updates.json', 'monzero-roadmap-updates-v1', updates, document.getElementById('update-list'), 5);
}
