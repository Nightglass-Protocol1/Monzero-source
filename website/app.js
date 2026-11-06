const toast = document.querySelector('.toast');

// The Menu button is handled by site-nav.js on every page.

document.querySelectorAll('[data-copy]').forEach((button) => {
  button.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(button.dataset.copy);
      toast.classList.add('show');
      window.setTimeout(() => toast.classList.remove('show'), 1600);
    } catch {
      button.textContent = 'Select manually';
    }
  });
});

document.querySelector('#year').textContent = new Date().getFullYear();

const number = new Intl.NumberFormat('en-GB');

const fields = {
  height: document.querySelector('#metric-height'),
  difficulty: document.querySelector('#metric-difficulty'),
  networkHashrate: document.querySelector('#metric-network-hashrate'),
  connections: document.querySelector('#metric-connections'),
  rpc: document.querySelector('#metric-rpc'),
  tipAge: document.querySelector('#metric-tip-age'),
  state: document.querySelector('#node-state'),
  dot: document.querySelector('#node-dot'),
  target: document.querySelector('#metric-target'),
  observed: document.querySelector('#metric-observed'),
};

let lastSuccessfulUpdate = null;

async function updateNodeStatus() {
  try {
    const response = await fetch('/api/node-info/index.php', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
      signal: AbortSignal.timeout(12000),
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const info = await response.json();
    if (info.status !== 'OK' || info.nettype !== 'mainnet' || !Number.isSafeInteger(info.height) || info.height < 1) throw new Error('Invalid mainnet observation');
    const observationFresh = Number.isSafeInteger(info.observed_at) && info.observed_at > 0
      && Date.now() / 1000 - info.observed_at <= 90 && info.observed_at <= Date.now() / 1000 + 30;
    lastSuccessfulUpdate = new Date();
    fields.observed.textContent = Number.isSafeInteger(info.observed_at)
      ? `Node observed ${new Date(info.observed_at * 1000).toLocaleString('en-GB')} (shared cache up to 15 seconds). Rewards include transaction fees.`
      : `Node status retrieved ${new Date().toLocaleString('en-GB')}. Rewards include transaction fees.`;
    // get_info height counts blocks; the newest block is one below it.
    fields.height.textContent = `#${number.format(Math.max(0, (info.height ?? 1) - 1))}`;
    fields.height.removeAttribute('aria-label');
    const tipAge = info.tip_age_seconds;
    fields.tipAge.textContent = Number.isFinite(tipAge) && tipAge >= 0 ? `Last block found ${formatAge(tipAge)} ago` : 'Last block time unavailable';
    fields.difficulty.textContent = Number.isFinite(info.difficulty) && info.difficulty > 0 && info.difficulty_top64 === 0 ? number.format(info.difficulty) : 'Unavailable';
    const target = info.target;
    fields.target.textContent = Number.isFinite(target) && target > 0 ? `${target} seconds` : 'Unavailable';
    const networkHashrate = Number.isFinite(target) && target > 0 && Number.isFinite(info.difficulty) && info.difficulty > 0 && info.difficulty_top64 === 0 ? info.difficulty / target : null;
    fields.networkHashrate.textContent = networkHashrate === null ? 'Unavailable' : formatHashrate(networkHashrate);
    const peerCount = info.peer_connections
      ?? (Number.isSafeInteger(info.incoming_connections_count) && Number.isSafeInteger(info.outgoing_connections_count)
        ? info.incoming_connections_count + info.outgoing_connections_count : null);
    fields.connections.textContent = info.connections_hidden ? 'Unavailable' : Number.isSafeInteger(peerCount) && peerCount >= 0 ? number.format(peerCount) : 'Unavailable';
    fields.connections.title = info.connections_hidden
      ? 'Connection counts are intentionally hidden by the public restricted RPC.'
      : 'Current incoming and outgoing P2P peer nodes, not unique people.';
    fields.rpc.textContent = info.restricted ? 'RESTRICTED' : 'ONLINE';
    if (!observationFresh) {
      fields.state.textContent = 'Observation stale or undated';
      fields.dot.className = 'status-dot offline';
    } else if (info.synchronized !== true) {
      fields.state.textContent = 'Node unsynchronized';
      fields.dot.className = 'status-dot offline';
    } else if (info.tip_fresh !== true) {
      fields.state.textContent = 'Network tip stale';
      fields.dot.className = 'status-dot offline';
    } else if (info.peer_ready !== true) {
      fields.state.textContent = 'Network under-peered';
      fields.dot.className = 'status-dot offline';
    } else {
      fields.state.textContent = 'Node synchronized';
      fields.dot.className = 'status-dot online';
    }
    return info;
  } catch {
    for (const key of ['height', 'difficulty', 'networkHashrate', 'connections', 'target']) fields[key].textContent = '—';
    fields.observed.textContent = 'Data temporarily unavailable.' + (lastSuccessfulUpdate ? ` Last successful update: ${lastSuccessfulUpdate.toLocaleString('en-GB')}.` : ' No successful update yet.');
    fields.state.textContent = 'Status unavailable';
    fields.height.setAttribute('aria-label', 'Latest block height unavailable');
    fields.rpc.textContent = '—';
    fields.tipAge.textContent = 'Network data unavailable';
    fields.dot.className = 'status-dot offline';
  }
}

function formatAge(seconds) {
  const value = Math.max(0, Math.round(Number(seconds) || 0));
  if (value < 60) return `${value}s`;
  if (value < 3600) return `${Math.floor(value / 60)}m`;
  if (value < 86400) return `${Math.floor(value / 3600)}h ${Math.floor((value % 3600) / 60)}m`;
  return `${Math.floor(value / 86400)}d`;
}

const chainStrip = document.querySelector('#chain-strip');

async function updateChainStrip(readiness) {
  try {
    if (!readiness || readiness.synchronized !== true || readiness.tip_fresh !== true) throw new Error('Node not ready');
    const response = await fetch('explorer/api.php', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'blocks', limit: 8 }),
      signal: AbortSignal.timeout(12000),
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    if (data.info?.nettype !== 'mainnet' || data.info?.status !== 'OK') throw new Error('Wrong network');
    const headers = data.headers;
    if (!Array.isArray(headers) || !headers.length || headers[0].height !== readiness.height - 1 || data.info.synchronized !== true) throw new Error('No matching ready tip');
    const latest = headers[0];
    document.querySelector('#metric-reward').textContent = Number.isSafeInteger(latest.reward) && latest.reward >= 0
      ? `${new Intl.NumberFormat('en-GB', {maximumFractionDigits: 11}).format(latest.reward / 1e11)} XMZ` : 'Unavailable';
    document.querySelector('#metric-block-time').textContent = Number.isSafeInteger(latest.timestamp) && latest.timestamp > 0
      ? new Date(latest.timestamp * 1000).toLocaleString('en-GB') : 'Unavailable';
    const now = Date.now() / 1000;
    chainStrip.replaceChildren(...headers.slice(0, 8).reverse().map((block) => {
      const item = document.createElement('li');
      const link = document.createElement('a');
      link.href = `explorer/#/block/${encodeURIComponent(block.height)}`;
      link.title = `Block ${number.format(block.height)} · ${number.format(block.num_txes || 0)} transactions`;
      const height = document.createElement('strong');
      height.textContent = `#${number.format(block.height)}`;
      const age = document.createElement('span');
      age.textContent = formatAge(now - Number(block.timestamp));
      link.append(height, age);
      item.append(link);
      return item;
    }));
    chainStrip.classList.add('loaded');
  } catch {
    chainStrip.replaceChildren();
    document.querySelector('#metric-reward').textContent = '—';
    document.querySelector('#metric-block-time').textContent = '—';
    chainStrip.classList.remove('loaded');
  }
}

let networkUpdating = false;
let lastNetworkAttempt = 0;
async function updateNetwork() {
  if (networkUpdating) return;
  networkUpdating = true;
  lastNetworkAttempt = Date.now();
  document.querySelector('.live-card').setAttribute('aria-busy', 'true');
  try {
    const readiness = await updateNodeStatus();
    await updateChainStrip(readiness);
  } finally {
    networkUpdating = false;
    document.querySelector('.live-card').setAttribute('aria-busy', 'false');
  }
}

updateNetwork();
window.setInterval(() => { if (!document.hidden) updateNetwork(); }, 60000);
document.addEventListener('visibilitychange', () => {
  if (!document.hidden && Date.now() - lastNetworkAttempt >= 60000) updateNetwork();
});

function formatHashrate(value) {
  let rate = Number(value) || 0;
  const units = ['H/s', 'kH/s', 'MH/s', 'GH/s'];
  let unit = 0;
  while (rate >= 1000 && unit < units.length - 1) {
    rate /= 1000;
    unit += 1;
  }
  return `${rate.toLocaleString('en-GB', { maximumFractionDigits: rate < 10 ? 2 : 1 })} ${units[unit]}`;
}
