Runtime exports only: 24h.json, 7d.json, 30d.json, 90d.json, 1y.json, all.json.
Generate with utils/website/network_history.py --output-directory website/network/data.
Keep the SQLite archive outside the public document root. JSON is intentionally
untracked. A missing export produces an unavailable state, never synthetic history.
See utils/website/NETWORK_HISTORY.md for collection and private storage details.
