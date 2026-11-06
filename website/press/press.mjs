// Live figures for the press kit (exact supply and the current block reward).
const $ = (id) => document.getElementById(id);
const json = (url) => fetch(url, { headers: { Accept: 'application/json' } }).then((r) => (r.ok ? r.json() : Promise.reject(new Error(String(r.status)))));
json('/api/v1/supply/').then((b) => {
  const d = b.data;
  $('fact-supply').textContent = `${Math.floor(Number(d.circulating_supply)).toLocaleString('en')} XMZ circulating, ${Math.floor(Number(d.total_supply)).toLocaleString('en')} XMZ total at height ${d.height.toLocaleString('en')}`;
}).catch(() => {});
json('/api/v1/network/stats/').then((b) => {
  const r = Number(b.data.top_block_reward_atomic) / 1e11;
  if (r > 0) $('fact-reward').textContent = `${r.toFixed(2)} XMZ for the latest block (including fees), declining smoothly; 3.26 XMZ per block tail emission`;
}).catch(() => {});
