# Network observation history

`network_history.py` stores dated tip observations from the validated statistics
adapter. It is not a canonical block index, independent consensus verifier or
complete emission archive. Existing `/stats/` keeps its recent-block charts.
The `/network/` consumer is implemented and locally tested. Production collection runs on monzero-eu (see "Production" below).

Python's standard library and SQLite are sufficient; no native build is required.
The PHP environment currently has no PDO database driver, so the website will
consume bounded public JSON rather than open the database.

## Collection and export

Use a dedicated database directory outside the website document root. It must
be owned by the collector user and private (0700); the database is 0600. Do not
reuse a wallet-profile or other application database. Unknown schema versions,
unrecognized tables, symlinks and source changes are rejected.

For the existing local preview:

```sh
python utils/website/network_history.py \
  --source preview \
  --database /home/tripo/.local/state/monzero-website-history/observations.sqlite \
  --output-directory /home/tripo/projects/Monzero-App/core/build/network-history-public-20261019
```

`preview` reads only `http://127.0.0.1:8765/stats/api.php`. `live` reads only
`https://monzero.org/stats/api.php`; the new v2 statistics adapter must be deployed
there before live collection works. Sources cannot be mixed in one database.
The local preview timer is enabled as described below. Production scheduling
remains a separate deployment step. Overlapping invocations serialize migrations
and inserts; duplicate observations are idempotent, conflicting observations
are rejected, and observations less than 60 seconds apart are skipped.

Network collection runs in a child process with a 12-second overall deadline;
redirects and responses larger than 2 MiB fail. Inputs must be mainnet v2
observations no older than five minutes. A source clock ahead of the collector
fails with an explicit clock message; correct clock synchronization rather than
silently changing source times.

For an export during a source outage, add `--export-only`. This opens an existing
database read-only and does not make a network request. A missing database is an
error; it cannot silently replace published history with an empty new archive. Without `--output-directory`, one selected `--range` is printed
to stdout. Do not redirect that output over a currently served file: use the
atomic publisher instead. Diagnostics go to stderr.

The publisher writes `24h.json`, `7d.json`, `30d.json`, `90d.json`, `1y.json` and
`all.json`. Each public file is replaced atomically and contains a generation
time. The six replacements are not one filesystem transaction; readers must
use each file's own metadata. On failure, old files may remain and must never be
labelled current merely because they exist. Database and journal files are not
published. Static hosting revalidation and UI staleness handling are implemented and
locally tested; production deployment remains pending.

## Meaning and bounds

At most one tip observation is stored per minute. Each range contains at most
721 time buckets aligned to its bucket width. Missing periods have no samples;
no historical values are invented. Means are approximate and per observation,
not time-weighted. Difficulty extrema are exact decimal strings. The export
includes sample counts, synchronized-sample counts and tip-age bounds; averages
can include a syncing node or stale tip and must be presented with those qualifiers.
Height can decrease after a reorganization. Older observations are retained as
what was reported at that time, not rewritten as canonical chain history.
Emission, circulating supply, node counts, software versions and block-interval
distributions are not collected by this layer.

Schema initialization and recording use explicit immediate transactions; export
uses a consistent read snapshot. Before future schema upgrades, use SQLite's
online backup API under the same private-directory permissions and verify that
the backup reopens. The regression suite includes a backup/export round trip.
No schema upgrade beyond initial version 1 is currently implemented.

```sh
python tests/website/test_network_history.py
```

Tests use temporary databases and loopback synthetic HTTP responses. Real local
acceptance exports are under `core/build/network-history-public-20261019`; they
contain only the mainnet observations actually collected through the preview.

## Local preview timer

The example units in `utils/website/systemd/` are explicitly for this checkout
layout and the preview on port 8765. They run no daemon, mining or compilation.
The service uses the private archive above and exports directly to
`website/network/data/`. Adjust paths before using another checkout. Install:

```sh
mkdir -p ~/.config/systemd/user
cp utils/website/systemd/monzero-network-history-preview.* ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user start monzero-network-history-preview.service
systemctl --user enable --now monzero-network-history-preview.timer
```

The timer runs 60 seconds after the previous invocation finishes, rather than
starting overlapping jobs. Collection has a 12-second transport deadline and a
60-second service timeout. A failing source does not overwrite previous exports;
the page's observation/export timestamps flag old data. The timer tries again at
the next interval. It does not start the preview server automatically, enable
user lingering or backfill missed observations after logout/sleep.

Inspect or stop it with:

```sh
systemctl --user status monzero-network-history-preview.timer
journalctl --user -u monzero-network-history-preview.service -n 20
systemctl --user disable --now monzero-network-history-preview.timer
systemctl --user stop monzero-network-history-preview.service
```

Stopping the timer does not erase the archive or public exports. A production
installation must use its own database, `--source live`, deployment paths and
service account after the v2 statistics endpoint has been deployed. Never change
an existing preview database's source to live.

Before removing this worktree, stop the timer and update both the collector and
preview paths. A forced termination during publishing can leave `.history-*`
temporary exports; these are ignored by Git and blocked by the hosting dotfile
rules. Remove only these temporary files while the collector is stopped.

## Production (monzero-eu)

The webspace has no cron, so production collection runs on the EU node as the
`xmznethistory` system user: `xmznethistory-collect.timer` records one `--source live`
observation per minute into `/var/lib/xmznethistory/archive/observations.sqlite`
(0700/0600) and exports to `/var/lib/xmznethistory/public/`;
`xmznethistory-upload.timer` sends the six exports every five minutes with
`history_upload.py` (gzip JSON, HMAC-SHA256 over timestamp, nonce and body). The
website endpoint `/api/network-history/` refuses bad or stale signatures, replayed
nonces, unknown ranges, non-`live` archives and more than 721 points, and replaces
`/network/data/<range>.json` atomically only with a newer `generated_at`.
Install or update with `bash utils/website/deploy-history-eu.sh` (keeps the archive).
The shared secret is `/etc/xmznethistory/secret` on the node and
`~/monzero-private/network-history-config.php` on the webspace.
Tests: `python tests/website/test_history_upload.py`.
