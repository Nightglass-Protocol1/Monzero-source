# Monzero website

Website for `monzero.org`, including the PHP-backed block explorer at
`/explorer/` and the local-draft asset workspace at `/app/`. It contains no
build-time dependencies.

The public source repository is
<https://github.com/Nightglass-Protocol1/Monzero-source>.

The homepage links to `/WHITEPAPER.md`. Deploy the repository-root
`WHITEPAPER.md` at that exact document-root path whenever the site is updated;
the packaged copies and public copy must come from the same reviewed source.

## Preview locally

```bash
cd website
php -S 127.0.0.1:8080
```

Open `http://127.0.0.1:8080` for the website and
`http://127.0.0.1:8080/explorer/` for the explorer. Studio is available at
`http://127.0.0.1:8080/app/`; it can preview and download unsigned JSON drafts
but deliberately has no wallet connection, signing, or submission path.

## VPS deployment

Copy this directory's contents to `/var/www/monzero`, install
`nginx-monzero.conf` as `/etc/nginx/sites-available/monzero`, enable the site,
set the `fastcgi_pass` socket to the PHP-FPM listener installed on the host,
test Nginx, and reload it. The included Nginx configuration executes `/api/node-info/index.php` through
PHP-FPM for `/api/node-info/`, preserving tip freshness and mainnet readiness
checks. PHP-FPM also serves the allowlisted explorer proxy and opt-in miner
statistics endpoint. Static cache rules retain the server security headers.
This port-80 example assumes a separately configured HTTPS terminator; verify
TLS and public headers before deployment.

On IONOS web hosting, upload the complete contents of this directory to the
domain's assigned document root. PHP must be enabled so `/explorer/api.php` can
proxy the allowlisted, read-only requests to the restricted public node.

`releases/genesis-pre11-notes.txt` is the human-readable companion to the
machine-readable pre11 metadata. It covers the CLI, source, and separately
packaged Windows graphical-wallet archive. Keep its release identity,
platform scope, and limitations aligned with `docs/RELEASE_NOTES.md` without
changing published archive hashes.

## Anonymous miner statistics

The miner table uses an opt-in heartbeat endpoint. It never accepts or returns
a wallet address, hostname, IP address, serial number, or hardware identifier.
Each reporter creates a random UUID; PHP pseudonymizes it with a server-side
HMAC secret before storage and exposes only the first eight pseudonym digits.

Configure these secrets in the PHP environment (use independent random values):

```text
MONZERO_STATS_SECRET=<at least 32 random characters, server only>
MONZERO_STATS_INGEST_TOKEN=<at least 24 random characters, team reporters>
MONZERO_STATS_FILE=/an/apache-writable/private/path/miner-stats.json
```

For shared hosting without environment-variable controls, create
`.monzero-miner-stats.php` one directory above the document root:

```php
<?php
return [
    'MONZERO_STATS_SECRET' => 'server-only-random-value',
    'MONZERO_STATS_INGEST_TOKEN' => 'team-reporter-random-value',
    'MONZERO_STATS_FILE' => __DIR__ . '/.monzero-miner-stats.json',
];
```

Generate suitable values with `openssl rand -hex 32`. Do not place the HMAC
secret in a download or reporter. Give only the ingest token to miners who opt
in, then run the reporter alongside their already-running local daemon:

```bash
MONZERO_STATS_TOKEN='team-ingest-token' \
  python3 utils/monzero-miner-reporter.py --interval 60
```

The table counts a miner as active for three minutes after its last heartbeat.
Hash rate and blocks found are self-reported. Block counts are derived from the
local daemon log and are not consensus-verified leaderboard claims.

The Windows heartbeat helper is
`utils/release/windows/monzero-miner-reporter.ps1`. Install its ingest token in
`%APPDATA%\Monzero\miner-stats-token.txt` with access limited to the reporting
account; never put that token in a public archive. `start-mining.bat` launches
the reporter automatically when that file exists; the reporter's named mutex
prevents duplicate instances. The homepage separately
shows `difficulty / target` as an estimated network hash rate even when no
miner has opted into reporting.

On Windows the daemon's default data and mining log directory is
`%PROGRAMDATA%\monzero`; the reporter diagnostic log remains under
`%APPDATA%\Monzero`.

### Mainnet network statistics

`/stats/` shows the latest 720 mainnet block headers, with 120/360/720-block
windows for estimated hashrate (difficulty / current target interval), difficulty,
block intervals and non-coinbase transaction counts. The GET-only `stats/api.php`
uses the fixed public node, a shared 60-second public-data cache and serialized
refreshes. It returns an error when mainnet data cannot be obtained; the browser
labels any retained snapshot with its original update time. No analytics SDK,
wallet identity collection, or fabricated user-growth series is included.
The existing opt-in miner endpoint supplies current reporting installations only.

