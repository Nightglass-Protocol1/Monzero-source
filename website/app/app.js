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

  function storageGet() {
    try { return JSON.parse(localStorage.getItem(DRAFTS_KEY) || '[]'); } catch { return []; }
  }
  function storageSet(drafts) {
    try { localStorage.setItem(DRAFTS_KEY, JSON.stringify(drafts)); return true; } catch { return false; }
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
    button.addEventListener('click', () => setKind(button.dataset.kind));
    button.addEventListener('keydown', (event) => {
      const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[event.key];
      if (!step) return;
      event.preventDefault();
      const next = all[(index + step + all.length) % all.length];
      next.focus();
      setKind(next.dataset.kind);
    });
  });

  // ---------------------------------------------------------------- artwork

  async function loadArt(file) {
    const info = $('#art-info');
    if (!file) return;
    if (!/^image\/(png|jpeg|webp|gif|avif)$/.test(file.type)) {
      info.hidden = false;
      info.textContent = 'Please choose a PNG, JPEG, WebP, GIF or AVIF image.';
      return;
    }
    if (file.size > MAX_IMAGE_BYTES) {
      info.hidden = false;
      info.textContent = `That image is ${formatBytes(file.size)}; the limit is 25 MB.`;
      return;
    }
    info.hidden = false;
    info.textContent = 'Fingerprinting…';
    const buffer = await file.arrayBuffer();
    const sha256 = await sha256Hex(buffer);
    const url = URL.createObjectURL(file);
    const img = new Image();
    try {
      await new Promise((resolve, reject) => { img.onload = resolve; img.onerror = reject; img.src = url; });
    } catch {
      URL.revokeObjectURL(url);
      info.textContent = 'That file could not be read as an image.';
      return;
    }
    if (state.art?.url) URL.revokeObjectURL(state.art.url);
    state.art = { sha256, mime: file.type, bytes: file.size, width: img.naturalWidth, height: img.naturalHeight, url, thumb: makeThumb(img) };
    info.textContent = `${file.name} · ${img.naturalWidth}×${img.naturalHeight} · ${formatBytes(file.size)}`;
    refresh();
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
    row.querySelector('button').addEventListener('click', () => { row.remove(); refresh(); });
    $('#traits').append(row);
    return t;
  }
  $('#add-trait').addEventListener('click', () => addTrait().focus());

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
    return out;
  }

  function descriptorArgs(problems) {
    const kind = state.kind;
    if (problems) return null;
    const hash = state.metadataHash;
    const ref = field('metadata_uri').value.trim() || 'none';
    const collection = (kind === 'nft' || kind === 'edition') && field('collection').value.trim() ? field('collection').value.trim().toLowerCase() : 'none';
    let supply = '1';
    let decimals = '0';
    if (kind === 'edition') supply = String(Number(field('edition_size').value));
    if (kind === 'fungible') {
      decimals = String(Number(field('decimals').value));
      supply = atomicSupply(field('supply').value, Number(decimals)).toString();
    }
    return [kind, supply, decimals, hash, ref, collection];
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
    const args = descriptorArgs(blocking);
    $('#cmd-create').textContent = args ? `asset_create ${args.join(' ')} ${slug(meta.name)}.mzi` : 'Fix the items above to get the command.';
    $('#cmd-issue').textContent = args ? `asset_issue YOUR_ADDRESS ${args.join(' ')}` : '—';
    $('#download-metadata').disabled = !meta.name;
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

  form.addEventListener('input', refresh);
  form.addEventListener('change', refresh);

  $('#download-metadata').addEventListener('click', () => {
    if (!state.metadataBytes) return;
    download(`${slug(field('name').value)}.metadata.json`, state.metadataBytes, 'application/json');
    toast('metadata.json downloaded. Upload it to your metadata address.');
  });
  $$('[data-copy]').forEach((b) => b.addEventListener('click', () => {
    const text = $(`#${b.dataset.copy}`).textContent;
    if (text && text !== '—' && !text.startsWith('Fix')) copyText(text);
  }));

  // ---------------------------------------------------------------- drafts

  function snapshot() {
    return {
      id: state.draftId || (crypto.randomUUID ? crypto.randomUUID() : String(Date.now())),
      kind: state.kind,
      saved: Date.now(),
      fields: Object.fromEntries(['name', 'symbol', 'decimals', 'supply', 'edition_size', 'description', 'creator', 'license',
        'external_url', 'collection', 'image', 'metadata_uri'].map((n) => [n, field(n).value])),
      traits: traits(),
      art: state.art ? { sha256: state.art.sha256, mime: state.art.mime, bytes: state.art.bytes, width: state.art.width, height: state.art.height, thumb: state.art.thumb } : null,
      hash: state.metadataHash,
    };
  }

  $('#save-draft').addEventListener('click', () => {
    if (!field('name').value.trim()) { toast('Give it a name first'); field('name').focus(); return; }
    const draft = snapshot();
    state.draftId = draft.id;
    const drafts = storageGet().filter((d) => d.id !== draft.id);
    drafts.unshift(draft);
    let kept = drafts.slice(0, 30);
    while (!storageSet(kept) && kept.length > 1) kept = kept.slice(0, -1);
    renderDrafts();
    toast('Saved to my drafts');
  });

  function openDraft(draft) {
    setKind(draft.kind);
    for (const [name, value] of Object.entries(draft.fields)) if (field(name)) field(name).value = value;
    $('#traits').replaceChildren();
    (draft.traits || []).forEach((t) => addTrait(t.trait_type, t.value));
    if (state.art?.url) URL.revokeObjectURL(state.art.url);
    state.art = draft.art ? { ...draft.art, url: '' } : null;
    const info = $('#art-info');
    info.hidden = !draft.art;
    if (draft.art) info.textContent = `Saved artwork ${draft.art.width}×${draft.art.height} · ${formatBytes(draft.art.bytes)} · drop the original again to replace it`;
    state.draftId = draft.id;
    refresh();
    $('#create').scrollIntoView({ behavior: 'smooth' });
  }

  function renderDrafts() {
    const drafts = storageGet();
    const grid = $('#drafts');
    if (!drafts.length) {
      const p = document.createElement('p');
      p.className = 'st-empty';
      p.textContent = 'Nothing saved yet. Drafts you save appear here.';
      grid.replaceChildren(p);
      return;
    }
    grid.replaceChildren(...drafts.map((d) => {
      const card = document.createElement('article');
      card.className = 'st-mini';
      const media = document.createElement('div');
      media.className = 'st-mini-media';
      if (d.art?.thumb) { const img = document.createElement('img'); img.src = d.art.thumb; img.alt = ''; media.append(img); }
      const body = document.createElement('div');
      body.className = 'st-mini-body';
      const kind = document.createElement('small');
      kind.textContent = `${KIND_LABEL[d.kind] || d.kind} · ${new Date(d.saved).toLocaleDateString()}`;
      const title = document.createElement('strong');
      title.textContent = d.fields.name || 'Untitled';
      const actions = document.createElement('div');
      actions.className = 'st-mini-actions';
      const open = document.createElement('button');
      open.type = 'button';
      open.className = 'st-link';
      open.textContent = 'Open';
      open.addEventListener('click', () => openDraft(d));
      const remove = document.createElement('button');
      remove.type = 'button';
      remove.className = 'st-link danger';
      remove.textContent = 'Delete';
      remove.addEventListener('click', () => {
        if (!confirm(`Delete the draft "${d.fields.name || 'Untitled'}"?`)) return;
        storageSet(storageGet().filter((x) => x.id !== d.id));
        if (state.draftId === d.id) state.draftId = null;
        renderDrafts();
      });
      actions.append(open, remove);
      body.append(kind, title, actions);
      card.append(media, body);
      return card;
    }));
  }

  // ---------------------------------------------------------------- verify

  async function verify() {
    const results = $('#verify-results');
    const metaFile = $('#verify-meta').files[0];
    const imageFile = $('#verify-image').files[0];
    const expected = $('#verify-expected').value.trim().toLowerCase();
    const items = [];
    const add = (level, message) => items.push({ level, message });
    if (!metaFile) {
      results.replaceChildren();
      return;
    }
    const bytes = new Uint8Array(await metaFile.arrayBuffer());
    const hash = await sha256Hex(bytes);
    add('info', `Metadata hash: ${hash}`);
    if (expected) {
      if (!/^[0-9a-f]{64}$/.test(expected)) add('warn', 'The expected hash should be 64 hexadecimal characters');
      else if (expected === hash) add('ok', 'Metadata matches the on-chain hash');
      else add('bad', 'Metadata does NOT match the expected hash: it has been changed or is the wrong file');
    } else {
      add('warn', 'Paste the asset\'s metadata hash to check this file against the chain');
    }
    let meta = null;
    try { meta = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)); } catch { add('bad', 'The file is not valid UTF-8 JSON'); }
    if (meta) {
      meta.standard === STANDARD ? add('ok', `Follows ${STANDARD}`) : add('warn', 'Does not declare monzero-metadata/1; fields may differ');
      if (typeof meta.name === 'string') add('info', `Name: ${meta.name.slice(0, 120)}`);
      if (imageFile) {
        const imageHash = await sha256Hex(await imageFile.arrayBuffer());
        if (!meta.image_sha256) add('warn', 'The metadata has no image fingerprint to check against');
        else if (String(meta.image_sha256).toLowerCase() === imageHash) add('ok', 'Image matches the fingerprint in the metadata');
        else add('bad', 'Image does NOT match the fingerprint in the metadata');
      }
    }
    results.replaceChildren(...items.map(({ level, message }) => {
      const li = document.createElement('li');
      li.className = level;
      li.textContent = message;
      return li;
    }));
  }
  ['#verify-meta', '#verify-image', '#verify-expected'].forEach((s) => $(s).addEventListener(s === '#verify-expected' ? 'input' : 'change', verify));
  $$('.st-verify .st-drop').forEach((zone) => {
    const input = zone.querySelector('input');
    input.addEventListener('change', () => {
      const f = input.files[0];
      if (f) zone.querySelector('small').textContent = f.name;
    });
  });

  // ---------------------------------------------------------------- network + on-chain assets

  function post(url, body) {
    return fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then((r) => { if (!r.ok) throw new Error(r.status); return r.json(); });
  }

  post('/api/node-info/', {}).then((info) => {
    const ready = Boolean(info.network_ready);
    $('#node-state').textContent = ready ? 'Network online' : (info.synchronized ? 'Node synchronized' : 'Node unavailable');
    $('#node-dot').classList.toggle('ready', ready);
    $('#height').textContent = `#${Number(Math.max(0, (info.height ?? 1) - 1)).toLocaleString()}`;
  }).catch(() => {
    $('#node-state').textContent = 'Network status unavailable';
    $('#node-dot').classList.add('offline');
  });

  post('../explorer/api.php', { action: 'assets', offset: 0, count: 24 }).then((data) => {
    const grid = $('#onchain');
    const assets = Array.isArray(data.assets) ? data.assets : [];
    const active = Boolean(data.supported && data.active);
    $('#asset-count').textContent = Number(data.total || assets.length || 0).toLocaleString();
    $('#asset-state').textContent = active ? 'Active' : 'Not yet active';
    $('#asset-state').classList.toggle('amber', !active);
    if (!assets.length) {
      const p = document.createElement('p');
      p.className = 'st-empty';
      p.textContent = active ? 'No assets have been issued yet. Be the first.'
        : 'No assets exist yet. Issuance opens when the asset upgrade activates; drafts you make now will be ready.';
      grid.replaceChildren(p);
      return;
    }
    grid.replaceChildren(...assets.map((a) => {
      const card = document.createElement('a');
      card.className = 'st-mini chain';
      card.href = `../explorer/#/asset/${encodeURIComponent(String(a.asset_id || ''))}`;
      const body = document.createElement('div');
      body.className = 'st-mini-body';
      const kind = document.createElement('small');
      kind.textContent = String(a.asset_type || a.class || 'asset');
      const id = document.createElement('code');
      id.textContent = `${String(a.asset_id || '').slice(0, 12)}…`;
      const hint = document.createElement('span');
      hint.textContent = a.metadata_reference ? `Metadata: ${String(a.metadata_reference).slice(0, 60)}` : 'No metadata address';
      body.append(kind, id, hint);
      card.append(body);
      return card;
    }));
  }).catch(() => {
    $('#asset-count').textContent = '—';
    $('#asset-state').textContent = 'Unknown';
    const p = document.createElement('p');
    p.className = 'st-empty';
    p.textContent = 'The asset list is unavailable right now.';
    $('#onchain').replaceChildren(p);
  });

  // ---------------------------------------------------------------- start

  if (!window.crypto?.subtle) {
    toast('This browser cannot compute hashes here. Open the Studio over https.');
  }
  addTrait('Background', '');
  setKind('nft');
  renderDrafts();
})();
