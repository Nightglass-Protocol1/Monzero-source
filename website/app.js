const menuButton = document.querySelector('.menu-toggle');
const nav = document.querySelector('#site-nav');
const toast = document.querySelector('.toast');

menuButton?.addEventListener('click', () => {
  const open = nav.classList.toggle('open');
  menuButton.setAttribute('aria-expanded', String(open));
});

nav?.querySelectorAll('a').forEach((link) => link.addEventListener('click', () => {
  nav.classList.remove('open');
  menuButton?.setAttribute('aria-expanded', 'false');
}));

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
const historyKey = 'monzero-network-chart-history-v1';
let chartHistory = { hashrate: [], difficulty: [] };
try {
  chartHistory = { ...chartHistory, ...JSON.parse(localStorage.getItem(historyKey) || '{}') };
} catch {
  chartHistory = { hashrate: [], difficulty: [] };
}

function addChartPoint(series, value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric) || numeric < 0) return;
  const points = Array.isArray(chartHistory[series]) ? chartHistory[series] : [];
  const now = Date.now();
  if (points.length && now - points.at(-1).time < 25000) return;
  points.push({ time: now, value: numeric });
  chartHistory[series] = points.slice(-120);
  try {
    localStorage.setItem(historyKey, JSON.stringify(chartHistory));
  } catch {
    // Charts still work for this page view when browser storage is disabled.
  }
  renderChart(series);
}

function renderChart(series) {
  const points = chartHistory[series] || [];
  const svg = document.querySelector(`#chart-${series}`);
  const line = svg?.querySelector('.chart-line');
  if (!line || !points.length) return;
  const values = points.map((point) => point.value);
  const minimum = Math.min(...values);
  const maximum = Math.max(...values);
  const spread = maximum - minimum || Math.max(maximum * .1, 1);
  const coordinates = points.map((point, index) => {
    const x = points.length === 1 ? 600 : (index / (points.length - 1)) * 600;
    const y = 165 - ((point.value - minimum) / spread) * 150;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  });
  line.setAttribute('points', coordinates.join(' '));
}
const fields = {
  height: document.querySelector('#metric-height'),
  difficulty: document.querySelector('#metric-difficulty'),
  networkHashrate: document.querySelector('#metric-network-hashrate'),
  connections: document.querySelector('#metric-connections'),
  rpc: document.querySelector('#metric-rpc'),
  state: document.querySelector('#node-state'),
  dot: document.querySelector('#node-dot'),
};

async function updateNodeStatus() {
  try {
    const response = await fetch('/api/node-info/index.php', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
      signal: AbortSignal.timeout(6000),
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const info = await response.json();
    fields.height.textContent = number.format(info.height ?? 0);
    fields.difficulty.textContent = number.format(info.difficulty ?? 0);
    const target = Number(info.target);
    const networkHashrate = target > 0 ? Number(info.difficulty ?? 0) / target : 0;
    fields.networkHashrate.textContent = formatHashrate(networkHashrate);
    document.querySelector('#chart-difficulty-value').textContent = number.format(info.difficulty ?? 0);
    addChartPoint('difficulty', info.difficulty);
    const peerCount = info.peer_connections
      ?? ((info.incoming_connections_count ?? 0) + (info.outgoing_connections_count ?? 0));
    fields.connections.textContent = info.connections_hidden ? 'HIDDEN' : number.format(peerCount);
    fields.connections.title = info.connections_hidden
      ? 'Connection counts are intentionally hidden by the public restricted RPC.'
      : 'Current incoming and outgoing P2P peer nodes, not unique people.';
    fields.rpc.textContent = info.restricted ? 'RESTRICTED' : 'ONLINE';
    if (info.status !== 'OK') {
      fields.state.textContent = info.status || 'Node unavailable';
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
  } catch {
    fields.state.textContent = 'Status unavailable';
    fields.rpc.textContent = '—';
    fields.dot.className = 'status-dot offline';
  }
}

updateNodeStatus();
window.setInterval(updateNodeStatus, 30000);

const minerFields = {
  active: document.querySelector('#miner-active'),
  hashrate: document.querySelector('#miner-hashrate'),
  blocks: document.querySelector('#miner-blocks'),
  updated: document.querySelector('#miner-updated'),
  rows: document.querySelector('#miner-rows'),
};

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

function renderMiners(data) {
  const miners = Array.isArray(data.miners) ? data.miners : [];
  minerFields.active.textContent = number.format(data.active_miners ?? miners.length);
  minerFields.hashrate.textContent = formatHashrate(data.total_hashrate ?? 0);
  document.querySelector('#chart-hashrate-value').textContent = formatHashrate(data.total_hashrate ?? 0);
  addChartPoint('hashrate', data.total_hashrate);
  minerFields.blocks.textContent = number.format(data.total_blocks ?? 0);
  minerFields.updated.textContent = data.generated_at
    ? new Date(data.generated_at * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
    : '—';

  if (!miners.length) {
    minerFields.rows.innerHTML = '<tr><td colspan="5" class="table-message">No miners have reported in the last three minutes.</td></tr>';
    return;
  }

  minerFields.rows.replaceChildren(...miners.map((miner, index) => {
    const row = document.createElement('tr');
    const values = [
      `#${index + 1}`,
      miner.name,
      formatHashrate(miner.hashrate),
      number.format(miner.blocks_found ?? 0),
      miner.active ? 'ACTIVE' : 'OFFLINE',
    ];
    values.forEach((value, column) => {
      const cell = document.createElement('td');
      cell.textContent = value;
      if (column === 4) cell.className = miner.active ? 'miner-online' : '';
      row.append(cell);
    });
    return row;
  }));
}

async function updateMinerStats() {
  try {
    const response = await fetch('/api/miner-stats/', {
      headers: { Accept: 'application/json' },
      signal: AbortSignal.timeout(6000),
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    renderMiners(await response.json());
  } catch {
    minerFields.active.textContent = '—';
    minerFields.hashrate.textContent = '—';
    minerFields.blocks.textContent = '—';
    minerFields.updated.textContent = '—';
    minerFields.rows.innerHTML = '<tr><td colspan="5" class="table-message">Miner statistics are temporarily unavailable.</td></tr>';
  }
}

updateMinerStats();
window.setInterval(updateMinerStats, 30000);
renderChart('hashrate');
renderChart('difficulty');
