// Monzero Studio batch builder: many images → numbered metadata files and a manifest,
// zipped. Every file is built with the same canonical metadata as the single-asset form.
(() => {
  'use strict';

  const core = window.MonzeroStudioCore;
  const $ = (selector, root = document) => root.querySelector(selector);
  const form = $('#bulk-form');
  if (!form) return;
  const field = (name) => form.elements[name];
  const MAX_FILES = 1000;
  const SHOWN = 120;
  const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' });

  const state = {
    images: [],     // { file, name, art, thumb, location } sorted by file name
    csv: null,      // { rows: Map(file → {name?, description?, traits[]}), columns }
    items: [],      // built entries
    building: 0,
  };

  function li(level, message) {
    const item = document.createElement('li');
    item.className = level;
    item.textContent = message;
    return item;
  }

  function status(text) { $('#bulk-status').textContent = text; }

  // ---------------------------------------------------------------- inputs

  // RFC 4180 CSV: quoted fields, doubled quotes, CRLF or LF.
  function parseCsv(text) {
    const rows = [];
    let row = [];
    let cell = '';
    let quoted = false;
    for (let i = 0; i < text.length; i += 1) {
      const c = text[i];
      if (quoted) {
        if (c === '"' && text[i + 1] === '"') { cell += '"'; i += 1; }
        else if (c === '"') quoted = false;
        else cell += c;
      } else if (c === '"') quoted = true;
      else if (c === ',') { row.push(cell); cell = ''; }
      else if (c === '\n' || c === '\r') {
        if (c === '\r' && text[i + 1] === '\n') i += 1;
        row.push(cell); rows.push(row); row = []; cell = '';
      } else cell += c;
    }
    if (cell || row.length) { row.push(cell); rows.push(row); }
    return rows.filter((r) => r.some((x) => x.trim()));
  }

  async function loadCsv(file) {
    if (!file) { state.csv = null; build(); return; }
    const rows = parseCsv((await file.text()).replace(/^﻿/, ''));
    const header = (rows.shift() || []).map((h) => h.trim());
    const fileCol = header.findIndex((h) => h.toLowerCase() === 'file');
    if (fileCol < 0) {
      state.csv = { error: 'The traits CSV needs a "file" column with each image\'s file name.' };
      build();
      return;
    }
    const special = { name: -1, description: -1 };
    header.forEach((h, i) => { if (h.toLowerCase() in special) special[h.toLowerCase()] = i; });
    const traitCols = header.map((h, i) => i).filter((i) => i !== fileCol && i !== special.name && i !== special.description && header[i]);
    const map = new Map();
    for (const r of rows) {
      const key = (r[fileCol] || '').trim();
      if (!key) continue;
      map.set(key, {
        name: special.name >= 0 ? (r[special.name] || '').trim() : '',
        description: special.description >= 0 ? (r[special.description] || '').trim() : '',
        traits: traitCols.map((i) => ({ trait_type: header[i].slice(0, 40), value: (r[i] || '').trim().slice(0, 60) })).filter((t) => t.value),
      });
    }
    state.csv = { rows: map, columns: traitCols.map((i) => header[i]) };
    build();
  }

  async function loadImages(files) {
    const list = [...files].slice(0, MAX_FILES).sort((a, b) => collator.compare(a.name, b.name));
    const skipped = files.length - list.length;
    state.images = [];
    state.items = [];
    render();
    const out = [];
    for (let i = 0; i < list.length; i += 1) {
      status(`Fingerprinting ${i + 1} of ${list.length}…`);
      const result = await core.fingerprint(list[i]);
      if (result.error) { out.push({ file: list[i], name: list[i].name, error: result.error }); continue; }
      out.push({ file: list[i], name: list[i].name, art: result.art, thumb: core.thumbnail(result.img, 200), location: result.location });
      URL.revokeObjectURL(result.url);
    }
    state.images = out;
    $('#bulk-count').textContent = `${out.length} image${out.length === 1 ? '' : 's'} selected${skipped > 0 ? ` (the first ${MAX_FILES}; ${skipped} left out)` : ''}`;
    status('');
    build();
  }

  // ---------------------------------------------------------------- build

  function settings() {
    const text = (name) => field(name).value.trim();
    return {
      kind: field('kind').value,
      pattern: text('pattern'),
      start: Math.max(0, Math.floor(Number(field('start').value) || 0)),
      digits: Number(field('digits').value),
      edition_size: field('edition_size').value,
      description: field('description').value,
      creator: field('creator').value,
      license: field('license').value,
      external_url: field('external_url').value,
      collection: field('collection').value,
      imageBase: text('image_base'),
      metadataBase: text('metadata_base'),
    };
  }

  async function build() {
    const seq = ++state.building;
    const s = settings();
    const checks = [];
    $('[data-bulk-edition]').hidden = s.kind !== 'edition';
    const images = state.images.filter((x) => x.art);
    const failed = state.images.filter((x) => x.error);
    if (!state.images.length) {
      state.items = [];
      render([li('warn', 'Add your images to begin')]);
      return;
    }
    checks.push(li('ok', `${images.length} image${images.length === 1 ? '' : 's'} fingerprinted`));
    if (failed.length) checks.push(li('bad', `${failed.length} file${failed.length === 1 ? '' : 's'} skipped: ${failed[0].name}: ${failed[0].error}`));
    if (!s.pattern) checks.push(li('bad', 'Add a name pattern, for example "Tide Glass #{n}"'));
    else if (!s.pattern.includes('{n}') && !(state.csv?.rows)) checks.push(li('warn', 'The name pattern has no {n}, so every piece gets the same name'));
    if (s.kind === 'edition') {
      const n = Number(s.edition_size);
      if (!(Number.isInteger(n) && n >= 2 && n <= 1000000)) checks.push(li('bad', 'Copies must be between 2 and 1,000,000'));
    }
    if (s.collection.trim() && !/^[0-9a-fA-F]{64}$/.test(s.collection.trim())) checks.push(li('bad', 'Collection ID must be 64 hexadecimal characters'));
    if (state.csv?.error) checks.push(li('bad', state.csv.error));
    if (!s.metadataBase) checks.push(li('warn', 'Add the metadata folder address so wallets can find each file'));
    const located = images.filter((x) => x.location).length;
    if (located) checks.push(li('warn', `${located} photo${located === 1 ? ' contains' : 's contain'} GPS location data. Remove it before uploading`));

    const seen = new Map();
    for (const x of images) {
      if (seen.has(x.art.sha256)) checks.push(li('bad', `${x.name} is the same image as ${seen.get(x.art.sha256)}`));
      else seen.set(x.art.sha256, x.name);
    }

    const rows = state.csv?.rows;
    if (rows) {
      const names = new Set(images.map((x) => x.name));
      const unmatched = [...rows.keys()].filter((k) => !names.has(k));
      const missing = images.filter((x) => !rows.has(x.name));
      if (unmatched.length) checks.push(li('warn', `${unmatched.length} CSV row${unmatched.length === 1 ? ' matches' : 's match'} no image (first: ${unmatched[0]})`));
      if (missing.length) checks.push(li('warn', `${missing.length} image${missing.length === 1 ? ' has' : 's have'} no CSV row (first: ${missing[0].name})`));
      if (!unmatched.length && !missing.length) checks.push(li('ok', `Traits matched for every image (${state.csv.columns.join(', ') || 'no trait columns'})`));
    }

    const items = [];
    let refProblem = '';
    for (let i = 0; i < images.length; i += 1) {
      const x = images[i];
      const number = String(s.start + i).padStart(s.digits, '0');
      const row = rows?.get(x.name);
      const name = (row?.name || s.pattern.replaceAll('{n}', number)).slice(0, 80);
      const image = s.imageBase ? s.imageBase + encodeURIComponent(x.name) : '';
      const metadataUri = s.metadataBase ? `${s.metadataBase}${number}.json` : '';
      refProblem ||= core.referenceProblem(image) && `Image address for ${x.name} ${core.referenceProblem(image)}`;
      refProblem ||= core.referenceProblem(metadataUri) && `Metadata address ${core.referenceProblem(metadataUri)}`;
      const input = {
        kind: s.kind,
        fields: { name, description: row?.description || s.description, creator: s.creator, license: s.license, external_url: s.external_url,
          image, collection: s.collection, edition_size: s.edition_size, metadata_uri: metadataUri },
        art: x.art,
        traits: row?.traits || [],
      };
      const meta = core.metadataFrom(input);
      const bytes = core.encoder.encode(core.canonical(meta));
      const hash = await core.sha256Hex(bytes);
      if (seq !== state.building) return;
      items.push({ number, source: x, meta, bytes, hash, input });
    }
    if (refProblem) checks.push(li('bad', refProblem));
    const names = new Set();
    const dupes = items.filter((it) => (names.has(it.meta.name) ? true : (names.add(it.meta.name), false))).length;
    if (dupes && s.pattern.includes('{n}')) checks.push(li('warn', `${dupes} piece${dupes === 1 ? ' shares' : 's share'} a name with another`));
    state.items = items;
    state.blocking = checks.some((c) => c.className === 'bad');
    render(checks);
  }

  // ---------------------------------------------------------------- output

  function render(checks = []) {
    $('#bulk-checks').replaceChildren(...checks);
    $('#bulk-zip').disabled = !state.items.length || state.blocking;
    renderRarity();
    const grid = $('#bulk-items');
    const shown = state.items.slice(0, SHOWN).map((it) => {
      const card = document.createElement('article');
      card.className = 'st-mini';
      const media = document.createElement('div');
      media.className = 'st-mini-media';
      if (it.source.thumb) { const img = document.createElement('img'); img.src = it.source.thumb; img.alt = ''; media.append(img); }
      const body = document.createElement('div');
      body.className = 'st-mini-body';
      const kind = document.createElement('small');
      kind.textContent = `${it.number}.json`;
      const title = document.createElement('strong');
      title.textContent = it.meta.name;
      const traits = document.createElement('span');
      traits.textContent = (it.meta.attributes || []).map((a) => `${a.trait_type}: ${a.value}`).join(' · ');
      const hash = document.createElement('code');
      hash.textContent = `${it.hash.slice(0, 10)}…`;
      hash.title = it.hash;
      body.append(kind, title, traits, hash);
      card.append(media, body);
      return card;
    });
    if (state.items.length > SHOWN) {
      const more = document.createElement('p');
      more.className = 'st-empty';
      more.textContent = `…and ${state.items.length - SHOWN} more in the download.`;
      shown.push(more);
    }
    grid.replaceChildren(...shown);
  }

  function renderRarity() {
    const box = $('#bulk-rarity');
    const counts = new Map();
    for (const it of state.items) {
      for (const a of it.meta.attributes || []) {
        if (!counts.has(a.trait_type)) counts.set(a.trait_type, new Map());
        const values = counts.get(a.trait_type);
        values.set(a.value, (values.get(a.value) || 0) + 1);
      }
    }
    if (!counts.size) { box.replaceChildren(); return; }
    const total = state.items.length;
    const head = document.createElement('h3');
    head.textContent = 'Trait rarity';
    const groups = [...counts].map(([type, values]) => {
      const group = document.createElement('div');
      group.className = 'st-rarity-group';
      const title = document.createElement('strong');
      title.textContent = type;
      group.append(title);
      [...values].sort((a, b) => a[1] - b[1]).forEach(([value, n]) => {
        const row = document.createElement('div');
        row.className = 'st-rarity-row';
        const label = document.createElement('span');
        label.textContent = value;
        const bar = document.createElement('i');
        bar.style.setProperty('--w', `${(n / total) * 100}%`);
        const pct = document.createElement('small');
        pct.textContent = `${n} · ${((n / total) * 100).toFixed(n / total < 0.1 ? 1 : 0)}%`;
        row.append(label, bar, pct);
        group.append(row);
      });
      return group;
    });
    box.replaceChildren(head, ...groups);
  }

  const csvCell = (v) => (/[",\n\r]/.test(String(v)) ? `"${String(v).replaceAll('"', '""')}"` : String(v));

  function downloadZip() {
    if (!state.items.length || state.blocking) return;
    const s = settings();
    const entries = [];
    const manifest = [['number', 'file', 'name', 'image_sha256', 'metadata_sha256', 'image', 'metadata_uri', 'traits']];
    for (const it of state.items) {
      entries.push({ name: `metadata/${it.number}.json`, data: it.bytes });
      manifest.push([it.number, it.source.name, it.meta.name, it.meta.image_sha256, it.hash, it.meta.image || '', it.input.fields.metadata_uri,
        (it.meta.attributes || []).map((a) => `${a.trait_type}: ${a.value}`).join('; ')]);
    }
    entries.push({ name: 'manifest.csv', data: manifest.map((r) => r.map(csvCell).join(',')).join('\r\n') + '\r\n' });
    entries.push({ name: 'README.txt', data: [
      'Made with Monzero Studio (https://monzero.org/app/).',
      '',
      `metadata/       ${state.items.length} metadata files in the monzero-metadata/1 standard. Upload this folder`,
      `                ${s.metadataBase ? `so each file is reachable at ${s.metadataBase}<number>.json.` : 'and note its address.'}`,
      '                Do not edit these files: any change, even whitespace, changes their hash.',
      'manifest.csv    Each piece: number, source image, name, image and metadata hashes, addresses and traits.',
      '',
      'These are design files, not issued assets. Issuing becomes possible only when assets',
      'activate (hard fork 17).',
      '',
      'Keep your original images: their SHA-256 must match image_sha256 in each metadata file.',
      'Anyone can check a piece at https://monzero.org/app/#verify',
      '',
    ].join('\n') });
    const label = core.slug(s.pattern.replaceAll('{n}', '').trim() || 'collection');
    core.download(`${label}-metadata.zip`, core.zip(entries));
  }

  // ---------------------------------------------------------------- events

  const drop = $('#bulk-drop');
  $('#bulk-images').addEventListener('change', (e) => loadImages(e.target.files));
  ['dragenter', 'dragover'].forEach((t) => drop.addEventListener(t, (e) => { e.preventDefault(); drop.classList.add('over'); }));
  ['dragleave', 'drop'].forEach((t) => drop.addEventListener(t, () => drop.classList.remove('over')));
  drop.addEventListener('drop', (e) => { e.preventDefault(); loadImages(e.dataTransfer.files); });
  $('#bulk-csv').addEventListener('change', (e) => loadCsv(e.target.files[0]));
  form.addEventListener('input', (e) => { if (e.target.type !== 'file') build(); });
  form.addEventListener('change', (e) => { if (e.target.type !== 'file') build(); });
  $('#bulk-zip').addEventListener('click', downloadZip);
  build();
})();
