// Founder disclosures (founder/disclosures.json, edited in /admin): view key and reserve proof.
// Without the file the page keeps its "will be published" text.
const text = (v, max) => typeof v === 'string' && v.trim() === v && v.length > 0 && v.length <= max && !/[\p{Cc}\p{Cf}\p{Cs}\u2028\u2029]/u.test(v);
const prose = (v, max) => typeof v === 'string' && text(v.replace(/\n/g, ' '), max);
const date = (v) => typeof v === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(v) && new Date(`${v}T00:00:00Z`).toISOString().slice(0, 10) === v;
const keys = (o, names) => o && typeof o === 'object' && !Array.isArray(o) && Object.keys(o).length === names.length && names.every((k) => Object.hasOwn(o, k));
const FIELDS = ['id', 'date', 'height', 'address', 'view_key', 'reserve_proof_url', 'reserve_proof_sha256', 'proof_message', 'proven_xmz', 'note'];
export function validDisclosures(d) {
  if (!keys(d, ['schema', 'updated_at', 'records']) || d.schema !== 'monzero-founder-disclosures-v1' || !date(d.updated_at) || !Array.isArray(d.records) || d.records.length > 120) return false;
  const ids = new Set();
  return d.records.every((r) => {
    if (!keys(r, FIELDS) || !text(r.id, 64) || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(r.id) || ids.has(r.id)) return false;
    ids.add(r.id);
    if (!date(r.date) || r.date > d.updated_at || !Number.isInteger(r.height) || r.height < 1 || r.height > 1e9) return false;
    if (typeof r.address !== 'string' || !/^F[1-9A-HJ-NP-Za-km-z]{94}$/.test(r.address)) return false;
    if (r.view_key !== null && (typeof r.view_key !== 'string' || !/^[0-9a-f]{64}$/.test(r.view_key))) return false;
    const proof = r.reserve_proof_url !== null;
    if (proof !== (r.reserve_proof_sha256 !== null) || proof !== (r.proof_message !== null) || proof !== (r.proven_xmz !== null)) return false;
    if (proof && (typeof r.reserve_proof_url !== 'string' || !/^\/founder\/proofs\/[a-z0-9-]{1,64}\.txt$/.test(r.reserve_proof_url) || typeof r.reserve_proof_sha256 !== 'string' || !/^[0-9a-f]{64}$/.test(r.reserve_proof_sha256)
      || !text(r.proof_message, 200) || typeof r.proven_xmz !== 'string' || !/^(0|[1-9]\d{0,11})(\.\d{1,11})?$/.test(r.proven_xmz))) return false;
    return r.note === null || prose(r.note, 1500);
  });
}
const $ = (id) => document.getElementById(id);
function el(tag, content, cls) { const e = document.createElement(tag); if (content !== undefined) e.textContent = content; if (cls) e.className = cls; return e; }
function copyable(value) {
  const box = el('p', value, 'address');
  const button = el('button', 'Copy', 'copy');
  button.type = 'button';
  button.addEventListener('click', () => navigator.clipboard?.writeText(value).then(() => { button.textContent = 'Copied'; setTimeout(() => { button.textContent = 'Copy'; }, 2000); }));
  const wrap = el('div'); wrap.append(box, button); return wrap;
}
function render(d) {
  const latest = [...d.records].sort((a, b) => (a.date < b.date ? 1 : -1))[0];
  if (!latest || (!latest.view_key && !latest.reserve_proof_url)) return;
  const box = $('disclosure');
  const parts = [el('p', `Published ${latest.date} at block height ${latest.height.toLocaleString('en')} for ${latest.address.slice(0, 8)}…${latest.address.slice(-6)}.`)];
  if (latest.view_key) {
    parts.push(el('h3', 'Secret view key'), copyable(latest.view_key));
    const how = el('p'); how.append('Load a view-only wallet: ', el('code', 'monzero-wallet-cli --generate-from-view-key founder-view-only'), ', then enter the address above and this view key. It shows every coin received; it cannot spend.');
    parts.push(how);
  }
  if (latest.reserve_proof_url) {
    parts.push(el('h3', `Reserve proof: ${latest.proven_xmz} XMZ`));
    const file = el('p'); const a = el('a', 'Download the proof file'); a.href = latest.reserve_proof_url; a.download = '';
    file.append(a, ` (SHA-256 `, el('code', latest.reserve_proof_sha256, 'hash'), ')');
    parts.push(file, el('p', `Signed message: "${latest.proof_message}"`));
    const how = el('p'); how.append('Check it in any wallet: ', el('code', `check_reserve_proof ${latest.address} ${JSON.stringify(latest.proof_message)} <proof file>`), '. It reports the amount proven and whether it is still unspent.');
    parts.push(how);
  }
  if (latest.note) parts.push(...latest.note.split(/\n+/).map((p) => el('p', p)));
  box.replaceChildren(...parts);
  const history = [...d.records].sort((a, b) => (a.date < b.date ? 1 : -1));
  if (history.length > 1) {
    $('disclosure-history').replaceChildren(...history.map((r) => el('li', `${r.date} (height ${r.height.toLocaleString('en')}): ${r.reserve_proof_url ? `reserve proof of ${r.proven_xmz} XMZ` : 'no reserve proof'}${r.view_key ? ', view key' : ''}`)));
    $('disclosure-history-wrap').hidden = false;
  }
}
if (typeof document !== 'undefined' && $('disclosure')) {
  fetch('/founder/disclosures.json', { cache: 'no-cache' }).then((r) => (r.ok ? r.json() : null)).then((d) => { if (d && validDisclosures(d)) render(d); }).catch(() => {});
}
