(() => {
  'use strict';

  const STANDARD = 'monzero-metadata/1';
  const DRAFTS_KEY = 'monzero-studio-drafts-v2';
  const MAX_IMAGE_BYTES = 25 * 1024 * 1024;
  const MAX_REFERENCE_BYTES = 256;
  const U64_MAX = (1n << 64n) - 1n;
  const KIND_LABEL = { nft: 'NFT', edition: 'Edition', collection: 'Collection', fungible: 'Token' };
  const ART_LABEL = { nft: 'Artwork', edition: 'Artwork', collection: 'Cover image', fungible: 'Logo (optional)' };

  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const form = $('#creator');
  const field = (name) => form.elements[name];
  const encoder = new TextEncoder();

  const state = {
    kind: 'nft',
    art: null,        // { sha256, mime, bytes, width, height, url, thumb }
    metadataBytes: null,
    metadataHash: '',
    draftId: null,
    artLoading: false,
  };

  // ---------------------------------------------------------------- utilities

  async function sha256Hex(data) {
    const digest = await crypto.subtle.digest('SHA-256', data);
    return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
  }

  // Canonical JSON: object keys sorted, no insignificant whitespace.
  function canonical(value) {
    if (Array.isArray(value)) return `[${value.map(canonical).join(',')}]`;
    if (value && typeof value === 'object') {
      return `{${Object.keys(value).sort().map((k) => `${JSON.stringify(k)}:${canonical(value[k])}`).join(',')}}`;
    }
    return JSON.stringify(value);
  }

  function formatBytes(n) {
    if (n < 1024) return `${n} B`;
    if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
    return `${(n / 1024 / 1024).toFixed(1)} MB`;
  }

  function slug(text) {
    return (text || 'asset').toLowerCase().normalize('NFKD').replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 40) || 'asset';
  }

  let toastTimer;
  function toast(message) {
    const el = $('#toast');
    el.textContent = message;
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove('show'), 2600);
  }

  function download(name, data, type) {
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob([data], { type }));
    link.download = name;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  }

  async function copyText(text) {
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      const area = document.createElement('textarea');
      area.value = text;
      document.body.append(area);
      area.select();
      document.execCommand('copy');
      area.remove();
    }
    toast('Copied');
  }

  // Parse a decimal supply into atomic units without floating point.
  function atomicSupply(text, decimals) {
    const clean = String(text || '').trim().replace(/[,_\s]/g, '');
    if (!/^\d+(\.\d+)?$/.test(clean)) return null;
    const [whole, frac = ''] = clean.split('.');
    if (frac.length > decimals) return null;
    const atomic = BigInt(whole + frac.padEnd(decimals, '0'));
    return atomic > 0n && atomic <= U64_MAX ? atomic : null;
  }

  function referenceProblem(value) {
    if (!value) return '';
    if (/\s/.test(value)) return 'must not contain spaces';
    try { const url = new URL(value); if (!['https:', 'ipfs:', 'ar:'].includes(url.protocol) || !url.hostname || url.username || url.password) return 'must be an HTTPS, ipfs:// or ar:// address'; } catch { return 'must be an HTTPS, ipfs:// or ar:// address'; }
    if (value.includes('\0')) return 'contains an invalid character';
    if (encoder.encode(value).length > MAX_REFERENCE_BYTES) return `must be at most ${MAX_REFERENCE_BYTES} bytes`;
    return '';
  }

  // ---------------------------------------------------------------- kind

  function setKind(kind) {
    state.kind = kind;
    $$('[data-kind]').forEach((b) => b.setAttribute('aria-checked', String(b.dataset.kind === kind)));
    $$('[data-only]').forEach((el) => { el.hidden = !el.dataset.only.split(' ').includes(kind); });
    $$('[data-hide]').forEach((el) => { el.hidden = el.dataset.hide.split(' ').includes(kind); });
    $('[data-label-art]').textContent = ART_LABEL[kind];
    field('name').placeholder = { nft: 'Nightshade #001', edition: 'Nightshade print', collection: 'Nightshade Originals', fungible: 'Nightshade Credit' }[kind];
    refresh();
  }

  $$('[data-kind]').forEach((button, index, all) => {
    button.addEventListener('click', () => { setKind(button.dataset.kind); markDirty(); });
    button.addEventListener('keydown', (event) => {
      const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[event.key];
      if (!step) return;
      event.preventDefault();
      const next = all[(index + step + all.length) % all.length];
      next.focus();
      setKind(next.dataset.kind); markDirty();
    });
  });

  // ---------------------------------------------------------------- artwork

  let artworkSeq = 0;
  async function prepareArt(file) {
    if (!/^image\/(png|jpeg|webp|gif|avif)$/.test(file.type) || file.size > MAX_IMAGE_BYTES) throw new Error('Choose a PNG, JPEG, WebP, GIF or AVIF image up to 25 MB.');
    const buffer = await file.arrayBuffer();
    const sha256 = await sha256Hex(buffer);
    const location = file.type === 'image/jpeg' && Boolean(window.MonzeroStudioCore?.hasLocation(buffer));
    const url = URL.createObjectURL(file), img = new Image();
    try {
      await new Promise((resolve, reject) => { img.onload = resolve; img.onerror = () => reject(new Error('That file could not be decoded as an image.')); img.src = url; });
      if (img.naturalWidth * img.naturalHeight > 40000000) throw new Error('Use an image with no more than 40 million pixels.');
      return { sha256, mime: file.type, bytes: file.size, width: img.naturalWidth, height: img.naturalHeight, url, thumb: makeThumb(img), original: file, filename: file.name || 'artwork', location };
    } catch (error) { URL.revokeObjectURL(url); throw error; }
  }
  async function loadArt(file) {
    if (!file) return;
    const seq = ++artworkSeq, info = $('#art-info'); state.artLoading = true;
    info.hidden = false; info.textContent = 'Reading and fingerprinting artwork…';
    try {
      const art = await prepareArt(file);
      if (seq !== artworkSeq) { URL.revokeObjectURL(art.url); return; }
      if (state.art?.url) URL.revokeObjectURL(state.art.url);
      state.art = art;
      info.textContent = `${file.name} · ${art.width}×${art.height} · ${formatBytes(file.size)} · original included in backups`;
      markDirty();
    } catch (error) { if (seq === artworkSeq) info.textContent = error.message + ' Previous artwork, if any, is unchanged.'; }
    finally { if (seq === artworkSeq) { state.artLoading = false; refresh(); } }
  }

  function makeThumb(img) {
    const size = 360;
    const scale = Math.min(1, size / Math.max(img.naturalWidth, img.naturalHeight));
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.round(img.naturalWidth * scale));
    canvas.height = Math.max(1, Math.round(img.naturalHeight * scale));
    canvas.getContext('2d').drawImage(img, 0, 0, canvas.width, canvas.height);
    try { return canvas.toDataURL('image/webp', 0.82); } catch { return ''; }
  }

  const dropzone = $('#dropzone');
  $('#art-input').addEventListener('change', (e) => loadArt(e.target.files[0]));
  ['dragenter', 'dragover'].forEach((t) => dropzone.addEventListener(t, (e) => { e.preventDefault(); dropzone.classList.add('over'); }));
  ['dragleave', 'drop'].forEach((t) => dropzone.addEventListener(t, () => dropzone.classList.remove('over')));
  dropzone.addEventListener('drop', (e) => { e.preventDefault(); loadArt(e.dataTransfer.files[0]); });

  // ---------------------------------------------------------------- traits

  function addTrait(type = '', value = '') {
    const row = document.createElement('div');
    row.className = 'st-trait';
    row.innerHTML = '<input aria-label="Trait name" maxlength="40" placeholder="Background"><input aria-label="Trait value" maxlength="60" placeholder="Midnight"><button type="button" aria-label="Remove trait">×</button>';
    const [t, v] = row.querySelectorAll('input');
    t.value = type;
    v.value = value;
    row.querySelector('button').addEventListener('click', () => { row.remove(); refresh(); markDirty(); });
    $('#traits').append(row);
    return t;
  }
  $('#add-trait').addEventListener('click', () => { addTrait().focus(); markDirty(); });

  function traits() {
    return $$('.st-trait').map((row) => {
      const [t, v] = row.querySelectorAll('input');
      return { trait_type: t.value.trim(), value: v.value.trim() };
    }).filter((x) => x.trait_type && x.value);
  }

  // ---------------------------------------------------------------- metadata

  function buildMetadata() {
    const kind = state.kind;
    const text = (name) => field(name).value.trim();
    const meta = { standard: STANDARD, class: kind, name: text('name') };
    for (const name of ['description', 'creator', 'external_url', 'image']) if (text(name)) meta[name] = text(name);
    meta.license = field('license').value;
    if (state.art) {
      Object.assign(meta, { image_sha256: state.art.sha256, image_mime: state.art.mime, image_bytes: state.art.bytes,
                            image_width: state.art.width, image_height: state.art.height });
    }
    if (kind !== 'fungible') {
      const list = traits();
      if (list.length) meta.attributes = list;
    }
    if (kind === 'edition') meta.edition_size = Number(field('edition_size').value) || 0;
    if (kind === 'fungible') {
      if (text('symbol')) meta.symbol = text('symbol').toUpperCase();
      meta.decimals = Number(field('decimals').value);
    }
    if ((kind === 'nft' || kind === 'edition') && text('collection')) meta.collection = text('collection').toLowerCase();
    return meta;
  }

  function checks() {
    const kind = state.kind;
    const out = [];
    const add = (level, message) => out.push({ level, message });
    const name = field('name').value.trim();
    name ? add('ok', 'Name set') : add('bad', 'Add a name');
    if (kind === 'nft' || kind === 'edition') state.art ? add('ok', 'Artwork fingerprinted') : add('bad', 'Add the artwork');
    if (state.artLoading) add('bad', 'Wait for the artwork to finish loading');
    if (state.art?.location) add('warn', 'This photo contains GPS location data. Remove it before you upload the image anywhere public');
    if (state.art && !state.art.original) add('warn', 'Original artwork is missing; add it again to include it in backups');
    if (state.art && !field('image').value.trim()) add('warn', 'Add the image address once it is uploaded');
    const imageProblem = referenceProblem(field('image').value.trim());
    if (imageProblem) add('bad', `Image address ${imageProblem}`);
    const metaUri = field('metadata_uri').value.trim();
    const metaProblem = referenceProblem(metaUri);
    if (metaProblem) add('bad', `Metadata address ${metaProblem}`);
    else if (!metaUri) add('warn', 'Add the metadata address so wallets can find it');
    else add('ok', 'Metadata address set');
    const collection = field('collection').value.trim();
    if ((kind === 'nft' || kind === 'edition') && collection && !/^[0-9a-fA-F]{64}$/.test(collection)) add('bad', 'Collection ID must be 64 hexadecimal characters');
    if (kind === 'edition') {
      const n = Number(field('edition_size').value);
      Number.isInteger(n) && n >= 2 && n <= 1000000 ? add('ok', `${n.toLocaleString()} numbered copies`) : add('bad', 'Copies must be between 2 and 1,000,000');
    }
    if (kind === 'fungible') {
      const d = Number(field('decimals').value);
      if (!Number.isInteger(d) || d < 0 || d > 11) add('bad', 'Decimals must be 0 to 11');
      else if (atomicSupply(field('supply').value, d) === null) add('bad', 'Enter a total supply that fits the chosen decimals');
      else add('ok', 'Supply is fixed forever: nobody can mint more');
    }
    if (field('external_url').value.trim() && !/^https:\/\//i.test(field('external_url').value.trim())) add('bad', 'Website must use an HTTPS address');
    return out;
  }

  let refreshSeq = 0;
  async function refresh() {
    const seq = ++refreshSeq;
    const meta = buildMetadata();
    const bytes = encoder.encode(canonical(meta));
    const hash = await sha256Hex(bytes);
    if (seq !== refreshSeq) return;
    state.metadataBytes = bytes;
    state.metadataHash = hash;

    const list = checks();
    $('#checks').replaceChildren(...list.map(({ level, message }) => {
      const li = document.createElement('li');
      li.className = level;
      li.textContent = message;
      return li;
    }));
    const blocking = list.some((c) => c.level === 'bad');
    $('#metadata-hash').textContent = hash;
    $('#download-metadata').disabled = blocking;
    $('#hosting-download').disabled = blocking;
    const summary = { Type: KIND_LABEL[state.kind], Name: meta.name || 'Not named', Supply: state.kind === 'fungible' ? field('supply').value || 'Not set' : state.kind === 'edition' ? String(meta.edition_size) : '1', License: meta.license, Artwork: state.art?.original ? 'Original included' : state.art ? 'Thumbnail only — add original' : 'Not added', Status: 'Local draft · not issued' };
    $('#review-summary').replaceChildren(...Object.entries(summary).flatMap(([key, value]) => { const dt = document.createElement('dt'), dd = document.createElement('dd'); dt.textContent = key; dd.textContent = value; return [dt, dd]; }));
    renderCard(meta, hash);
  }

  function renderCard(meta, hash) {
    const media = $('#card-media');
    const src = state.art?.url || state.art?.thumb;
    if (src) {
      let img = media.querySelector('img');
      if (!img) { img = document.createElement('img'); img.alt = ''; media.replaceChildren(img); }
      if (img.src !== src) img.src = src;
    } else {
      const span = document.createElement('span');
      span.textContent = state.kind === 'fungible' ? 'Token logo' : 'Your artwork';
      media.replaceChildren(span);
    }
    media.classList.toggle('round', state.kind === 'fungible');
    $('#card-kind').textContent = KIND_LABEL[state.kind];
    $('#card-edition').textContent = state.kind === 'edition' && meta.edition_size ? `Edition of ${meta.edition_size.toLocaleString()}`
      : state.kind === 'fungible' && meta.symbol ? meta.symbol : '';
    $('#card-name').textContent = meta.name || 'Untitled';
    $('#card-creator').textContent = meta.creator ? `by ${meta.creator}` : '';
    $('#card-desc').textContent = meta.description || '';
    $('#card-traits').replaceChildren(...(meta.attributes || []).map((a) => {
      const li = document.createElement('li');
      const small = document.createElement('small');
      small.textContent = a.trait_type;
      li.append(small, document.createTextNode(a.value));
      return li;
    }));
    $('#card-print').textContent = hash ? `${hash.slice(0, 8)}…${hash.slice(-8)}` : '—';
  }

  form.addEventListener('submit', (event) => event.preventDefault());
  form.addEventListener('input', () => { refresh(); markDirty(); });
  form.addEventListener('change', () => { refresh(); markDirty(); });

  // Guided creation; navigation never signs or issues anything.
  let step = 0;
  function showStep(index) {
    step = Math.max(0, Math.min(3, index));
    $$('[data-panel]').forEach((panel, i) => { panel.hidden = i !== step; });
    $$('[data-step]').forEach((button, i) => { if (i === step) button.setAttribute('aria-current', 'step'); else button.removeAttribute('aria-current'); });
    $('#step-back').disabled = step === 0; $('#step-next').hidden = step === 3;
    $('#step-position').textContent = `Step ${step + 1} of 4`;
  }
  $$('[data-step]').forEach(button => button.addEventListener('click', () => showStep(Number(button.dataset.step))));
  $('#step-back').addEventListener('click', () => showStep(step - 1));
  $('#step-next').addEventListener('click', () => showStep(step + 1));
  function downloadMetadata() {
    if (checks().some(item => item.level === 'bad')) { toast('Complete the required details and artwork first.'); showStep(3); return; }
    const bytes = encoder.encode(canonical(buildMetadata()));
    download(`${slug(field('name').value)}.metadata.json`, bytes, 'application/json');
    toast('Metadata downloaded. Upload these exact bytes to your chosen host.');
  }
  $('#download-metadata').addEventListener('click', downloadMetadata);
  $('#hosting-download').addEventListener('click', downloadMetadata);

  // Atomic, non-evicting local project storage. No successful message until commit.
  const FIELDS = ['name','symbol','decimals','supply','edition_size','description','creator','license','external_url','collection','image','metadata_uri','local_collection'];
  let dbPromise, projects = [], dirty = false, editVersion = 0, saveTimer, saving = Promise.resolve(), opening = false;
  function database() {
    if (!dbPromise) dbPromise = new Promise((resolve, reject) => {
      const request = indexedDB.open('monzero-studio-projects', 1);
      request.onupgradeneeded = () => { request.result.createObjectStore('projects', { keyPath: 'id' }); request.result.createObjectStore('settings'); };
      request.onsuccess = () => { request.result.onversionchange = () => request.result.close(); resolve(request.result); };
      request.onerror = () => reject(request.error); request.onblocked = () => reject(new Error('Close other Studio tabs and try again.'));
    });
    return dbPromise;
  }
  async function store(mode, action, name = 'projects') {
    const db = await database();
    return new Promise((resolve, reject) => {
      const tx = db.transaction(name, mode); let result;
      tx.oncomplete = () => resolve(result); tx.onerror = tx.onabort = () => reject(tx.error || new Error('Storage could not complete the change.'));
      try { const req = action(tx.objectStore(name)); if (req) req.onsuccess = () => { result = req.result; }; }
      catch (error) { tx.abort(); reject(error); }
    });
  }
  function saveMessage(message, failure = false) { $('#save-status').textContent = message; $('#save-status').classList.toggle('error', failure); }
  function markDirty() {
    if (opening) return;
    dirty = true; editVersion++; saveMessage('Unsaved changes');
    clearTimeout(saveTimer); saveTimer = setTimeout(() => saveCurrent(false), 900);
    $('#hosting-result').textContent = ''; // Earlier host checks no longer describe the current draft.
  }
  function snapshot() {
    return { id: state.draftId || crypto.randomUUID(), kind: state.kind, saved: Date.now(),
      fields: Object.fromEntries(FIELDS.map(name => [name, field(name).value])), traits: traits(),
      art: state.art ? { ...state.art, url: '' } : null };
  }
  function saveCurrent(explicit = false) {
    clearTimeout(saveTimer);
    saving = saving.catch(() => {}).then(async () => {
      if (state.artLoading) { saveMessage('Artwork is still loading; save will follow.'); saveTimer = setTimeout(() => saveCurrent(false), 500); return false; }
      if (!dirty && state.draftId) { if (explicit) toast('All changes are already saved.'); return true; }
      if (!field('name').value.trim()) { saveMessage('Add a name to enable autosave'); if (explicit) toast('Give the project a name first.'); return false; }
      const record = snapshot(), version = editVersion;
      saveMessage('Saving…');
      try {
        await store('readwrite', records => records.put(record));
        state.draftId = record.id;
        if (version === editVersion) dirty = false;
        saveMessage(dirty ? 'New changes waiting to save…' : 'Saved on this browser');
        $('#project-label').textContent = record.fields.name;
        await refreshLibrary(); if (explicit) toast(dirty ? 'Earlier changes saved; newer edits are still waiting.' : 'Project saved, including available original artwork.');
        return !dirty;
      } catch (error) {
        saveMessage('Not saved — storage is unavailable or full. Download a project backup.', true);
        if (explicit) toast('Save failed. Your current work is still open; download a backup.');
        return false;
      }
    });
    return saving;
  }
  $('#save-draft').addEventListener('click', () => saveCurrent(true));
  window.addEventListener('beforeunload', event => { if (dirty || state.artLoading) { event.preventDefault(); event.returnValue = ''; } });
  async function beforeSwitch() {
    if (state.artLoading) { toast('Wait for the artwork to finish loading.'); return false; }
    if (!dirty) { await saving; return true; }
    if (await saveCurrent(false)) return true;
    return confirm('This project has unsaved changes. Discard them and continue? Download a backup first if you want to keep them.');
  }
  async function refreshLibrary() {
    projects = (await store('readonly', records => records.getAll())).sort((a,b) => b.saved - a.saved);
    for (const id of ['draft-filter','batch-collection','local-collection']) {
      const select = $(`#${id}`), previous = select.value;
      select.replaceChildren(new Option(id === 'draft-filter' ? 'All projects' : id === 'local-collection' ? 'No local collection' : 'Choose a collection', ''));
      for (const project of projects.filter(p => p.kind === 'collection')) select.add(new Option(project.fields.name, project.id));
      select.value = previous;
    }
    renderDrafts();
  }
  function actionButton(label, action, danger = false) {
    const button = document.createElement('button'); button.type = 'button'; button.className = 'st-link' + (danger ? ' danger' : ''); button.textContent = label;
    button.addEventListener('click', () => Promise.resolve().then(action).catch(error => toast(error.message || 'The operation could not be completed.')));
    return button;
  }
  function renderDrafts() {
    const search = $('#draft-search').value.trim().toLowerCase(), collection = $('#draft-filter').value;
    const list = projects.filter(p => (!search || p.fields.name.toLowerCase().includes(search)) && (!collection || p.id === collection || p.fields.local_collection === collection));
    $('#drafts').replaceChildren(...list.map(project => {
      const card = document.createElement('article'); card.className = 'st-mini';
      const media = document.createElement('div'); media.className = 'st-mini-media';
      if (/^data:image\/(webp|png|jpeg);base64,/.test(project.art?.thumb || '')) { const img = new Image(); img.src = project.art.thumb; img.alt = ''; media.append(img); }
      const body = document.createElement('div'); body.className = 'st-mini-body';
      const kind = document.createElement('small'); kind.textContent = `${KIND_LABEL[project.kind]} · ${new Date(project.saved).toLocaleDateString()}${project.art && !project.art.original ? ' · original missing' : ''}`;
      const title = document.createElement('strong'); title.textContent = project.fields.name;
      const actions = document.createElement('div'); actions.className = 'st-mini-actions';
      actions.append(actionButton('Open', () => openDraft(project)), actionButton('Duplicate', async () => {
        const copy = { ...project, id: crypto.randomUUID(), saved: Date.now(), fields: { ...project.fields, name: `${project.fields.name.slice(0,72)} copy` } };
        await store('readwrite', records => records.put(copy)); await refreshLibrary(); toast('Independent copy saved.');
      }), actionButton('Rename', async () => {
        if (project.id === state.draftId && !await beforeSwitch()) return;
        const name = prompt('Project name (up to 80 characters)', project.fields.name)?.trim(); if (!name) return;
        if (name.length > 80) throw new Error('Use at most 80 characters.');
        const latest = projects.find(p => p.id === project.id) || project;
        const renamed = { ...latest, saved: Date.now(), fields: { ...latest.fields, name } };
        await store('readwrite', records => records.put(renamed));
        if (project.id === state.draftId) { field('name').value = name; $('#project-label').textContent = name; refresh(); }
        await refreshLibrary();
      }), actionButton('Backup', () => exportRecords([project], project.fields.name)), actionButton('Delete', async () => {
        if (project.id === state.draftId && !await beforeSwitch()) return;
        if (projects.some(p => p.fields.local_collection === project.id)) throw new Error('Move or delete the collection’s member drafts before deleting the collection.');
        if (!confirm(`Delete “${project.fields.name}” from this browser? Download a backup first to keep it.`)) return;
        await store('readwrite', records => records.delete(project.id));
        if (state.draftId === project.id) resetProject();
        await refreshLibrary();
      }, true));
      body.append(kind,title,actions);card.append(media,body);return card;
    }));
    if (!list.length) { const p = document.createElement('p'); p.className='st-empty';p.textContent = projects.length ? 'No projects match your filters.' : 'Your saved projects will appear here.'; $('#drafts').append(p); }
  }
  $('#draft-search').addEventListener('input', renderDrafts); $('#draft-filter').addEventListener('change', renderDrafts);
  async function openDraft(project) {
    if (!await beforeSwitch()) return;
    project = projects.find(p => p.id === project.id) || project;
    opening = true; clearTimeout(saveTimer); artworkSeq++;
    try {
      if (state.art?.url) URL.revokeObjectURL(state.art.url);
      form.reset(); for (const name of FIELDS) field(name).value = project.fields[name] ?? field(name).value;
      $('#traits').replaceChildren(); (project.traits || []).forEach(t => addTrait(t.trait_type,t.value));
      state.art = project.art ? { ...project.art, url: project.art.original ? URL.createObjectURL(project.art.original) : '' } : null;
      state.draftId = project.id; state.artLoading = false; setKind(project.kind); dirty = false;
      $('#project-label').textContent = project.fields.name; saveMessage('Saved on this browser');
      $('#art-info').hidden = !state.art; $('#art-info').textContent = state.art ? `${state.art.width}×${state.art.height} · ${state.art.original ? 'Original artwork restored' : 'Thumbnail only. Add the original artwork again.'}` : '';
      $('#hosting-result').textContent = ''; showStep(0); $('#create').scrollIntoView({behavior:'smooth'});
    } finally { opening = false; }
  }
  function resetProject() {
    opening = true;clearTimeout(saveTimer);artworkSeq++;
    if (state.art?.url) URL.revokeObjectURL(state.art.url);
    form.reset();state.art=null;state.draftId=null;state.artLoading=false;dirty=false;
    $('#traits').replaceChildren();addTrait('Background','');$('#art-info').hidden=true;$('#hosting-result').textContent='';
    $('#project-label').textContent='New project';saveMessage('Not saved yet');setKind('nft');showStep(0);opening=false;
  }
  $('#new-project').addEventListener('click', async () => { if (await beforeSwitch()) resetProject(); });

  // Portable backups include original bytes; importing always creates fresh IDs.
  function blobBase64(blob) { return new Promise((resolve,reject) => { const reader=new FileReader();reader.onload=()=>resolve(reader.result.split(',')[1]);reader.onerror=()=>reject(new Error('Could not read artwork for backup.'));reader.readAsDataURL(blob); }); }
  async function exportRecords(records, name) {
    if (records.reduce((sum,p)=>sum+(p.art?.original?.size || 0),0) > 50*1024*1024) throw new Error('This backup exceeds 50 MB of artwork. Back up individual projects instead.');
    const data = [];
    for (const project of records) {
      const art = project.art ? { ...project.art, url: undefined, original: undefined } : null;
      if (art && project.art.original) art.original_base64 = await blobBase64(project.art.original);
      data.push({...project, art});
    }
    download(`${slug(name)}.studio.json`, JSON.stringify({standard:'monzero-studio-project/1', projects:data}), 'application/json');
    toast('Project backup downloaded. It can be imported into Studio.');
  }
  $('#export-project').addEventListener('click', () => {
    if (state.artLoading) return toast('Wait for the artwork to finish loading.');
    exportRecords([snapshot()], field('name').value).catch(error=>toast(error.message));
  });
  async function validatedProject(raw, legacy = false) {
    if (!raw || typeof raw !== 'object' || !Object.hasOwn(KIND_LABEL,raw.kind) || !raw.fields || typeof raw.fields !== 'object') throw new Error('Invalid project format.');
    const fields = {};
    for (const name of FIELDS) { const value = raw.fields[name] ?? ''; if (typeof value !== 'string' || value.length > (name === 'description' ? 2000 : 256)) throw new Error('Invalid or oversized project field.'); fields[name] = value; }
    if (!fields.name.trim() || fields.name.length > 80) throw new Error('A project needs a name of up to 80 characters.');
    if (!Array.isArray(raw.traits) || raw.traits.length > 100 || raw.traits.some(t=>!t || typeof t.trait_type!=='string' || typeof t.value!=='string' || t.trait_type.length>40 || t.value.length>60)) throw new Error('Invalid traits.');
    let art = null;
    if (raw.art) {
      const a = raw.art;
      if (typeof a.original_base64 === 'string') {
        if (a.original_base64.length > Math.ceil(MAX_IMAGE_BYTES/3)*4 || !/^[A-Za-z0-9+/]*={0,2}$/.test(a.original_base64)) throw new Error('Invalid artwork data.');
        let bytes;try { bytes=Uint8Array.from(atob(a.original_base64),c=>c.charCodeAt(0)); } catch { throw new Error('Invalid artwork encoding.'); }
        art = await prepareArt(new File([bytes], typeof a.filename==='string' ? a.filename.slice(0,120) : 'artwork', {type:a.mime}));
        URL.revokeObjectURL(art.url);art.url='';
        if (art.sha256 !== a.sha256) throw new Error('Artwork fingerprint does not match this backup.');
      } else {
        if (!/^[a-f0-9]{64}$/.test(a.sha256) || !/^image\/(png|jpeg|webp|gif|avif)$/.test(a.mime) || !Number.isSafeInteger(a.bytes) || a.bytes<1 || a.bytes>MAX_IMAGE_BYTES || !Number.isSafeInteger(a.width) || !Number.isSafeInteger(a.height) || a.width<1 || a.height<1 || a.width*a.height>40000000) throw new Error('Invalid saved artwork details.');
        art = {sha256:a.sha256,mime:a.mime,bytes:a.bytes,width:a.width,height:a.height,thumb:typeof a.thumb==='string' && a.thumb.length<1000000 && /^data:image\/(webp|png|jpeg);base64,/.test(a.thumb) ? a.thumb : '',original:null,url:''};
      }
    }
    return {id:legacy && typeof raw.id==='string' && raw.id.length<100 ? raw.id : crypto.randomUUID(),kind:raw.kind,saved:Date.now(),fields,traits:raw.traits.map(t=>({trait_type:t.trait_type,value:t.value})),art};
  }
  $('#import-project').addEventListener('change', async event => {
    const input=event.target,file=input.files[0];if(!file)return;
    try {
      if (file.size>80*1024*1024) throw new Error('Backup files must be smaller than 80 MB.');
      const data=JSON.parse(await file.text());
      if(data.standard!=='monzero-studio-project/1' || !Array.isArray(data.projects) || !data.projects.length || data.projects.length>100) throw new Error('Choose a Monzero Studio project backup with 1–100 projects.');
      const ids=new Map(),records=[];let total=0;
      for(const raw of data.projects){if(typeof raw.id!=='string' || ids.has(raw.id))throw new Error('Invalid or duplicate project identifiers.');const project=await validatedProject(raw);ids.set(raw.id,project.id);records.push(project);total+=project.art?.original?.size || 0;if(total>50*1024*1024)throw new Error('Backup contains more than 50 MB of artwork.');}
      let unlinked=0;for(const project of records){const old=project.fields.local_collection;if(old && !ids.has(old))unlinked++;project.fields.local_collection=ids.get(old)||'';if(project.fields.local_collection && !records.some(p=>p.id===project.fields.local_collection && p.kind==='collection'))throw new Error('Invalid collection link.');}
      await store('readwrite',store=>{records.forEach(record=>store.add(record));});await refreshLibrary();toast(`${records.length} projects imported as new copies.${unlinked?' Links to collections outside this backup were removed.':''}`);
    } catch(error){toast(`Import failed: ${error.message}`);} finally{input.value='';}
  });
  async function migrateLegacy() {
    if (await store('readonly',store=>store.get('legacy-v2'),'settings')) return;
    let raw; try { raw=JSON.parse(localStorage.getItem(DRAFTS_KEY)||'[]'); } catch { throw new Error('Older drafts could not be read. They have not been changed.'); }
    if(!Array.isArray(raw))throw new Error('Older draft data is malformed. It has not been changed.');
    const records=[];for(const item of raw)records.push(await validatedProject(item,true));
    const existing=await store('readonly',store=>store.getAllKeys());
    await store('readwrite',store=>{records.filter(r=>!existing.includes(r.id)).forEach(r=>store.add(r));});
    await store('readwrite',store=>store.put(true,'legacy-v2'),'settings');
    if(records.length)$('#library-status').textContent='Older drafts imported. Their thumbnails are preserved; add original artwork to include it in backups. The older storage copy has been retained.';
  }

  // Collection batches are all-or-nothing, sharing details without inventing chain IDs.
  $('#batch-create').addEventListener('click', async () => {
    const button=$('#batch-create'),status=$('#batch-status');button.disabled=true;
    try {
      if(dirty && !await saveCurrent(false))throw new Error('Save or back up your open project before creating a batch.');
      const parent=projects.find(p=>p.id===$('#batch-collection').value && p.kind==='collection');
      const files=[...$('#batch-art').files];
      if(!parent)throw new Error('Choose a saved collection first.');
      if(!files.length || files.length>20 || files.reduce((n,f)=>n+f.size,0)>50*1024*1024)throw new Error('Choose 1–20 images totalling no more than 50 MB.');
      const records=[];
      for(const [i,file] of files.entries()){
        status.textContent=`Preparing artwork ${i+1} of ${files.length}…`;
        const art=await prepareArt(file);URL.revokeObjectURL(art.url);art.url='';
        records.push({id:crypto.randomUUID(),kind:'nft',saved:Date.now()+i,fields:{...parent.fields,name:file.name.replace(/\.[^.]+$/,'').slice(0,80)||'Untitled artwork',image:'',metadata_uri:'',collection:'',local_collection:parent.id},traits:parent.traits.map(t=>({...t})),art});
      }
      await store('readwrite',store=>{records.forEach(record=>store.add(record));});await refreshLibrary();$('#draft-filter').value=parent.id;renderDrafts();$('#batch-art').value='';status.textContent=`Added ${records.length} NFT drafts. Open each to edit its artwork details and traits.`;
    }catch(error){status.textContent=`Nothing added: ${error.message}`;}finally{button.disabled=false;}
  });
  $('#export-collection').addEventListener('click',async()=>{
    try{if(dirty && !await saveCurrent(false))throw new Error('Save the open project first, or use its individual backup button.');const parent=projects.find(p=>p.id===$('#batch-collection').value && p.kind==='collection');if(!parent)throw new Error('Choose a collection first.');await exportRecords([parent,...projects.filter(p=>p.fields.local_collection===parent.id)],parent.fields.name);}catch(error){toast(error.message);}
  });

  // Bounded reads and explicit user-triggered requests; no server-side URL proxy.
  async function fetchBytes(url, maxBytes, options = {}) {
    const controller = new AbortController(), timeout = setTimeout(() => controller.abort(), 15000);
    try {
      const response = await fetch(url, {...options, signal:controller.signal, credentials:'omit', referrerPolicy:'no-referrer'});
      if (!response.ok) throw new Error(`Request failed (${response.status}).`);
      if (Number(response.headers.get('content-length')) > maxBytes) throw new Error('The response is too large.');
      const reader = response.body?.getReader();
      if (!reader) { const bytes = new Uint8Array(await response.arrayBuffer()); if(bytes.length>maxBytes)throw new Error('The response is too large.');return bytes; }
      let size = 0;const chunks=[];
      while (true) {
        const {done,value}=await reader.read();if(done)break;size+=value.length;
        if(size>maxBytes){await reader.cancel();throw new Error('The response is too large.');}chunks.push(value);
      }
      const result=new Uint8Array(size);let offset=0;for(const chunk of chunks){result.set(chunk,offset);offset+=chunk.length;}return result;
    } finally { clearTimeout(timeout); }
  }
  async function post(url, body) {
    const bytes=await fetchBytes(url,2*1024*1024,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const data=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
    if(!data || typeof data!=='object' || data.error)throw new Error('The node could not return a usable response.');return data;
  }
  function hostURL(reference) {
    let value=reference.trim();
    if(value.startsWith('ipfs://'))value='https://ipfs.io/ipfs/'+value.slice(7).replace(/^ipfs\//,'');
    else if(value.startsWith('ar://'))value='https://arweave.net/'+value.slice(5);
    const url=new URL(value);
    if(url.protocol!=='https:' || url.username || url.password)throw new Error('Use an HTTPS, ipfs:// or ar:// address without embedded credentials.');
    // Do not turn an imported reference into a browser request to a local service.
    const hostname=url.hostname.toLowerCase();
    if(hostname==='localhost' || hostname.endsWith('.localhost') || hostname.endsWith('.local') || hostname.includes(':') || /^\d+\.\d+\.\d+\.\d+$/.test(hostname))throw new Error('Use a public host name for address checks.');
    return url.href;
  }
  let hostCheckSeq=0;
  async function checkHost(kind) {
    const seq=++hostCheckSeq,version=editVersion,result=$('#hosting-result'),button=$(`#check-${kind}-host`);
    button.disabled=true;result.textContent='Checking the hosted file…';
    try {
      const ref=field(kind==='image'?'image':'metadata_uri').value;
      if(!ref.trim())throw new Error('Add the file’s address first.');
      if(kind==='image' && !state.art)throw new Error('Add local artwork to compare against.');
      if(state.artLoading)throw new Error('Wait for the artwork to finish loading.');
      const expected=kind==='image'?state.art.sha256:await sha256Hex(encoder.encode(canonical(buildMetadata())));
      const actual=await sha256Hex(await fetchBytes(hostURL(ref),kind==='image'?MAX_IMAGE_BYTES:1024*1024));
      if(seq!==hostCheckSeq || version!==editVersion)return;
      result.textContent=actual===expected?`Hosted ${kind} matches this draft’s exact bytes.`:`Hosted ${kind} does not match this draft. Upload the correct file and check again.`;
    } catch(error) {
      if(seq===hostCheckSeq && version===editVersion)result.textContent=`Not verified: ${error.name==='TypeError' || error.name==='AbortError' ? 'the host is unreachable, timed out, or does not allow browser checks. You can download its file and use the verifier below.' : error.message}`;
    } finally {button.disabled=false;}
  }
  $('#check-image-host').addEventListener('click',()=>checkHost('image'));
  $('#check-metadata-host').addEventListener('click',()=>checkHost('metadata'));

  // A pasted hash is not an on-chain observation. Bind verified provenance to the lookup.
  let verifiedAsset=null,verifySeq=0,lookupSeq=0;
  function resultsList(items) {
    $('#verify-results').replaceChildren(...items.map(({level,message})=>{const li=document.createElement('li');li.className=level;li.textContent=message;return li;}));
  }
  async function verify() {
    const seq=++verifySeq,items=[];
    const add=(level,message)=>items.push({level,message});
    const metaFile=$('#verify-meta').files[0],imageFile=$('#verify-image').files[0];
    const expected=$('#verify-expected').value.trim().toLowerCase(),chain=verifiedAsset;
    if(!metaFile){resultsList([]);return;}
    try {
      if(metaFile.size>1024*1024)throw new Error('Metadata must be no larger than 1 MB.');
      const bytes=new Uint8Array(await metaFile.arrayBuffer()),hash=await sha256Hex(bytes);
      add('info',`Metadata fingerprint: ${hash}`);
      if(!expected)add('warn','No expected hash provided. Metadata has not been matched against an independent reference.');
      else if(!/^[a-f0-9]{64}$/.test(expected))add('bad','The expected hash must contain 64 hexadecimal characters.');
      else if(hash!==expected)add('bad','Metadata does NOT match the expected hash.');
      else if(chain && chain.hash===expected)add('ok',`Metadata matches the hash returned by the public node for asset ${chain.id.slice(0,12)}…${chain.active?'':' (asset protocol inactive)'}.`);
      else add('ok','Metadata matches the supplied hash. No on-chain lookup has verified that hash.');
      const meta=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
      if(!meta || Array.isArray(meta) || typeof meta!=='object')throw new Error('Metadata must be a JSON object.');
      if(meta.standard==='monzero-metadata/1' && Object.hasOwn(KIND_LABEL,meta.class) && typeof meta.name==='string' && meta.name.trim())add('ok','Metadata declares the supported standard, asset class and name.');
      else add('warn','Metadata is missing a supported standard, class or name.');
      if(imageFile){
        if(imageFile.size>MAX_IMAGE_BYTES)throw new Error('Artwork verification is limited to 25 MB.');
        if(typeof meta.image_sha256!=='string' || !/^[a-f0-9]{64}$/i.test(meta.image_sha256))add('warn','Metadata has no valid artwork fingerprint.');
        else {const imageHash=await sha256Hex(await imageFile.arrayBuffer());add(imageHash===meta.image_sha256.toLowerCase()?'ok':'bad',imageHash===meta.image_sha256.toLowerCase()?'Artwork matches the fingerprint in the metadata.':'Artwork does NOT match the fingerprint in the metadata.');}
      }else add('warn','No artwork selected. Its fingerprint has not been checked.');
      add('info','Matching bytes does not prove authorship, copyright ownership or an asset’s value.');
    }catch(error){add('bad',error instanceof SyntaxError?'The metadata is not valid JSON.':error.message);}
    if(seq===verifySeq)resultsList(items);
  }
  function clearLookup(message='No on-chain lookup performed.') {lookupSeq++;verifiedAsset=null;$('#lookup-status').textContent=message;}
  $('#verify-expected').addEventListener('input',()=>{clearLookup('Manual hash supplied. No on-chain confirmation.');verify();});
  $('#verify-asset-id').addEventListener('input',()=>{clearLookup();verify();});
  for(const selector of ['#verify-meta','#verify-image'])$(selector).addEventListener('change',event=>{
    const file=event.target.files[0];event.target.closest('label').querySelector('small').textContent=file?file.name:(selector==='#verify-meta'?'metadata.json':'optional');verify();
  });
  $('#lookup-asset').addEventListener('click',async()=>{
    clearLookup();const seq=lookupSeq,id=$('#verify-asset-id').value.trim().toLowerCase(),button=$('#lookup-asset');
    if(!/^[a-f0-9]{64}$/.test(id)){$('#lookup-status').textContent='Enter a valid 64-character asset ID.';verify();return;}
    button.disabled=true;$('#lookup-status').textContent='Looking up the public node’s record…';
    try {
      const data=await post('../explorer/api.php',{action:'asset',asset_id:id});if(seq!==lookupSeq)return;
      const hash=data.asset?.metadata_content_hash;
      if(data.supported!==true || typeof data.active!=='boolean' || typeof data.asset?.asset_id!=='string' || data.asset.asset_id.toLowerCase()!==id || typeof hash!=='string' || !/^[a-f0-9]{64}$/i.test(hash) || /^0+$/.test(hash))throw new Error('No usable committed metadata hash was returned.');
      verifiedAsset={id,hash:hash.toLowerCase(),active:data.active};$('#verify-expected').value=verifiedAsset.hash;
      $('#lookup-status').textContent=`Hash retrieved from the public node at ${new Date().toLocaleTimeString()}.${data.active?'':' The node reports the asset protocol is inactive.'} Select metadata to compare its bytes.`;
    }catch(error){if(seq===lookupSeq)$('#lookup-status').textContent='On-chain lookup unavailable or no usable record returned. This does not establish whether the asset exists. Manual file verification remains available.';}
    finally{button.disabled=false;if(seq===lookupSeq)verify();}
  });

  async function refreshNetwork() {
    const button=$('#retry-network');button.disabled=true;
    await Promise.allSettled([
      post('/api/node-info/',{}).then(info=>{
        $('#node-state').textContent=info.network_ready===true?'Network online':info.synchronized===true?'Node synchronized':'Node not ready';
        $('#node-dot').classList.toggle('ready',info.network_ready===true);
        $('#node-dot').classList.remove('offline');
        $('#height').textContent=Number.isSafeInteger(info.height)?`#${Math.max(0,info.height-1).toLocaleString()}`:'—';
      }).catch(()=>{$('#node-state').textContent='Network status unavailable';$('#node-dot').classList.remove('ready');$('#node-dot').classList.add('offline');$('#height').textContent='—';}),
      post('../explorer/api.php',{action:'assets',offset:0,count:24}).then(data=>{
        if(data.supported!==true || typeof data.active!=='boolean' || !Array.isArray(data.assets) || !Number.isSafeInteger(data.total) || data.total<0)throw new Error('Unusable asset status');
        $('#asset-count').textContent=data.total.toLocaleString();$('#asset-state').textContent=data.active?'Active':'Not active';$('#asset-state').classList.toggle('amber',!data.active);
        $('#network-note').textContent=`Asset status checked at ${new Date().toLocaleTimeString()}. ${data.active?'Studio still prepares local files only.':'The node reports asset issuance is inactive.'}`;
        $('#onchain').replaceChildren(...data.assets.map(asset=>{
          const card=document.createElement('a');card.className='st-mini chain';
          card.href=`../explorer/#/asset/${encodeURIComponent(String(asset.asset_id||''))}`;
          const body=document.createElement('div');body.className='st-mini-body';const title=document.createElement('strong');title.textContent=`Asset ${String(asset.asset_id||'').slice(0,12)}…`;
          const hint=document.createElement('small');hint.textContent=asset.metadata_reference?'External metadata available · open explorer':'No external metadata address';body.append(title,hint);card.append(body);return card;
        }));
        if(!data.assets.length){const empty=document.createElement('p');empty.className='st-empty';empty.textContent=data.active?'No issued assets were returned by the node.':'The node reports no assets and an inactive asset protocol. You can continue designing local drafts.';$('#onchain').append(empty);}
      }).catch(()=>{
        $('#asset-count').textContent='—';$('#asset-state').textContent='Unconfirmed';$('#asset-state').classList.add('amber');
        $('#network-note').textContent=`Asset service unavailable at ${new Date().toLocaleTimeString()}. Activation and asset counts could not be confirmed. Your local projects are still usable.`;
        const p=document.createElement('p');p.className='st-empty';p.textContent='Could not retrieve the asset registry. This is not evidence that no assets exist. Retry the status check or visit the explorer.';$('#onchain').replaceChildren(p);
      })
    ]);button.disabled=false;
  }
  $('#retry-network').addEventListener('click',refreshNetwork);

  // Opening Studio does not create an empty draft or alter old local storage.
  const requestedKind = new URLSearchParams(location.search).get('kind');
  addTrait('Background','');setKind(KIND_LABEL[requestedKind] ? requestedKind : 'nft');showStep(0);refreshNetwork();

  // Used by the guided tour, sample artwork and card image (studio-extras.js, tour.js).
  window.MonzeroStudio = {
    state, toast, download, loadArt, addTrait, traits, buildMetadata, showStep, KIND_LABEL,
    setKind(kind) { setKind(kind); markDirty(); },
    clearTraits() { $('#traits').replaceChildren(); refresh(); markDirty(); },
    setField(name, value) { field(name).value = value; refresh(); markDirty(); },
    isEmpty: () => !field('name').value.trim() && !state.art,
    update: () => { refresh(); markDirty(); },
  };
  document.dispatchEvent(new CustomEvent('monzero-studio-ready'));
  (async()=>{
    try{await database();try{await migrateLegacy();}catch(error){$('#library-status').textContent=error.message;}await refreshLibrary();}
    catch{$('#library-status').textContent='Browser storage is unavailable. You can still create and download project backups; autosave will not be available.';saveMessage('Browser storage unavailable',true);}
  })();

})();
