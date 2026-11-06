// Monzero forum: BBCode toolbar, emoji picker, Write/Preview tabs, click-to-show images and
// "Share" (copy post link). The forms work without this file; it only adds shortcuts that insert
// the same plain codes members could type, and loads images only when a reader asks.

const ICONS = {
  quote: 'M6 17h3l2-4V7H5v6h3zm8 0h3l2-4V7h-6v6h3z',
  code: 'M8.6 16.6 4 12l4.6-4.6L7.2 6 1.2 12l6 6zm6.8 0L20 12l-4.6-4.6L16.8 6l6 6-6 6z',
  list: 'M4 6h2v2H4zm4 0h12v2H8zM4 11h2v2H4zm4 0h12v2H8zm-4 5h2v2H4zm4 0h12v2H8z',
  link: 'M10.6 13.4a1 1 0 0 1 0-1.4l3.5-3.5a3 3 0 1 1 4.2 4.2l-2 2-1.4-1.4 2-2a1 1 0 0 0-1.4-1.4L12 13.4a1 1 0 0 1-1.4 0zm2.8-2.8a1 1 0 0 1 0 1.4L9.9 15.5a3 3 0 1 1-4.2-4.2l2-2 1.4 1.4-2 2a1 1 0 0 0 1.4 1.4l3.5-3.5a1 1 0 0 1 1.4 0z',
  spoiler: 'M12 5c5 0 9 4 10 7-1 3-5 7-10 7S3 15 2 12c1-3 5-7 10-7zm0 3a4 4 0 1 0 0 8 4 4 0 0 0 0-8zm0 2a2 2 0 1 1 0 4 2 2 0 0 1 0-4z',
  image: 'M4 4h16a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zm0 13h16l-5-6-4 5-3-3zM8 7a2 2 0 1 0 0 4 2 2 0 0 0 0-4z',
  emoji: 'M12 2a10 10 0 1 1 0 20 10 10 0 0 1 0-20zm-3 7a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3zm6 0a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3zm-7.5 5a5 5 0 0 0 9 0z',
};
const COLORS = ['red', 'orange', 'yellow', 'green', 'teal', 'blue', 'purple', 'gray'];
const IMAGE_PREF = 'monzero-forum-images';

// --- Images: shown only when the reader asks, or always if they turned that on (this browser only).
function alwaysShowImages() { try { return localStorage.getItem(IMAGE_PREF) === 'always'; } catch { return false; } }
function setAlwaysShowImages(on) { try { if (on) localStorage.setItem(IMAGE_PREF, 'always'); else localStorage.removeItem(IMAGE_PREF); } catch { /* not remembered */ } }

