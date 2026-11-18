const $ = (selector) => document.querySelector(selector);
const view = $('#view');
const content = $('#content');
const number = new Intl.NumberFormat('en-GB');
const atomic = 100_000_000_000;
let chainInfo = null;

const escapeHtml = (value) => String(value ?? '').replace(/[&<>'"]/g, (char) => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[char]));
const shortHash = (hash, size = 12) => hash ? `${hash.slice(0, size)}…${hash.slice(-8)}` : '—';
const xmz = (value) => `${(Number(value || 0) / atomic).toLocaleString('en-GB', {maximumFractionDigits: 11})} XMZ`;
const date = (timestamp) => timestamp ? new Date(timestamp * 1000).toLocaleString() : '—';
const age = (timestamp) => {
  const seconds = Math.max(0, Math.floor(Date.now() / 1000 - timestamp));
  if (seconds < 60) return `${seconds}s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`;
  return `${Math.floor(seconds / 86400)}d ago`;
};

async function api(action, data = {}) {
  const response = await fetch('api.php', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({action, ...data})});
  const result = await response.json().catch(() => ({}));
  if (!response.ok || result.error) throw new Error(result.error || `HTTP ${response.status}`);
  return result;
}

function loading(label = 'Loading chain data…') { view.innerHTML = `<div class="loading"><span></span>${escapeHtml(label)}</div>`; }
function showError(message = '') {
  const fragment = $('#error-template').content.cloneNode(true);
  if (message) fragment.querySelector('p').textContent = message;
  view.replaceChildren(fragment);
}

function updateStats(info) {
  chainInfo = info;
  $('#stat-height').textContent = number.format(info.height || 0);
  $('#stat-difficulty').textContent = number.format(info.difficulty || 0);
  $('#stat-hashrate').textContent = `${number.format(Math.round((info.difficulty || 0) / (info.target || 120)))} H/s estimated`;
  $('#stat-transactions').textContent = number.format(info.tx_count || 0);
  $('#stat-connections').textContent = number.format((info.incoming_connections_count || 0) + (info.outgoing_connections_count || 0));
  $('#node-label').textContent = info.status === 'OK' ? 'Node online' : info.status;
  $('#node-dot').className = info.status === 'OK' ? 'online' : 'offline';
}

function headerTitle(kicker, title) {
  $('.section-title').innerHTML = `<div><p class="eyebrow">${escapeHtml(kicker)}</p><h2>${title}</h2></div><a class="back" href="#/">← Recent blocks</a>`;
}

async function showBlocks() {
  loading();
  $('.section-title').innerHTML = `<div><p class="eyebrow">Live ledger</p><h2>Recent blocks</h2></div><button id="refresh" class="ghost" type="button">Refresh</button>`;
  try {
    const result = await api('blocks', {limit: 15});
    updateStats(result.info);
    view.innerHTML = `<div class="table-wrap"><table><thead><tr><th>Height</th><th>Age</th><th>Block hash</th><th>Transactions</th><th>Difficulty</th><th>Reward</th><th>Size</th></tr></thead><tbody>${result.headers.map((block) => `
      <tr><td><a class="link" href="#/block/${block.height}">${number.format(block.height)}</a></td><td class="muted">${age(block.timestamp)}</td><td><a class="link hash truncate" title="${block.hash}" href="#/block/${block.hash}">${shortHash(block.hash)}</a></td><td>${number.format(block.num_txes || 0)}</td><td class="muted">${number.format(block.difficulty || 0)}</td><td class="reward">${xmz(block.reward)}</td><td class="muted">${number.format(block.block_size || block.block_weight || 0)} B</td></tr>`).join('')}</tbody></table></div>`;
    $('#refresh').addEventListener('click', showBlocks);
  } catch (error) { showError(error.message); setOffline(); }
}

async function showBlock(value) {
  loading('Loading block…');
  try {
    const input = /^\d+$/.test(value) ? {height:Number(value)} : {hash:value};
    const block = await api('block', input);
    const h = block.block_header || {};
    headerTitle('Block', `#${number.format(h.height ?? input.height)}`);
    const txs = block.tx_hashes || [];
    view.innerHTML = `<div class="detail-grid">
      ${datum('Block hash', `<code>${escapeHtml(h.hash)}</code>`)}
      ${datum('Timestamp', `<strong>${escapeHtml(date(h.timestamp))}</strong>`)}
      ${datum('Age', `<strong>${escapeHtml(age(h.timestamp))}</strong>`)}
      ${datum('Reward', `<strong class="big">${xmz(h.reward)}</strong>`)}
      ${datum('Difficulty', `<strong>${number.format(h.difficulty || 0)}</strong>`)}
      ${datum('Transactions', `<strong>${number.format(h.num_txes || txs.length)}</strong>`)}
      ${datum('Block size', `<strong>${number.format(h.block_size || h.block_weight || 0)} bytes</strong>`)}
      ${datum('Nonce', `<strong>${number.format(h.nonce || 0)}</strong>`)}
      ${datum('Version', `<strong>v${h.major_version ?? '—'}.${h.minor_version ?? '—'}</strong>`)}
      ${datum('Previous block', h.prev_hash ? `<a class="link hash truncate" href="#/block/${h.prev_hash}">${shortHash(h.prev_hash)}</a>` : '<strong>Genesis</strong>')}
      ${datum('Proof-of-work hash', `<code>${escapeHtml(h.pow_hash || 'Not requested')}</code>`)}
      ${datum('Confirmations', `<strong>${chainInfo ? number.format(Math.max(0, chainInfo.height - h.height)) : '—'}</strong>`)}
    </div>
    <h3 class="subheading">Transactions in this block</h3>
    ${txs.length ? `<div class="table-wrap"><table><thead><tr><th>#</th><th>Transaction hash</th></tr></thead><tbody>${txs.map((hash, i) => `<tr><td class="muted">${i + 1}</td><td><a class="link hash" href="#/tx/${hash}">${hash}</a></td></tr>`).join('')}</tbody></table></div>` : '<div class="empty"><strong>Coinbase only</strong><p>This block contains no regular transactions.</p></div>'}
    <details class="raw"><summary>Raw block JSON</summary><pre>${escapeHtml(JSON.stringify(block, null, 2))}</pre></details>`;
  } catch (error) { await tryTransaction(value, error); }
}

async function tryTransaction(value, originalError) {
  if (!/^[0-9a-f]{64}$/i.test(value)) return showError(originalError.message);
  try { await showTransaction(value); }
  catch {
    try { await showAsset(value); }
    catch { showError('No block, transaction, or asset matched this hash.'); }
  }
}

async function showTransaction(hash) {
  loading('Loading transaction…');
  const result = await api('transaction', {hash});
  const tx = result.txs?.[0] || {};
  let decoded = {};
  try { decoded = JSON.parse(tx.as_json || result.txs_as_json?.[0] || '{}'); } catch {}
  headerTitle('Transaction', `<span class="truncate" title="${escapeHtml(hash)}">${escapeHtml(shortHash(hash, 16))}</span>`);
  const vin = decoded.vin || [];
  const vout = decoded.vout || [];
  view.innerHTML = `<div class="detail-grid">
    ${datum('Transaction hash', `<code>${escapeHtml(hash)}</code>`)}
    ${datum('Block height', tx.block_height != null ? `<a class="link" href="#/block/${tx.block_height}">${number.format(tx.block_height)}</a>` : '<strong>Unconfirmed</strong>')}
    ${datum('Confirmations', `<strong>${number.format(tx.confirmations || 0)}</strong>`)}
    ${datum('Fee', `<strong class="orange">${xmz(tx.fee || decoded.rct_signatures?.txnFee || 0)}</strong>`)}
    ${datum('Size', `<strong>${number.format(tx.size || tx.weight || 0)} bytes</strong>`)}
    ${datum('Version', `<strong>${decoded.version ?? '—'}</strong>`)}
    ${datum('Inputs', `<strong>${number.format(vin.length)}</strong>`)}
    ${datum('Outputs', `<strong>${number.format(vout.length)}</strong>`)}
    ${datum('In pool', `<strong>${tx.in_pool ? 'Yes' : 'No'}</strong>`)}
  </div><details class="raw" open><summary>Decoded transaction JSON</summary><pre>${escapeHtml(JSON.stringify(decoded, null, 2))}</pre></details>`;
}

const assetClasses = {1:'Fungible token', 2:'NFT', 3:'Collection', 4:'Edition'};
const zeroHash = /^0{64}$/;
function assetAmount(value, decimals = 0) {
  try {
    const places = Math.max(0, Math.min(255, Number(decimals) || 0));
    const raw = BigInt(value || 0);
    if (!places) return raw.toString();
    const scale = 10n ** BigInt(places);
    const fraction = (raw % scale).toString().padStart(places, '0').replace(/0+$/, '');
    return `${raw / scale}${fraction ? `.${fraction}` : ''}`;
  } catch { return String(value ?? '—'); }
}

async function showAssets() {
  loading('Loading asset registry…');
  try {
    const result = await api('assets', {offset:0, count:100});
    headerTitle('Authenticated registry', 'Assets & NFTs');
    const assets = result.assets || [];
    view.innerHTML = `${result.supported === false ? '<div class="notice"><strong>Asset RPC upgrade pending</strong><p>The public explorer node does not expose the authenticated asset registry yet.</p></div>' : result.active ? '' : '<div class="notice"><strong>Assets are inactive</strong><p>The connected network has not activated the asset hard fork. Entries shown here are authenticated development-state records, not transferable production assets.</p></div>'}
      ${assets.length ? `<div class="table-wrap"><table><thead><tr><th>Asset</th><th>Class</th><th>Supply</th><th>Issued at</th><th>Metadata trust</th></tr></thead><tbody>${assets.map((asset) => `
        <tr><td><a class="link hash" href="#/asset/${escapeHtml(asset.asset_id)}">${shortHash(asset.asset_id, 16)}</a></td><td>${escapeHtml(assetClasses[asset.asset_class] || `Unknown (${asset.asset_class})`)}</td><td>${escapeHtml(assetAmount(asset.atomic_supply, asset.display_decimals))}</td><td><a class="link" href="#/block/${asset.height}">#${number.format(asset.height || 0)}</a></td><td>${asset.collection_id ? '<span class="trust verified">Controller-authorized member</span>' : '<span class="trust">Issuer-signed descriptor</span>'}</td></tr>`).join('')}</tbody></table></div>` : '<div class="empty"><strong>No registered assets</strong><p>No authenticated issuance records are present on this network.</p></div>'}
      <p class="registry-note">Showing ${number.format(assets.length)} of ${number.format(result.total || 0)} records. External metadata is never trusted solely because its reference appears on-chain.</p>`;
  } catch (error) { showError(error.message); }
}

async function showAsset(assetId) {
  loading('Loading asset…');
  const result = await api('asset', {asset_id:assetId});
  const asset = result.asset || {};
  const collectionAuthorized = Boolean(asset.collection_id);
  const hasMetadataHash = asset.metadata_content_hash && !zeroHash.test(asset.metadata_content_hash);
  headerTitle(assetClasses[asset.asset_class] || 'Asset', `<span class="truncate" title="${escapeHtml(asset.asset_id)}">${escapeHtml(shortHash(asset.asset_id, 16))}</span>`);
  view.innerHTML = `${result.active ? '' : '<div class="notice"><strong>Inactive network feature</strong><p>This record is displayed for verification and development. The connected network does not currently permit active asset transactions.</p></div>'}
    <div class="detail-grid">
      ${datum('Asset ID', `<code>${escapeHtml(asset.asset_id)}</code>`)}
      ${datum('Class', `<strong>${escapeHtml(assetClasses[asset.asset_class] || `Unknown (${asset.asset_class})`)}</strong>`)}
      ${datum('Fixed supply', `<strong class="big">${escapeHtml(assetAmount(asset.atomic_supply, asset.display_decimals))}</strong>`)}
      ${datum('Display decimals', `<strong>${number.format(asset.display_decimals || 0)}</strong>`)}
      ${datum('Issuance height', `<a class="link" href="#/block/${asset.height}">#${number.format(asset.height || 0)}</a>`)}
      ${datum('Issuer key', `<code>${escapeHtml(asset.issuer_key)}</code>`)}
      ${datum('Collection authority', collectionAuthorized ? `<span class="trust verified">Controller-authorized</span><a class="link hash" href="#/asset/${escapeHtml(asset.collection_id)}">${escapeHtml(shortHash(asset.collection_id, 16))}</a>` : '<span class="trust">Standalone issuance</span>')}
      ${datum('Metadata content hash', hasMetadataHash ? `<code>${escapeHtml(asset.metadata_content_hash)}</code>` : '<strong>Not committed</strong>')}
      ${datum('External metadata reference', asset.metadata_reference ? `<code>${escapeHtml(asset.metadata_reference)}</code><small class="warning">Untrusted until downloaded content matches the signed hash.</small>` : '<strong>None</strong>')}
      ${datum('Known outputs', `<strong>${number.format(result.output_total || 0)}</strong>`)}
    </div>
    <h3 class="subheading">Confidential outputs</h3>
    ${(result.outputs || []).length ? `<div class="table-wrap"><table><thead><tr><th>Output ID</th><th>Height</th><th>Index</th><th>Commitment</th></tr></thead><tbody>${result.outputs.map((output) => `<tr><td><code>${escapeHtml(shortHash(output.output_id, 14))}</code></td><td><a class="link" href="#/block/${output.height}">#${number.format(output.height || 0)}</a></td><td>${number.format(output.output_index || 0)}</td><td><code>${escapeHtml(shortHash(output.commitment, 14))}</code></td></tr>`).join('')}</tbody></table></div>` : '<div class="empty"><strong>No live outputs</strong><p>No unspent confidential outputs are recorded for this asset.</p></div>'}`;
}

function datum(label, value) { return `<div class="datum"><span>${escapeHtml(label)}</span>${value}</div>`; }
function setOffline() { $('#node-dot').className = 'offline'; $('#node-label').textContent = 'Node unavailable'; }

async function route() {
  const route = location.hash.replace(/^#\/?/, '').split('/').filter(Boolean);
  if (location.hash) content.scrollIntoView({behavior:'smooth', block:'start'});
  if (!route.length) return showBlocks();
  if (route[0] === 'assets') return showAssets();
  if (route[0] === 'asset' && route[1]) {
    try { return await showAsset(route[1]); } catch (error) { return showError(error.message); }
  }
  if (route[0] === 'block' && route[1]) return showBlock(route[1]);
  if (route[0] === 'tx' && route[1]) {
    try { return await showTransaction(route[1]); } catch (error) { return showError(error.message); }
  }
  showError();
}

$('#search-form').addEventListener('submit', (event) => {
  event.preventDefault();
  const value = $('#search-input').value.trim();
  if (/^\d+$/.test(value) || /^[0-9a-f]{64}$/i.test(value)) location.hash = `#/block/${value}`;
  else showError('Enter a numeric block height or a 64-character hexadecimal hash.');
});
$('#latest-search').addEventListener('click', () => { if (chainInfo?.height) location.hash = `#/block/${chainInfo.height - 1}`; });
window.addEventListener('hashchange', route);
api('info').then(updateStats).catch(setOffline);
route();
setInterval(() => api('info').then(updateStats).catch(setOffline), 30000);
