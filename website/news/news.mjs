// Top crypto headlines (news/data/*.json, written by the signed news ingest).
const $ = (id) => document.getElementById(id);
const https = (v) => { try { const u = new URL(v); return u.protocol === 'https:' && !u.username && !u.password; } catch { return false; } };
const text = (v, max) => typeof v === 'string' && v.trim() === v && v.length > 0 && v.length <= max && !/[\p{Cc}\p{Cf}\u2028\u2029]/u.test(v);

export function validNews(d) {
  if (!d || d.schema !== 'monzero-news-v1' || !Number.isInteger(d.generated_at) || !/^\d{4}-\d{2}-\d{2}$/.test(d.date) || !Array.isArray(d.stories) || !Array.isArray(d.sources)) return false;
  const names = new Set(d.sources.filter((s) => s && text(s.name, 60) && https(s.home)).map((s) => s.name));
  return d.sources.length === names.size && d.stories.length <= 30 && d.stories.every((s) => s && text(s.title, 300) && https(s.url) && names.has(s.source)
    && Number.isInteger(s.published_at) && Number.isInteger(s.outlets) && s.outlets >= 1 && Array.isArray(s.also) && s.also.every((a) => a && names.has(a.source) && https(a.url)));
}

export function ago(seconds) {
  if (seconds < 3600) return `${Math.max(1, Math.round(seconds / 60))} min ago`;
  if (seconds < 86400) return `${Math.round(seconds / 3600)} h ago`;
  return `${Math.round(seconds / 86400)} days ago`;
}

function link(href, label, cls) {
  const a = document.createElement('a');
  a.href = href; a.textContent = label; a.rel = 'noopener noreferrer nofollow';
  if (cls) a.className = cls;
  return a;
}

function render(d, live) {
  $('news-title').textContent = live ? 'Today' : d.date;
  const updated = new Date(d.generated_at * 1000).toISOString().replace('T', ' ').slice(0, 16);
  $('news-status').textContent = d.stories.length ? `${d.stories.length} top stories, updated ${updated} UTC.` : 'No headlines yet for this day.';
  $('stories').replaceChildren(...d.stories.map((s) => {
    const li = document.createElement('li');
    const title = link(s.url, s.title, 'headline');
    const meta = document.createElement('div');
    meta.className = 'meta';
    meta.textContent = `${s.source} · ${live ? ago(d.generated_at - s.published_at) : new Date(s.published_at * 1000).toISOString().slice(11, 16) + ' UTC'}`;
    if (s.outlets > 1) { const b = document.createElement('span'); b.className = 'badge'; b.textContent = `${s.outlets} outlets`; meta.append(b); }
    li.append(title, meta);
    if (s.also.length) {
      const also = document.createElement('div');
      also.className = 'also';
      also.append('Also covered by ');
      s.also.forEach((a, i) => { if (i) also.append(', '); also.append(link(a.url, a.source)); });
      li.append(also);
    }
    return li;
  }));
  $('sources').replaceChildren(...d.sources.map((s) => {
    const li = document.createElement('li');
    if (!s.ok) li.className = 'down';
    li.append(link(s.home, s.name), ` (${s.region})${s.ok ? '' : ' - not reachable at the last update'}`);
    return li;
  }));
}

async function load(day) {
  $('news-status').textContent = 'Loading the headlines…';
  try {
    const r = await fetch(`/news/data/${day}.json`, { cache: 'no-cache' });
    if (!r.ok) throw new Error(String(r.status));
    const d = await r.json();
    if (!validNews(d)) throw new Error('invalid');
    render(d, day === 'latest');
  } catch {
    $('stories').replaceChildren();
    $('news-status').textContent = 'The headlines are unavailable right now. Try again later.';
  }
}

async function init() {
  const select = $('day');
  try {
    const r = await fetch('/news/data/index.json', { cache: 'no-cache' });
    const idx = r.ok ? await r.json() : null;
    if (idx && Array.isArray(idx.days)) for (const day of idx.days.slice(1, 30)) if (/^\d{4}-\d{2}-\d{2}$/.test(day)) select.append(new Option(day, day));
  } catch { /* the live list still works */ }
  select.disabled = false;
  select.addEventListener('change', () => load(select.value));
  load('latest');
}

if (typeof document !== 'undefined' && $('stories')) init();