function showImage(holder) {
  if (holder.dataset.loaded) return;
  const src = holder.dataset.src || '';
  if (!/^https:\/\//i.test(src)) return;
  holder.dataset.loaded = '1';
  const link = document.createElement('a');
  link.href = src; link.target = '_blank'; link.rel = 'nofollow ugc noopener noreferrer'; link.className = 'bb-img-full';
  const img = new Image();
  img.referrerPolicy = 'no-referrer'; img.decoding = 'async';
  img.alt = `Image from ${new URL(src).hostname}`;
  img.addEventListener('load', () => holder.classList.add('bb-img-shown'));
  img.addEventListener('error', () => {
    holder.classList.remove('bb-img-shown'); holder.classList.add('bb-img-failed'); link.remove();
    const button = holder.querySelector('.bb-img-show'); if (button) button.textContent = 'Could not load image';
  });
  img.src = src;
  link.append(img); holder.append(link);
}

function setUpImages(root, auto = false) {
  root.querySelectorAll('.bb-img').forEach((holder) => {
    if (holder.dataset.ready) return;
    holder.dataset.ready = '1';
    const button = document.createElement('button');
    button.type = 'button'; button.className = 'bb-img-show'; button.textContent = 'Show image';
    button.addEventListener('click', () => showImage(holder));
    holder.append(button);
    if (auto || alwaysShowImages()) showImage(holder);
  });
}

// One "Always show images" switch per page that has images in posts.
function addImagePreference() {
  const thread = document.querySelector('.bb-thread');
  if (!thread || !thread.querySelector('.bb-img')) return;
  const label = document.createElement('label');
  label.className = 'bb-img-pref';
  const box = document.createElement('input');
  box.type = 'checkbox'; box.checked = alwaysShowImages();
  box.addEventListener('change', () => {
    setAlwaysShowImages(box.checked);
    if (box.checked) thread.querySelectorAll('.bb-img').forEach(showImage);
  });
  label.append(box, ' Always show images in posts (images come from other websites, which can see that you loaded them)');
  thread.before(label);
}

function svg(path) {
  const ns = 'http://www.w3.org/2000/svg';
  const el = document.createElementNS(ns, 'svg');
  el.setAttribute('viewBox', '0 0 24 24'); el.setAttribute('aria-hidden', 'true');
  const p = document.createElementNS(ns, 'path'); p.setAttribute('d', path); el.append(p);
  return el;
}

function setUpEditor(editor) {
  const area = editor.querySelector('textarea');
  const bar = editor.querySelector('.bb-toolbar');
  if (!area || !bar) return;
  let emoji = {};
  try { emoji = JSON.parse(editor.dataset.emoji || '{}'); } catch { /* picker stays empty */ }

  // Wrap the selection in open/close codes (or insert them around the cursor).
  const wrap = (open, close = '') => {
    const { selectionStart: start, selectionEnd: end, value } = area;
    const selected = value.slice(start, end);
    area.setRangeText(open + selected + close, start, end, 'select');
    if (selected) area.setSelectionRange(start, start + open.length + selected.length + close.length);
    else area.setSelectionRange(start + open.length, start + open.length);
    area.focus();
  };
  const insert = (text) => { area.setRangeText(text, area.selectionStart, area.selectionEnd, 'end'); area.focus(); };

  const button = (label, title, action, { icon, cls } = {}) => {
    const b = document.createElement('button');
    b.type = 'button'; b.title = title; b.setAttribute('aria-label', title);
    b.className = 'bb-tool' + (cls ? ' ' + cls : '');
    if (icon) b.append(svg(ICONS[icon])); else b.textContent = label;
    b.addEventListener('click', action);
    return b;
  };
  const group = (...items) => { const g = document.createElement('span'); g.className = 'bb-toolgroup'; g.append(...items); return g; };

  const link = () => {
    const selected = area.value.slice(area.selectionStart, area.selectionEnd);
    const url = window.prompt('Link address (must start with https:// or http://)', /^https?:\/\//i.test(selected) ? selected : 'https://');
    if (url === null) return;
    if (!/^https?:\/\/[^\s"'<>\[\]]+$/i.test(url.trim())) { window.alert('Enter a full address that starts with https:// or http://'); return; }
    if (selected && !/^https?:\/\//i.test(selected)) wrap(`[url=${url.trim()}]`, '[/url]');
    else insert(`[url]${url.trim()}[/url]`);
  };
  const image = () => {
    const selected = area.value.slice(area.selectionStart, area.selectionEnd).trim();
    const url = window.prompt('Image address (must start with https://)', /^https:\/\//i.test(selected) ? selected : 'https://');
    if (url === null) return;
    if (!/^https:\/\/[^\s"'<>\[\]]+$/i.test(url.trim())) { window.alert('Enter a full image address that starts with https://'); return; }
    area.setRangeText(`[img]${url.trim()}[/img]`, area.selectionStart, area.selectionEnd, 'end');
    area.focus();
  };
  const list = () => {
    const selected = area.value.slice(area.selectionStart, area.selectionEnd);
    const items = selected ? selected.split('\n').filter((l) => l.trim()).map((l) => `[*]${l}`).join('\n') : '[*]';
    area.setRangeText(`[list]\n${items}\n[/list]`, area.selectionStart, area.selectionEnd, 'end');
    area.focus();
  };

  const color = document.createElement('select');
  color.className = 'bb-tool bb-color-select'; color.title = 'Text colour'; color.setAttribute('aria-label', 'Text colour');
  color.append(new Option('Colour', ''), ...COLORS.map((c) => new Option(c[0].toUpperCase() + c.slice(1), c)));
  color.addEventListener('change', () => { if (color.value) wrap(`[color=${color.value}]`, '[/color]'); color.value = ''; });

  // Emoji picker: a small grid that inserts the emoji itself.
  const picker = document.createElement('div');
  picker.className = 'bb-emoji'; picker.hidden = true; picker.setAttribute('role', 'dialog'); picker.setAttribute('aria-label', 'Emoji');
  for (const [name, char] of Object.entries(emoji)) {
    const b = document.createElement('button');
    b.type = 'button'; b.textContent = char; b.title = `:${name}:`; b.setAttribute('aria-label', name.replace(/_/g, ' '));
    b.addEventListener('click', () => { insert(char); picker.hidden = true; toggle.setAttribute('aria-expanded', 'false'); });
    picker.append(b);
  }
  const toggle = button('', 'Emoji', () => {
    picker.hidden = !picker.hidden;
    toggle.setAttribute('aria-expanded', String(!picker.hidden));
    if (!picker.hidden) picker.querySelector('button')?.focus();
  }, { icon: 'emoji' });
  toggle.setAttribute('aria-expanded', 'false');
  picker.addEventListener('keydown', (e) => { if (e.key === 'Escape') { picker.hidden = true; toggle.setAttribute('aria-expanded', 'false'); toggle.focus(); } });
  document.addEventListener('click', (e) => { if (!picker.hidden && !picker.contains(e.target) && !toggle.contains(e.target)) { picker.hidden = true; toggle.setAttribute('aria-expanded', 'false'); } });

  bar.append(
    group(button('B', 'Bold (Ctrl+B)', () => wrap('[b]', '[/b]'), { cls: 'bb-tool-b' }), button('I', 'Italic (Ctrl+I)', () => wrap('[i]', '[/i]'), { cls: 'bb-tool-i' }),
      button('U', 'Underline (Ctrl+U)', () => wrap('[u]', '[/u]'), { cls: 'bb-tool-u' }), button('S', 'Strikethrough', () => wrap('[s]', '[/s]'), { cls: 'bb-tool-s' })),
    group(button('', 'Quote', () => wrap('[quote]', '[/quote]'), { icon: 'quote' }), button('', 'Code', () => wrap('[code]\n', '\n[/code]'), { icon: 'code' }),
      button('', 'List', list, { icon: 'list' }), button('', 'Insert link', link, { icon: 'link' }), button('', 'Insert image', image, { icon: 'image' }), button('', 'Spoiler', () => wrap('[spoiler]', '[/spoiler]'), { icon: 'spoiler' })),
    group(color, toggle),
    picker,
  );
  bar.hidden = false;

  // Write / Preview tabs: the server renders the preview exactly as it will be posted.
  const form = editor.closest('form');
  const tabs = document.createElement('div');
  tabs.className = 'bb-tabs'; tabs.setAttribute('role', 'tablist');
  const pane = document.createElement('div');
  pane.className = 'board-body bb-livepreview'; pane.hidden = true; pane.id = `${area.id}-preview`; pane.setAttribute('role', 'tabpanel'); pane.tabIndex = 0;
  const tab = (text, selected) => {
    const t = document.createElement('button');
    t.type = 'button'; t.className = 'bb-tab'; t.textContent = text; t.setAttribute('role', 'tab'); t.setAttribute('aria-selected', String(selected));
    tabs.append(t); return t;
  };
  const write = tab('Write', true);
  const preview = tab('Preview', false);
  const select = (showPreview) => {
    write.setAttribute('aria-selected', String(!showPreview)); preview.setAttribute('aria-selected', String(showPreview));
    area.hidden = showPreview; bar.hidden = showPreview; pane.hidden = !showPreview;
  };
  write.addEventListener('click', () => { select(false); area.focus(); });
  preview.addEventListener('click', async () => {
    if (!form) return;
    select(true);
    pane.textContent = 'Loading preview…';
    const data = new FormData(form);
    data.set(area.name, area.value);
    data.delete('preview');
    try {
      const reply = await fetch('/community/board/?do=preview', { method: 'POST', body: data, credentials: 'same-origin' });
      if (!reply.ok && reply.status !== 400) throw new Error(String(reply.status));
      pane.innerHTML = await reply.text();   // escaped, server-rendered post HTML
      setUpImages(pane, true);
    } catch {
      pane.textContent = 'The preview could not be loaded. Use the Preview button below instead.';
    }
  });
  editor.querySelector('label[for]')?.after(tabs);
  area.after(pane);

  area.addEventListener('keydown', (e) => {
    if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
    const tag = { b: 'b', i: 'i', u: 'u' }[e.key.toLowerCase()];
    if (tag) { e.preventDefault(); wrap(`[${tag}]`, `[/${tag}]`); }
  });
}

document.querySelectorAll('.bb-editor').forEach(setUpEditor);
setUpImages(document.querySelector('.bb-preview') || document.createElement('div'), true);   // the Preview button's panel
document.querySelectorAll('.bb-thread').forEach((thread) => setUpImages(thread));
addImagePreference();

// Likes toggle in place; without this script the form posts and the page reloads at the post.
document.querySelectorAll('.bb-like-form').forEach((form) => {
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const button = form.querySelector('button');
    button.disabled = true;
    try {
      const reply = await fetch(form.action, { method: 'POST', body: new FormData(form), credentials: 'same-origin', headers: { Accept: 'application/json' } });
      const data = await reply.json();
      if (!reply.ok) { window.alert(data.error || 'That did not work. Try again.'); return; }
      button.setAttribute('aria-pressed', String(data.liked));
      button.title = data.liked ? 'Remove your like' : 'Like this post';
      button.querySelector('.bb-like-label').textContent = data.liked ? 'Liked' : 'Like';
      button.querySelector('.bb-like-count').textContent = data.count ? data.count.toLocaleString() : '';
      const summary = form.closest('.bb-likes')?.querySelector('.bb-like-summary');
      if (summary) summary.textContent = data.summary;
    } catch {
      form.submit();
    } finally {
      button.disabled = false;
    }
  });
});

// "Share" copies the post's link; without clipboard access it simply follows the link.
document.querySelectorAll('[data-share]').forEach((link) => {
  link.addEventListener('click', async (e) => {
    const url = new URL(link.getAttribute('href'), location.href).href;
    if (!navigator.clipboard) return;
    e.preventDefault();
    try {
      await navigator.clipboard.writeText(url);
      const label = link.textContent;
      link.textContent = 'Link copied';
      setTimeout(() => { link.textContent = label; }, 1600);
    } catch { location.href = url; }
  });
});
