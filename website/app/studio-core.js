// Monzero Studio core: metadata, hashing and file helpers shared by the Studio,
// its batch builder and the guided tour. No DOM access except canvas drawing.
// The metadata produced here must stay byte-identical to the wallet's Assets page.
(() => {
  'use strict';

  const STANDARD = 'monzero-metadata/1';
  const MAX_IMAGE_BYTES = 25 * 1024 * 1024;
  const MAX_REFERENCE_BYTES = 256;
  const U64_MAX = (1n << 64n) - 1n;
  const IMAGE_TYPES = /^image\/(png|jpeg|webp|gif|avif)$/;
  const encoder = new TextEncoder();

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

  // fields: raw form strings; art: fingerprint or null; traits: [{trait_type, value}] already trimmed.
  function metadataFrom({ kind, fields, art, traits }) {
    const text = (name) => String(fields[name] ?? '').trim();
    const meta = { standard: STANDARD, class: kind, name: text('name') };
    for (const name of ['description', 'creator', 'external_url', 'image']) if (text(name)) meta[name] = text(name);
    meta.license = fields.license;
    if (art) {
      Object.assign(meta, { image_sha256: art.sha256, image_mime: art.mime, image_bytes: art.bytes,
                            image_width: art.width, image_height: art.height });
    }
    if (kind !== 'fungible' && traits.length) meta.attributes = traits;
    if (kind === 'edition') meta.edition_size = Number(fields.edition_size) || 0;
    if (kind === 'fungible') {
      if (text('symbol')) meta.symbol = text('symbol').toUpperCase();
      meta.decimals = Number(fields.decimals);
    }
    if ((kind === 'nft' || kind === 'edition') && text('collection')) meta.collection = text('collection').toLowerCase();
    return meta;
  }

  // The asset descriptor arguments shared by asset_create and asset_issue.
  function descriptorArgs({ kind, fields }, hash) {
    const ref = String(fields.metadata_uri || '').trim() || 'none';
    const collection = (kind === 'nft' || kind === 'edition') && String(fields.collection || '').trim()
      ? String(fields.collection).trim().toLowerCase() : 'none';
    let supply = '1';
    let decimals = '0';
    if (kind === 'edition') supply = String(Number(fields.edition_size));
    if (kind === 'fungible') {
      decimals = String(Number(fields.decimals));
      supply = atomicSupply(fields.supply, Number(decimals)).toString();
    }
    return [kind, supply, decimals, hash, ref, collection];
  }

  function loadImage(url) {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = reject;
      img.src = url;
    });
  }

  function thumbnail(img, size = 360) {
    const scale = Math.min(1, size / Math.max(img.naturalWidth, img.naturalHeight));
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.round(img.naturalWidth * scale));
    canvas.height = Math.max(1, Math.round(img.naturalHeight * scale));
    canvas.getContext('2d').drawImage(img, 0, 0, canvas.width, canvas.height);
    try { return canvas.toDataURL('image/webp', 0.82); } catch { return ''; }
  }

  // Does a JPEG carry GPS coordinates? Looks for the GPS IFD pointer (tag 0x8825) in its Exif block.
  function hasLocation(buffer) {
    const view = new DataView(buffer);
    if (view.byteLength < 4 || view.getUint16(0) !== 0xffd8) return false;
    let offset = 2;
    while (offset + 4 <= view.byteLength) {
      const marker = view.getUint16(offset);
      if ((marker & 0xff00) !== 0xff00 || marker === 0xffda) return false;
      const length = view.getUint16(offset + 2);
      if (marker === 0xffe1 && offset + 10 <= view.byteLength && view.getUint32(offset + 4) === 0x45786966) {
        const tiff = offset + 10;
        if (tiff + 8 > view.byteLength) return false;
        const little = view.getUint16(tiff) === 0x4949;
        const ifd = tiff + view.getUint32(tiff + 4, little);
        if (ifd + 2 > view.byteLength) return false;
        const count = view.getUint16(ifd, little);
        for (let i = 0; i < count; i += 1) {
          const entry = ifd + 2 + i * 12;
          if (entry + 2 > view.byteLength) return false;
          if (view.getUint16(entry, little) === 0x8825) return true;
        }
        return false;
      }
      offset += 2 + length;
    }
    return false;
  }

  // Fingerprint an image file. Returns { art, img, url, location } or { error }.
  async function fingerprint(file) {
    if (!IMAGE_TYPES.test(file.type)) return { error: 'Please choose a PNG, JPEG, WebP, GIF or AVIF image.' };
    if (file.size > MAX_IMAGE_BYTES) return { error: `That image is ${formatBytes(file.size)}; the limit is 25 MB.` };
    const buffer = await file.arrayBuffer();
    const sha256 = await sha256Hex(buffer);
    const url = URL.createObjectURL(file);
    let img;
    try { img = await loadImage(url); } catch {
      URL.revokeObjectURL(url);
      return { error: 'That file could not be read as an image.' };
    }
    const art = { sha256, mime: file.type, bytes: file.size, width: img.naturalWidth, height: img.naturalHeight };
    return { art, img, url, location: file.type === 'image/jpeg' && hasLocation(buffer) };
  }

  // ---------------------------------------------------------------- sample artwork

  function rng(seed) {
    let s = seed >>> 0 || 1;
    return () => { s ^= s << 13; s ^= s >>> 17; s ^= s << 5; return (s >>> 0) / 4294967296; };
  }

  const PALETTES = [
    ['#0b1720', '#69d5c2', '#9ae4d7', '#edc48d', '#ff8a9f'],
    ['#1b1030', '#ff7ab6', '#ffd36e', '#6ee7ff', '#f3f0ff'],
    ['#0f1a12', '#b8f28a', '#4fd1a5', '#f6f1c1', '#2f6f4f'],
    ['#1a1410', '#ff9f5a', '#ffd9a8', '#6a8dff', '#fff4e6'],
    ['#101426', '#8ea2ff', '#c3b6ff', '#69d5c2', '#ffe08a'],
  ];
  const PALETTE_NAMES = ['Tide', 'Bloom', 'Moss', 'Ember', 'Dusk'];
  const SHAPE_NAMES = ['Orbits', 'Ridges', 'Shards'];

  // Draws a generative piece and returns { canvas, traits }.
  function sampleArtwork(seed, size = 1024) {
    const random = rng(seed);
    const p = Math.floor(random() * PALETTES.length);
    const shape = Math.floor(random() * SHAPE_NAMES.length);
    const [bg, ...colors] = PALETTES[p];
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = size;
    const g = canvas.getContext('2d');
    const glow = g.createRadialGradient(size * random(), size * random(), 0, size / 2, size / 2, size * 0.8);
    glow.addColorStop(0, `${colors[0]}55`);
    glow.addColorStop(1, bg);
    g.fillStyle = bg;
    g.fillRect(0, 0, size, size);
    g.fillStyle = glow;
    g.fillRect(0, 0, size, size);
    g.globalCompositeOperation = 'lighter';
    for (let i = 0; i < 28; i += 1) {
      const color = colors[Math.floor(random() * colors.length)];
      g.strokeStyle = `${color}${['66', '99', 'cc'][i % 3]}`;
      g.fillStyle = `${color}22`;
      g.lineWidth = 2 + random() * 10;
      const x = size * random();
      const y = size * random();
      const r = size * (0.04 + random() * 0.28);
      g.beginPath();
      if (shape === 0) g.arc(size / 2, size / 2, r * 1.6, random() * 6.3, random() * 6.3);
      else if (shape === 1) {
        g.moveTo(0, y);
        for (let k = 1; k <= 8; k += 1) g.lineTo((size / 8) * k, y + (random() - 0.5) * r);
      } else {
        g.moveTo(x, y);
        g.lineTo(x + r, y + r * (random() - 0.5));
        g.lineTo(x + r * (random() - 0.5), y + r);
        g.closePath();
        g.fill();
      }
      g.stroke();
    }
    g.globalCompositeOperation = 'source-over';
    return { canvas, traits: [{ trait_type: 'Palette', value: PALETTE_NAMES[p] }, { trait_type: 'Shape', value: SHAPE_NAMES[shape] }] };
  }

  function canvasFile(canvas, name) {
    return new Promise((resolve) => canvas.toBlob((blob) => resolve(new File([blob], name, { type: 'image/png' })), 'image/png'));
  }

  // ---------------------------------------------------------------- zip (stored, no compression)

  const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n += 1) {
      let c = n;
      for (let k = 0; k < 8; k += 1) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      table[n] = c >>> 0;
    }
    return table;
  })();

  function crc32(bytes) {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i += 1) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  // entries: [{ name, data: Uint8Array | string }] → Blob
  function zip(entries) {
    const parts = [];
    const central = [];
    let offset = 0;
    for (const entry of entries) {
      const name = encoder.encode(entry.name);
      const data = typeof entry.data === 'string' ? encoder.encode(entry.data) : entry.data;
      const crc = crc32(data);
      const local = new DataView(new ArrayBuffer(30));
      local.setUint32(0, 0x04034b50, true);
      local.setUint16(4, 20, true);
      local.setUint16(6, 0x0800, true);   // UTF-8 names
      local.setUint32(14, crc, true);
      local.setUint32(18, data.length, true);
      local.setUint32(22, data.length, true);
      local.setUint16(26, name.length, true);
      parts.push(local, name, data);
      const record = new DataView(new ArrayBuffer(46));
      record.setUint32(0, 0x02014b50, true);
      record.setUint16(4, 20, true);
      record.setUint16(6, 20, true);
      record.setUint16(8, 0x0800, true);
      record.setUint32(16, crc, true);
      record.setUint32(20, data.length, true);
      record.setUint32(24, data.length, true);
      record.setUint16(28, name.length, true);
      record.setUint32(42, offset, true);
      central.push(record, name);
      offset += 30 + name.length + data.length;
    }
    const size = central.reduce((n, part) => n + part.byteLength, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, entries.length, true);
    end.setUint16(10, entries.length, true);
    end.setUint32(12, size, true);
    end.setUint32(16, offset, true);
    return new Blob([...parts, ...central, end], { type: 'application/zip' });
  }

  function download(name, data, type) {
    const link = document.createElement('a');
    link.href = URL.createObjectURL(data instanceof Blob ? data : new Blob([data], { type }));
    link.download = name;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  }

  window.MonzeroStudioCore = {
    STANDARD, MAX_IMAGE_BYTES, U64_MAX, encoder,
    sha256Hex, canonical, formatBytes, slug, atomicSupply, referenceProblem,
    metadataFrom, descriptorArgs, loadImage, thumbnail, hasLocation, fingerprint,
    sampleArtwork, canvasFile, crc32, zip, download,
  };
})();