Validation: PHP syntax, JavaScript syntax and real mainnet API retrieval; Chromium
checks at 1440px and 390px, 720/120-block selection, retained-snapshot and first-load
outage states. Wine mainnet GUI screenshots use a separate empty demonstration
wallet connected to the public remote node, without mining or transfers.

### Nginx configuration regression check

With Nginx and `php-cgi` available, run:

```sh
python3 tests/website/check_nginx.py
```

If needed, set `NGINX_BINARY` to the Nginx executable and
`NGINX_FASTCGI_PARAMS` to its distribution's `fastcgi_params` file. The check
uses temporary files and loopback ports, validates configuration syntax, and
checks PHP directory routing, POST preservation, static/error security headers,
module/checksum MIME types, cache behavior and hidden-file denial. It neither
changes the installed server configuration nor contacts the blockchain node.
The standard website suite tests the actual PHP observation logic separately.
This local HTTP check does not validate a production TLS terminator, PHP-FPM
socket permissions, Apache `.htaccess`, or public-hosting response headers.

Keep security `add_header` directives at server scope: a location with its own
`add_header` can suppress inherited headers. The static locations use `expires`
for caching without adding a location-level `add_header`.
See the [Nginx headers documentation](https://nginx.org/en/docs/http/ngx_http_headers_module.html).

Behind a reverse proxy, configure Nginx's `set_real_ip_from` for only the actual
trusted proxy IPs/CIDRs and `real_ip_header` for the header that proxy overwrites.
Do not copy a broad trust range or accept visitor-supplied forwarding headers.
Verify that PHP receives distinct client `REMOTE_ADDR` values before publishing:
the pool dashboard's per-client limits and public-profile proxy depend on it.
The example deliberately does not guess the deployment's trusted proxy addresses.
Directory redirects are relative to preserve the external HTTPS origin; the
node-status slash redirect uses 308 so POST requests retain their method.

### Public node observation cache

The node-status PHP adapter shares public observations for at most 15 seconds.
Its private temporary directory is named from the deployed script directory;
files are mode 0600 in a mode-0700 directory. Only public node information and
its observation time are stored, without request bodies or visitor identifiers.
A nonblocking lock permits one upstream refresh at a time; competing refreshes
return 503 with Retry-After. Failed node lookups have a five-second cooldown.
Expired observations are not served as an outage fallback. Tip age and readiness
are recalculated on each response, and the UI labels the original observation
time. Browser/proxy caching remains disabled with Cache-Control: no-store.

PHP must be able to create and lock files in its temporary directory. Cache
storage errors fail closed with 503. Upstream responses are capped at 256 KiB,
redirects are disabled, and each request retains its existing timeout. The node
URL remains the existing HTTP endpoint; this cache does not authenticate that
transport or replace production TLS and edge abuse-control requirements.

### Legacy supply endpoint

`/api/supply/` retains its numeric plain-text output and existing JSON fields for
listing-site compatibility. This is an explicit legacy exception: those numbers
are an estimate from a saved anchor plus unpenalized emission, not a reverified
circulating-supply measurement. JSON metadata and the `X-Monzero-Supply-Status`
header disclose that limitation; existing plain-text consumers may not read them.
The homepage therefore shows supply unavailable rather than reusing that value.
Do not treat `circulating_supply` or `total_supply` as verified totals without
checking the anchor against a trusted unrestricted node and accounting for reward
penalties and the intended supply definition. GMP is required by the calculation.

### Apache static policy regression check

Run `python3 tests/website/check_apache.py` where Apache is available. Override
`HTTPD_BINARY`, `HTTPD_MODULES` and `HTTPD_MIME_TYPES` for the local distribution.
It starts a temporary loopback-only instance using the repository `.htaccess`.
Static MIME, cache and security headers (including errors and conditional module
responses) are tested without the rewrite module, then HTTPS redirects are
checked in a separate run with rewrite enabled. It does not install configuration,
exercise PHP, or prove TLS/proxy behavior on the production host.

Stable `.mjs` entry points/imports must revalidate (`Cache-Control: no-cache`),
including 304 responses. CSS and classic JS retain the existing seven-day cache;
change their URL version whenever their contents change. Release 1 updates the
homepage script and shared mining CSS versions. The benchmark catalog fetch also
requests revalidation rather than silently retaining an old publication snapshot.

Apache disables inherited expiry rules for modules and catalog JSON and clears
an ordinary Cache-Control header before setting the revalidation policy. The
regression fixture deliberately enables server-wide expiry to check that only
one no-cache header survives. Production acceptance must verify `mod_headers`,
`mod_mime`, appropriate `.htaccess` overrides and the resulting public headers;
`IfModule` guards are compatibility measures, not proof that a host enforces them.

### Optional local worker labels

The dashboard returns opaque HMAC identifiers scoped to a mining address and
pool login name, never the raw name. Connections sharing a login share a label.
Configure `MONZERO_POOL_WORKER_LABEL_KEY` as 64 lowercase hexadecimal characters
in the PHP service environment, using a persistent private secret outside the
webroot (for example, a mode-0600 service environment file). Do not log or publish
its contents. Missing or invalid configuration disables worker labels while other
dashboard observations remain available. Rate-state cleanup does not rotate the
key; deliberate key rotation invalidates saved associations and requires relabeling.
No label text is sent to the server.

The browser keeps labels in memory until the visitor explicitly enables local
saving. It stores at most 100 opaque-ID/label pairs (60 characters each), without
addresses, activity or raw worker names. Users can forget saved labels, switch
saving off or clear the dashboard to remove them. Storage failures are reported
without claiming deletion succeeded. Labels are notes, not proof of machine
identity. Auto-refresh pauses while a worker-label control has focus.

### Session mining alerts

The pool dashboard has separate opt-ins for in-memory monitoring and browser
notifications. Monitoring adds no requests and uses the existing manual/visible-tab
refreshes. It evaluates a configurable decline from the first positive hashrate
reading, minimum connected-worker count and repeated failed lookups. Unknown data
is not zero; worker-count changes are not individual worker identification.
Two qualifying observations must be 30–90 seconds apart; duplicates/old server
timestamps are ignored, gaps and unknown metrics break consecutive counts, and
healthy readings reset conditions even if they arrive too quickly to count up.
Continuing conditions are latched; after recovery, ten-minute per-condition
cooldowns apply. Baselines, alerts and notification opt-in clear on address change,
Stop, Clear or page close. In-flight lookups from before enabling are excluded.

Browser delivery follows the user-gesture and secure-context requirements in
[MDN's Notifications API documentation](https://developer.mozilla.org/en-US/docs/Web/API/Notifications_API).
Permission alone never enables delivery on a later visit. Generic notifications
omit addresses, labels and amounts. Unsupported delivery leaves on-page alerts
working. Permission and already displayed OS notifications may outlive the tab.
This is not a background monitoring service: hidden tabs and focused worker-label
controls pause automatic refresh. No email/Telegram integration or service worker
was introduced. Network-difficulty notifications remain outstanding.

Payment alerts are a separate opt-in within session monitoring. The first valid
list of up to 50 pool-sent payments establishes a baseline; later newly observed
transaction IDs are deduplicated and grouped at a ten-minute notification limit.
This is not confirmation verification or proof of the actual send time. Missing
or malformed lists do not establish a baseline. The pool history window can miss
payments during long gaps. After 1,000 distinct IDs, payment monitoring pauses
with a visible message; it never evicts old IDs and re-alerts on them. Re-enabling
starts a fresh baseline. IDs and pending counts remain in memory, and browser
notifications still contain no transaction ID, address or amount.
Incomplete initial history can make an old payment appear newly observed later;
this is disclosed rather than inferred away using unverified payment timestamps.
Queued counts wait for a valid list and pause if the ID limit is reached. Stop,
Clear and address changes also uncheck the payment option.

### Statistics source validation

`stats/contract.php` validates an exact window of up to 720 ordered headers,
consecutive heights, hash links and agreement with the `get_info` tip. Wrong
network/target, bootstrap-marked responses, orphan headers, incomplete lists,
wide difficulty and unsafe numeric values fail closed. Backward timestamps remain
valid data; the UI already omits their negative interval. This is not independent
consensus verification. Fixed upstream HTTP transport remains an explicit trust
limitation pending authenticated HTTPS support.

The stats cache is deployment-scoped with private permissions. Fresh readers
share locks; only refresh holds the exclusive lock, with a second cache check
after upgrading. Busy refresh returns no-store 503, never stale success. Responses
are bounded to 2 MiB on both PHP transports with redirects disabled. Failures have
a five-second cooldown; success caches for 60 seconds and downstream max-age uses
remaining lifetime. Corrupt, future-dated or malformed cached observations are
rejected. Source errors are not echoed or logged verbatim. Historical archival
storage and broader edge rate controls are still separate remaining work.

### Historical network observation collector

The interpreted collector and private SQLite/archive export design are documented
in [`utils/website/NETWORK_HISTORY.md`](../utils/website/NETWORK_HISTORY.md). It
exports bounded public summaries for 24h/7d/30d/90d/1y/all ranges, while retaining
the database outside the website. The `/network/` consumer and production job
configuration are not yet implemented/deployed. This is tip-observation history,
not a canonical block index; missing metrics are not fabricated.
